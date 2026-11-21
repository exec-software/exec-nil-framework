// An embedding host may supply a failed C++ stream without a corresponding
// errno. Such a stream is not a clean end of input on either runtime route.
#include "codegen.hpp"
#include "native_runtime.hpp"
#include "nil_loader.hpp"
#include "vm.hpp"

#include <cerrno>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
unsigned checks = 0;

void require(bool condition, const std::string &message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}

struct Program {
  std::vector<ny::Instruction> code;
  std::unordered_map<std::string, ny::Value> constants;
};

Program fixture(bool binary, bool optional) {
  ny::nil_module module;
  module.module_name = "console_input_state";
  const std::string result = optional ? "nil::std::core::Option<text>" : "text";
  module.enums = {{"nil::std::core::Option<text>",
                   {{"None", ""}, {"Some", "text"}}}};
  ny::nil_function read;
  read.name = "read";
  read.return_type = result;
  read.body = {{"INTEROP_CALL", {"intrinsic",
      optional ? "console.read_line_option" : "console.read_line", "0", result}},
      {"RET", {}}};
  module.functions.push_back(read);
  std::ostringstream serialized(std::ios::out | std::ios::binary);
  if (binary) {
    ny::codegen_options options;
    ny::nil_binary_writer writer(serialized, options);
    writer.emit_binary_module(module);
  } else {
    ny::nil_writer writer(serialized);
    writer.emit_text_module(module);
  }
  std::istringstream input(serialized.str(), std::ios::in | std::ios::binary);
  Program program;
  program.code = binary ? ny::parseBinaryProgram(input, program.constants)
                        : ny::parseProgram(input, program.constants);
  return program;
}

void vm_answer(ny::VM &vm, const char *variant, const std::string &line = {}) {
  vm.callFunction("read", 0);
  require(!vm.getStack().empty(), "VM console input returned no result");
  const auto &answer = std::get<ny::EnumValue>(vm.getStack().back());
  require(answer.variant_name == variant, "VM console input returned the wrong variant");
  if (answer.variant_name == "Option::Some") {
    require(answer.payloads.size() == 1 &&
                std::get<std::string>(answer.payloads.front()) == line,
            "VM console input changed the line");
  } else {
    require(answer.payloads.empty(), "VM EOF carried a line payload");
  }
}

void vm_contract(bool binary) {
  const auto program = fixture(binary, true);
  const std::ios::iostate failures[] = {
      std::ios::failbit, std::ios::badbit,
      std::ios::badbit | std::ios::eofbit,
      std::ios::badbit | std::ios::failbit | std::ios::eofbit};
  for (const auto state : failures) {
    ny::VM vm;
    std::istringstream input("unread\n");
    input.setstate(state);
    vm.setInputStream(input);
    vm.loadProgram(program.code, program.constants);
    bool failed = false;
    errno = 0;
    try {
      vm.callFunction("read", 0);
    } catch (const std::runtime_error &error) {
      failed = std::string(error.what()).find(
          "console.read_line_option: input is unavailable") != std::string::npos;
    }
    require(failed, "VM accepted a failed embedded stream as clean EOF");
  }
  {
    ny::VM vm;
    std::istringstream input("\nlast");
    vm.setInputStream(input);
    vm.loadProgram(program.code, program.constants);
    vm_answer(vm, "Option::Some", "");
    vm_answer(vm, "Option::Some", "last");
    errno = EACCES; // An old unrelated host error is not an input error.
    vm_answer(vm, "Option::None");
    vm_answer(vm, "Option::None");
  }
  {
    ny::VM vm;
    std::istringstream input;
    vm.setInputStream(input);
    vm.loadProgram(program.code, program.constants);
    vm_answer(vm, "Option::None");
  }
  {
    const auto required_line = fixture(binary, false);
    ny::VM vm;
    std::istringstream input;
    vm.setInputStream(input);
    vm.loadProgram(required_line.code, required_line.constants);
    bool failed = false;
    try {
      vm.callFunction("read", 0);
    } catch (const std::runtime_error &error) {
      failed = std::string(error.what()).find(
          "console.read_line: input is unavailable or reached EOF") != std::string::npos;
    }
    require(failed, "VM required read_line lost its existing EOF failure contract");
  }
}

class cin_buffer {
public:
  explicit cin_buffer(std::streambuf *buffer)
      : previous_state_(std::cin.rdstate()), previous_buffer_(std::cin.rdbuf(buffer)) {}
  ~cin_buffer() {
    std::cin.rdbuf(previous_buffer_);
    std::cin.clear(previous_state_);
  }
private:
  std::ios::iostate previous_state_;
  std::streambuf *previous_buffer_;
};

void native_answer(std::int32_t expected, const std::string &line = {}) {
  std::uint64_t value = UINT64_MAX;
  const char *error = nullptr;
  const auto status = nyrt_io_v1("console.read_line_option", 0, 0, 0, &value, &error);
  require(status == expected && !error, "native console input returned the wrong status");
  if (expected == 2) {
    const auto *text = reinterpret_cast<const char *>(static_cast<std::uintptr_t>(value));
    require(text && text == line, "native console input changed the line");
  } else {
    require(value == 0, "native EOF carried a line payload");
  }
}

void native_contract() {
  nyrt_context_begin(0, nullptr);
  const std::ios::iostate failures[] = {
      std::ios::failbit, std::ios::badbit,
      std::ios::badbit | std::ios::eofbit,
      std::ios::badbit | std::ios::failbit | std::ios::eofbit};
  for (const auto state : failures) {
    std::istringstream input("unread\n");
    cin_buffer redirect(input.rdbuf());
    std::cin.setstate(state);
    std::uint64_t value = UINT64_MAX;
    const char *error = nullptr;
    errno = 0;
    const auto status = nyrt_io_v1("console.read_line_option", 0, 0, 0, &value, &error);
    require(status == 0 && error && std::string(error) ==
                "console.read_line_option: input is unavailable",
            "native accepted a failed embedded stream as clean EOF");
    require(value == 0, "native failed read returned a line payload");
  }
  {
    std::istringstream input("\nlast");
    cin_buffer redirect(input.rdbuf());
    native_answer(2, "");
    native_answer(2, "last");
    errno = EACCES;
    native_answer(1);
    native_answer(1);
  }
  {
    std::istringstream input;
    cin_buffer redirect(input.rdbuf());
    native_answer(1);
  }
  nyrt_context_end();
}
} // namespace

int main() {
  try {
    vm_contract(false);
    vm_contract(true);
    native_contract();
    std::cout << "Console input state: " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Console input state failure: " << error.what() << '\n';
    return 1;
  }
}
