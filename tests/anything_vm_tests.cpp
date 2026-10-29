#include "codegen.hpp"
#include "nil_contract.hpp"
#include "nil_loader.hpp"
#include "vm.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
int failures = 0;
using Body = std::vector<ny::nil_instruction>;
void check(bool value, const std::string &message) {
  if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
ny::nil_module module(Body body, const std::string &result = "i64") {
  ny::nil_module mod;
  mod.module_name = "anything_contract";
  ny::nil_function main;
  main.name = "main"; main.return_type = result; main.body = std::move(body);
  mod.functions.push_back(std::move(main));
  mod.string_constants.push_back({"message", "hello"});
  return mod;
}
std::string serialize(const ny::nil_module &mod, bool binary) {
  std::ostringstream output(std::ios::out | std::ios::binary);
  if (binary) {
    ny::codegen_options options;
    ny::nil_binary_writer writer(output, options); writer.emit_binary_module(mod);
  } else {
    ny::nil_writer writer(output); writer.emit_text_module(mod);
  }
  return output.str();
}
std::vector<ny::Instruction> parse(const ny::nil_module &mod, bool binary,
                                 std::unordered_map<std::string, ny::Value> &constants) {
  std::istringstream input(serialize(mod, binary), std::ios::in | std::ios::binary);
  return binary ? ny::parseBinaryProgram(input, constants) : ny::parseProgram(input, constants);
}
ny::Value execute(const ny::nil_module &mod, bool binary) {
  std::unordered_map<std::string, ny::Value> constants;
  const auto program = parse(mod, binary, constants);
  ny::VM vm; vm.setMemoryMode(ny::MemoryMode::SAFE_GC);
  vm.loadProgram(program, constants); vm.resetExecution("main"); vm.execute(false);
  if (vm.getStack().size() != 1) throw std::runtime_error("expected one result");
  return vm.getStack().back();
}
void integer(const ny::nil_module &mod, std::int64_t expected, const std::string &name) {
  for (bool binary : {false, true}) {
    try {
      const auto value = execute(mod, binary);
      const auto *actual = std::get_if<std::int64_t>(&value);
      check(actual && *actual == expected, name);
    } catch (const std::exception &error) { check(false, name + ": " + error.what()); }
  }
}
void rejects(const ny::nil_module &mod, const std::string &fragment, bool load_only = false) {
  for (bool binary : {false, true}) {
    bool rejected = false;
    try {
      if (load_only) { std::unordered_map<std::string, ny::Value> constants; (void)parse(mod, binary, constants); }
      else (void)execute(mod, binary);
    } catch (const std::runtime_error &error) {
      rejected = std::string(error.what()).find(fragment) != std::string::npos;
      if (!rejected) std::cerr << "Unexpected diagnostic: " << error.what() << '\n';
    }
    check(rejected, "reject " + fragment);
  }
}
void values() {
  integer(module({{"PUSH_CONST", {"42"}}, {"BOX_ANY", {"i64"}},
      {"UNBOX_ANY", {"i64"}}, {"RET", {}}}), 42, "scalar round trip");
  integer(module({{"PUSH_CONST", {"42"}}, {"BOX_ANY", {"i64"}}, {"STORE_LOCAL", {"value"}},
      {"PUSH_CONST_STR", {"message"}}, {"BOX_ANY", {"text"}}, {"STORE_LOCAL", {"value"}},
      {"LOAD_LOCAL", {"value"}}, {"IS_ANY_TYPE", {"text"}}, {"RET", {}}}), 1,
      "same binding changes runtime type");
  integer(module({{"PUSH_CONST", {"1"}}, {"CAST", {"bool"}}, {"BOX_ANY", {"bool"}},
      {"IS_ANY_TYPE", {"i64"}}, {"RET", {}}}), 0, "bool and integer identities differ");
  integer(module({{"PUSH_CONST", {"9"}}, {"BOX_ANY", {"i64"}},
      {"NEW_ARRAY_LITERAL", {"1", "anything"}}, {"PUSH_CONST", {"0"}}, {"GET_INDEX", {}},
      {"UNBOX_ANY", {"i64"}}, {"RET", {}}}), 9, "anything array preserves its box");
  integer(module({{"PUSH_CONST", {"2"}}, {"PUSH_CONST", {"7"}}, {"BOX_ANY", {"i64"}},
      {"INTEROP_CALL", {"intrinsic", "array.new_with_value", "2", "array<anything>", "anything"}},
      {"PUSH_CONST", {"1"}}, {"INTEROP_CALL", {"intrinsic", "array.get", "2", "anything", "anything"}},
      {"UNBOX_ANY", {"i64"}}, {"RET", {}}}), 7, "typed array intrinsic preserves boxes");
  integer(module({{"NEW_ARRAY_LITERAL", {"0", "anything"}}, {"STORE_LOCAL", {"items"}},
      {"LOAD_LOCAL", {"items"}}, {"PUSH_CONST", {"29"}}, {"BOX_ANY", {"i64"}},
      {"CALL", {"array__push", "2"}}, {"LOAD_LOCAL", {"items"}}, {"PUSH_CONST", {"0"}},
      {"CALL", {"array__get", "2"}}, {"UNBOX_ANY", {"i64"}}, {"RET", {}}}), 29,
      "legacy direct array adapters preserve boxed values");
  auto holder = module({{"NEW_STRUCT", {"Holder"}}, {"PUSH_CONST", {"19"}}, {"BOX_ANY", {"i64"}},
      {"SET_FIELD", {"item"}}, {"BOX_ANY", {"Holder"}}, {"UNBOX_ANY", {"Holder"}},
      {"GET_FIELD", {"item"}}, {"UNBOX_ANY", {"i64"}}, {"RET", {}}});
  holder.structs.push_back({"Holder", {{"item", "anything"}}});
  integer(holder, 19, "anything struct fields retain boxed values");
  auto enumeration = module({{"PUSH_CONST", {"23"}}, {"BOX_ANY", {"i64"}},
      {"NEW_VARIANT", {"Maybe::Some", "1", "Maybe", "anything"}},
      {"BOX_ANY", {"Maybe"}}, {"UNBOX_ANY", {"Maybe"}},
      {"UNWRAP_VARIANT", {"1", "Maybe", "anything"}}, {"UNBOX_ANY", {"i64"}}, {"RET", {}}});
  enumeration.enums.push_back({"Maybe", {{"None", ""}, {"Some", "anything"}}});
  integer(enumeration, 23, "anything enum payload survives round trip");
  integer(module({{"PUSH_CONST", {"1"}}, {"ALLOC", {"i64"}}, {"STORE_LOCAL", {"pointer"}},
      {"LOAD_LOCAL", {"pointer"}}, {"PUSH_CONST", {"31"}}, {"WRITE_PTR", {}},
      {"LOAD_LOCAL", {"pointer"}}, {"BOX_ANY", {"ptr<i64>"}}, {"UNBOX_ANY", {"ptr<i64>"}},
      {"READ_PTR", {}}, {"RET", {}}}), 31, "typed raw pointer retains identity");
  integer(module({{"PUSH_CONST", {"47"}}, {"NEW_ARRAY_LITERAL", {"1", "i64"}},
      {"BOX_ANY", {"array<i64>"}}, {"STORE_GLOBAL", {"root", "anything"}},
      {"PUSH_CONST", {"2"}}, {"INTEROP_CALL", {"intrinsic", "gc.collect", "1", "void"}},
      {"LOAD_GLOBAL", {"root", "anything"}}, {"UNBOX_ANY", {"array<i64>"}},
      {"PUSH_CONST", {"0"}}, {"GET_INDEX", {}}, {"RET", {}}}), 47,
      "GC follows a sole boxed global root");
}
void invalid() {
  rejects(module({{"PUSH_CONST", {"4"}}, {"BOX_ANY", {"i64"}}, {"UNBOX_ANY", {"text"}}, {"RET", {}}}, "text"),
      "anything contains 'i64', cannot extract 'text'");
  rejects(module({{"PUSH_CONST", {"4"}}, {"UNBOX_ANY", {"i64"}}, {"RET", {}}}), "explicitly boxed", true);
  rejects(module({{"PUSH_CONST", {"4"}}, {"BOX_ANY", {"i64"}}, {"CONSOLE_WRITELN", {}},
      {"PUSH_CONST", {"0"}}, {"RET", {}}}), "anything must be extracted with a checked cast before this operation", true);
  auto typed_call = module({{"PUSH_CONST", {"4"}}, {"BOX_ANY", {"i64"}}, {"CALL", {"consume", "1"}},
      {"PUSH_CONST", {"0"}}, {"RET", {}}});
  ny::nil_function consume;
  consume.name = "consume"; consume.params = {"value: i64"}; consume.return_type = "void";
  consume.body = {{"STORE_LOCAL", {"value"}}, {"RET", {}}};
  typed_call.functions.push_back(std::move(consume));
  rejects(typed_call, "anything must be extracted with a checked cast before use as 'i64'", true);
  rejects(module({{"PUSH_CONST", {"1"}}, {"ALLOC", {"i64"}}, {"STORE_LOCAL", {"pointer"}},
      {"LOAD_LOCAL", {"pointer"}}, {"FREE", {}}, {"LOAD_LOCAL", {"pointer"}}, {"BOX_ANY", {"ptr<i64>"}},
      {"POP", {}}, {"PUSH_CONST", {"0"}}, {"RET", {}}}),
      "BOX_ANY: expected a fully initialized value of exact type 'ptr<i64>'");
  rejects(module({{"PUSH_CONST", {"4"}}, {"BOX_ANY", {"ptr<i64>"}}, {"POP", {}}, {"PUSH_CONST", {"0"}}, {"RET", {}}}), "exact proven", true);
  rejects(module({{"PUSH_CONST", {"4"}}, {"CAST", {"i32"}}, {"BOX_ANY", {"i64"}},
      {"POP", {}}, {"PUSH_CONST", {"0"}}, {"RET", {}}}), "exact proven", true);
  auto wrong_enum = module({{"NEW_VARIANT", {"First::Item", "0", "First", "void"}},
      {"BOX_ANY", {"Second"}}, {"POP", {}}, {"PUSH_CONST", {"0"}}, {"RET", {}}});
  wrong_enum.enums.push_back({"First", {{"Item", ""}}});
  wrong_enum.enums.push_back({"Second", {{"Item", ""}}});
  rejects(wrong_enum, "exact proven", true);
  rejects(module({{"PUSH_CONST", {"4"}}, {"BOX_ANY", {"ref<i64>"}}, {"RET", {}}}, "anything"), "canonical concrete", true);
  rejects(module({{"PUSH_CONST", {"4"}}, {"BOX_ANY", {"owned<i64>"}}, {"RET", {}}}, "anything"), "canonical concrete", true);
  rejects(module({{"PUSH_CONST", {"4"}}, {"BOX_ANY", {"anything"}}, {"RET", {}}}, "anything"), "canonical concrete", true);
  rejects(module({{"PUSH_CONST", {"4"}}, {"NEW_ARRAY_LITERAL", {"1", "anything"}}, {"PUSH_CONST", {"0"}},
      {"GET_INDEX", {}}, {"UNBOX_ANY", {"i64"}}, {"RET", {}}}), "boxed elements", true);
  rejects(module({{"NEW_ARRAY_LITERAL", {"0", "anything"}}, {"PUSH_CONST", {"0"}}, {"GET_INDEX", {}},
      {"UNBOX_ANY", {"i64"}}, {"RET", {}}}), "anything array read is out of bounds");
  auto uninitialized = module({{"NEW_STRUCT", {"Record"}}, {"BOX_ANY", {"Record"}},
      {"POP", {}}, {"PUSH_CONST", {"0"}}, {"RET", {}}});
  uninitialized.structs.push_back({"Record", {{"value", "i64"}}});
  rejects(uninitialized, "exact proven", true);
  for (const auto &value : {"-1", "2", "0.5", "1.5"})
    rejects(module({{"PUSH_CONST", {value}}, {"CAST", {"bool"}}, {"RET", {}}}), "exactly 0 or 1");
  rejects(module({{"PUSH_CONST_STR", {"message"}}, {"CAST", {"bool"}}, {"RET", {}}}),
      "expected true/false, yes/no, 1/0, or on/off (case-insensitive, no surrounding whitespace)");
  for (const auto &value : {"0", "1", "0.0", "1.0"})
    integer(module({{"PUSH_CONST", {value}}, {"CAST", {"bool"}}, {"RET", {}}}),
        value[0] == '1' ? 1 : 0, std::string("strict bool accepts ") + value);
  rejects(module({{"PUSH_CONST", {"2"}}, {"NEW_ARRAY_LITERAL", {"1", "bool"}},
      {"PUSH_CONST", {"0"}}, {"GET_INDEX", {}}, {"RET", {}}}), "exactly 0 or 1");
}
} // namespace
int main() {
  try { values(); invalid(); }
  catch (const std::exception &error) { check(false, error.what()); }
  std::cout << "Anything VM/text/binary contract and strict Boolean checks complete\n";
  return failures ? 1 : 0;
}
