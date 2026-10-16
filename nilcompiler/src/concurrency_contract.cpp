#include "concurrency_contract.hpp"
#include "codegen.hpp"
#include "vm.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <unordered_map>

namespace ny {
namespace {
[[noreturn]] void invalid(const std::string &message) {
  throw std::runtime_error("NIL parallel contract: " + message);
}
std::string parameter_type(const std::string &parameter) {
  const auto colon = parameter.find(':');
  if (colon == std::string::npos) return {};
  const auto begin = parameter.find_first_not_of(" \t", colon + 1);
  if (begin == std::string::npos) return {};
  const auto end = parameter.find_last_not_of(" \t");
  return parameter.substr(begin, end - begin + 1);
}
// An allowlist is intentional: adding a language opcode cannot accidentally
// grant a worker access to process state, handles, pointers, FFI or I/O.
const std::unordered_set<std::string> pure_operations = {
    "PUSH_CONST", "POP", "LOAD_LOCAL", "STORE_LOCAL", "CALL", "RET",
    "NOP", "LABEL", "JMP", "JMP_IF", "JMP_IF_NOT", "CMP_EQ", "CMP_NE",
    "CMP_LT", "CMP_LE", "CMP_GT", "CMP_GE", "NOT", "ADD", "SUB", "MUL",
    "DIV", "MOD", "NEG", "BIT_AND", "BIT_OR", "BIT_XOR", "SHL", "SHR",
    "CAST", "MATH_SIN", "MATH_COS", "MATH_TAN", "MATH_ASIN", "MATH_ACOS",
    "MATH_ATAN", "MATH_ATAN2", "MATH_SQRT", "MATH_POW", "MATH_EXP",
    "MATH_LOG", "MATH_LOG10", "MATH_ABS", "MATH_FLOOR", "MATH_CEIL",
    "MATH_ROUND", "MATH_TRUNC", "MATH_CBRT", "MATH_MIN", "MATH_MAX"};
std::string flat_operation(Opcode operation) {
#define NY_PURE_OPCODE(name) case Opcode::name: return #name
  switch (operation) {
    NY_PURE_OPCODE(PUSH_CONST); NY_PURE_OPCODE(POP);
    NY_PURE_OPCODE(LOAD_LOCAL); NY_PURE_OPCODE(STORE_LOCAL);
    NY_PURE_OPCODE(CALL); NY_PURE_OPCODE(RET); NY_PURE_OPCODE(NOP);
    NY_PURE_OPCODE(LABEL); NY_PURE_OPCODE(JMP); NY_PURE_OPCODE(JMP_IF);
    NY_PURE_OPCODE(JMP_IF_NOT); NY_PURE_OPCODE(CMP_EQ); NY_PURE_OPCODE(CMP_NE);
    NY_PURE_OPCODE(CMP_LT); NY_PURE_OPCODE(CMP_LE); NY_PURE_OPCODE(CMP_GT);
    NY_PURE_OPCODE(CMP_GE); NY_PURE_OPCODE(NOT); NY_PURE_OPCODE(ADD);
    NY_PURE_OPCODE(SUB); NY_PURE_OPCODE(MUL); NY_PURE_OPCODE(DIV);
    NY_PURE_OPCODE(MOD); NY_PURE_OPCODE(NEG); NY_PURE_OPCODE(BIT_AND);
    NY_PURE_OPCODE(BIT_OR); NY_PURE_OPCODE(BIT_XOR); NY_PURE_OPCODE(SHL);
    NY_PURE_OPCODE(SHR); NY_PURE_OPCODE(CAST); NY_PURE_OPCODE(MATH_SIN);
    NY_PURE_OPCODE(MATH_COS); NY_PURE_OPCODE(MATH_TAN); NY_PURE_OPCODE(MATH_ASIN);
    NY_PURE_OPCODE(MATH_ACOS); NY_PURE_OPCODE(MATH_ATAN); NY_PURE_OPCODE(MATH_ATAN2);
    NY_PURE_OPCODE(MATH_SQRT); NY_PURE_OPCODE(MATH_POW); NY_PURE_OPCODE(MATH_EXP);
    NY_PURE_OPCODE(MATH_LOG); NY_PURE_OPCODE(MATH_LOG10); NY_PURE_OPCODE(MATH_ABS);
    NY_PURE_OPCODE(MATH_FLOOR); NY_PURE_OPCODE(MATH_CEIL); NY_PURE_OPCODE(MATH_ROUND);
    NY_PURE_OPCODE(MATH_TRUNC); NY_PURE_OPCODE(MATH_CBRT); NY_PURE_OPCODE(MATH_MIN);
    NY_PURE_OPCODE(MATH_MAX); NY_PURE_OPCODE(PARALLEL_MAP);
    default: return "forbidden opcode " + std::to_string(static_cast<int>(operation));
  }
#undef NY_PURE_OPCODE
}
} // namespace

bool is_parallel_scalar(const std::string &type) {
  return type == "i32" || type == "i64" || type == "u8" ||
         type == "bool" || type == "f64";
}

std::unordered_set<std::string>
collect_nil_parallel_workers(const nil_module &module) {
  bool relevant = false;
  for (const auto &function : module.functions)
    for (const auto &instruction : function.body)
      relevant |= instruction.opname == "PARALLEL_MAP";
  if (!relevant) return {};
  std::unordered_map<std::string, const nil_function *> definitions;
  for (const auto &function : module.functions)
    if (!definitions.emplace(function.name, &function).second)
      invalid("duplicate function '" + function.name + "'");
  std::unordered_set<std::string> workers;
  std::vector<std::string> pending;
  for (const auto &function : module.functions) {
    for (const auto &instruction : function.body) {
      if (instruction.opname != "PARALLEL_MAP") continue;
      if (instruction.args.size() != 3 || instruction.args[0].empty() ||
          !is_parallel_scalar(instruction.args[1]) ||
          !is_parallel_scalar(instruction.args[2]))
        invalid("PARALLEL_MAP requires worker, input scalar and output scalar");
      const auto found = definitions.find(instruction.args[0]);
      if (found == definitions.end()) invalid("undefined worker '" + instruction.args[0] + "'");
      const auto &worker = *found->second;
      if (worker.params.size() != 1 || !worker.generic_params.empty() ||
          parameter_type(worker.params[0]) != instruction.args[1] ||
          worker.return_type != instruction.args[2])
        invalid("worker signature does not match PARALLEL_MAP scalar metadata");
      if (workers.insert(worker.name).second) pending.push_back(worker.name);
    }
  }
  for (std::size_t index = 0; index < pending.size(); ++index) {
    const auto &function = *definitions.at(pending[index]);
    if (!function.generic_params.empty() ||
        (function.return_type != "void" && !is_parallel_scalar(function.return_type)))
      invalid("worker helper requires a concrete scalar/void return signature");
    for (const auto &parameter : function.params)
      if (!is_parallel_scalar(parameter_type(parameter)))
        invalid("worker helper parameters must be copied scalars");
    for (const auto &annotation : function.annotations) {
      const auto first = std::find_if_not(annotation.begin(), annotation.end(),
          [](unsigned char value) { return std::isspace(value) != 0; });
      const std::string dispatch(first, annotation.end());
      if (dispatch.rfind("@intrinsic", 0) == 0 ||
          dispatch.rfind("@interop", 0) == 0 || dispatch.rfind("@extern", 0) == 0)
        invalid("worker helpers cannot use intrinsic or external declarations");
    }
    for (const auto &instruction : function.body) {
      if (!pure_operations.count(instruction.opname))
        invalid("worker '" + function.name + "' contains non-isolated operation '" + instruction.opname + "'");
      if (instruction.opname == "CAST" &&
          (instruction.args.size() != 1 || !is_parallel_scalar(instruction.args[0])))
        invalid("worker casts must remain scalar");
      if (instruction.opname != "CALL") continue;
      if (instruction.args.size() != 2) invalid("worker calls require exact direct arity");
      const auto callee = definitions.find(instruction.args[0]);
      if (callee == definitions.end()) invalid("worker cannot call unresolved or builtin function '" + instruction.args[0] + "'");
      if (instruction.args[1] != std::to_string(callee->second->params.size()))
        invalid("worker helper call arity mismatch");
      if (workers.insert(callee->first).second) pending.push_back(callee->first);
    }
  }
  return workers;
}

void validate_nil_parallel_contract(const nil_module &module) {
  (void)collect_nil_parallel_workers(module);
}

void validate_nil_parallel_contract(const std::vector<Instruction> &program) {
  if (std::none_of(program.begin(), program.end(), [](const auto &instruction) {
        return instruction.op == Opcode::PARALLEL_MAP;
      })) return;
  nil_module module;
  nil_function *function = nullptr;
  for (const auto &instruction : program) {
    if (instruction.is_function_entry) {
      if (instruction.args.size() != 1) invalid("invalid worker function boundary");
      module.functions.push_back({});
      function = &module.functions.back();
      function->name = instruction.args[0];
      function->return_type = instruction.function_return_type;
      function->annotations = instruction.function_annotations;
      if (instruction.function_parameter_types.size() != instruction.function_param_count)
        invalid("parallel programs require typed function parameters");
      for (std::size_t index = 0; index < instruction.function_param_count; ++index)
        function->params.push_back("arg" + std::to_string(index) + ":" + instruction.function_parameter_types[index]);
    } else if (instruction.is_function_end) {
      function = nullptr;
    } else if (function) {
      function->body.push_back({flat_operation(instruction.op), instruction.args, instruction.line});
    } else if (instruction.op == Opcode::PARALLEL_MAP) {
      invalid("PARALLEL_MAP outside a declared function");
    }
  }
  validate_nil_parallel_contract(module);
}
} // namespace ny
