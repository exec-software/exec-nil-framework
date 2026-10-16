#include "codegen.hpp"
#include "nil_contract.hpp"
#include "nil_loader.hpp"
#include "vm.hpp"

#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const std::string &message) {
  if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
ny::nil_function fn(const std::string &name, const std::vector<ny::nil_instruction> &body,
                    const std::string &result = "void") {
  ny::nil_function function;
  function.name = name; function.return_type = result; function.body = body;
  return function;
}
ny::nil_module module(const std::vector<ny::nil_instruction> &body) {
  ny::nil_module result;
  result.module_name = "fault_tests";
  result.structs.push_back({"problem::Fault", {{"value", "i64"}, {"array", "i64[]"}}});
  result.structs.push_back({"other::Fault", {{"value", "i64"}}});
  result.functions.push_back(fn("main", body));
  return result;
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
void rejection(const ny::nil_module &mod, const std::string &label) {
  for (bool binary : {false, true}) {
    bool rejected = false;
    try { std::unordered_map<std::string, ny::Value> constants; (void)parse(mod, binary, constants); }
    catch (const std::runtime_error &) { rejected = true; }
    check(rejected, label + (binary ? " binary VM" : " textual VM"));
    rejected = false;
    try {
      std::istringstream input(serialize(mod, binary), std::ios::in | std::ios::binary);
      if (binary) (void)ny::parseBinaryNilModule(input); else (void)ny::parseTextNilModule(input);
    } catch (const std::runtime_error &) { rejected = true; }
    check(rejected, label + (binary ? " binary module" : " textual module"));
  }
}

void test_unwind_and_debug_restart() {
  auto mod = module({
      {"PUSH_CONST", {"17"}}, {"FAULT_ENTER", {"outer", "dispatch"}},
      {"PUSH_CONST", {"99"}}, {"CALL", {"explode", "0"}}, {"POP", {}},
      {"FAULT_LEAVE", {"outer"}}, {"POP", {}}, {"RET", {}},
      {"LABEL", {"dispatch"}}, {"FAULT_SAVE", {"saved"}},
      {"CONSOLE_WRITELN", {}}, {"FAULT_MATCH", {"saved", "other::Fault"}},
      {"CONSOLE_WRITELN", {}}, {"FAULT_BIND", {"saved", "problem::Fault"}},
      {"GET_FIELD", {"value"}}, {"CONSOLE_WRITELN", {}},
      {"FAULT_DROP", {"saved"}}, {"RET", {}}});
  mod.functions.push_back(fn("explode", {{"NEW_STRUCT", {"problem::Fault"}},
      {"PUSH_CONST", {"42"}}, {"SET_FIELD", {"value"}}, {"FAULT_RAISE", {"problem::Fault"}, 71}}));
  for (bool binary : {false, true}) {
    std::unordered_map<std::string, ny::Value> constants;
    auto program = parse(mod, binary, constants);
    ny::VM vm; std::ostringstream output; vm.setOutputStream(output); vm.loadProgram(program, constants);
    vm.resetExecution("main");
    bool saw_unwind = false;
    for (int steps = 0; vm.getPC() < vm.getProgram().size() && steps < 100; ++steps) {
      const auto &instruction = vm.getProgram()[vm.getPC()];
      if (instruction.op == ny::Opcode::LABEL && instruction.args[0] == "dispatch") {
        const auto frames = vm.getDebugFrames();
        saw_unwind = frames.size() == 1 && frames[0].function_name == "main" &&
                     vm.getStack().size() == 1 && std::get<std::int64_t>(vm.getStack()[0]) == 17;
      }
      vm.stepInstruction();
    }
    check(saw_unwind, "debugger observes real unwound frame and exact saved prefix");
    check(output.str() == "17\n0\n42\n", "stepped fault execution exact output");
    output.str(""); output.clear(); vm.resetExecution("main"); vm.execute(false);
    check(output.str() == "17\n0\n42\n", "restart clears old fault records and matches normal execution");
  }
}

void test_rooted_payload_and_retained_rethrow() {
  auto mod = module({
      {"FAULT_ENTER", {"outer", "dispatch"}}, {"CALL", {"explode", "0"}},
      {"FAULT_LEAVE", {"outer"}}, {"RET", {}}, {"LABEL", {"dispatch"}},
      {"FAULT_SAVE", {"saved"}}, {"PUSH_CONST", {"2"}},
      {"INTEROP_CALL", {"intrinsic", "gc.collect", "1", "void"}},
      {"FAULT_ENTER", {"inner", "inner_dispatch"}}, {"FAULT_RETHROW", {"saved"}},
      {"LABEL", {"inner_dispatch"}}, {"FAULT_SAVE", {"again"}},
      {"FAULT_BIND", {"again", "problem::Fault"}}, {"PUSH_CONST", {"91"}},
      {"SET_FIELD", {"value"}}, {"POP", {}}, {"FAULT_DROP", {"again"}},
      {"FAULT_BIND", {"saved", "problem::Fault"}}, {"GET_FIELD", {"value"}},
      {"CONSOLE_WRITELN", {}}, {"FAULT_BIND", {"saved", "problem::Fault"}},
      {"GET_FIELD", {"array"}}, {"PUSH_CONST", {"0"}}, {"GET_INDEX", {}},
      {"CONSOLE_WRITELN", {}}, {"FAULT_DROP", {"saved"}}, {"RET", {}}});
  mod.functions.push_back(fn("explode", {{"NEW_STRUCT", {"problem::Fault"}},
      {"PUSH_CONST", {"42"}}, {"NEW_ARRAY_LITERAL", {"1", "i64"}},
      {"SET_FIELD", {"array"}}, {"FAULT_RAISE", {"problem::Fault"}, 81}}));
  for (bool binary : {false, true}) {
    std::unordered_map<std::string, ny::Value> constants;
    auto program = parse(mod, binary, constants);
    ny::VM vm; std::ostringstream output; vm.setOutputStream(output);
    vm.setMemoryMode(ny::MemoryMode::SAFE_GC); vm.loadProgram(program, constants); vm.execute();
    check(output.str() == "91\n42\n", "saved payload roots heap child and rethrow retains shared identity");
  }
}

void test_uncaught_and_raw_values() {
  auto mod = module({{"NEW_STRUCT", {"problem::Fault"}}, {"FAULT_RAISE", {"problem::Fault"}, 123}});
  for (bool binary : {false, true}) {
    std::unordered_map<std::string, ny::Value> constants;
    auto program = parse(mod, binary, constants);
    ny::VM vm; vm.loadProgram(program, constants);
    bool caught = false;
    try { vm.execute(); } catch (const ny::UncaughtFault &error) {
      caught = std::string(error.what()) == "Ny uncaught fault: problem::Fault at line 123";
    }
    check(caught, "uncaught explicit fault keeps canonical type and original line");
  }
  std::vector<ny::Instruction> raw = {
      {ny::Opcode::LABEL, {"main"}, 0, true, 0, "void"},
      {ny::Opcode::LOAD_GLOBAL, {"payload"}},
      {ny::Opcode::FAULT_RAISE, {"problem::Fault"}, 19},
      {ny::Opcode::LABEL, {"end"}, 0, false, 0, "", true}};
  for (const ny::Value &payload : {ny::Value(std::int64_t(1)), ny::Value(ny::StructValue{"other::Fault"})}) {
    ny::VM vm; vm.loadProgram(raw, {}); vm.setGlobal("payload", payload);
    bool rejected = false;
    try { vm.execute(); } catch (const ny::UncaughtFault &) {} catch (const std::runtime_error &) { rejected = true; }
    check(rejected, "loader-bypassing VM rejects nonstruct and nominally wrong payloads");
  }
  raw[1] = {ny::Opcode::FAULT_LEAVE, {"missing"}};
  bool rejected = false;
  try { ny::VM vm; vm.loadProgram(raw, {}); } catch (const std::runtime_error &) { rejected = true; }
  check(rejected, "loader-bypassing VM rejects malformed region control flow");
}

void test_generic_array_argument_identity() {
  auto mod = module({{"FAULT_ENTER", {"r", "landing"}},
      {"NEW_STRUCT", {"Envelope<i64[]>"}}, {"FAULT_RAISE", {"Envelope<i64[]>"}, 17},
      {"LABEL", {"landing"}}, {"FAULT_SAVE", {"s"}},
      {"FAULT_BIND", {"s", "Envelope<i64[]>"}}, {"POP", {}},
      {"FAULT_DROP", {"s"}}, {"RET", {}}});
  mod.structs.push_back({"Envelope<i64[]>", {}});
  for (bool binary : {false, true}) {
    std::unordered_map<std::string, ny::Value> constants;
    auto program = parse(mod, binary, constants);
    std::istringstream input(serialize(mod, binary), std::ios::in | std::ios::binary);
    if (binary) (void)ny::parseBinaryNilModule(input); else (void)ny::parseTextNilModule(input);
    ny::VM vm; vm.loadProgram(program, constants); vm.execute();
    check(vm.getPC() >= vm.getProgram().size(), "concrete struct may have an array generic argument");
  }
}

void test_parameter_and_nested_field_types() {
  auto mod = module({{"FAULT_ENTER", {"r", "landing"}}, {"NEW_STRUCT", {"Holder"}},
      {"NEW_STRUCT", {"problem::Fault"}}, {"PUSH_CONST", {"73"}},
      {"SET_FIELD", {"value"}}, {"SET_FIELD", {"fault"}}, {"CALL", {"raise_field", "1"}},
      {"FAULT_LEAVE", {"r"}}, {"RET", {}}, {"LABEL", {"landing"}},
      {"FAULT_SAVE", {"s"}}, {"FAULT_BIND", {"s", "problem::Fault"}},
      {"GET_FIELD", {"value"}}, {"CONSOLE_WRITELN", {}}, {"FAULT_DROP", {"s"}}, {"RET", {}}});
  mod.structs.push_back({"Holder", {{"fault", "problem::Fault"}}});
  auto raise = fn("raise_field", {{"STORE_LOCAL", {"holder"}}, {"LOAD_LOCAL", {"holder"}},
      {"GET_FIELD", {"fault"}}, {"FAULT_RAISE", {"problem::Fault"}, 29}});
  raise.params = {"holder:Holder"}; mod.functions.push_back(raise);
  for (bool binary : {false, true}) {
    std::unordered_map<std::string, ny::Value> constants;
    auto program = parse(mod, binary, constants);
    ny::VM vm; std::ostringstream output; vm.setOutputStream(output);
    vm.loadProgram(program, constants); vm.execute();
    check(output.str() == "73\n", "parameter and nested field preserve exact fault type through serialized NIL");
  }
  mod.functions.back().body[2] = {"GET_FIELD", {"missing"}};
  rejection(mod, "unproven nested field payload");
}

void test_malformed_contracts() {
  rejection(module({{"FAULT_LEAVE", {"missing"}}, {"RET", {}}}), "orphan leave");
  rejection(module({{"FAULT_SAVE", {"missing"}}, {"RET", {}}}), "orphan pending save");
  rejection(module({{"FAULT_RESUME", {"missing"}}}), "absent saved fault");
  rejection(module({{"FAULT_RETHROW", {"missing"}}}), "absent retained fault");
  rejection(module({{"PUSH_CONST", {"7"}}, {"FAULT_RAISE", {"problem::Fault"}}}), "scalar payload");
  rejection(module({{"PUSH_CONST", {"7"}}, {"CAST", {"problem::Fault"}},
                    {"FAULT_RAISE", {"problem::Fault"}}}), "cast-fabricated payload");
  rejection(module({{"NEW_STRUCT", {"other::Fault"}}, {"FAULT_RAISE", {"problem::Fault"}}}), "wrong nominal payload");
  rejection(module({{"NEW_STRUCT", {"missing::Fault"}}, {"FAULT_RAISE", {"missing::Fault"}}}), "missing schema");
  rejection(module({{"FAULT_ENTER", {"r", "absent"}}, {"RET", {}}}), "missing dispatch");
  rejection(module({{"FAULT_ENTER", {"r", "landing"}}, {"RET", {}},
                    {"LABEL", {"landing"}}, {"FAULT_SAVE", {"s"}}, {"FAULT_DROP", {"s"}}, {"RET", {}}}), "live region return");
  rejection(module({{"FAULT_ENTER", {"r", "landing"}}, {"FAULT_LEAVE", {"wrong"}}, {"RET", {}},
                    {"LABEL", {"landing"}}, {"FAULT_SAVE", {"s"}}, {"FAULT_DROP", {"s"}}, {"RET", {}}}), "wrong region leave");
  rejection(module({{"FAULT_ENTER", {"r", "landing"}}, {"FAULT_ENTER", {"r", "landing"}},
                    {"LABEL", {"landing"}}, {"FAULT_SAVE", {"s"}}, {"FAULT_DROP", {"s"}}, {"RET", {}}}), "duplicate live region");
  rejection(module({{"FAULT_ENTER", {"r", "landing"}}, {"JMP", {"landing"}},
                    {"LABEL", {"landing"}}, {"FAULT_SAVE", {"s"}}, {"FAULT_DROP", {"s"}}, {"RET", {}}}), "ordinary jump into dispatch");
  rejection(module({{"FAULT_ENTER", {"r", "landing"}}, {"FAULT_LEAVE", {"r"}}, {"RET", {}},
                    {"LABEL", {"landing"}}, {"PUSH_CONST", {"3"}}, {"RET", {}}}), "ordinary execution with pending fault");
  rejection(module({{"PUSH_CONST", {"8"}}, {"FAULT_ENTER", {"r", "landing"}}, {"POP", {}},
                    {"FAULT_LEAVE", {"r"}}, {"RET", {}}, {"LABEL", {"landing"}},
                    {"FAULT_SAVE", {"s"}}, {"FAULT_DROP", {"s"}}, {"POP", {}}, {"RET", {}}}), "consume protected stack prefix");
  rejection(module({{"FAULT_RAISE", {"problem::Fault", "extra"}}}), "wrong operand arity");
}
} // namespace

int main() {
  try {
    test_unwind_and_debug_restart(); test_rooted_payload_and_retained_rethrow();
    test_uncaught_and_raw_values(); test_generic_array_argument_identity();
    test_parameter_and_nested_field_types(); test_malformed_contracts();
  } catch (const std::exception &error) {
    std::cerr << "Unexpected exception: " << error.what() << '\n'; return 1;
  }
  if (failures) return 1;
  std::cout << "Fault NIL/VM contracts passed\n";
  return 0;
}
