#include "vm.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {
using ny::Instruction;
using ny::Opcode;
int failures = 0;

void check(bool ok, const std::string &label) {
  if (!ok) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}

ny::Value execute(std::vector<Instruction> program,
                  const ny::Value &injected = int64_t(0)) {
  ny::VM vm;
  vm.setMemoryMode(ny::MemoryMode::SAFE_GC);
  vm.loadProgram(program, {});
  vm.setGlobal("injected", injected);
  vm.execute();
  return vm.getStack().empty() ? ny::Value(int64_t(0)) : vm.getStack().back();
}

void integer(const ny::Value &value, int64_t expected, const std::string &label) {
  const auto *actual = std::get_if<int64_t>(&value);
  check(actual && *actual == expected, label);
}

void reject(std::vector<Instruction> program, const std::string &message,
             const ny::Value &injected = int64_t(0)) {
  try { (void)execute(std::move(program), injected); check(false, message); }
  catch (const std::runtime_error &error) { check(error.what() == message, message); }
}

void comparisons() {
  const std::vector<std::pair<Opcode, std::string>> comparisons = {
      {Opcode::CMP_EQ, "CMP_EQ"}, {Opcode::CMP_NE, "CMP_NE"},
      {Opcode::CMP_LT, "CMP_LT"}, {Opcode::CMP_LE, "CMP_LE"},
      {Opcode::CMP_GT, "CMP_GT"}, {Opcode::CMP_GE, "CMP_GE"}};
  for (const auto &[opcode, name] : comparisons) {
    integer(execute({{Opcode::NEW_ARRAY_LITERAL, {"0", "i64"}},
        {Opcode::STORE_LOCAL, {"alive"}},
        {Opcode::PUSH_CONST, {"17179869184"}},
        {Opcode::PUSH_CONST, {"17179869184"}}, {opcode, {}}}),
        opcode == Opcode::CMP_EQ || opcode == Opcode::CMP_LE || opcode == Opcode::CMP_GE,
        name + " accepts every scalar integer, including a live array address");
    const auto message = name + ": comparison is not defined for array values";
    reject({{Opcode::NEW_ARRAY_LITERAL, {"0", "i64"}},
        {Opcode::STORE_LOCAL, {"array"}}, {Opcode::LOAD_LOCAL, {"array"}},
        {Opcode::LOAD_LOCAL, {"array"}}, {opcode, {}}}, message);
    for (bool array_first : {false, true}) {
      std::vector<Instruction> body = {{Opcode::NEW_ARRAY, {"1"}},
          {Opcode::STORE_LOCAL, {"array"}}};
      body.push_back(array_first ? Instruction{Opcode::LOAD_LOCAL, {"array"}}
                                : Instruction{Opcode::PUSH_CONST, {"17179869184"}});
      body.push_back(array_first ? Instruction{Opcode::PUSH_CONST, {"17179869184"}}
                                : Instruction{Opcode::LOAD_LOCAL, {"array"}});
      body.push_back({opcode, {}});
      reject(body, message);
    }
    reject({{Opcode::LOAD_GLOBAL, {"injected"}},
        {Opcode::PUSH_CONST, {"0.0"}}, {opcode, {}}}, message, ny::ArrayValue{});
  }
}

void propagation() {
  check(std::holds_alternative<ny::ArrayHandle>(execute({
      {Opcode::NEW_ARRAY_LITERAL, {"0", "i64"}},
      {Opcode::STORE_GLOBAL, {"array"}}, {Opcode::LOAD_GLOBAL, {"array"}}})),
      "globals preserve array identity");
  check(std::holds_alternative<ny::ArrayHandle>(execute({
      {Opcode::NEW_ARRAY_LITERAL, {"0", "i64"}},
      {Opcode::NEW_VARIANT, {"Option::Some", "1"}},
      {Opcode::UNWRAP_VARIANT, {"1"}}})), "enum payload preserves array identity");
  check(std::holds_alternative<ny::ArrayHandle>(execute({
      {Opcode::NEW_ARRAY_LITERAL, {"0", "i64"}},
      {Opcode::NEW_ARRAY_LITERAL, {"1", "array<i64>"}},
      {Opcode::PUSH_CONST, {"0"}}, {Opcode::GET_INDEX, {}}})),
      "nested heap arrays preserve array identity");
  check(std::holds_alternative<ny::ArrayHandle>(execute({
      {Opcode::NEW_ARRAY_LITERAL, {"0", "array<i64>"}},
      {Opcode::PUSH_CONST, {"0"}}, {Opcode::GET_INDEX, {}}})),
      "out-of-bounds nested-array default retains array kind");
  check(std::holds_alternative<ny::ArrayHandle>(execute({
      {Opcode::NEW_STRUCT, {"Box"}}, {Opcode::NEW_ARRAY_LITERAL, {"0", "i64"}},
      {Opcode::SET_FIELD, {"item"}}, {Opcode::GET_FIELD, {"item"}}})),
      "struct field preserves array identity");
  check(std::holds_alternative<ny::ArrayHandle>(execute({
      {Opcode::PUSH_CONST, {"1"}}, {Opcode::ALLOC, {"Box"}},
      {Opcode::STORE_LOCAL, {"pointer"}}, {Opcode::LOAD_LOCAL, {"pointer"}},
      {Opcode::NEW_STRUCT, {"Box"}}, {Opcode::WRITE_PTR, {}},
      {Opcode::LOAD_LOCAL, {"pointer"}}, {Opcode::NEW_ARRAY_LITERAL, {"0", "i64"}},
      {Opcode::SET_FIELD_PTR, {"item"}}, {Opcode::LOAD_LOCAL, {"pointer"}},
      {Opcode::READ_PTR, {}}, {Opcode::GET_FIELD, {"item"}}})),
      "raw-pointer field assignment preserves array identity");
  integer(execute({{Opcode::PUSH_CONST, {"1"}}, {Opcode::ALLOC, {"Box"}},
      {Opcode::STORE_LOCAL, {"pointer"}}, {Opcode::LOAD_LOCAL, {"pointer"}},
      {Opcode::NEW_STRUCT, {"Box"}}, {Opcode::WRITE_PTR, {}},
      {Opcode::LOAD_LOCAL, {"pointer"}}, {Opcode::NEW_ARRAY_LITERAL, {"0", "i64"}},
      {Opcode::SET_FIELD_PTR, {"item"}}, {Opcode::LOAD_LOCAL, {"pointer"}},
      {Opcode::PUSH_CONST, {"71"}}, {Opcode::SET_FIELD_PTR, {"item"}},
      {Opcode::LOAD_LOCAL, {"pointer"}}, {Opcode::READ_PTR, {}},
      {Opcode::GET_FIELD, {"item"}}}), 71,
      "raw-pointer scalar field assignment clears old array kind");
  integer(execute({{Opcode::NEW_STRUCT, {"Box"}},
      {Opcode::NEW_ARRAY_LITERAL, {"0", "i64"}}, {Opcode::SET_FIELD, {"item"}},
      {Opcode::PUSH_CONST, {"17179869184"}}, {Opcode::SET_FIELD, {"item"}},
      {Opcode::GET_FIELD, {"item"}}}), 17179869184LL,
      "scalar reassignment removes stale array field identity");
  const auto text = execute({{Opcode::NEW_STRUCT, {"Box"}},
      {Opcode::NEW_ARRAY_LITERAL, {"0", "i64"}}, {Opcode::SET_FIELD, {"item"}},
      {Opcode::LOAD_GLOBAL, {"injected"}}, {Opcode::SET_FIELD, {"item"}},
      {Opcode::GET_FIELD, {"item"}}}, std::string("text"));
  check(std::get_if<std::string>(&text) && std::get<std::string>(text) == "text",
      "text reassignment removes stale array field identity");

  // After the first assignment, the array is reachable only through the
  // global -> struct -> enum -> array-of-arrays -> leaf chain.
  integer(execute({{Opcode::NEW_STRUCT, {"Box"}},
      {Opcode::PUSH_CONST, {"42"}}, {Opcode::NEW_ARRAY_LITERAL, {"1", "i64"}},
      {Opcode::NEW_ARRAY_LITERAL, {"1", "array<i64>"}},
      {Opcode::NEW_VARIANT, {"Option::Some", "1"}}, {Opcode::SET_FIELD, {"item"}},
      {Opcode::STORE_GLOBAL, {"root"}}, {Opcode::PUSH_CONST, {"2"}},
      {Opcode::INTEROP_CALL, {"intrinsic", "gc.collect", "1", "void"}},
      {Opcode::LOAD_GLOBAL, {"root"}}, {Opcode::GET_FIELD, {"item"}},
      {Opcode::UNWRAP_VARIANT, {"1"}}, {Opcode::PUSH_CONST, {"0"}},
      {Opcode::GET_INDEX, {}}, {Opcode::PUSH_CONST, {"0"}},
      {Opcode::GET_INDEX, {}}}), 42, "GC follows enum and nested-array roots");
  integer(execute({{Opcode::NEW_STRUCT, {"Box"}},
      {Opcode::PUSH_CONST, {"43"}}, {Opcode::NEW_ARRAY_LITERAL, {"1", "i64"}},
      {Opcode::SET_FIELD, {"item"}}, {Opcode::STORE_GLOBAL, {"root"}},
      {Opcode::PUSH_CONST, {"2"}},
      {Opcode::INTEROP_CALL, {"intrinsic", "gc.collect", "1", "void"}},
      {Opcode::LOAD_GLOBAL, {"root"}}, {Opcode::GET_FIELD, {"item"}},
      {Opcode::PUSH_CONST, {"0"}}, {Opcode::GET_INDEX, {}}}), 43,
      "GC follows separate tagged struct array fields");

  check(std::holds_alternative<ny::ArrayHandle>(execute({
      {Opcode::LABEL, {"identity"}, 0, true, 1, "array<i64>"},
      {Opcode::STORE_LOCAL, {"value"}}, {Opcode::LOAD_LOCAL, {"value"}},
      {Opcode::RET, {}}, {Opcode::LABEL, {"identity.end"}, 0, false, 0, "", true},
      {Opcode::LABEL, {"main"}, 0, true, 0, "array<i64>"},
      {Opcode::NEW_ARRAY_LITERAL, {"0", "i64"}},
      {Opcode::CALL, {"identity", "1"}}, {Opcode::RET, {}},
      {Opcode::LABEL, {"main.end"}, 0, false, 0, "", true}})),
      "function arguments and returns preserve array identity");
}

void malformed_operations() {
  reject({{Opcode::NEW_ARRAY_LITERAL, {"0", "i64"}}, {Opcode::CAST, {"i64"}}},
         "CAST: value cannot be converted to 'i64'");
  integer(execute({{Opcode::NEW_ARRAY_LITERAL, {"0", "i64"}},
      {Opcode::STORE_LOCAL, {"array"}}, {Opcode::PUSH_CONST, {"17179869184"}},
      {Opcode::PUSH_CONST, {"17179869184"}}, {Opcode::ADD, {}}}),
      34359738368LL, "untyped scalar addition does not concatenate live arrays");
  integer(execute({{Opcode::NEW_ARRAY, {"2"}}, {Opcode::NEW_ARRAY, {"3"}},
      {Opcode::ADD, {}}, {Opcode::LEN, {}}}), 5,
      "legacy array concatenation preserves tagged result");
  reject({{Opcode::NEW_ARRAY, {"1"}}, {Opcode::STORE_LOCAL, {"array"}},
      {Opcode::PUSH_CONST, {"17179869184"}},
      {Opcode::INTEROP_CALL, {"intrinsic", "array.length", "1", "i32", "i64"}}},
      "array intrinsic requires a matching typed array handle");
  integer(execute({{Opcode::NEW_ARRAY, {"1"}}, {Opcode::STORE_LOCAL, {"array"}},
      {Opcode::PUSH_CONST, {"17179869184"}}, {Opcode::CALL, {"array__len", "1"}}}),
      0, "legacy array length does not dereference a forged scalar");
  integer(execute({{Opcode::PUSH_CONST, {"42"}}, {Opcode::NEW_ARRAY_LITERAL, {"1", "i64"}},
      {Opcode::STORE_LOCAL, {"array"}}, {Opcode::PUSH_CONST, {"17179869184"}},
      {Opcode::PUSH_CONST, {"0"}}, {Opcode::CALL, {"array__get", "2"}}}),
      0, "legacy array get does not dereference a forged scalar");
  integer(execute({{Opcode::NEW_ARRAY, {"1"}}, {Opcode::STORE_LOCAL, {"array"}},
      {Opcode::PUSH_CONST, {"17179869184"}}, {Opcode::CALL, {"vm__get_address", "1"}}}),
      17179869184LL, "unsafe address helper does not reinterpret scalar as array");
}
} // namespace

int main() {
  try { comparisons(); propagation(); malformed_operations(); }
  catch (const std::exception &error) {
    check(false, std::string("unexpected exception: ") + error.what());
  }
  if (failures) return 1;
  std::cout << "VM array identity contracts passed\n";
  return 0;
}
