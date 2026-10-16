#include "owned_contract.hpp"
#include "codegen.hpp"
#include "vm.hpp"
#include <algorithm>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>

namespace ny {
namespace {
struct scope {
  std::size_t identity;
  std::set<std::string> may;
  std::set<std::string> must;
};
struct region {
  std::string identity;
  std::size_t destination;
  std::vector<std::size_t> scopes;
};
struct flow {
  std::vector<scope> scopes;
  std::vector<region> regions;
};
bool owned(const std::string &op) { return op.rfind("OWN_", 0) == 0; }
bool owned(Opcode op) {
  return op == Opcode::OWN_ENTER || op == Opcode::OWN_LEAVE ||
         op == Opcode::OWN_BIND || op == Opcode::OWN_MOVE || op == Opcode::OWN_ALLOC;
}
void validate(const nil_function &function) {
  const auto &body = function.body;
  if (std::none_of(body.begin(), body.end(), [](const auto &instruction) {
        return owned(instruction.opname);
      })) return;
  const auto fail = [&](std::size_t pc, const std::string &message) -> void {
    throw std::runtime_error("NIL ownership contract in '" + function.name +
        "' at instruction " + std::to_string(pc + 1) + ": " + message);
  };
  std::map<std::string, std::size_t> labels;
  for (std::size_t pc = 0; pc < body.size(); ++pc) {
    const auto &instruction = body[pc];
    if (instruction.opname == "LABEL") {
      if (instruction.args.size() != 1 || !labels.emplace(instruction.args[0], pc).second)
        fail(pc, "invalid or duplicate label");
    }
    if (!owned(instruction.opname)) continue;
    const bool slot_op = instruction.opname == "OWN_BIND" || instruction.opname == "OWN_MOVE";
    if (instruction.opname == "OWN_ALLOC") {
      if (instruction.args.size() != 1 ||
          (instruction.args[0] != "i32" && instruction.args[0] != "i64" && instruction.args[0] != "u8"))
        fail(pc, "owned allocation must use i32/i64/u8 buffer storage");
      if (pc + 2 >= body.size() || body[pc + 1].opname != "STORE_LOCAL" ||
          body[pc + 2].opname != "OWN_BIND" || body[pc + 1].args != body[pc + 2].args)
        fail(pc, "owned allocation must immediately bind its result");
      continue;
    }
    if ((slot_op && (instruction.args.size() != 1 || instruction.args[0].empty())) ||
        (!slot_op && (instruction.opname != "OWN_ENTER" && instruction.opname != "OWN_LEAVE")) ||
        (!slot_op && !instruction.args.empty()))
      fail(pc, "invalid ownership operands");
    if (instruction.opname == "OWN_BIND") {
      if (pc < 2 || body[pc - 1].opname != "STORE_LOCAL" ||
          body[pc - 1].args != instruction.args ||
          (body[pc - 2].opname != "OWN_ALLOC" && body[pc - 2].opname != "OWN_MOVE"))
        fail(pc, "binding requires an immediate fresh allocation or transfer into its local slot");
      if (body[pc - 2].opname == "OWN_ALLOC" &&
          (body[pc - 2].args.size() != 1 ||
           (body[pc - 2].args[0] != "i32" && body[pc - 2].args[0] != "i64" &&
            body[pc - 2].args[0] != "u8")))
        fail(pc, "owned allocation must use i32/i64/u8 buffer storage");
    }
    if (instruction.opname == "OWN_MOVE") {
      if (pc + 2 >= body.size() || body[pc + 1].opname != "STORE_LOCAL" ||
          body[pc + 2].opname != "OWN_BIND" ||
          body[pc + 1].args != body[pc + 2].args)
        fail(pc, "transfer must bind immediately into a new owner slot");
    }
  }
  const auto destination = [&](const std::string &label, std::size_t pc) {
    const auto target = labels.find(label);
    if (target == labels.end()) fail(pc, "branch targets an absent local label");
    return target->second;
  };
  std::vector<std::optional<flow>> incoming(body.size());
  std::deque<std::size_t> pending;
  const auto enqueue = [&](std::size_t pc, const flow &next) {
    if (pc == body.size()) {
      if (!next.scopes.empty()) fail(pc, "function fallthrough leaves owned scopes live");
      return;
    }
    if (pc > body.size()) fail(pc, "invalid control-flow target");
    if (!incoming[pc]) { incoming[pc] = next; pending.push_back(pc); return; }
    auto &prior = *incoming[pc];
    if (prior.scopes.size() != next.scopes.size() || prior.regions.size() != next.regions.size())
      fail(pc, "incompatible lexical scopes at control-flow merge");
    for (std::size_t index = 0; index < prior.regions.size(); ++index)
      if (prior.regions[index].identity != next.regions[index].identity ||
          prior.regions[index].destination != next.regions[index].destination ||
          prior.regions[index].scopes != next.regions[index].scopes)
        fail(pc, "incompatible fault regions at ownership merge");
    bool changed = false;
    for (std::size_t index = 0; index < prior.scopes.size(); ++index) {
      auto &left = prior.scopes[index]; const auto &right = next.scopes[index];
      if (left.identity != right.identity) fail(pc, "jump crosses an owned lexical scope");
      for (const auto &slot : right.may) changed |= left.may.insert(slot).second;
      for (auto slot = left.must.begin(); slot != left.must.end();) {
        if (!right.must.count(*slot)) { slot = left.must.erase(slot); changed = true; }
        else ++slot;
      }
    }
    if (changed) pending.push_back(pc);
  };
  enqueue(0, {});
  while (!pending.empty()) {
    const auto pc = pending.front(); pending.pop_front();
    auto state = *incoming[pc];
    const auto &instruction = body[pc]; const auto &op = instruction.opname;
    const auto &args = instruction.args;
    const auto exception_edge = [&] {
      if (state.regions.empty()) return;
      auto thrown = state;
      const auto region = thrown.regions.back(); thrown.regions.pop_back();
      if (region.scopes.size() > thrown.scopes.size())
        fail(pc, "fault would restore already-left owned scopes");
      for (std::size_t index = 0; index < region.scopes.size(); ++index)
        if (region.scopes[index] != thrown.scopes[index].identity)
          fail(pc, "fault region has incompatible owned scope identity");
      thrown.scopes.resize(region.scopes.size());
      enqueue(region.destination, thrown);
    };
    bool terminal = false;
    if (op == "OWN_ENTER") {
      if (state.scopes.size() >= 1024) fail(pc, "owned scope nesting limit exceeded");
      state.scopes.push_back({pc, {}, {}});
    } else if (op == "OWN_ALLOC") {
      if (state.scopes.empty()) fail(pc, "allocation outside an owned scope");
    } else if (op == "OWN_LEAVE") {
      if (state.scopes.empty()) fail(pc, "leave without matching owned scope");
      for (const auto &region : state.regions)
        if (region.scopes.size() >= state.scopes.size())
          fail(pc, "owned scope cannot leave across an active fault boundary");
      state.scopes.pop_back();
    } else if (op == "OWN_BIND") {
      if (state.scopes.empty()) fail(pc, "binding outside an owned scope");
      for (const auto &scope : state.scopes)
        if (scope.may.count(args[0])) fail(pc, "binding overwrites a possibly live owner");
      state.scopes.back().may.insert(args[0]);
      state.scopes.back().must.insert(args[0]);
    } else if (op == "OWN_MOVE") {
      bool found = false;
      for (auto &scope : state.scopes) {
        if (scope.may.count(args[0])) {
          if (!scope.must.count(args[0])) fail(pc, "move of a conditionally absent owner");
          scope.may.erase(args[0]); scope.must.erase(args[0]); found = true; break;
        }
      }
      if (!found) fail(pc, "move of an unbound or already-moved local");
    } else if (op == "STORE_LOCAL" && args.size() == 1) {
      for (const auto &scope : state.scopes)
        if (scope.may.count(args[0])) fail(pc, "store overwrites a live owned slot");
    } else if (op == "FAULT_ENTER") {
      if (args.size() != 2) fail(pc, "invalid fault enter operands");
      region next{args[0], destination(args[1], pc), {}};
      for (const auto &scope : state.scopes) next.scopes.push_back(scope.identity);
      state.regions.push_back(std::move(next)); exception_edge();
    } else if (op == "FAULT_LEAVE") {
      if (args.size() != 1 || state.regions.empty() || state.regions.back().identity != args[0])
        fail(pc, "invalid fault leave in owned function");
      state.regions.pop_back();
    } else if (op == "FAULT_RAISE" || op == "FAULT_RESUME" || op == "FAULT_RETHROW") {
      exception_edge(); terminal = true;
    } else if (op == "CALL" || op == "CALL_METHOD") {
      exception_edge();
    } else if (op == "RET") {
      // The runtime always unwinds this function's remaining owner scopes.
      terminal = true;
    } else if (op == "JMP" || op == "JMP_IF" || op == "JMP_IF_NOT") {
      if (args.size() != 1) fail(pc, "invalid branch operands");
      enqueue(destination(args[0], pc), state);
      terminal = op == "JMP";
    }
    if (!terminal) enqueue(pc + 1, state);
  }
}
std::string operation(Opcode op) {
#define OP(name) case Opcode::name: return #name
  switch (op) {
  OP(OWN_ENTER); OP(OWN_LEAVE); OP(OWN_BIND); OP(OWN_MOVE); OP(OWN_ALLOC);
  OP(LABEL); OP(JMP); OP(JMP_IF); OP(JMP_IF_NOT); OP(RET);
  OP(FAULT_ENTER); OP(FAULT_LEAVE); OP(FAULT_RAISE); OP(FAULT_RESUME); OP(FAULT_RETHROW);
  OP(CALL); OP(CALL_METHOD); OP(STORE_LOCAL); OP(ALLOC);
  default: return "OTHER";
  }
#undef OP
}
}
void validate_nil_owned_contract(const nil_module &module) {
  for (const auto &function : module.functions) validate(function);
}
void validate_nil_owned_contract(const std::vector<Instruction> &program) {
  if (std::none_of(program.begin(), program.end(), [](const auto &instruction) {
        return owned(instruction.op);
      })) return;
  nil_module module;
  nil_function *function = nullptr;
  for (const auto &instruction : program) {
    if (instruction.is_function_entry) {
      if (instruction.args.size() != 1)
        throw std::runtime_error("NIL ownership contract: malformed function boundary");
      module.functions.emplace_back(); function = &module.functions.back();
      function->name = instruction.args[0];
    } else if (instruction.is_function_end) {
      function = nullptr;
    } else if (function) {
      function->body.push_back({operation(instruction.op), instruction.args, instruction.line});
    } else if (owned(instruction.op)) {
      throw std::runtime_error("NIL ownership contract: ownership operation outside a function");
    }
  }
  validate_nil_owned_contract(module);
}
}
