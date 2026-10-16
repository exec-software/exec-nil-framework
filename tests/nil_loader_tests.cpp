#include "codegen.hpp"
#include "nil_contract.hpp"
#include "nil_loader.hpp"
#include "nil_legacy_fixture.hpp"
#include "vm.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::string serialize_binary(const ny::nil_module &module) {
  std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
  ny::codegen_options options;
  ny::nil_binary_writer writer(stream, options);
  writer.emit_binary_module(module);
  return stream.str();
}

std::vector<ny::Instruction>
parse_binary(const std::string &bytes,
             std::unordered_map<std::string, ny::Value> &constants) {
  std::istringstream stream(bytes, std::ios::in | std::ios::binary);
  return ny::parseBinaryProgram(stream, constants);
}

std::vector<ny::Instruction>
parse_module(const ny::nil_module &module, bool binary,
             std::unordered_map<std::string, ny::Value> &constants) {
  if (binary)
    return parse_binary(serialize_binary(module), constants);
  std::stringstream stream;
  ny::nil_writer writer(stream);
  writer.emit_text_module(module);
  return ny::parseProgram(stream, constants);
}

ny::nil_function make_function(
    const std::string &name, const std::string &return_type,
    const std::vector<ny::nil_instruction> &body,
    const std::vector<std::string> &params = {}) {
  ny::nil_function function;
  function.name = name;
  function.return_type = return_type;
  function.params = params;
  function.body = body;
  return function;
}

ny::nil_library_metadata parse_library(const std::string &bytes) {
  std::istringstream stream(bytes, std::ios::in | std::ios::binary);
  return ny::parseBinaryLibraryMetadata(stream);
}

std::uint32_t read_le_u32(const std::string &bytes, std::size_t offset) {
  std::uint32_t value = 0;
  for (unsigned i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(
                 static_cast<unsigned char>(bytes.at(offset + i)))
             << (8U * i);
  }
  return value;
}

std::uint64_t read_le_u64(const std::string &bytes, std::size_t offset) {
  std::uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i) {
    value |= static_cast<std::uint64_t>(
                 static_cast<unsigned char>(bytes.at(offset + i)))
             << (8U * i);
  }
  return value;
}

void write_le_u32(std::string &bytes, std::size_t offset, std::uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    bytes.at(offset + i) = static_cast<char>((value >> (8U * i)) & 0xffU);
}

void write_le_u64(std::string &bytes, std::size_t offset, std::uint64_t value) {
  for (unsigned i = 0; i < 8; ++i)
    bytes.at(offset + i) = static_cast<char>((value >> (8U * i)) & 0xffU);
}

void expect_binary_rejection(const std::string &bytes,
                             const std::string &message_fragment,
                             const std::string &description) {
  bool rejected = false;
  try {
    std::unordered_map<std::string, ny::Value> constants;
    (void)parse_binary(bytes, constants);
  } catch (const std::runtime_error &error) {
    rejected = message_fragment.empty() ||
               std::string(error.what()).find(message_fragment) !=
                   std::string::npos;
  }
  check(rejected, description);
}

class nonseekable_stringbuf : public std::stringbuf {
public:
  explicit nonseekable_stringbuf(const std::string &value)
      : std::stringbuf(value, std::ios::in | std::ios::binary) {}

protected:
  pos_type seekoff(off_type, std::ios_base::seekdir,
                   std::ios_base::openmode) override {
    return pos_type(off_type(-1));
  }

  pos_type seekpos(pos_type, std::ios_base::openmode) override {
    return pos_type(off_type(-1));
  }
};

void test_binary_function_boundaries_and_debug_lines() {
  ny::nil_module module;
  module.module_name = "loader_test";
  module.string_constants.push_back({"S0", "helper"});

  ny::nil_function helper;
  helper.name = "helper";
  helper.body.push_back({"PUSH_CONST_STR", {"S0"}, 41});
  helper.body.push_back({"RET", {}, 42});
  module.functions.push_back(helper);

  ny::nil_function main;
  main.name = "main";
  main.body.push_back({"CALL", {"helper", "0"}, 51});
  main.body.push_back({"RET", {}, 52});
  module.functions.push_back(main);

  std::unordered_map<std::string, ny::Value> constants;
  const auto program = parse_binary(serialize_binary(module), constants);

  check(program.size() == 10,
        "two binary functions should retain their bodies and wrappers");
  if (program.size() == 10) {
    check(program[0].op == ny::Opcode::JMP &&
              program[0].args ==
                  std::vector<std::string>{"$ny.internal.function-end.0"},
          "helper should begin with its skip jump");
    check(program[1].op == ny::Opcode::LABEL &&
              program[1].args == std::vector<std::string>{"helper"},
          "helper should retain its function-entry label");
    check(program[4].op == ny::Opcode::LABEL &&
              program[4].args ==
                  std::vector<std::string>{"$ny.internal.function-end.0"},
          "helper should end with its skip label");
    check(program[5].op == ny::Opcode::JMP &&
              program[5].args ==
                  std::vector<std::string>{"$ny.internal.function-end.1"},
          "main should begin with its skip jump");
    check(program[6].op == ny::Opcode::LABEL &&
              program[6].args == std::vector<std::string>{"main"},
          "main should retain its function-entry label");
    check(program[9].op == ny::Opcode::LABEL &&
              program[9].args ==
                  std::vector<std::string>{"$ny.internal.function-end.1"},
          "main should end with its skip label");
    check(program[2].line == 41 && program[3].line == 42 &&
              program[7].line == 51 && program[8].line == 52,
          "binary debug lines should survive loading");
  }

  const auto constant = constants.find("S0");
  check(constant != constants.end() &&
            std::holds_alternative<std::string>(constant->second) &&
            std::get<std::string>(constant->second) == "helper",
        "binary constants should still round-trip");
}

void test_textual_function_declaration_lines() {
  ny::nil_module module;
  module.module_name = "declaration_lines";
  auto helper = make_function(
      "helper", "i32",
      {{"STORE_LOCAL", {"value"}, 42}, {"LOAD_LOCAL", {"value"}, 43},
       {"RET", {}, 44}},
      {"value:i32"});
  helper.source_line = 41;
  helper.annotations = {"@ai_context(\"declaration metadata\")"};
  module.functions.push_back(helper);
  auto main = make_function(
      "main", "i32", {{"PUSH_CONST", {"0"}, 51}, {"RET", {}, 52}});
  main.source_line = 2147483647;
  module.functions.push_back(main);
  module.functions.push_back(make_function("unknown", "void", {{"RET", {}, 0}}));

  std::stringstream text;
  ny::nil_writer writer(text);
  writer.emit_text_module(module);
  check(text.str().find("# declaration_line 41\nfunction helper(value:i32) -> i32:\n") !=
            std::string::npos,
        "declaration metadata is a preceding comment, not a function-header change");
  const auto textual = ny::parseTextNilModule(text);
  std::istringstream binary(serialize_binary(module), std::ios::in | std::ios::binary);
  const auto encoded = ny::parseBinaryNilModule(binary);
  check(textual->functions.size() == module.functions.size() &&
            encoded->functions.size() == module.functions.size(),
        "both module readers retain the declaration records");
  if (textual->functions.size() == module.functions.size() &&
      encoded->functions.size() == module.functions.size()) {
    for (std::size_t index = 0; index < module.functions.size(); ++index) {
      const auto &expected = module.functions[index];
      const auto &actual = textual->functions[index];
      const auto &other = encoded->functions[index];
      check(actual.source_line == expected.source_line &&
                actual.source_line == other.source_line &&
                actual.name == other.name && actual.params == other.params &&
                actual.return_type == other.return_type &&
                actual.annotations == other.annotations,
            "textual and binary declaration metadata agree for " + expected.name);
    }
  }
  std::istringstream program_input(text.str());
  std::unordered_map<std::string, ny::Value> constants;
  check(!ny::parseProgram(program_input, constants).empty(),
        "the VM text reader accepts emitted declaration-line comments");

  const std::string body = "function main() -> i32:\nPUSH_CONST 0\nRET\nend function\n";
  for (const auto &prefix : std::vector<std::pair<std::string, int>>{
           {"", 0},
           {"# declaration_line_notes are an ordinary comment\n", 0},
           {"# declaration_line 0\n", 0},
           {"  #\tdeclaration_line\t2147483647  \r\n# ordinary comment\n\n", 2147483647}}) {
    std::istringstream metadata_input(prefix.first + body);
    const auto parsed = ny::parseTextNilModule(metadata_input);
    check(parsed->functions.front().source_line == prefix.second,
          "legacy, zero, and bounded declaration lines retain their meaning");
    std::istringstream instruction_input(prefix.first + body);
    constants.clear();
    check(!ny::parseProgram(instruction_input, constants).empty(),
          "both text readers accept the same declaration-line spelling");
  }

  std::vector<std::string> malformed;
  for (const auto &number : {"", "-1", "+1", "2147483648",
                             "184467440737095516160", "1x", "1 2", "1 ; line 2",
                             "0x10", "1.0"})
    malformed.push_back("# declaration_line " + std::string(number) + "\n" + body);
  malformed.push_back("# declaration_line 0\n# declaration_line 0\n" + body);
  malformed.push_back("# declaration_line 1\n# declaration_line 2\n" + body);
  for (const auto &intervening : {"module other\n", "const S0 = \"value\"\n",
                                  "@ai_context(\"not adjacent\")\n", "@test main\n",
                                  "struct Record\nend struct\n", "NOP\n"})
    malformed.push_back("# declaration_line 1\n" + std::string(intervening) + body);
  malformed.push_back("# declaration_line 1\n");
  malformed.push_back(body + "# declaration_line 1\n");
  malformed.push_back("function main() -> i32:\n# declaration_line 1\nPUSH_CONST 0\nRET\nend function\n");
  malformed.push_back("struct Record\n# declaration_line 1\nvalue:i32\nend struct\n" + body);
  malformed.push_back("enum Choice\n# declaration_line 1\nYes\nend enum\n" + body);
  malformed.push_back("class Record 0\n# declaration_line 1\nend class\n" + body);
  for (std::size_t index = 0; index < malformed.size(); ++index) {
    for (const bool metadata_reader : {false, true}) {
      bool rejected = false;
      try {
        std::istringstream input(malformed[index]);
        if (metadata_reader) {
          (void)ny::parseTextNilModule(input);
        } else {
          constants.clear();
          (void)ny::parseProgram(input, constants);
        }
      } catch (const std::runtime_error &error) {
        rejected = std::string(error.what()).find("declaration line") != std::string::npos;
      }
      check(rejected, std::string(metadata_reader ? "module" : "VM") +
                          " reader rejects malformed declaration-line case " +
                          std::to_string(index));
    }
  }
  module.functions.front().source_line = -1;
  bool rejected = false;
  try {
    std::ostringstream invalid;
    ny::nil_writer invalid_writer(invalid);
    invalid_writer.emit_text_module(module);
  } catch (const std::runtime_error &error) {
    rejected = std::string(error.what()).find("declaration line") != std::string::npos;
  }
  check(rejected, "text writer rejects negative declaration-line metadata");
}

void test_textual_function_annotations_are_lossless() {
  ny::nil_module module;
  module.module_name = "annotation_lines";
  auto main = make_function(
      "main", "i32", {{"PUSH_CONST", {"0"}, 51}, {"RET", {}, 52}});
  main.source_line = 50;
  main.annotations = {
      "@custom(ordinary)",
      "@ai_context(\"first\nsecond\rcarriage\ttab\")",
      R"(@ai_context("literal \n and \"quotes\""))",
      "@ai_context(\"trailing spaces\")  "};
  module.functions.push_back(main);
  std::stringstream text;
  ny::nil_writer writer(text);
  writer.emit_text_module(module);
  check(text.str().find("@custom(ordinary)\n") != std::string::npos &&
            text.str().find("# function_annotation \"") != std::string::npos &&
            text.str().find("first\nsecond") == std::string::npos,
        "only annotations needing lossless quoting use reserved comment records");
  const auto textual = ny::parseTextNilModule(text);
  std::istringstream binary(serialize_binary(module), std::ios::in | std::ios::binary);
  const auto encoded = ny::parseBinaryNilModule(binary);
  check(textual->functions.size() == 1 && encoded->functions.size() == 1 &&
            textual->functions.front().annotations == main.annotations &&
            encoded->functions.front().annotations == main.annotations &&
            textual->functions.front().source_line == main.source_line,
        "text and binary preserve annotation controls, escapes, order, and trailing whitespace exactly");
  std::istringstream program_input(text.str());
  std::unordered_map<std::string, ny::Value> constants;
  check(!ny::parseProgram(program_input, constants).empty(),
        "VM reader validates quoted annotation records without executing metadata");

  const std::string body = "function main() -> i32:\nPUSH_CONST 0\nRET\nend function\n";
  const std::string annotation = "# function_annotation \"@custom(quoted)\"\n";
  std::vector<std::string> malformed = {
      "# function_annotation\n" + body,
      "# function_annotation @custom\n" + body,
      "# function_annotation \"\"\n" + body,
      "# function_annotation \"not-an-annotation\"\n" + body,
      "# function_annotation \"@custom\\0\"\n" + body,
      "# function_annotation \"@first\" \"@second\"\n" + body,
      "# function_annotation \"@custom\",\n" + body,
      "# function_annotation \"@custom\" ; trailing\n" + body,
      "# function_annotation \"@custom\n" + body,
      annotation,
      body + annotation,
      annotation + "module next\n" + body,
      annotation + "const S0 = \"value\"\n" + body,
      annotation + "@test main\n" + body,
      annotation + "NOP\n" + body,
      "# declaration_line 1\n" + annotation + body,
      "function main() -> i32:\n" + annotation + "PUSH_CONST 0\nRET\nend function\n",
      "struct Record\n" + annotation + "value:i32\nend struct\n" + body,
      "enum Choice\n" + annotation + "Yes\nend enum\n" + body,
      "class Record 0\n" + annotation + "end class\n" + body};
  std::string excessive;
  for (std::size_t index = 0; index < 65537; ++index)
    excessive += annotation;
  malformed.push_back(std::move(excessive) + body);
  for (std::size_t index = 0; index < malformed.size(); ++index) {
    for (const bool metadata_reader : {false, true}) {
      bool rejected = false;
      try {
        std::istringstream input(malformed[index]);
        if (metadata_reader) {
          (void)ny::parseTextNilModule(input);
        } else {
          constants.clear();
          (void)ny::parseProgram(input, constants);
        }
      } catch (const std::runtime_error &) {
        rejected = true;
      }
      check(rejected, std::string(metadata_reader ? "module" : "VM") +
                          " reader rejects malformed quoted annotation case " +
                          std::to_string(index));
    }
  }
  for (const std::string &invalid : {std::string{}, std::string("not-annotation"),
                                     std::string("@bad\0tail", 9)}) {
    module.functions.front().annotations = {invalid};
    bool rejected = false;
    try {
      std::ostringstream output;
      ny::nil_writer invalid_writer(output);
      invalid_writer.emit_text_module(module);
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    check(rejected, "text writer rejects invalid function annotation metadata");
  }
}

void test_binary_library_metadata_round_trip_and_safety() {
  ny::nil_module library;
  library.module_name = "ny::sample::library";
  library.is_library = true;
  library.dependencies = {"ny::lang::core", "ny::lang::io"};
  library.embedded_sources = {
      {"sample/library.ny",
       "module ny::sample::library;\nfn answer() -> i32 { return 42; }\n"}};

  ny::nil_function answer;
  answer.name = "answer";
  answer.body.push_back({"PUSH_CONST", {"42"}, 2});
  answer.body.push_back({"RET", {}, 2});
  library.functions.push_back(answer);

  const std::string bytes = serialize_binary(library);
  const auto metadata = parse_library(bytes);
  check(metadata.module_name == library.module_name,
        "NIL v2 library should retain its logical module name");
  check(metadata.dependencies == library.dependencies,
        "NIL v2 library should retain dependency metadata");
  check(metadata.sources.size() == 1 &&
            metadata.sources[0].path == "sample/library.ny" &&
            metadata.sources[0].contents ==
                library.embedded_sources[0].contents,
        "NIL v2 library should retain its semantic source capsule");

  std::unordered_map<std::string, ny::Value> constants;
  const auto program = parse_binary(bytes, constants);
  check(!program.empty(),
        "the normal NIL loader should structurally validate v2 libraries");

  ny::nil_module executable = library;
  executable.is_library = false;
  bool executable_rejected = false;
  try {
    (void)parse_library(serialize_binary(executable));
  } catch (const std::runtime_error &error) {
    executable_rejected =
        std::string(error.what()).find("not a version 2 library") !=
        std::string::npos;
  }
  check(executable_rejected,
        "library metadata loading should reject executable NIL v1 files");

  for (const std::string &unsafe_path :
       {std::string("../outside.ny"), std::string("sample/./library.ny"),
        std::string("sample//library.ny"), std::string("sample/library.ny/"),
        std::string("sample/library.ny\0hidden", 24)}) {
    ny::nil_module unsafe = library;
    unsafe.embedded_sources[0].path = unsafe_path;
    bool unsafe_rejected = false;
    try {
      (void)parse_library(serialize_binary(unsafe));
    } catch (const std::runtime_error &error) {
      unsafe_rejected =
          std::string(error.what()).find("unsafe source entry") !=
          std::string::npos;
    }
    check(unsafe_rejected,
          "library metadata loading should reject non-canonical or unsafe "
          "source paths");
  }
}

void test_legacy_binary_magic() {
  ny::nil_module module;
  ny::nil_function main;
  main.name = "main";
  main.body.push_back({"RET", {}, 1});
  module.functions.push_back(main);

  const std::string current = ny_test::legacy_executable_bytes(module);
  for (const std::string legacy_magic : {std::string("MIL\0", 4),
                                         std::string("\0LIM", 4)}) {
    std::string legacy = current;
    legacy.replace(0, 4, legacy_magic);
    std::unordered_map<std::string, ny::Value> constants;
    const auto program = parse_binary(legacy, constants);
    check(program.size() == 4 && program[1].op == ny::Opcode::LABEL &&
              program[1].args == std::vector<std::string>{"main"},
          "legacy 0.1 binary magic should remain readable");
  }
}

void test_binary_encoding_is_little_endian_and_streamable() {
  ny::nil_module module;
  module.module_name = "endian_test";
  ny::nil_function main;
  main.name = "main";
  main.body.push_back({"RET", {}, 1});
  module.functions.push_back(main);

  const std::string binary = serialize_binary(module);
  std::stringstream public_writer_stream(std::ios::in | std::ios::out |
                                         std::ios::binary);
  ny::nil_writer public_writer(public_writer_stream);
  public_writer.emit_binary_module(module);
  check(public_writer_stream.str() == binary,
        "nil_writer::emit_binary_module should use the binary serializer");

  check(binary.size() >= 116, "binary NIL should include its complete header");
  if (binary.size() >= 116) {
    check(binary.compare(0, 4, std::string("NIL\0", 4)) == 0,
          "binary NIL should use the current byte-string magic");
    check(read_le_u32(binary, 4) == 3,
          "binary NIL version should be encoded little-endian");
    check(read_le_u32(binary, 8) == 0,
          "binary NIL flags should be encoded little-endian");
    check(read_le_u32(binary, 12) == 5,
          "binary NIL section count should be encoded little-endian");
    check(read_le_u32(binary, 16) == 1 && read_le_u64(binary, 20) == 116,
          "binary NIL section entries should be encoded little-endian");
  }

  nonseekable_stringbuf buffer(binary);
  std::istream stream(&buffer);
  std::unordered_map<std::string, ny::Value> constants;
  const auto program = ny::parseBinaryProgram(stream, constants);
  check(program.size() == 4 && program[1].is_function_entry,
        "binary NIL loading should not require a seekable stream");
}

void test_malformed_binary_structure_is_rejected() {
  ny::nil_module module;
  module.module_name = "malformed_test";
  ny::nil_function main;
  main.name = "main";
  main.body.push_back({"RET", {}, 1});
  module.functions.push_back(main);
  const std::string valid = serialize_binary(module);

  std::string unsupported_version = valid;
  write_le_u32(unsupported_version, 4, 99);
  expect_binary_rejection(unsupported_version, "version",
                          "unsupported binary NIL versions should be rejected");

  std::string unsupported_flags = valid;
  write_le_u32(unsupported_flags, 8, 1);
  expect_binary_rejection(unsupported_flags, "flags",
                          "unknown binary NIL flags should be rejected");

  std::string unknown_section = valid;
  write_le_u32(unknown_section, 16, 99);
  expect_binary_rejection(unknown_section, "section type",
                          "unknown binary NIL sections should be rejected");

  std::string duplicate_section = valid;
  write_le_u32(duplicate_section, 36, 1);
  expect_binary_rejection(duplicate_section, "Duplicate",
                          "duplicate binary NIL sections should be rejected");

  std::string outside_file = valid;
  write_le_u64(outside_file, 20,
               static_cast<std::uint64_t>(outside_file.size()) + 1);
  expect_binary_rejection(outside_file, "outside the file",
                          "out-of-bounds binary NIL sections should be rejected");

  std::string overlapping = valid;
  write_le_u64(overlapping, 40, read_le_u64(overlapping, 20));
  expect_binary_rejection(overlapping, "overlap",
                          "overlapping binary NIL sections should be rejected");

  std::string trailing_section_data = valid;
  const std::uint64_t module_size = read_le_u64(trailing_section_data, 108);
  trailing_section_data.push_back('\0');
  write_le_u64(trailing_section_data, 108, module_size + 1);
  expect_binary_rejection(
      trailing_section_data, "trailing data",
      "trailing bytes inside a known binary NIL section should be rejected");

  expect_binary_rejection(valid.substr(0, valid.size() - 1), "outside the file",
                          "truncated binary NIL sections should be rejected");
}

void test_instruction_shapes_are_rejected_before_execution() {
  for (const ny::nil_instruction invalid : {
           ny::nil_instruction{"POP", {"unexpected"}, 1},
           ny::nil_instruction{"RET", {"unexpected"}, 1},
           ny::nil_instruction{"CALL", {"main"}, 1},
           ny::nil_instruction{"CAST", {}, 1},
           ny::nil_instruction{"CAST", {"i32", "i64"}, 1},
           ny::nil_instruction{"NEW_ARRAY", {"-1"}, 1},
           ny::nil_instruction{"NEW_ARRAY_LITERAL", {"1000001"}, 1},
       }) {
    ny::nil_module module;
    ny::nil_function main;
    main.name = "main";
    main.body.push_back(invalid);
    module.functions.push_back(main);
    expect_binary_rejection(serialize_binary(module), "NIL opcode",
                            "invalid binary opcode operands should be rejected");
  }

  for (const std::string text : {
           std::string("function main:\r\n  POP unexpected\r\nend function\r\n"),
           std::string("function main:\n  RET unexpected\nend function\n"),
           std::string("function main:\n  CALL main\nend function\n"),
           std::string("function main:\n  CAST\nend function\n"),
           std::string("function main:\n  CAST i32 i64\nend function\n"),
           std::string("function main:\n  NEW_ARRAY -1\nend function\n"),
       }) {
    bool rejected = false;
    try {
      std::istringstream stream(text);
      std::unordered_map<std::string, ny::Value> constants;
      (void)ny::parseProgram(stream, constants);
    } catch (const std::runtime_error &error) {
      rejected = std::string(error.what()).find("NIL opcode") !=
                 std::string::npos;
    }
    check(rejected, "invalid textual opcode operands should be rejected");
  }
}

void test_generated_nonvoid_fallthrough_is_explicit_and_fail_closed() {
  ny::nil_module module;
  module.module_name = "fallthrough_test";
  ny::nil_function main;
  main.name = "main";
  main.return_type = "i32";
  main.body.push_back(
      {"RET", {ny::nil_nonvoid_fallthrough_operand}, 17});
  module.functions.push_back(main);

  std::stringstream text_stream;
  ny::nil_writer text_writer(text_stream);
  text_writer.emit_text_module(module);

  std::unordered_map<std::string, ny::Value> text_constants;
  const auto textual = ny::parseProgram(text_stream, text_constants);
  std::unordered_map<std::string, ny::Value> binary_constants;
  const auto binary = parse_binary(serialize_binary(module), binary_constants);

  for (const auto *program : {&textual, &binary}) {
    const auto instruction = std::find_if(
        program->begin(), program->end(), [](const ny::Instruction &candidate) {
          return candidate.op == ny::Opcode::RET;
        });
    check(instruction != program->end() &&
              instruction->args == std::vector<std::string>{
                                       ny::nil_nonvoid_fallthrough_operand},
          "generated fallthrough should survive both NIL formats exactly");

    bool failed_closed = false;
    try {
      ny::VM vm;
      vm.loadProgram(*program, {});
      vm.execute();
    } catch (const std::runtime_error &error) {
      failed_closed =
          std::string(error.what()).find("compiler-generated fallthrough") !=
          std::string::npos;
    }
    check(failed_closed,
          "executed generated non-void fallthrough should fail closed");
  }
}

void test_shared_opcode_mapping() {
  const std::vector<ny::nil_instruction> instructions = {
      {"LOAD_GLOBAL", {"global"}, 11},
      {"STORE_GLOBAL", {"global"}, 12},
      {"STRLEN", {}, 13},
      {"CONSOLE_WRITE", {}, 14},
      {"CONSOLE_WRITELN", {}, 15},
  };
  const std::vector<ny::Opcode> expected = {
      ny::Opcode::LOAD_GLOBAL,     ny::Opcode::STORE_GLOBAL,
      ny::Opcode::STRLEN,         ny::Opcode::CONSOLE_WRITE,
      ny::Opcode::CONSOLE_WRITELN,
  };

  ny::nil_module module;
  ny::nil_function function;
  function.name = "opcodes";
  function.body = instructions;
  module.functions.push_back(function);

  std::unordered_map<std::string, ny::Value> binary_constants;
  const auto binary = parse_binary(serialize_binary(module), binary_constants);

  std::istringstream text(
      "module loader_test\n"
      "function opcodes:\n"
      "  LOAD_GLOBAL global ; line 11\n"
      "  STORE_GLOBAL global ; line 12\n"
      "  STRLEN ; line 13\n"
      "  CONSOLE_WRITE ; line 14\n"
      "  CONSOLE_WRITELN ; line 15\n"
      "end function\n");
  std::unordered_map<std::string, ny::Value> text_constants;
  const auto textual = ny::parseProgram(text, text_constants);

  check(binary.size() == expected.size() + 3,
        "binary opcode fixture should contain one function wrapper");
  check(textual.size() == expected.size() + 3,
        "text opcode fixture should contain one function wrapper");
  if (binary.size() == expected.size() + 3 &&
      textual.size() == expected.size() + 3) {
    for (std::size_t i = 0; i < expected.size(); ++i) {
      check(binary[i + 2].op == expected[i],
            "binary loader should decode every shared opcode");
      check(textual[i + 2].op == expected[i],
            "text loader should decode every shared opcode");
    }
  }
}

void test_unknown_opcodes_are_rejected() {
  ny::nil_module module;
  ny::nil_function main;
  main.name = "main";
  main.body.push_back({"UNKNOWN_TEST_OPCODE", {}, 7});
  module.functions.push_back(main);

  bool binary_rejected = false;
  try {
    std::unordered_map<std::string, ny::Value> constants;
    (void)parse_binary(serialize_binary(module), constants);
  } catch (const std::runtime_error &error) {
    const std::string message = error.what();
    binary_rejected =
        message.find("UNKNOWN_TEST_OPCODE") != std::string::npos &&
        message.find("function 'main'") != std::string::npos;
  }
  check(binary_rejected,
        "binary loader should reject unknown opcodes with function context");

  bool text_rejected = false;
  try {
    std::istringstream text(
        "module loader_test\n"
        "struct ignored_metadata\n"
        "  value: i32\n"
        "end struct\n"
        "@test\n"
        "function main:\n"
        "  UNKNOWN_TEST_OPCODE\n"
        "end function\n");
    std::unordered_map<std::string, ny::Value> constants;
    (void)ny::parseProgram(text, constants);
  } catch (const std::runtime_error &error) {
    const std::string message = error.what();
    text_rejected =
        message.find("UNKNOWN_TEST_OPCODE") != std::string::npos &&
        message.find("textual NIL line 7") != std::string::npos;
  }
  check(text_rejected,
        "text loader should reject unknown opcodes while accepting metadata");
}

void test_vm_entry_and_exit_queries() {
  const std::vector<ny::Instruction> program = {
      {ny::Opcode::LABEL, {"main"}, 0, true},
      {ny::Opcode::PUSH_CONST, {"7"}, 0},
      {ny::Opcode::RET, {}, 0},
  };
  ny::VM vm;
  vm.loadProgram(program, {});
  check(vm.hasFunction("main"), "VM should expose its main entry point");
  vm.execute();
  check(vm.getProgramExitCode() == 7,
        "VM should expose a top-level integer result as the exit code");

  ny::VM library_vm;
  library_vm.loadProgram({{ny::Opcode::LABEL, {"helper"}, 0}}, {});
  check(!library_vm.hasFunction("main"),
        "VM should distinguish loadable libraries from programs with main");
}

void test_function_arity_survives_both_nil_formats() {
  ny::nil_module module;
  ny::nil_function helper;
  helper.name = "helper";
  helper.params.push_back("callback: fn(i32, i32) -> i32");
  helper.body.push_back({"STORE_LOCAL", {"callback"}, 2});
  helper.body.push_back({"RET", {}, 3});
  module.functions.push_back(helper);

  std::unordered_map<std::string, ny::Value> binary_constants;
  const auto binary = parse_binary(serialize_binary(module), binary_constants);

  std::istringstream text(
      "module loader_test\n"
      "function helper(callback: fn(i32, i32) -> i32):\n"
      "  STORE_LOCAL callback\n"
      "  RET\n"
      "end function\n");
  std::unordered_map<std::string, ny::Value> text_constants;
  const auto textual = ny::parseProgram(text, text_constants);

  for (const auto *program : {&binary, &textual}) {
    check(program->size() >= 2 && (*program)[1].is_function_entry &&
              (*program)[1].function_param_count == 1,
          "function parameter count should survive NIL loading");

    bool rejected = false;
    try {
      ny::VM vm;
      vm.loadProgram(*program, {});
      vm.callFunction("helper", 0);
    } catch (const std::runtime_error &error) {
      rejected = std::string(error.what()).find("expects 1 arguments, got 0") !=
                 std::string::npos;
    }
    check(rejected,
          "VM embedding calls should reject mismatched function arity");
  }
}

void test_function_return_types_survive_both_nil_formats() {
  ny::nil_module module;
  module.functions = {
      make_function("legacy", "", {{"RET", {}, 1}}),
      make_function("empty", "void", {{"RET", {}, 1}}),
      make_function("scalar", "i64", {{"PUSH_CONST", {"7"}, 1}, {"RET", {}, 2}}),
      make_function("callback", "Result<i64, text>", {{"RET", {}, 1}},
                    {"callback: fn(i32, i32) -> i32"}),
  };
  for (const bool binary : {false, true}) {
    std::unordered_map<std::string, ny::Value> constants;
    const auto program = parse_module(module, binary, constants);
    for (const auto &function : module.functions) {
      const auto entry = std::find_if(
          program.begin(), program.end(), [&](const ny::Instruction &instruction) {
            return instruction.is_function_entry &&
                   instruction.args == std::vector<std::string>{function.name};
          });
      check(entry != program.end() &&
                entry->function_return_type == function.return_type,
            std::string(binary ? "binary" : "text") + ": return type of " +
                function.name + " survives loading exactly");
    }
  }
}

void test_vm_typed_return_contract() {
  struct return_case {
    std::string name;
    std::string return_type;
    std::vector<ny::nil_instruction> body;
    bool reject;
    std::vector<int64_t> values;
    std::string diagnostic = "invalid RET stack";
  };
  const std::vector<return_case> cases = {
      {"typed-value", "i64", {{"PUSH_CONST", {"7"}, 1}, {"RET", {}, 2}},
       false, {7}},
      {"typed-void", "void", {{"RET", {}, 1}}, false, {}},
      {"missing-value", "i64", {{"RET", {}, 1}}, true, {}},
      {"extra-value", "i64",
       {{"PUSH_CONST", {"7"}, 1}, {"PUSH_CONST", {"8"}, 2}, {"RET", {}, 3}},
       true, {}},
      {"void-value", "void", {{"PUSH_CONST", {"7"}, 1}, {"RET", {}, 2}},
       true, {}},
      {"scalar-fallthrough", "i64", {{"PUSH_CONST", {"7"}, 1}}, true, {},
       "without RET"},
      {"void-fallthrough", "void", {}, true, {}, "without RET"},
      {"legacy-empty", "", {{"RET", {}, 1}}, false, {}},
      {"legacy-values", "",
       {{"PUSH_CONST", {"7"}, 1}, {"PUSH_CONST", {"8"}, 2}, {"RET", {}, 3}},
       false, {7, 8}},
  };
  for (const auto &test : cases) {
    ny::nil_module module;
    module.functions.push_back(make_function("main", test.return_type, test.body));
    for (const bool binary : {false, true}) {
      const std::string description =
          std::string(binary ? "binary" : "text") + ": " + test.name;
      std::unordered_map<std::string, ny::Value> constants;
      const auto program = parse_module(module, binary, constants);
      ny::VM vm;
      vm.loadProgram(program, constants);
      bool rejected = false;
      try {
        vm.execute();
      } catch (const std::runtime_error &error) {
        rejected = true;
        check(std::string(error.what()).find(test.diagnostic) !=
                  std::string::npos,
              description + ": precise return-stack diagnostic: " + error.what());
      }
      check(rejected == test.reject, description + ": typed return outcome");
      if (!rejected && !test.reject) {
        check(vm.getStack().size() == test.values.size(),
              description + ": return operand count");
        for (std::size_t i = 0;
             i < test.values.size() && i < vm.getStack().size(); ++i)
          check(std::holds_alternative<int64_t>(vm.getStack()[i]) &&
                    std::get<int64_t>(vm.getStack()[i]) == test.values[i],
                description + ": return operand value");
      }
    }
  }

  // A callee's typed return is checked against its own operand segment, never
  // against a suspended caller expression, including for embedded host calls.
  for (const auto &test : cases) {
    if (!test.reject)
      continue;
    ny::nil_module module;
    module.functions = {
        make_function("helper", test.return_type, test.body),
        make_function("main", "i64",
                      {{"PUSH_CONST", {"91"}, 1}, {"CALL", {"helper", "0"}, 2},
                       {"RET", {}, 3}}),
    };
    for (const bool binary : {false, true}) {
      std::unordered_map<std::string, ny::Value> constants;
      const auto program = parse_module(module, binary, constants);
      for (const bool embedded : {false, true}) {
        const std::string description =
            std::string(binary ? "binary" : "text") + ": " + test.name +
            (embedded ? " embedded callee" : " NIL callee");
        ny::VM vm;
        vm.loadProgram(program, constants);
        bool rejected = false;
        try {
          if (embedded) {
            vm.pushValue(int64_t(91));
            vm.callFunction("helper", 0);
          } else {
            vm.execute();
          }
        } catch (const std::runtime_error &error) {
          rejected = std::string(error.what()).find(test.diagnostic) !=
                     std::string::npos;
        }
        check(rejected, description + ": malformed callee return rejected");
        check(!vm.getStack().empty() &&
                  std::holds_alternative<int64_t>(vm.getStack().front()) &&
                  std::get<int64_t>(vm.getStack().front()) == 91,
              description + ": suspended caller operand retained");
      }
    }
  }
}

ny::Value return_contract_adapter(const std::vector<ny::Value> &arguments,
                                  ny::VM *) {
  if (!arguments.empty())
    throw std::runtime_error("return-contract adapter expected no arguments");
  return int64_t(37);
}

void test_vm_void_interop_has_no_result() {
  ny::nil_module module;
  module.functions = {
      make_function("helper", "void",
                    {{"PUSH_CONST", {"0"}, 1},
                     {"INTEROP_CALL", {"std_time", "sleep", "1", "void"}, 2},
                     {"INTEROP_CALL", {"intrinsic", "gc.enter_critical", "0", "void"}, 3},
                     {"INTEROP_CALL", {"intrinsic", "gc.leave_critical", "0", "void"}, 4},
                     {"INTEROP_CALL", {"test", "adapter", "0", "void"}, 5},
                     {"RET", {}, 6}}),
      make_function("main", "i64",
                    {{"PUSH_CONST", {"91"}, 1}, {"CALL", {"helper", "0"}, 2},
                     {"INTEROP_CALL", {"test", "adapter", "0", "i64"}, 3},
                     {"ADD", {}, 4}, {"RET", {}, 5}}),
  };
  for (const bool binary : {false, true}) {
    std::unordered_map<std::string, ny::Value> constants;
    const auto program = parse_module(module, binary, constants);
    ny::VM vm;
    vm.loadProgram(program, constants);
    vm.setGlobal("adapter", ny::ny::NyForeignFunc(
                                reinterpret_cast<void *>(&return_contract_adapter), 0));
    bool completed = false;
    try {
      vm.execute();
      completed = true;
    } catch (const std::runtime_error &error) {
      check(false, std::string(binary ? "binary" : "text") +
                       ": void interop failed: " + error.what());
    }
    check(completed && vm.getStack().size() == 1 && vm.getProgramExitCode() == 128,
          std::string(binary ? "binary" : "text") +
              ": void interop discards bridge/dummy results but preserves caller "
              "operands and nonvoid results");
  }
}

void test_vm_typed_function_cannot_escape_without_return() {
  for (const std::string &target :
       {std::string("legacy"), std::string("legacy_body"),
        std::string("$ny.internal.function-end.0"),
        std::string("$ny.internal.function-end.1")}) {
    ny::nil_module module;
    module.functions = {
        make_function("main", "i64", {{"JMP", {target}, 1}}),
        make_function("legacy", "",
                      {{"LABEL", {"legacy_body"}, 1},
                       {"PUSH_CONST", {"7"}, 2}, {"RET", {}, 3}}),
    };
    for (const bool binary : {false, true}) {
      std::unordered_map<std::string, ny::Value> constants;
      const auto program = parse_module(module, binary, constants);
      ny::VM vm;
      vm.loadProgram(program, constants);
      bool rejected = false;
      try {
        vm.execute();
      } catch (const std::runtime_error &error) {
        rejected = std::string(error.what()).find("without RET") !=
                   std::string::npos;
      }
      check(rejected, std::string(binary ? "binary" : "text") +
                          ": typed function rejects escape to " + target);
    }
  }
}

void test_vm_trapped_process_exit_is_not_a_return() {
  // Process exit is a successful non-returning operation, even when reached
  // inside a typed callee with a suspended caller operand. Legacy interop with
  // no explicit result type keeps its historical behavior too.
  for (const std::string &return_type :
       {std::string("void"), std::string("i64"), std::string()}) {
    std::vector<std::string> arguments = {"std_sys", "exit", "1"};
    if (!return_type.empty())
      arguments.push_back(return_type);
    ny::nil_module module;
    module.functions = {
        make_function("exit_now", "i64",
                      {{"PUSH_CONST", {"23"}, 1}, {"INTEROP_CALL", arguments, 2}}),
        make_function("main", "i64",
                      {{"PUSH_CONST", {"91"}, 1}, {"CALL", {"exit_now", "0"}, 2},
                       {"PUSH_CONST", {"99"}, 3}, {"RET", {}, 4}}),
    };
    for (const bool binary : {false, true}) {
      std::unordered_map<std::string, ny::Value> constants;
      const auto program = parse_module(module, binary, constants);
      ny::VM vm;
      vm.loadProgram(program, constants);
      vm.setProcessExitTrapping(true);
      bool completed = false;
      try {
        vm.execute();
        completed = true;
      } catch (const std::runtime_error &error) {
        check(false, std::string(binary ? "binary" : "text") +
                         ": trapped process exit failed: " + error.what());
      }
      check(completed && vm.getRequestedExitCode() == 23 &&
                vm.getProgramExitCode() == 91 && vm.getStack().size() == 1 &&
                std::get<int64_t>(vm.getStack().front()) == 91,
            std::string(binary ? "binary" : "text") +
                ": trapped process exit bypasses RET and interop result checks");
    }
  }
}

void test_vm_set_index_consumes_all_operands() {
  struct storage_case {
    std::string name;
    std::vector<ny::nil_instruction> creation;
  };
  const std::vector<storage_case> cases = {
      {"array literal", {{"PUSH_CONST", {"7"}, 2},
                         {"NEW_ARRAY_LITERAL", {"1", "i64"}, 3}}},
      {"pointer", {{"PUSH_CONST", {"1"}, 2}, {"ALLOC", {"i64"}, 3}}},
      {"fixed-size array", {{"NEW_ARRAY", {"2"}, 3}}},
  };
  for (const auto &test : cases) {
    std::vector<ny::nil_instruction> body = {{"PUSH_CONST", {"13"}, 1}};
    body.insert(body.end(), test.creation.begin(), test.creation.end());
    const std::vector<ny::nil_instruction> mutation = {
        {"STORE_LOCAL", {"storage"}, 4}, {"LOAD_LOCAL", {"storage"}, 5},
        {"PUSH_CONST", {"0"}, 6}, {"PUSH_CONST", {"42"}, 7},
        {"SET_INDEX", {}, 8}, {"LOAD_LOCAL", {"storage"}, 9},
        {"PUSH_CONST", {"0"}, 10}, {"GET_INDEX", {}, 11},
        {"ADD", {}, 12}, {"RET", {}, 13},
    };
    body.insert(body.end(), mutation.begin(), mutation.end());
    ny::nil_module module;
    module.functions = {
        make_function("mutate", "i64", body),
        make_function("main", "i64",
                      {{"PUSH_CONST", {"91"}, 1}, {"CALL", {"mutate", "0"}, 2},
                       {"ADD", {}, 3}, {"RET", {}, 4}}),
    };
    for (const bool binary : {false, true}) {
      const std::string description =
          std::string(binary ? "binary" : "text") + " " + test.name + " SET_INDEX";
      std::unordered_map<std::string, ny::Value> constants;
      const auto program = parse_module(module, binary, constants);
      ny::VM vm;
      vm.loadProgram(program, constants);
      bool completed = false;
      try {
        vm.execute();
        completed = true;
      } catch (const std::runtime_error &error) {
        check(false, description + ": assignment failed: " + error.what());
      }
      check(completed && vm.getStack().size() == 1 && vm.getProgramExitCode() == 146,
            description + ": assignment has no result, GET_INDEX observes the "
                          "write, and pending local/caller operands survive");
    }
  }
}

void test_vm_set_index_rejects_embedded_value_arrays() {
  ny::nil_module module;
  module.functions = {
      make_function("mutate", "void",
                    {{"STORE_LOCAL", {"array"}, 1}, {"LOAD_LOCAL", {"array"}, 2},
                     {"PUSH_CONST", {"0"}, 3}, {"PUSH_CONST", {"99"}, 4},
                     {"SET_INDEX", {}, 5}, {"RET", {}, 6}},
                    {"array: array<i64>"}),
  };
  for (const bool binary : {false, true}) {
    std::unordered_map<std::string, ny::Value> constants;
    const auto program = parse_module(module, binary, constants);
    ny::VM vm;
    vm.loadProgram(program, constants);
    ny::ArrayValue value_array;
    value_array.elements.push_back(int64_t(7));
    vm.pushValue(int64_t(91));
    vm.pushValue(value_array);
    bool rejected = false;
    try {
      vm.callFunction("mutate", 1);
    } catch (const std::runtime_error &error) {
      const std::string diagnostic = error.what();
      rejected = diagnostic.find("SET_INDEX") != std::string::npos &&
                 diagnostic.find("legacy value-array writes are unsupported") !=
                     std::string::npos;
    }
    check(rejected && vm.getStack().size() == 1 &&
              std::get<int64_t>(vm.getStack().front()) == 91 &&
              std::get<int64_t>(value_array.elements.front()) == 7,
          std::string(binary ? "binary" : "text") +
              ": embedded ArrayValue mutation fails explicitly without changing "
              "the host array or suspended caller operand");
  }
}

void test_vm_declared_functions_precede_legacy_builtins() {
  for (const std::string &name :
       {std::string("String__len"), std::string("array__push"),
        std::string("core__print"), std::string("println"),
        std::string("panic"), std::string("f64_to_str"),
        std::string("user__f64_to_str_extra"),
        std::string("user_vm__get_address_suffix")}) {
    for (const bool indirect : {false, true}) {
      ny::nil_module module;
      module.functions.push_back(make_function(
          name, "i64",
          {{"STORE_LOCAL", {"right"}, 1}, {"STORE_LOCAL", {"left"}, 2},
           {"LOAD_LOCAL", {"left"}, 3}, {"PUSH_CONST", {"10"}, 4},
           {"MUL", {}, 5}, {"LOAD_LOCAL", {"right"}, 6},
           {"ADD", {}, 7}, {"RET", {}, 8}},
          {"left: i64", "right: i64"}));
      std::vector<ny::nil_instruction> body;
      if (indirect) {
        body.push_back({"PUSH_FN_PTR", {name}, 1});
        body.push_back({"STORE_LOCAL", {"String__len"}, 2});
      }
      const std::vector<ny::nil_instruction> call = {
          {"PUSH_CONST", {"91"}, 3}, {"PUSH_CONST", {"3"}, 4},
          {"PUSH_CONST", {"4"}, 5},
          {"CALL", {indirect ? "String__len" : name, "2"}, 6},
          {"ADD", {}, 7}, {"RET", {}, 8},
      };
      body.insert(body.end(), call.begin(), call.end());
      module.functions.push_back(make_function("main", "i64", body));
      for (const bool binary : {false, true}) {
        const std::string description =
            std::string(binary ? "binary" : "text") +
            (indirect ? " indirect " : " direct ") + name;
        std::unordered_map<std::string, ny::Value> constants;
        const auto program = parse_module(module, binary, constants);
        ny::VM vm;
        vm.loadProgram(program, constants);
        std::ostringstream output;
        vm.setOutputStream(output);
        bool completed = false;
        try {
          vm.execute();
          completed = true;
        } catch (const std::runtime_error &error) {
          check(false, description + ": declared call failed: " + error.what());
        }
        check(completed && vm.getStack().size() == 1 &&
                  vm.getProgramExitCode() == 125 && output.str().empty(),
              description + ": declared arity/body precedes builtin name dispatch");
      }
    }
  }

  // Explicit intrinsic wrapper bodies continue to work even when their public
  // function name is also a legacy builtin spelling.
  ny::nil_module wrapper;
  wrapper.string_constants.push_back({"number", "37"});
  wrapper.functions = {
      make_function("array__len", "i32",
                    {{"STORE_LOCAL", {"value"}, 1}, {"LOAD_LOCAL", {"value"}, 2},
                     {"INTEROP_CALL", {"intrinsic", "convert.to_i32", "1", "i32"}, 3},
                     {"RET", {}, 4}},
                    {"value: text"}),
      make_function("main", "i32",
                    {{"PUSH_CONST_STR", {"number"}, 1},
                     {"CALL", {"array__len", "1"}, 2}, {"RET", {}, 3}}),
  };
  for (const bool binary : {false, true}) {
    std::unordered_map<std::string, ny::Value> constants;
    const auto program = parse_module(wrapper, binary, constants);
    ny::VM vm;
    vm.loadProgram(program, constants);
    vm.execute();
    check(vm.getStack().size() == 1 && vm.getProgramExitCode() == 37,
          std::string(binary ? "binary" : "text") +
              ": declared builtin-named wrappers retain explicit intrinsic behavior");
  }
}

void test_array_scalar_metadata_aliases() {
  for (const bool binary : {false, true}) {
    const auto prefix = std::string(binary ? "binary" : "text") + ": ";
    {
      ny::nil_module module;
      module.structs.push_back({"ExactCase", {{"answer", "i32"}}});
      module.functions.push_back(make_function("main", "ExactCase", {
          {"NEW_STRUCT", {"ExactCase"}, 1}, {"PUSH_CONST", {"37"}, 2},
          {"SET_FIELD", {"answer"}, 3}, {"NEW_ARRAY_LITERAL", {"1", "ExactCase"}, 4},
          {"PUSH_CONST", {"0"}, 5}, {"GET_INDEX", {}, 6}, {"RET", {}, 7}}));
      std::unordered_map<std::string, ny::Value> constants;
      ny::VM vm;
      vm.loadProgram(parse_module(module, binary, constants), constants);
      vm.execute();
      check(vm.getStack().size() == 1 &&
                std::holds_alternative<ny::StructValue>(vm.getStack().back()) &&
                std::get<ny::StructValue>(vm.getStack().back()).type_name == "ExactCase",
            prefix + "normalizing scalar aliases preserves nominal aggregate names");
    }
    for (const std::string element : {"F64", " f64 ", "f 64", "TEXT", " text ",
                                       "t ext", "String", " STR "}) {
      const bool floating = element.find('6') != std::string::npos;
      const std::string canonical = floating ? "f64" : "text";
      for (const bool wrong_value : {false, true}) {
        ny::nil_module module;
        module.string_constants.push_back({"message", "array text"});
        // A concrete intrinsic must not re-label arbitrary contents from a
        // literal whose built-in element type uses an equivalent spelling.
        const bool use_text = floating == wrong_value;
        module.functions.push_back(make_function("main", "void", {
            {use_text ? "PUSH_CONST_STR" : "PUSH_CONST",
             {use_text ? "message" : "1.25"}, 1},
            {"NEW_ARRAY_LITERAL", {"1", element}, 2},
            {"PUSH_CONST", {"0"}, 3},
            {"INTEROP_CALL", {"intrinsic", "array.get", "2", canonical, canonical}, 4},
            {"STORE_GLOBAL", {"result", canonical}, 5}, {"RET", {}, 6}}));
        bool rejected = false;
        try {
          std::unordered_map<std::string, ny::Value> constants;
          ny::VM vm;
          vm.loadProgram(parse_module(module, binary, constants), constants);
          vm.execute();
        } catch (const std::runtime_error &error) {
          const std::string diagnostic = error.what();
          rejected = diagnostic.find(floating ? "expected an f64 value"
                                              : "expected a text value") != std::string::npos;
          check(rejected, prefix + "scalar alias failure has a precise value diagnostic");
        }
        check(rejected == wrong_value,
              prefix + "scalar aliases enforce matching literal element kinds");
      }
      for (const bool intrinsic : {false, true}) {
        ny::nil_module module;
        auto body = std::vector<ny::nil_instruction>{
            {"NEW_ARRAY_LITERAL", {"0", element}, 1}, {"PUSH_CONST", {"7"}, 2}};
        body.push_back(intrinsic
            ? ny::nil_instruction{"INTEROP_CALL",
                {"intrinsic", "array.get", "2", canonical, canonical}, 3}
            : ny::nil_instruction{"GET_INDEX", {}, 3});
        body.push_back({"STORE_GLOBAL", {"result", canonical}, 4});
        body.push_back({"RET", {}, 5});
        module.functions.push_back(make_function("main", "void", body));
        bool rejected = false;
        try {
          std::unordered_map<std::string, ny::Value> constants;
          ny::VM vm;
          vm.loadProgram(parse_module(module, binary, constants), constants);
          vm.execute();
        } catch (const std::runtime_error &) {
          rejected = true;
        }
        check(!rejected, prefix + "scalar aliases have type-correct out-of-bounds defaults");
      }
    }
  }
}

void test_typed_global_storage_contract() {
  for (const bool binary : {false, true}) {
    const auto prefix = std::string(binary ? "binary" : "text") + ": ";
    for (const auto &types : std::vector<std::vector<std::string>>{
             {"value", "text"}, {"value"}}) {
      ny::nil_module module;
      module.string_constants.push_back({"message", "typed global"});
      module.functions.push_back(make_function("main", "i32", {
          {types.size() == 2 ? "PUSH_CONST_STR" : "PUSH_CONST",
           {types.size() == 2 ? "message" : "37"}, 1},
          {"STORE_GLOBAL", types, 2}, {"LOAD_GLOBAL", types, 3},
          {"POP", {}, 4}, {"PUSH_CONST", {"0"}, 5}, {"RET", {}, 6}}));
      std::unordered_map<std::string, ny::Value> constants;
      ny::VM vm;
      vm.loadProgram(parse_module(module, binary, constants), constants);
      vm.execute();
      check(vm.getProgramExitCode() == 0,
            prefix + "typed text and legacy scalar global storage are accepted");
    }
    for (const auto &load : std::vector<std::vector<std::string>>{
             {"value"}, {"value", "i64"}, {"value", ""},
             {"value", "ptr<i32>"}, {"value", "text", "extra"}}) {
      ny::nil_module module;
      module.functions.push_back(make_function("main", "i32", {
          {"PUSH_CONST", {"0"}, 1}, {"STORE_GLOBAL", {"value", "text"}, 2},
          {"LOAD_GLOBAL", load, 3}, {"POP", {}, 4},
          {"PUSH_CONST", {"0"}, 5}, {"RET", {}, 6}}));
      bool rejected = false;
      try {
        std::unordered_map<std::string, ny::Value> constants;
        (void)parse_module(module, binary, constants);
      } catch (const std::runtime_error &error) {
        const std::string diagnostic = error.what();
        rejected = diagnostic.find("storage type") != std::string::npos ||
                   diagnostic.find("expects 1 to 2 operand(s)") != std::string::npos;
      }
      check(rejected, prefix + "conflicting, erased, or malformed global metadata rejects");
      bool module_rejected = false;
      try {
        std::stringstream serialized;
        if (binary) {
          serialized.str(serialize_binary(module));
          (void)ny::parseBinaryNilModule(serialized);
        } else {
          ny::nil_writer writer(serialized);
          writer.emit_text_module(module);
          (void)ny::parseTextNilModule(serialized);
        }
      } catch (const std::runtime_error &) {
        module_rejected = true;
      }
      check(module_rejected, prefix + "native module loader rejects invalid global metadata");
    }
    for (const auto &body : std::vector<std::vector<ny::nil_instruction>>{
             {{"LOAD_GLOBAL", {"value", "text"}, 1}},
             {{"PUSH_CONST", {"7"}, 1}, {"STORE_GLOBAL", {"value", "text"}, 2}},
             {{"PUSH_CONST_STR", {"message"}, 1}, {"STORE_GLOBAL", {"value", "i64"}, 2}},
             {{"PUSH_CONST", {"1.5"}, 1}, {"STORE_GLOBAL", {"value", "i64"}, 2}},
             {{"PUSH_CONST", {"7"}, 1}, {"STORE_GLOBAL", {"value", "f64"}, 2}},
             {{"PUSH_CONST", {"256"}, 1}, {"STORE_GLOBAL", {"value", "u8"}, 2}},
             {{"PUSH_CONST", {"2"}, 1}, {"STORE_GLOBAL", {"value", "bool"}, 2}}}) {
      ny::nil_module module;
      module.string_constants.push_back({"message", "typed global"});
      module.functions.push_back(make_function("main", "void", body));
      module.functions.back().body.push_back({"RET", {}, 3});
      bool rejected = false;
      try {
        std::unordered_map<std::string, ny::Value> constants;
        ny::VM vm;
        vm.loadProgram(parse_module(module, binary, constants), constants);
        vm.execute();
      } catch (const std::runtime_error &error) {
        const std::string diagnostic = error.what();
        rejected = diagnostic.find("Global not found") != std::string::npos ||
                   diagnostic.find("does not match storage type") != std::string::npos;
      }
      check(rejected, prefix + "global value mismatch/read before store fails at runtime");
    }
  }
}

void test_vm_scalar_arithmetic_widths() {
  struct arithmetic_case {
    const char *left;
    const char *right;
    std::vector<std::string> types;
    int64_t expected;
  };
  for (const auto &test : std::vector<arithmetic_case>{
           {"2147483647", "1", {"i32"}, -2147483647LL - 1},
           {"255", "1", {"u8"}, 0},
           {"2147483647", "1", {}, 2147483648LL},
           {"9223372036854775807", "1", {"i64"}, INT64_MIN}}) {
    ny::nil_module module;
    module.functions.push_back(make_function(
        "main", "i64", {{"PUSH_CONST", {test.left}, 1},
                        {"PUSH_CONST", {test.right}, 2},
                        {"ADD", test.types, 3}, {"RET", {}, 4}}));
    for (const bool binary : {false, true}) {
      std::unordered_map<std::string, ny::Value> constants;
      ny::VM vm;
      vm.loadProgram(parse_module(module, binary, constants), constants);
      vm.execute();
      check(vm.getStack().size() == 1 &&
                std::holds_alternative<int64_t>(vm.getStack().back()) &&
                std::get<int64_t>(vm.getStack().back()) == test.expected,
            std::string(binary ? "binary" : "text") +
                ": arithmetic uses explicit width or legacy i64 width");
    }
  }
  for (const auto &operation : std::vector<ny::nil_instruction>{
           {"ADD", {"bool"}, 3}, {"BIT_AND", {"f64"}, 3},
           {"NEG", {"i32", "i64"}, 3}}) {
    ny::nil_module module;
    module.functions.push_back(make_function(
        "main", "i64", {{"PUSH_CONST", {"1"}, 1},
                        {"PUSH_CONST", {"2"}, 2}, operation, {"RET", {}, 4}}));
    for (const bool binary : {false, true}) {
      bool rejected = false;
      try {
        std::unordered_map<std::string, ny::Value> constants;
        (void)parse_module(module, binary, constants);
      } catch (const std::runtime_error &error) {
        const std::string message = error.what();
        rejected = message.find("unsupported scalar arithmetic type") != std::string::npos ||
                   message.find("expects 0 to 1 operand(s)") != std::string::npos;
      }
      check(rejected, "malformed arithmetic metadata rejected before execution");
    }
  }
}

void test_vm_scalar_cast_boundaries() {
  const std::vector<double> invalid_floats = {
      9223372036854775808.0, -9223372036854777856.0,
      std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity(),
  };
  for (const std::string &target :
       {std::string("i32"), std::string("i64"), std::string("u8")}) {
    ny::nil_module module;
    module.functions.push_back(make_function(
        "convert", target,
        {{"STORE_LOCAL", {"value"}, 1}, {"LOAD_LOCAL", {"value"}, 2},
         {"CAST", {target}, 3}, {"RET", {}, 4}},
        {"value: f64"}));
    for (const bool binary : {false, true}) {
      std::unordered_map<std::string, ny::Value> constants;
      const auto program = parse_module(module, binary, constants);
      for (const double value : invalid_floats) {
        ny::VM vm;
        vm.loadProgram(program, constants);
        vm.pushValue(int64_t(91));
        vm.pushValue(value);
        bool rejected = false;
        try {
          vm.callFunction("convert", 1);
        } catch (const std::runtime_error &error) {
          rejected = std::string(error.what()).find(
                         "floating-point value is non-finite or outside i64 range") !=
                     std::string::npos;
        }
        check(rejected && vm.getStack().size() == 1 &&
                  std::get<int64_t>(vm.getStack().front()) == 91,
              std::string(binary ? "binary" : "text") + ": " + target +
                  " rejects unsafe floating conversion before a host integer cast");
      }
    }
  }
  for (const std::string &target :
       {std::string("i8"), std::string("i16"), std::string("u16"),
        std::string("u32"), std::string("u64"), std::string("isize"),
        std::string("usize"), std::string("char"), std::string("f32")}) {
    ny::nil_module module;
    module.functions.push_back(make_function(
        "main", target,
        {{"PUSH_CONST", {"7"}, 1}, {"CAST", {target}, 2}, {"RET", {}, 3}}));
    for (const bool binary : {false, true}) {
      std::unordered_map<std::string, ny::Value> constants;
      const auto program = parse_module(module, binary, constants);
      ny::VM vm;
      vm.loadProgram(program, constants);
      bool rejected = false;
      try {
        vm.execute();
      } catch (const std::runtime_error &error) {
        rejected = std::string(error.what()).find("CAST: unsupported scalar target") !=
                   std::string::npos;
      }
      check(rejected, std::string(binary ? "binary" : "text") + ": CAST to " +
                          target + " does not silently preserve a scalar word");
    }
  }
}

void test_textual_quoted_operands_round_trip_as_one_argument() {
  const std::string generic_type = "Result<i32, text>";

  ny::nil_module module;
  module.module_name = "quoted_operand_test";
  ny::nil_function main;
  main.name = "main";
  main.body.push_back({"PUSH_CONST", {"23"}, 10});
  main.body.push_back({"CAST", {generic_type}, 11});
  main.body.push_back({"RET", {}, 12});
  module.functions.push_back(main);

  std::stringstream text_stream;
  ny::nil_writer text_writer(text_stream);
  text_writer.emit_text_module(module);
  std::unordered_map<std::string, ny::Value> text_constants;
  const auto textual = ny::parseProgram(text_stream, text_constants);

  std::unordered_map<std::string, ny::Value> binary_constants;
  const auto binary = parse_binary(serialize_binary(module), binary_constants);

  for (const auto *program : {&textual, &binary}) {
    check(program->size() == 6,
          "quoted-operand fixture should retain its function wrapper");
    if (program->size() == 6) {
      check((*program)[3].op == ny::Opcode::CAST &&
                (*program)[3].args == std::vector<std::string>{generic_type},
            "a quoted generic type containing whitespace should remain one "
            "exact operand");
      check((*program)[3].line == 11,
            "a comment after a quoted operand should retain debug metadata");
    }

    ny::VM vm;
    vm.loadProgram(*program, {});
    bool rejected = false;
    try {
      vm.execute();
    } catch (const std::runtime_error &error) {
      rejected = std::string(error.what()).find(
                     "CAST: unsupported target '" + generic_type + "'") !=
                 std::string::npos;
    }
    check(rejected,
          "quoted nominal targets remain exact and cannot turn a scalar into an enum");
  }

  for (const std::string malformed : {
           std::string("function main:\n  CAST \"Result<i32, text>\n"
                       "end function\n"),
           std::string("function main:\n  CAST \"i32\"suffix\n"
                       "end function\n"),
       }) {
    bool rejected = false;
    try {
      std::istringstream stream(malformed);
      std::unordered_map<std::string, ny::Value> constants;
      (void)ny::parseProgram(stream, constants);
    } catch (const std::runtime_error &error) {
      rejected = std::string(error.what()).find("quoted operand") !=
                 std::string::npos;
    }
    check(rejected,
          "malformed quoted textual operands should be rejected explicitly");
  }
}

void test_duplicate_and_missing_labels_are_rejected() {
  for (const std::vector<ny::Instruction> program : {
           std::vector<ny::Instruction>{
               {ny::Opcode::LABEL, {"duplicate"}, 0},
               {ny::Opcode::LABEL, {"duplicate"}, 0}},
           std::vector<ny::Instruction>{{ny::Opcode::LABEL, {}, 0}}}) {
    bool rejected = false;
    try {
      ny::VM vm;
      vm.loadProgram(program, {});
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    check(rejected, "invalid NIL labels should be rejected during loading");
  }
}

void test_vm_call_frame_operand_isolation() {
  struct frame_case {
    std::string name;
    std::vector<ny::nil_instruction> body;
    bool should_reject;
  };
  const std::vector<frame_case> cases = {
      {"empty-pop", {{"POP", {}, 1}}, true},
      {"optional-array-size", {{"NEW_ARRAY", {}, 1}, {"POP", {}, 2}}, false},
      {"optional-allocation-count",
       {{"ALLOC", {"i64"}, 1}, {"FREE", {}, 2}}, false},
      {"store-underflow", {{"STORE_LOCAL", {"stolen"}, 1}}, true},
      {"arithmetic-underflow",
       {{"PUSH_CONST", {"1"}, 1}, {"ADD", {}, 2}}, true},
      {"print-underflow", {{"CALL", {"println", "1"}, 1}}, true},
      {"partial-print",
       {{"PUSH_CONST", {"12"}, 1}, {"CALL", {"println", "2"}, 2}}, true},
      {"interop-underflow",
       {{"INTEROP_CALL", {"intrinsic", "convert.to_i32", "1", "i32"}, 1}},
       true},
      {"field-underflow",
       {{"PUSH_CONST", {"1"}, 1}, {"SET_FIELD", {"value"}, 2}}, true},
      {"index-underflow",
       {{"PUSH_CONST", {"0"}, 1}, {"PUSH_CONST", {"1"}, 2},
        {"SET_INDEX", {}, 3}}, true},
      {"user-call-underflow", {{"CALL", {"identity", "1"}, 1}}, true},
      {"caller-local-reference", {{"REF_LOCAL", {"caller_only"}, 1}}, true},
      {"global-reference",
       {{"REF_LOCAL", {"shared"}, 1}, {"STORE_LOCAL", {"copy"}, 2}}, false},
  };
  for (const auto &test : cases) {
    ny::nil_module module;
    module.module_name = "frame_isolation";
    ny::nil_function identity;
    identity.name = "identity";
    identity.params = {"value: i64"};
    identity.return_type = "i64";
    identity.body = {{"STORE_LOCAL", {"value"}, 1},
                     {"LOAD_LOCAL", {"value"}, 2}, {"RET", {}, 3}};
    module.functions.push_back(identity);
    ny::nil_function helper;
    helper.name = "helper";
    helper.return_type = "void";
    helper.body = test.body;
    helper.body.push_back({"RET", {}, 4});
    module.functions.push_back(helper);
    ny::nil_function main;
    main.name = "main";
    main.return_type = "i64";
    main.body = {{"PUSH_CONST", {"91"}, 1},
                 {"STORE_LOCAL", {"caller_only"}, 1},
                 {"LOAD_LOCAL", {"caller_only"}, 1},
                 {"CALL", {"helper", "0"}, 2}, {"RET", {}, 3}};
    module.functions.push_back(main);

    std::stringstream text_stream;
    ny::nil_writer writer(text_stream);
    writer.emit_text_module(module);
    std::unordered_map<std::string, ny::Value> constants;
    const auto textual = ny::parseProgram(text_stream, constants);
    const auto binary = parse_binary(serialize_binary(module), constants);
    for (const auto *program : {&textual, &binary}) {
      ny::VM vm;
      vm.loadProgram(*program, constants);
      vm.setGlobal("shared", int64_t(17));
      std::ostringstream output;
      vm.setOutputStream(output);
      bool rejected = false;
      bool threw = false;
      try {
        vm.execute();
      } catch (const std::runtime_error &error) {
        threw = true;
        const std::string diagnostic = error.what();
        rejected = diagnostic.find("underflow") != std::string::npos ||
                   diagnostic.find("empty stack") != std::string::npos ||
                   diagnostic.find("not found in current frame") != std::string::npos;
        check(rejected, test.name + ": unexpected runtime error: " + diagnostic);
      }
      check(threw == test.should_reject, test.name + ": frame outcome");
      check(!vm.getStack().empty() &&
                std::holds_alternative<int64_t>(vm.getStack().front()) &&
                std::get<int64_t>(vm.getStack().front()) == 91,
            test.name + ": caller operand survives");
      check(output.str().empty(), test.name + ": no partial builtin output");
      if (!test.should_reject)
        check(vm.getStack().size() == 1 && vm.getProgramExitCode() == 91,
              test.name + ": only the caller result remains");
    }
  }

  // Recursion suspends a different caller operand at every depth. Parameter
  // stores may consume only the arguments owned by that particular call.
  ny::nil_module recursion;
  recursion.functions = {
      make_function("sum", "i64",
                    {{"STORE_LOCAL", {"depth"}, 1}, {"LOAD_LOCAL", {"depth"}, 2},
                     {"PUSH_CONST", {"0"}, 3}, {"CMP_EQ", {}, 4},
                     {"JMP_IF_NOT", {"recurse"}, 5}, {"PUSH_CONST", {"0"}, 6},
                     {"RET", {}, 7}, {"LABEL", {"recurse"}, 8},
                     {"LOAD_LOCAL", {"depth"}, 9}, {"LOAD_LOCAL", {"depth"}, 10},
                     {"PUSH_CONST", {"1"}, 11}, {"SUB", {}, 12},
                     {"CALL", {"sum", "1"}, 13}, {"ADD", {}, 14}, {"RET", {}, 15}},
                    {"depth: i64"}),
      make_function("pair", "i64",
                    {{"STORE_LOCAL", {"right"}, 1}, {"STORE_LOCAL", {"left"}, 2},
                     {"LOAD_LOCAL", {"left"}, 3}, {"PUSH_CONST", {"10"}, 4},
                     {"MUL", {}, 5}, {"LOAD_LOCAL", {"right"}, 6},
                     {"ADD", {}, 7}, {"RET", {}, 8}},
                    {"left: i64", "right: i64"}),
      make_function("empty", "void", {{"RET", {}, 1}}),
      make_function("main", "i64",
                    {{"PUSH_CONST", {"99"}, 1}, {"PUSH_CONST", {"5"}, 2},
                     {"CALL", {"sum", "1"}, 3}, {"ADD", {}, 4}, {"RET", {}, 5}}),
  };
  for (const bool binary : {false, true}) {
    const std::string description = std::string(binary ? "binary" : "text") + ": ";
    std::unordered_map<std::string, ny::Value> constants;
    const auto program = parse_module(recursion, binary, constants);
    ny::VM vm;
    vm.loadProgram(program, constants);
    vm.execute();
    check(vm.getStack().size() == 1 && vm.getProgramExitCode() == 114,
          description + "recursive calls retain all suspended caller operands");

    // Failed host calls at end-of-program must not index program_[pc_], consume
    // the prior result, alter the PC, or leave a partially installed frame.
    const auto old_pc = vm.getPC();
    const auto old_depth = vm.getCallDepth();
    bool rejected = false;
    try {
      vm.callFunction("pair", 2);
    } catch (const std::runtime_error &error) {
      rejected = std::string(error.what()).find("Stack underflow") !=
                 std::string::npos;
    }
    check(rejected && vm.getPC() == old_pc && vm.getCallDepth() == old_depth &&
              vm.getProgramExitCode() == 114,
          description + "embedding underflow leaves the invocation intact");
    vm.pushValue(int64_t(3));
    vm.callFunction("sum", 1);
    check(vm.getStack().size() == 2 && vm.getProgramExitCode() == 6 &&
              std::get<int64_t>(vm.getStack().front()) == 114,
          description + "repeated embedding retains prior results below arguments");
    vm.pushValue(int64_t(3));
    vm.pushValue(int64_t(4));
    vm.callFunction("pair", 2);
    check(vm.getStack().size() == 3 && vm.getProgramExitCode() == 34 &&
              std::get<int64_t>(vm.getStack()[0]) == 114 &&
              std::get<int64_t>(vm.getStack()[1]) == 6,
          description + "embedded parameters retain order and prior return values");
    const auto before_void = vm.getStack();
    vm.callFunction("empty", 0);
    check(vm.getStack().size() == before_void.size() &&
              vm.getProgramExitCode() == 34 &&
              std::get<int64_t>(vm.getStack()[0]) == 114 &&
              std::get<int64_t>(vm.getStack()[1]) == 6,
          description + "embedded void calls produce no dummy result");

    ny::VM empty;
    empty.loadProgram(program, constants);
    const auto entry_pc = empty.getPC();
    rejected = false;
    try {
      empty.callFunction("sum", 1);
    } catch (const std::runtime_error &error) {
      rejected = std::string(error.what()).find("Stack underflow") !=
                 std::string::npos;
    }
    check(rejected && empty.getPC() == entry_pc && empty.getCallDepth() == 1 &&
              empty.getStack().empty(),
          description + "embedding rejects unavailable arguments before state change");
  }

  for (const bool use_array : {false, true}) {
    std::istringstream mutation_source(
        "function mutate() -> void:\n" +
        std::string(use_array ? "PUSH_CONST 0\nPUSH_CONST 999\nSET_INDEX\n"
                              : "PUSH_CONST 999\nSET_FIELD value\n") +
        "RET\nend function\n");
    std::unordered_map<std::string, ny::Value> constants;
    const auto mutation_program = ny::parseProgram(mutation_source, constants);
    ny::VM mutation;
    mutation.loadProgram(mutation_program, constants);
    ny::StructValue record;
    record.type_name = "record";
    record.fields()["value"] = 88;
    ny::ArrayValue array;
    array.elements.push_back(int64_t(88));
    mutation.pushValue(use_array ? ny::Value(array) : ny::Value(record));
    bool rejected = false;
    try {
      mutation.callFunction("mutate", 0);
    } catch (const std::runtime_error &error) {
      rejected = std::string(error.what()).find(use_array ? "underflow" : "empty stack") !=
                 std::string::npos;
    }
    check(rejected && mutation.getStack().size() == 1,
          "callee cannot mutate an aggregate in its caller's operand prefix");
    if (mutation.getStack().size() == 1) {
      const auto &value = mutation.getStack().front();
      check(use_array
                ? std::get<int64_t>(std::get<ny::ArrayValue>(value).elements[0]) == 88
                : std::get<ny::StructValue>(value).fields().at("value") == 88,
            "caller aggregate remains unchanged on rejected mutation");
    }
    check(record.fields().at("value") == 88,
          "rejected mutation does not affect a shared caller struct");
  }
}

} // namespace

void test_binary_type_annotations_round_trip() {
  // A record, class and choice each state what they are for. Binary NIL used
  // to drop all three: its type sections had no annotation field, so intent
  // survived textual emission and was lost by --emit-binary-nil. The two
  // encodings have to state the same thing or the manifest depends on which
  // one a build happened to produce.
  ny::nil_module module;
  module.module_name = "annotated";
  ny::nil_struct_def shape;
  shape.name = "rectangle";
  shape.fields = {{"width", "i64"}};
  shape.annotations = {"@ai_context(\"a shape on screen\")"};
  module.structs.push_back(shape);
  ny::nil_enum_def state;
  state.name = "window_state";
  state.variants = {{"Hidden", ""}, {"Visible", ""}};
  state.annotations = {"@ai_context(\"what a window is doing\")"};
  module.enums.push_back(state);
  // A class is emitted as a struct schema and a class record under one name,
  // so the fixture carries both, as a real artifact does. The intent sits on
  // the class record.
  ny::nil_struct_def meter_schema;
  meter_schema.name = "Meter";
  meter_schema.fields = {{"value", "i64"}};
  module.structs.push_back(meter_schema);
  ny::nil_class_def meter;
  meter.name = "Meter";
  meter.annotations = {"@ai_context(\"a metered thing\")"};
  module.classes.push_back(meter);
  ny::nil_function entry;
  entry.name = "main";
  entry.return_type = "i32";
  entry.body = {{"PUSH_CONST", {"0"}}, {"RET", {}}};
  module.functions.push_back(entry);

  const auto bytes = serialize_binary(module);
  check(static_cast<unsigned char>(bytes.at(8)) == ny::binary_nil_type_annotation_flag,
        "an artifact stating type intent sets the type-annotation flag");
  std::istringstream binary(bytes, std::ios::in | std::ios::binary);
  const auto encoded = ny::parseBinaryNilModule(binary);
  check(encoded->structs.size() == 2 && encoded->structs[0].annotations == shape.annotations,
        "a record keeps its stated purpose through binary NIL");
  check(encoded->structs.size() == 2 && encoded->structs[1].annotations.empty(),
        "a class's struct half carries no duplicate statement");
  check(encoded->enums.size() == 1 && encoded->enums[0].annotations == state.annotations,
        "a choice keeps its stated purpose through binary NIL");
  check(encoded->classes.size() == 1 && encoded->classes[0].annotations == meter.annotations,
        "a class keeps its stated purpose through binary NIL");

  // A module that states nothing must be byte-identical to before the flag
  // existed, so the layout only changes for artifacts that use it.
  ny::nil_module silent = module;
  silent.structs[0].annotations.clear();
  silent.enums[0].annotations.clear();
  silent.classes[0].annotations.clear();
  const auto quiet = serialize_binary(silent);
  check(static_cast<unsigned char>(quiet.at(8)) == 0U,
        "an artifact stating no type intent leaves the flag clear");
  std::istringstream quiet_input(quiet, std::ios::in | std::ios::binary);
  const auto quiet_module = ny::parseBinaryNilModule(quiet_input);
  check(quiet_module->structs.size() == 2 && quiet_module->structs[0].annotations.empty(),
        "a flag-clear artifact reads no annotations");

  // An unknown flag bit is refused rather than ignored: an artifact that
  // states more than this reader understands must fail loudly instead of
  // being read as one that states less.
  auto unknown = bytes;
  unknown.at(8) = static_cast<char>(ny::binary_nil_known_flags + 1U);
  bool refused = false;
  try {
    std::istringstream input(unknown, std::ios::in | std::ios::binary);
    (void)ny::parseBinaryNilModule(input);
  } catch (const std::exception &) { refused = true; }
  check(refused, "an unknown binary NIL flag bit is refused");

  // A type annotation that is not one is refused, as on the function path.
  ny::nil_module malformed = module;
  malformed.structs[0].annotations = {"ai_context(\"no leading sigil\")"};
  refused = false;
  try {
    std::istringstream input(serialize_binary(malformed), std::ios::in | std::ios::binary);
    (void)ny::parseBinaryNilModule(input);
  } catch (const std::exception &) { refused = true; }
  check(refused, "a type annotation without '@' is refused");
}

int main() {
  test_binary_function_boundaries_and_debug_lines();
  test_textual_function_declaration_lines();
  test_textual_function_annotations_are_lossless();
  test_binary_library_metadata_round_trip_and_safety();
  test_legacy_binary_magic();
  test_binary_encoding_is_little_endian_and_streamable();
  test_malformed_binary_structure_is_rejected();
  test_instruction_shapes_are_rejected_before_execution();
  test_generated_nonvoid_fallthrough_is_explicit_and_fail_closed();
  test_shared_opcode_mapping();
  test_unknown_opcodes_are_rejected();
  test_vm_entry_and_exit_queries();
  test_function_arity_survives_both_nil_formats();
  test_function_return_types_survive_both_nil_formats();
  test_vm_typed_return_contract();
  test_vm_void_interop_has_no_result();
  test_vm_typed_function_cannot_escape_without_return();
  test_vm_trapped_process_exit_is_not_a_return();
  test_vm_set_index_consumes_all_operands();
  test_vm_set_index_rejects_embedded_value_arrays();
  test_vm_declared_functions_precede_legacy_builtins();
  test_vm_scalar_cast_boundaries();
  test_typed_global_storage_contract();
  test_array_scalar_metadata_aliases();
  test_vm_scalar_arithmetic_widths();
  test_textual_quoted_operands_round_trip_as_one_argument();
  test_duplicate_and_missing_labels_are_rejected();
  test_vm_call_frame_operand_isolation();
  test_binary_type_annotations_round_trip();

  if (failures != 0) {
    std::cerr << failures << " nil_loader test(s) failed\n";
    return 1;
  }
  std::cout << "nil_loader tests passed\n";
  return 0;
}
