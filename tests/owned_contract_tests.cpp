#include "codegen.hpp"
#include "owned_contract.hpp"
#include "vm.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
int failures = 0;
using body = std::vector<ny::nil_instruction>;
void test(const std::string &name, body instructions, bool expected) {
  ny::nil_module module;
  module.functions.push_back({});
  auto &function = module.functions.back();
  function.name = name;
  function.return_type = "void";
  function.body = std::move(instructions);
  bool accepted = true;
  try { ny::validate_nil_owned_contract(module); }
  catch (const std::runtime_error &error) {
    accepted = false;
    if (expected) std::cerr << name << ": " << error.what() << '\n';
  }
  if (accepted != expected) { ++failures; std::cerr << "FAIL " << name << '\n'; }
}
body allocation() {
  return {{"OWN_ENTER", {}}, {"PUSH_CONST", {"2"}}, {"OWN_ALLOC", {"i32"}},
          {"STORE_LOCAL", {"0"}}, {"OWN_BIND", {"0"}}};
}
void append(body &target, body values) {
  target.insert(target.end(), values.begin(), values.end());
}
}
int main() {
  auto normal = allocation(); append(normal, {{"OWN_LEAVE", {}}, {"RET", {}}});
  test("normal", normal, true);
  auto transferred = allocation();
  append(transferred, {{"OWN_ENTER", {}}, {"OWN_MOVE", {"0"}},
      {"STORE_LOCAL", {"1"}}, {"OWN_BIND", {"1"}},
      {"OWN_LEAVE", {}}, {"OWN_LEAVE", {}}, {"RET", {}}});
  test("transfer", transferred, true);
  auto returning = allocation(); append(returning, {{"RET", {}}});
  test("return_runtime_unwinds", returning, true);
  auto conditional = allocation();
  append(conditional, {{"JMP_IF_NOT", {"join"}}, {"OWN_ENTER", {}},
      {"OWN_MOVE", {"0"}}, {"STORE_LOCAL", {"1"}}, {"OWN_BIND", {"1"}},
      {"OWN_LEAVE", {}}, {"LABEL", {"join"}}, {"OWN_LEAVE", {}}, {"RET", {}}});
  test("conditional_drop", conditional, true);
  auto fault = allocation();
  append(fault, {{"FAULT_ENTER", {"region", "caught"}}, {"OWN_ENTER", {}},
      {"PUSH_CONST", {"1"}}, {"OWN_ALLOC", {"u8"}}, {"STORE_LOCAL", {"1"}},
      {"OWN_BIND", {"1"}}, {"CALL", {"may_fail", "0"}}, {"OWN_LEAVE", {}},
      {"FAULT_LEAVE", {"region"}}, {"JMP", {"done"}}, {"LABEL", {"caught"}},
      {"FAULT_SAVE", {"saved"}}, {"LABEL", {"done"}}, {"OWN_LEAVE", {}}, {"RET", {}}});
  test("fault_unwinds_inner_scope", fault, true);
  test("scope_underflow", {{"OWN_LEAVE", {}}, {"RET", {}}}, false);
  test("scope_operand", {{"OWN_ENTER", {"extra"}}, {"RET", {}}}, false);
  test("move_operand", {{"OWN_MOVE", {}}, {"RET", {}}}, false);
  test("bind_operand", {{"OWN_BIND", {}}, {"RET", {}}}, false);
  test("unbound_move", {{"OWN_ENTER", {}}, {"OWN_MOVE", {"0"}},
      {"STORE_LOCAL", {"1"}}, {"OWN_BIND", {"1"}}, {"RET", {}}}, false);
  auto duplicate = allocation();
  append(duplicate, {{"PUSH_CONST", {"2"}}, {"OWN_ALLOC", {"i32"}},
      {"STORE_LOCAL", {"0"}}, {"OWN_BIND", {"0"}}, {"RET", {}}});
  test("overwrite_owner", duplicate, false);
  auto store = allocation(); append(store, {{"PUSH_CONST", {"9"}}, {"STORE_LOCAL", {"0"}}, {"RET", {}}});
  test("arbitrary_store_owner", store, false);
  auto unbound_transfer = allocation(); append(unbound_transfer, {{"OWN_MOVE", {"0"}}, {"RET", {}}});
  test("unbound_transfer", unbound_transfer, false);
  auto two_moves = transferred; two_moves.resize(two_moves.size() - 3);
  append(two_moves, {{"OWN_MOVE", {"0"}}, {"STORE_LOCAL", {"2"}}, {"OWN_BIND", {"2"}}, {"RET", {}}});
  test("double_move", two_moves, false);
  auto forged = normal; forged[2].opname = "LOAD_LOCAL";
  test("forged_alias_binding", forged, false);
  auto raw = normal; raw[2].opname = "ALLOC";
  test("raw_allocation_not_owned", raw, false);
  auto wrong = normal; wrong[2].args = {"f64"};
  test("unsupported_storage", wrong, false);
  auto absent = normal; absent.erase(absent.begin());
  test("allocation_without_scope", absent, false);
  test("fallthrough_live_scope", allocation(), false);
  test("jump_scope_mismatch", {{"JMP_IF", {"join"}}, {"OWN_ENTER", {}},
      {"LABEL", {"join"}}, {"RET", {}}}, false);
  auto maybe_move = conditional; maybe_move.resize(maybe_move.size() - 2);
  append(maybe_move, {{"OWN_MOVE", {"0"}}, {"STORE_LOCAL", {"2"}}, {"OWN_BIND", {"2"}}, {"RET", {}}});
  test("move_conditionally_absent_owner", maybe_move, false);
  test("loop_grows_scopes", {{"LABEL", {"again"}}, {"OWN_ENTER", {}}, {"JMP", {"again"}}}, false);
  test("fault_scope_underflow", {{"OWN_ENTER", {}}, {"FAULT_ENTER", {"r", "caught"}},
      {"OWN_LEAVE", {}}, {"RET", {}}, {"LABEL", {"caught"}}, {"RET", {}}}, false);
  try {
    ny::validate_nil_owned_contract(std::vector<ny::Instruction>{{ny::Opcode::OWN_ENTER, {}, 0}});
    ++failures; std::cerr << "FAIL flat ownership outside function\n";
  } catch (const std::runtime_error &) {}
  std::cout << "23 ownership CFG fixtures and flat boundary rejection checked\n";
  return failures ? 1 : 0;
}
