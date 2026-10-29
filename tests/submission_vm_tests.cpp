#include "codegen.hpp"
#include "nil_loader.hpp"
#include "submission_profile.hpp"
#include "vm.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>

namespace {
int failures = 0;
void check(bool ok, const std::string &name) {
  if (!ok) { ++failures; std::cerr << "FAIL: " << name << '\n'; }
}
ny::nil_module module(std::string type, std::vector<ny::nil_instruction> body) {
  ny::nil_module result;
  result.module_name = "root";
  ny::nil_function function;
  function.name = "__nil_submission";
  function.return_type = std::move(type);
  function.body = std::move(body);
  result.functions.push_back(std::move(function));
  return result;
}
void load(ny::VM &vm, const ny::nil_module &module) {
  ny::validate_nil_submission_profile(module);
  std::ostringstream source;
  ny::nil_writer(source).emit_text_module(module);
  std::istringstream input(source.str());
  std::unordered_map<std::string, ny::Value> constants;
  auto program = ny::parseProgram(input, constants);
  vm.loadProgram(program, constants);
  vm.resetExecution("__nil_submission");
}
void finish(ny::VM &vm) { while (vm.stepInstruction()) {} }
ny::submission_value evaluate(const ny::nil_module &module, ny::pure_execution_limits limits = {}) {
  ny::VM vm;
  vm.configurePureExecution(std::move(limits));
  load(vm, module);
  finish(vm);
  return vm.snapshotPureValue(vm.getStack().back(), module.functions.front().return_type);
}
void rejects(const std::function<void()> &action, const std::string &name) {
  try { action(); check(false, name); }
  catch (const std::runtime_error &) {}
}
void budget(const std::function<void()> &action, const std::string &name,
            ny::pure_execution_error::kind kind = ny::pure_execution_error::kind::budget) {
  try { action(); check(false, name); }
  catch (const ny::pure_execution_error &error) { check(error.reason == kind, name + " typed reason"); }
  catch (const std::exception &error) { check(false, name + ": " + error.what()); }
}
void scalars() {
  const auto integer = evaluate(module("i32", {{"PUSH_CONST", {"2147483647"}},
      {"PUSH_CONST", {"1"}}, {"ADD", {"i32"}}, {"RET", {}}}));
  check(integer.type == "i32" && integer.scalar == "-2147483648", "shared VM i32 overflow semantics");
  const auto boolean = evaluate(module("bool", {{"PUSH_CONST", {"4"}},
      {"PUSH_CONST", {"5"}}, {"CMP_LT", {}}, {"RET", {}}}));
  check(boolean.scalar == "true", "canonical boolean snapshot");
  const auto negative_zero = evaluate(module("f64", {{"PUSH_CONST", {"-0.0"}}, {"RET", {}}}));
  check(negative_zero.scalar == "-0.0", "negative zero survives snapshot");
  const auto floating = evaluate(module("f64", {{"PUSH_CONST", {"0.1"}}, {"RET", {}}}));
  check(std::stod(floating.scalar) == 0.1, "f64 roundtrip snapshot");
  rejects([&] { (void)evaluate(module("i32", {{"PUSH_CONST", {"1"}},
      {"PUSH_CONST", {"0"}}, {"DIV", {"i32"}}, {"RET", {}}})); }, "division failure propagates");
  ny::VM vm; vm.configurePureExecution();
  rejects([&] { (void)vm.snapshotPureValue(std::numeric_limits<double>::infinity(), "f64"); },
          "nonfinite snapshot rejected");
  rejects([&] { (void)vm.snapshotPureValue(int64_t(2), "bool"); }, "noncanonical boolean rejected");
  rejects([&] { (void)vm.snapshotPureValue(ny::ArrayHandle{1}, "array<i32>"); }, "foreign array handle rejected");
}
ny::nil_module text_module() {
  auto result = module("text", {{"PUSH_CONST_STR", {"a"}}, {"PUSH_CONST_STR", {"a"}},
      {"STR_CONCAT", {}}, {"RET", {}}});
  result.string_constants = {{"a", "abcd"}};
  return result;
}
void limits() {
  auto limits = ny::pure_execution_limits{};
  limits.max_text_bytes = 7;
  ny::VM vm; vm.configurePureExecution(limits); load(vm, text_module());
  while (vm.getProgram()[vm.getPC()].op != ny::Opcode::STR_CONCAT) vm.stepInstruction();
  const auto count = vm.pureInstructionsExecuted();
  budget([&] { vm.stepInstruction(); }, "concatenation checked before mutation");
  check(vm.getStack().size() == 2 && std::get<std::string>(vm.getStack()[0]) == "abcd" &&
        std::get<std::string>(vm.getStack()[1]) == "abcd" && vm.pureInstructionsExecuted() == count,
        "failed preflight preserves operands and execution count");

  limits = {}; limits.max_text_bytes = 3;
  ny::VM formatting; formatting.configurePureExecution(limits);
  load(formatting, module("text", {{"PUSH_CONST", {"1234"}}, {"TO_STRING", {"i32"}}, {"RET", {}}}));
  while (formatting.getProgram()[formatting.getPC()].op != ny::Opcode::TO_STRING) formatting.stepInstruction();
  const auto format_count = formatting.pureInstructionsExecuted();
  budget([&] { formatting.stepInstruction(); }, "formatting respects a small custom text budget");
  check(formatting.getStack().size() == 1 && std::get<int64_t>(formatting.getStack().back()) == 1234 &&
        formatting.pureInstructionsExecuted() == format_count, "formatting failure preserves its scalar operand");
  limits.max_text_bytes = 2;
  check(evaluate(module("text", {{"PUSH_CONST", {"42"}}, {"TO_STRING", {"i32"}}, {"RET", {}}}), limits).scalar == "42",
        "small text budget admits an exactly fitting scalar");
  limits.max_text_bytes = 4;
  check(evaluate(module("text", {{"PUSH_CONST", {"1"}}, {"TO_STRING", {"bool"}}, {"RET", {}}}), limits).scalar == "true",
        "boolean formatting uses its actual text size");

  auto small_tuple = module("tuple<text>", {{"NEW_STRUCT", {"tuple<text>"}},
      {"PUSH_CONST_STR", {"t"}}, {"SET_FIELD", {"0"}}, {"RET", {}}});
  small_tuple.structs = {{"tuple<text>", {{"0", "text"}}, {"@nil_tuple"}}};
  small_tuple.string_constants = {{"t", "a"}};
  limits.max_text_bytes = 1;
  check(evaluate(small_tuple, limits).elements.front().scalar == "a",
        "text budget applies to values rather than tuple metadata");

  limits = {}; limits.max_instructions = 1;
  budget([&] { (void)evaluate(module("i32", {{"PUSH_CONST", {"42"}}, {"RET", {}}}), limits); },
         "instruction budget");
  limits = {}; limits.max_work_bytes = 300;
  budget([&] { (void)evaluate(text_module(), limits); }, "work budget");
  limits = {}; limits.max_snapshot_bytes = 70;
  budget([&] { (void)evaluate(text_module(), limits); }, "snapshot byte budget");
  limits = {}; limits.cancelled = [] { return true; };
  budget([&] { (void)evaluate(text_module(), limits); }, "cancellation",
         ny::pure_execution_error::kind::cancelled);

  bool cancelled = false;
  limits = {}; limits.cancelled = [&] { return cancelled; };
  ny::VM snapshot; snapshot.configurePureExecution(limits); load(snapshot, text_module()); finish(snapshot);
  cancelled = true;
  budget([&] { (void)snapshot.snapshotPureValue(snapshot.getStack().back(), "text"); },
         "snapshot cancellation", ny::pure_execution_error::kind::cancelled);

  limits = {}; limits.max_snapshot_nodes = 2;
  budget([&] { (void)evaluate(module("array<i32>", {{"PUSH_CONST", {"1"}},
      {"PUSH_CONST", {"2"}}, {"NEW_ARRAY_LITERAL", {"2", "i32"}}, {"RET", {}}}), limits); },
      "aggregate node budget");
}
void ownership() {
  const auto array = evaluate(module("array<i32>", {{"PUSH_CONST", {"3"}},
      {"PUSH_CONST", {"7"}}, {"NEW_ARRAY_LITERAL", {"2", "i32"}}, {"RET", {}}}));
  check(array.elements.size() == 2 && array.elements[0].scalar == "3" && array.elements[1].scalar == "7",
        "array snapshot outlives VM heap");
  auto nested = module("array<array<i32>>", {{"PUSH_CONST", {"9"}},
      {"NEW_ARRAY_LITERAL", {"1", "i32"}}, {"NEW_ARRAY_LITERAL", {"1", "array<i32>"}}, {"RET", {}}});
  check(evaluate(nested).elements.front().elements.front().scalar == "9", "nested array snapshot");

  auto tuple = module("tuple<text,array<i32>>", {{"NEW_STRUCT", {"tuple<text,array<i32>>"}},
      {"PUSH_CONST_STR", {"t"}}, {"SET_FIELD", {"0"}}, {"PUSH_CONST", {"9"}},
      {"NEW_ARRAY_LITERAL", {"1", "i32"}}, {"SET_FIELD", {"1"}}, {"RET", {}}});
  tuple.structs = {{"tuple<text,array<i32>>", {{"0", "text"}, {"1", "array<i32>"}}, {"@nil_tuple"}}};
  tuple.string_constants = {{"t", "owned"}};
  ny::VM vm; vm.configurePureExecution(); load(vm, tuple); finish(vm);
  const auto result = vm.snapshotPureValue(vm.getStack().back(), tuple.functions.front().return_type);
  std::get<ny::StructValue>(vm.getStack().back()).data->string_fields["0"] = "changed";
  check(result.elements[0].scalar == "owned" && result.elements[1].elements[0].scalar == "9",
        "tuple snapshot does not retain mutable StructData aliases");
  vm.resetExecution("__nil_submission");
  check(result.elements[1].elements[0].scalar == "9", "snapshot survives reset");
  check(vm.pureInstructionsExecuted() == 0 && vm.pureWorkBytesCharged() == 0, "reset starts fresh budgets");
}
void admission() {
  auto valid = module("i32", {{"PUSH_CONST", {"42"}}, {"RET", {}}});
  std::ostringstream text; ny::nil_writer(text).emit_text_module(valid);
  std::istringstream input(text.str());
  std::unordered_map<std::string, ny::Value> constants;
  const auto program = ny::parseProgram(input, constants);
  auto altered = program;
  altered[2] = {ny::Opcode::TIME_NOW, {}};
  rejects([&] { ny::VM vm; vm.configurePureExecution(); vm.loadProgram(altered, constants); },
          "raw executable alteration is revalidated");
  altered = program;
  altered.insert(altered.end() - 1, {ny::Opcode::CONSOLE_WRITELN, {}});
  rejects([&] { ny::VM vm; vm.configurePureExecution(); vm.loadProgram(altered, constants); },
          "effectful unreachable instruction denied before execution");
  altered = program; altered[2] = {ny::Opcode::JMP, {"__nil_submission"}};
  rejects([&] { ny::VM vm; vm.configurePureExecution(); vm.loadProgram(altered, constants); },
          "branch out of admitted body rejected");
  rejects([&] { ny::VM vm; vm.loadProgram(program, constants); vm.configurePureExecution(); },
          "pure mode requires fresh VM");
}
void no_ambient_profile() {
  const auto directory = std::filesystem::temp_directory_path() /
      ("nil-pure-profile-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directory(directory);
  const auto path = directory / "capture.json";
  const auto *old = std::getenv("NIL_MEMORY_PROFILE");
  const std::string prior = old ? old : "";
  const bool had_prior = old != nullptr;
#ifdef _WIN32
  _putenv_s("NIL_MEMORY_PROFILE", path.string().c_str());
#else
  setenv("NIL_MEMORY_PROFILE", path.string().c_str(), 1);
#endif
  (void)evaluate(module("i32", {{"PUSH_CONST", {"1"}}, {"RET", {}}}));
  check(!std::filesystem::exists(path), "pure execution ignores ambient Aperture output");
#ifdef _WIN32
  _putenv_s("NIL_MEMORY_PROFILE", prior.c_str());
#else
  if (had_prior) setenv("NIL_MEMORY_PROFILE", prior.c_str(), 1); else unsetenv("NIL_MEMORY_PROFILE");
#endif
  std::filesystem::remove_all(directory);
}
} // namespace

int main() {
  try { scalars(); limits(); ownership(); admission(); no_ambient_profile(); }
  catch (const std::exception &error) { check(false, std::string("unexpected exception: ") + error.what()); }
  if (!failures) std::cout << "pure submission VM tests passed\n";
  return failures ? 1 : 0;
}
