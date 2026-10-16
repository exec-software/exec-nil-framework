#include "nil_loader.hpp"
#include "codegen.hpp"

#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
size_t rejected_readers = 0;
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
void u32(std::string& output, std::uint32_t value) {
    for (int i = 0; i != 4; ++i) output.push_back(static_cast<char>(value >> (8 * i)));
}
void string(std::string& output, const std::string& value) {
    u32(output, static_cast<std::uint32_t>(value.size()));
    output += value;
}
std::uint64_t read(const std::string& bytes, size_t offset, size_t count) {
    require(offset + count <= bytes.size(), "test binary offset out of bounds");
    std::uint64_t value = 0;
    for (size_t i = 0; i < count; ++i)
        value |= std::uint64_t(static_cast<unsigned char>(bytes[offset + i])) << (8 * i);
    return value;
}
void write(std::string& bytes, size_t offset, size_t count, std::uint64_t value) {
    for (size_t i = 0; i < count; ++i) bytes.at(offset + i) = static_cast<char>(value >> (8 * i));
}
std::string replace_debug_section(std::string original, const std::string& payload) {
    const size_t count = static_cast<size_t>(read(original, 12, 4));
    for (size_t i = 0; i < count; ++i) {
        const size_t entry = 16 + i * 20;
        if (read(original, entry, 4) != 8) continue;
        const size_t offset = static_cast<size_t>(read(original, entry + 4, 8));
        require(offset + read(original, entry + 12, 8) == original.size(), "debug section is not last");
        original.resize(offset);
        original += payload;
        write(original, entry + 12, 8, payload.size());
        return original;
    }
    throw std::runtime_error("debug section missing");
}
std::string record(const std::string& path, const std::vector<ny::DebugLocalBinding>& locals) {
    std::string result;
    string(result, path);
    u32(result, static_cast<std::uint32_t>(locals.size()));
    for (const auto& local : locals) {
        string(result, local.name);
        string(result, local.storage);
        string(result, local.type);
        u32(result, static_cast<std::uint32_t>(local.scope_depth));
    }
    return result;
}
std::string payload(const std::string& first) {
    std::string result;
    u32(result, 1);
    u32(result, 4);
    result += first;
    for (int i = 1; i < 4; ++i) result += record("/source/main.ny", {});
    return result;
}
void reject(const std::string& bytes, bool binary, const std::string& context) {
    for (bool flat : {false, true}) {
        bool rejected = false;
        try {
            std::istringstream input(bytes, std::ios::binary);
            if (flat) {
                std::unordered_map<std::string, ny::Value> constants;
                if (binary) (void)ny::parseBinaryProgram(input, constants);
                else (void)ny::parseProgram(input, constants);
            } else if (binary) (void)ny::parseBinaryNilModule(input);
            else (void)ny::parseTextNilModule(input);
        } catch (const std::exception&) { rejected = true; }
        require(rejected, context + (flat ? " accepted by VM reader" : " accepted by module reader"));
        ++rejected_readers;
    }
}
} // namespace

int main() {
    try {
        ny::nil_module module;
        module.module_name = "debug_metadata";
        ny::nil_function function;
        function.name = "main";
        function.return_type = "i64";
        function.source_line = 10;
        const std::string file = "/source/space and \"quote\"-λ.ny";
        function.body = {{"PUSH_CONST", {"7"}, 11, file, {}},
                         {"STORE_LOCAL", {"$ny.local.0.value"}, 11, file, {}},
                         {"LOAD_LOCAL", {"$ny.local.0.value"}, 12, file,
                          {{"value", "$ny.local.0.value", "i64", 2}}},
                         {"RET", {}, 12, file, {{"value", "$ny.local.0.value", "i64", 2}}}};
        module.functions.push_back(function);
        ny::codegen_options options;
        std::ostringstream binary, text;
        ny::nil_binary_writer(binary, options).emit_binary_module(module);
        ny::nil_writer(text, options).emit_text_module(module);
        require(read(binary.str(), 4, 4) == 5, "source maps did not select NIL v5");
        for (bool is_binary : {false, true}) {
            const auto bytes = is_binary ? binary.str() : text.str();
            std::istringstream input(bytes, std::ios::binary);
            auto parsed = is_binary ? ny::parseBinaryNilModule(input) : ny::parseTextNilModule(input);
            const auto& instruction = parsed->functions.at(0).body.at(2);
            require(instruction.source_file == file && instruction.debug_line == 12 &&
                    instruction.debug_locals.size() == 1 && instruction.debug_locals[0].name == "value" &&
                    instruction.debug_locals[0].storage == "$ny.local.0.value" &&
                    instruction.debug_locals[0].type == "i64" && instruction.debug_locals[0].scope_depth == 2,
                    "module source map round trip lost metadata");
            std::istringstream flat_input(bytes, std::ios::binary);
            std::unordered_map<std::string, ny::Value> constants;
            const auto flat = is_binary ? ny::parseBinaryProgram(flat_input, constants)
                                        : ny::parseProgram(flat_input, constants);
            bool found = false;
            for (const auto& item : flat) if (item.op == ny::Opcode::LOAD_LOCAL) {
                found = true;
                require(item.source_file == file && item.debug_locals.size() == 1 &&
                        item.debug_locals[0].name == "value", "VM source map round trip lost metadata");
            }
            require(found, "round trip lost executable instruction");
            ny::VM vm;
            vm.loadProgram(flat, constants);
            vm.resetExecution("main");
            vm.execute(false);
            require(vm.getProgramExitCode() == 7, "source maps changed execution");
        }
        const auto textual = [](const std::string& metadata) {
            return "module malformed\nfunction main:\n" + metadata + "\nRET ; line 1\nend function\n";
        };
        const std::vector<std::string> bad_text = {
            "# source_debug +0 \"/main.ny\" 0", "# source_debug \" 0\" \"/main.ny\" 0",
            "# source_debug 0 \"/main.ny\" +0", "# source_debug 0 \"/main.ny\" ١",
            "# source_debug 0 \"/main.ny\" 1 \"value\" \"slot\" \"i64\" +1",
            "# source_debug 1 \"/main.ny\" 0", "# source_debug -1 \"/main.ny\" 0",
            "# source_debug 0 \"/main.ny\" -1", "# source_debug 0 \"/main.ny\" 1025",
            "# source_debug 0 \"\" 0", "# source_debug 0 \"/main.ny\" 1 \"value\" \"slot\" \"i64\" -1",
            "# source_debug 0 \"/main.ny\" 1 \"value\" \"slot\" \"i64\" 1025",
            "# source_debug 0 \"/main.ny\" 1 \"\" \"slot\" \"i64\" 1",
            "# source_debug 0 \"/main.ny\" 1 \"value\" \"\" \"i64\" 1",
            "# source_debug 0 \"/main.ny\" 1 \"value\" \"slot\" \"\" 1",
            "# source_debug 0 \"/main.ny\" 2 \"value\" \"slot\" \"i64\" 1 \"value\" \"other\" \"i64\" 2",
            "# source_debug 0 \"/main.ny\" 2 \"value\" \"slot\" \"i64\" 1 \"other\" \"slot\" \"i64\" 2",
            "# source_debug 0 \"/main.ny\" 0 trailing",
            "# source_debug 0 \"/main.ny\" 0\n# source_debug 0 \"/main.ny\" 0",
            "# source_debug 0 \"" + std::string(16385, 'x') + "\" 0",
            "# source_debug 0 \"/main.ny\" 1 \"" + std::string(16385, 'x') + "\" \"slot\" \"i64\" 1"
        };
        for (size_t i = 0; i < bad_text.size(); ++i) reject(textual(bad_text[i]), false, "text case " + std::to_string(i));
        reject("# source_debug 0 \"/main.ny\" 0\nfunction main:\nRET\nend function\n", false, "orphan module record");
        reject("function main:\n# source_debug 0 \"/main.ny\" 0\nend function\n", false, "orphan function record");
        const std::vector<ny::DebugLocalBinding> good = {{"value", "slot", "i64", 1}};
        std::vector<std::string> bad_records = {
            record("", good), record(std::string(16385, 'x'), {}),
            record(std::string("/main\0.ny", 9), {}),
            record("/main\n.ny", {}),
            record(std::string("/main\xff.ny", 9), {}),
            record(std::string("/main\xc0\xaf.ny", 10), {}),
            record(std::string("/main\xed\xa0\x80.ny", 11), {}),
            record(std::string("/main\xf4\x90\x80\x80.ny", 12), {}),
            record("/main.ny", {{"value", "slot", "i64", -1}}),
            record("/main.ny", {{"value", "slot", "i64", 1025}}),
            record("/main.ny", {{"", "slot", "i64", 1}}),
            record("/main.ny", {{"value", "", "i64", 1}}),
            record("/main.ny", {{"value", "slot", "", 1}}),
            record("/main.ny", {{"value", "slot", "i64\n", 1}}),
            record("/main.ny", {{std::string(16385, 'x'), "slot", "i64", 1}}),
            record("/main.ny", {{"value", "slot", "i64", 1}, {"value", "other", "i64", 2}}),
            record("/main.ny", {{"value", "slot", "i64", 1}, {"other", "slot", "i64", 2}})
        };
        for (size_t i = 0; i < bad_records.size(); ++i)
            reject(replace_debug_section(binary.str(), payload(bad_records[i])), true, "binary record " + std::to_string(i));
        auto valid_payload = payload(record("/main.ny", good));
        auto corrupt = valid_payload;
        write(corrupt, 0, 4, 2);
        reject(replace_debug_section(binary.str(), corrupt), true, "function count mismatch");
        corrupt = valid_payload;
        write(corrupt, 4, 4, 3);
        reject(replace_debug_section(binary.str(), corrupt), true, "instruction count mismatch");
        corrupt = valid_payload;
        corrupt.pop_back();
        reject(replace_debug_section(binary.str(), corrupt), true, "truncated record");
        reject(replace_debug_section(binary.str(), valid_payload + "x"), true, "trailing record data");
        for (auto& instruction : module.functions[0].body) {
            instruction.source_file.clear();
            instruction.debug_locals.clear();
        }
        std::ostringstream without_debug;
        ny::nil_binary_writer(without_debug, options).emit_binary_module(module);
        require(read(without_debug.str(), 4, 4) == 3 && without_debug.str().find(file) == std::string::npos,
                "non-debug binary unexpectedly disclosed source metadata");
        std::cout << "source debug metadata: four-reader round trips and " << rejected_readers
                  << " malformed-reader rejections passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "source debug metadata regression: " << error.what() << '\n';
        return 1;
    }
}
