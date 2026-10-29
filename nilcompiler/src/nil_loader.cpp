#include "nil_loader.hpp"
#include "annotation_arguments.hpp"
#include "nil_contract.hpp"
#include "binding_reference.hpp"
#include "concurrency_contract.hpp"
#include "owned_contract.hpp"
#include "numeric_literal.hpp"
#include "scalar_text.hpp"
#include "vm.hpp"
#include "../../common/content_digest.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace ny {

namespace {

constexpr std::uint64_t max_binary_nil_size = 256ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t max_section_count = 64;
constexpr std::uint32_t max_function_count = 100'000;
constexpr std::uint32_t max_constant_count = 1'000'000;
constexpr std::uint32_t max_parameter_count = 65'536;
constexpr std::uint32_t max_instruction_count = 1'000'000;
constexpr std::uint32_t max_operand_count = 65'536;
constexpr std::uint32_t max_binary_string_size = 16U * 1024U * 1024U;
constexpr std::uint32_t max_literal_element_count = 1'000'000;
constexpr std::uint32_t max_embedded_source_count = 10'000;
constexpr std::size_t max_textual_line_size = 16U * 1024U * 1024U;

struct opcode_entry {
  std::string_view name;
  Opcode opcode;
};

// Keep this table in Opcode declaration order. Text and binary NIL both use
// this single decoder so an instruction cannot silently work in only one
// representation.
constexpr opcode_entry opcode_table[] = {
    {"PUSH_CONST_STR", Opcode::PUSH_CONST_STR},
    {"PUSH_CONST", Opcode::PUSH_CONST},
    {"PUSH_FN_PTR", Opcode::PUSH_FN_PTR},
    {"POP", Opcode::POP},
    {"LOAD_LOCAL", Opcode::LOAD_LOCAL},
    {"STORE_LOCAL", Opcode::STORE_LOCAL},
    {"REF_LOCAL", Opcode::REF_LOCAL},
    {"LOAD_GLOBAL", Opcode::LOAD_GLOBAL},
    {"STORE_GLOBAL", Opcode::STORE_GLOBAL},
    {"CALL", Opcode::CALL},
    {"RET", Opcode::RET},
    {"NOP", Opcode::NOP},
    {"JMP_IF_NOT", Opcode::JMP_IF_NOT},
    {"JMP", Opcode::JMP},
    {"JMP_IF", Opcode::JMP_IF},
    {"LABEL", Opcode::LABEL},
    {"CMP_EQ", Opcode::CMP_EQ},
    {"CMP_NE", Opcode::CMP_NE},
    {"CMP_LT", Opcode::CMP_LT},
    {"CMP_LE", Opcode::CMP_LE},
    {"CMP_GT", Opcode::CMP_GT},
    {"CMP_GE", Opcode::CMP_GE},
    {"NOT", Opcode::NOT},
    {"ADD", Opcode::ADD},
    {"SUB", Opcode::SUB},
    {"MUL", Opcode::MUL},
    {"DIV", Opcode::DIV},
    {"MOD", Opcode::MOD},
    {"NEG", Opcode::NEG},
    {"BIT_AND", Opcode::BIT_AND},
    {"BIT_OR", Opcode::BIT_OR},
    {"BIT_XOR", Opcode::BIT_XOR},
    {"SHL", Opcode::SHL},
    {"SHR", Opcode::SHR},
    {"ALLOC", Opcode::ALLOC},
    {"FREE", Opcode::FREE},
    {"READ_PTR", Opcode::READ_PTR},
    {"WRITE_PTR", Opcode::WRITE_PTR},
    {"NEW_STRUCT", Opcode::NEW_STRUCT},
    {"GET_FIELD", Opcode::GET_FIELD},
    {"SET_FIELD", Opcode::SET_FIELD},
    {"SET_FIELD_PTR", Opcode::SET_FIELD_PTR},
    {"NEW_ARRAY", Opcode::NEW_ARRAY},
    {"GET_INDEX", Opcode::GET_INDEX},
    {"SET_INDEX", Opcode::SET_INDEX},
    {"LEN", Opcode::LEN},
    {"NEW_VARIANT", Opcode::NEW_VARIANT},
    {"IS_VARIANT", Opcode::IS_VARIANT},
    {"UNWRAP_VARIANT", Opcode::UNWRAP_VARIANT},
    {"NEW_ARRAY_LITERAL", Opcode::NEW_ARRAY_LITERAL},
    {"STR_CONCAT", Opcode::STR_CONCAT},
    {"TO_STRING", Opcode::TO_STRING},
    {"CAST", Opcode::CAST},
    {"STRLEN", Opcode::STRLEN},
    {"INTEROP_CALL", Opcode::INTEROP_CALL},
    {"MATH_SIN", Opcode::MATH_SIN},
    {"MATH_COS", Opcode::MATH_COS},
    {"MATH_TAN", Opcode::MATH_TAN},
    {"MATH_ASIN", Opcode::MATH_ASIN},
    {"MATH_ACOS", Opcode::MATH_ACOS},
    {"MATH_ATAN", Opcode::MATH_ATAN},
    {"MATH_ATAN2", Opcode::MATH_ATAN2},
    {"MATH_SQRT", Opcode::MATH_SQRT},
    {"MATH_POW", Opcode::MATH_POW},
    {"MATH_EXP", Opcode::MATH_EXP},
    {"MATH_LOG", Opcode::MATH_LOG},
    {"MATH_LOG10", Opcode::MATH_LOG10},
    {"MATH_ABS", Opcode::MATH_ABS},
    {"MATH_FLOOR", Opcode::MATH_FLOOR},
    {"MATH_CEIL", Opcode::MATH_CEIL},
    {"MATH_ROUND", Opcode::MATH_ROUND},
    {"MATH_TRUNC", Opcode::MATH_TRUNC},
    {"MATH_CBRT", Opcode::MATH_CBRT},
    {"MATH_MIN", Opcode::MATH_MIN},
    {"MATH_MAX", Opcode::MATH_MAX},
    {"CONSOLE_WRITE", Opcode::CONSOLE_WRITE},
    {"CONSOLE_WRITELN", Opcode::CONSOLE_WRITELN},
    {"TIME_NOW", Opcode::TIME_NOW},
    {"TIME_SLEEP", Opcode::TIME_SLEEP},
    {"PTR_ADD", Opcode::PTR_ADD},
    {"PTR_SUB", Opcode::PTR_SUB},
    {"INLINE_ASM", Opcode::INLINE_ASM},
    {"FAULT_ENTER", Opcode::FAULT_ENTER},
    {"FAULT_LEAVE", Opcode::FAULT_LEAVE},
    {"FAULT_RAISE", Opcode::FAULT_RAISE},
    {"FAULT_SAVE", Opcode::FAULT_SAVE},
    {"FAULT_MATCH", Opcode::FAULT_MATCH},
    {"FAULT_BIND", Opcode::FAULT_BIND},
    {"FAULT_DROP", Opcode::FAULT_DROP},
    {"FAULT_RESUME", Opcode::FAULT_RESUME},
    {"FAULT_RETHROW", Opcode::FAULT_RETHROW},
    {"CLASS_UPCAST", Opcode::CLASS_UPCAST},
    {"CALL_METHOD", Opcode::CALL_METHOD},
    {"PARALLEL_MAP", Opcode::PARALLEL_MAP},
    {"OWN_ENTER", Opcode::OWN_ENTER},
    {"OWN_LEAVE", Opcode::OWN_LEAVE},
    {"OWN_BIND", Opcode::OWN_BIND},
    {"OWN_MOVE", Opcode::OWN_MOVE},
    {"OWN_ALLOC", Opcode::OWN_ALLOC},
};

constexpr std::size_t opcode_table_size =
    sizeof(opcode_table) / sizeof(opcode_table[0]);

constexpr bool opcode_table_matches_enum() {
  for (std::size_t i = 0; i < opcode_table_size; ++i) {
    if (opcode_table[i].name.empty() ||
        static_cast<std::size_t>(opcode_table[i].opcode) != i) {
      return false;
    }
  }
  return true;
}

static_assert(opcode_table_size == static_cast<std::size_t>(Opcode::COUNT),
              "NIL opcode table must cover every VM opcode");
static_assert(opcode_table_matches_enum(),
              "NIL opcode table must match VM opcode declaration order");

Opcode decode_opcode(const std::string &name, const std::string &context) {
  for (const auto &entry : opcode_table) {
    if (entry.name == name)
      return entry.opcode;
  }
  throw std::runtime_error("Unknown NIL opcode '" + name + "'" + context);
}

struct argument_range {
  std::size_t minimum;
  std::size_t maximum;
};

argument_range opcode_argument_range(Opcode opcode) {
  switch (opcode) {
  case Opcode::PUSH_CONST_STR:
  case Opcode::PUSH_CONST:
  case Opcode::PUSH_FN_PTR:
  case Opcode::LOAD_LOCAL:
  case Opcode::STORE_LOCAL:
  case Opcode::REF_LOCAL:
  case Opcode::OWN_BIND:
  case Opcode::OWN_MOVE:
  case Opcode::OWN_ALLOC:
  case Opcode::JMP_IF_NOT:
  case Opcode::JMP:
  case Opcode::JMP_IF:
  case Opcode::LABEL:
  case Opcode::NEW_STRUCT:
  case Opcode::GET_FIELD:
  case Opcode::SET_FIELD:
  case Opcode::SET_FIELD_PTR:
  case Opcode::FAULT_LEAVE:
  case Opcode::FAULT_RAISE:
  case Opcode::FAULT_SAVE:
  case Opcode::FAULT_DROP:
  case Opcode::FAULT_RESUME:
  case Opcode::FAULT_RETHROW:
  case Opcode::CLASS_UPCAST:
    return {1, 1};
  case Opcode::CALL_METHOD:
  case Opcode::PARALLEL_MAP:
    return {3, 3};
  case Opcode::LOAD_GLOBAL:
  case Opcode::STORE_GLOBAL:
    return {1, 2}; // Legacy name or name plus exact scalar/text storage type.
  case Opcode::IS_VARIANT:
    // Legacy: tag. Concrete generic metadata: tag, enum type, payload type.
    return {1, 3};
  case Opcode::NEW_ARRAY_LITERAL:
    // Operand 0 is the element count. New producers also preserve the
    // contextual element type in operand 1; retaining the one-operand form
    // keeps historical NIL readable by nilinterpreter while native lowering
    // rejects its missing ABI metadata.
    return {1, 2};
  case Opcode::CALL:
  case Opcode::FAULT_ENTER:
  case Opcode::FAULT_MATCH:
  case Opcode::FAULT_BIND:
    return {2, 2};
  case Opcode::INLINE_ASM:
    return {4, 4};
  case Opcode::NEW_VARIANT:
    // Legacy: tag, count. Concrete generic metadata appends enum and payload.
    return {2, 4};
  case Opcode::ALLOC:
  case Opcode::NEW_ARRAY:
    return {0, 1};
  case Opcode::CAST:
    return {1, 1};
  case Opcode::TO_STRING:
    return {0, 1}; // Legacy dynamic conversion or an explicit scalar source type.
  case Opcode::ADD:
  case Opcode::SUB:
  case Opcode::MUL:
  case Opcode::DIV:
  case Opcode::MOD:
  case Opcode::NEG:
  case Opcode::BIT_AND:
  case Opcode::BIT_OR:
  case Opcode::BIT_XOR:
  case Opcode::SHL:
  case Opcode::SHR:
    return {0, 1}; // Legacy word arithmetic or an exact scalar result type.
  case Opcode::UNWRAP_VARIANT:
    // Legacy: no operands or count. Concrete metadata: count, enum, payload.
    return {0, 3};
  case Opcode::INTEROP_CALL:
    return {3, 5};
  case Opcode::RET:
    return {0, 1};
  case Opcode::POP:
  case Opcode::NOP:
  case Opcode::OWN_ENTER:
  case Opcode::OWN_LEAVE:
  case Opcode::CMP_EQ:
  case Opcode::CMP_NE:
  case Opcode::CMP_LT:
  case Opcode::CMP_LE:
  case Opcode::CMP_GT:
  case Opcode::CMP_GE:
  case Opcode::NOT:
  case Opcode::FREE:
  case Opcode::READ_PTR:
  case Opcode::WRITE_PTR:
  case Opcode::GET_INDEX:
  case Opcode::SET_INDEX:
  case Opcode::LEN:
  case Opcode::STR_CONCAT:
  case Opcode::STRLEN:
  case Opcode::MATH_SIN:
  case Opcode::MATH_COS:
  case Opcode::MATH_TAN:
  case Opcode::MATH_ASIN:
  case Opcode::MATH_ACOS:
  case Opcode::MATH_ATAN:
  case Opcode::MATH_ATAN2:
  case Opcode::MATH_SQRT:
  case Opcode::MATH_POW:
  case Opcode::MATH_EXP:
  case Opcode::MATH_LOG:
  case Opcode::MATH_LOG10:
  case Opcode::MATH_ABS:
  case Opcode::MATH_FLOOR:
  case Opcode::MATH_CEIL:
  case Opcode::MATH_ROUND:
  case Opcode::MATH_TRUNC:
  case Opcode::MATH_CBRT:
  case Opcode::MATH_MIN:
  case Opcode::MATH_MAX:
  case Opcode::CONSOLE_WRITE:
  case Opcode::CONSOLE_WRITELN:
  case Opcode::TIME_NOW:
  case Opcode::TIME_SLEEP:
  case Opcode::PTR_ADD:
  case Opcode::PTR_SUB:
    return {0, 0};
  case Opcode::COUNT:
    break;
  }
  throw std::runtime_error("Invalid internal NIL opcode");
}

bool is_nonnegative_count(const std::string &value,
                          std::uint64_t maximum = max_operand_count) {
  if (value.empty() || value.front() == '-')
    return false;
  try {
    std::size_t consumed = 0;
    const auto parsed = std::stoull(value, &consumed, 10);
    return consumed == value.size() && parsed <= maximum &&
           parsed <= static_cast<unsigned>(INT_MAX);
  } catch (...) {
    return false;
  }
}

bool is_numeric_literal(const std::string &value) {
  if (value.empty())
    return false;
  try {
    std::size_t consumed = 0;
    if (is_floating_literal_spelling(value)) {
      double parsed = 0.0;
      return !parse_floating_literal(value, parsed);
    } else {
      const bool hexadecimal = has_hexadecimal_integer_prefix(value);
      (void)std::stoll(value, &consumed, hexadecimal ? 0 : 10);
    }
    return consumed == value.size();
  } catch (...) {
    return false;
  }
}

void validate_instruction_arity(Opcode opcode, const std::string &name,
                                std::size_t argument_count,
                                const std::string &context) {
  const auto range = opcode_argument_range(opcode);
  if (argument_count < range.minimum || argument_count > range.maximum) {
    const std::string expected =
        range.minimum == range.maximum
            ? std::to_string(range.minimum)
            : std::to_string(range.minimum) + " to " +
                  std::to_string(range.maximum);
    throw std::runtime_error("NIL opcode '" + name + "' expects " + expected +
                             " operand(s), received " +
                             std::to_string(argument_count) + context);
  }
}

void validate_instruction(Opcode opcode, const std::string &name,
                          const std::vector<std::string> &arguments,
                          const std::string &context) {
  validate_instruction_arity(opcode, name, arguments.size(), context);
  if (opcode == Opcode::TO_STRING && !arguments.empty() &&
      !supports_scalar_text(arguments[0]))
    throw std::runtime_error("NIL TO_STRING requires a supported scalar source type" + context);
  if (opcode == Opcode::OWN_ALLOC && arguments[0] != "i32" && arguments[0] != "i64" && arguments[0] != "u8")
    throw std::runtime_error("NIL OWN_ALLOC requires i32, i64 or u8 elements" + context);

  if ((opcode >= Opcode::FAULT_ENTER && opcode <= Opcode::FAULT_RETHROW) ||
      opcode == Opcode::CLASS_UPCAST || opcode == Opcode::CALL_METHOD ||
      opcode == Opcode::OWN_BIND || opcode == Opcode::OWN_MOVE || opcode == Opcode::PARALLEL_MAP) {
    for (const auto &argument : arguments)
      if (argument.empty() || std::any_of(argument.begin(), argument.end(), [](unsigned char c) {
            return c < 32 || c == 127;
          }))
        throw std::runtime_error("NIL fault operands must be nonempty and contain no control characters" + context);
  }

  if ((opcode == Opcode::LOAD_GLOBAL || opcode == Opcode::STORE_GLOBAL) &&
      (arguments.front().empty() ||
       (arguments.size() == 2 &&
        !is_typed_global_storage_type(arguments[1]))))
    throw std::runtime_error("NIL global access has an empty name or unsupported "
                             "storage type" + context);

  const bool arithmetic = opcode == Opcode::ADD || opcode == Opcode::SUB ||
      opcode == Opcode::MUL || opcode == Opcode::DIV || opcode == Opcode::MOD ||
      opcode == Opcode::NEG;
  const bool bitwise = opcode == Opcode::BIT_AND || opcode == Opcode::BIT_OR ||
      opcode == Opcode::BIT_XOR || opcode == Opcode::SHL || opcode == Opcode::SHR;
  if ((arithmetic || bitwise) && !arguments.empty()) {
    const auto &type = arguments.front();
    if (type != "i32" && type != "i64" && type != "u8" &&
        !(arithmetic && type == "f64"))
      throw std::runtime_error("NIL opcode '" + name +
          "' has unsupported scalar arithmetic type '" + type + "'" + context);
  }

  if (opcode == Opcode::INLINE_ASM &&
      (arguments[0] != "x86_64" || arguments[1].empty() ||
       !is_nonnegative_count(arguments[2], 8) ||
       (arguments[3] != "0" && arguments[3] != "1"))) {
    throw std::runtime_error("NIL INLINE_ASM has invalid target, template "
                             "reference, or operand counts" + context);
  }

  if (opcode == Opcode::RET && !arguments.empty() &&
      arguments.front() != nil_nonvoid_fallthrough_operand) {
    throw std::runtime_error("NIL opcode 'RET' has unknown control-flow "
                             "operand '" +
                             arguments.front() + "'" + context);
  }

  const bool invalid_variant_metadata_shape =
      (opcode == Opcode::NEW_VARIANT && arguments.size() != 2 &&
       arguments.size() != 4) ||
      (opcode == Opcode::IS_VARIANT && arguments.size() != 1 &&
       arguments.size() != 3) ||
      (opcode == Opcode::UNWRAP_VARIANT && arguments.size() != 0 &&
       arguments.size() != 1 && arguments.size() != 3);
  if (invalid_variant_metadata_shape) {
    throw std::runtime_error("NIL opcode '" + name +
                             "' has an incomplete concrete enum metadata "
                             "pair" + context);
  }

  if (opcode == Opcode::PUSH_CONST &&
      !is_numeric_literal(arguments.front())) {
    throw std::runtime_error("NIL opcode 'PUSH_CONST' has invalid numeric "
                             "operand '" + arguments.front() + "'" + context);
  }

  std::size_t count_operand = std::numeric_limits<std::size_t>::max();
  switch (opcode) {
  case Opcode::CALL:
  case Opcode::NEW_VARIANT:
    count_operand = 1;
    break;
  case Opcode::INTEROP_CALL:
  case Opcode::CALL_METHOD:
    count_operand = 2;
    break;
  case Opcode::NEW_ARRAY_LITERAL:
    count_operand = 0;
    break;
  case Opcode::NEW_ARRAY:
    if (!arguments.empty())
      count_operand = 0;
    break;
  case Opcode::UNWRAP_VARIANT:
    if (!arguments.empty())
      count_operand = 0;
    break;
  default:
    break;
  }
  const std::uint64_t maximum = opcode == Opcode::NEW_ARRAY_LITERAL ||
                                        opcode == Opcode::NEW_ARRAY
                                    ? max_literal_element_count
                                    : max_operand_count;
  if (count_operand != std::numeric_limits<std::size_t>::max() &&
      !is_nonnegative_count(arguments[count_operand], maximum)) {
    throw std::runtime_error("NIL opcode '" + name +
                             "' has invalid count operand '" +
                             arguments[count_operand] + "'" + context);
  }
}

template <typename Instructions>
void validate_global_storage_contract(const Instructions &instructions,
    std::unordered_map<std::string, std::string> &global_types) {
  for (const auto &instruction : instructions) {
    bool global = false;
    if constexpr (std::is_same_v<typename Instructions::value_type,
                                 nil_instruction>) {
      global = instruction.opname == "LOAD_GLOBAL" ||
               instruction.opname == "STORE_GLOBAL";
    } else {
      global = instruction.op == Opcode::LOAD_GLOBAL ||
               instruction.op == Opcode::STORE_GLOBAL;
    }
    if (!global)
      continue;
    const auto &name = instruction.args.front();
    const auto type = instruction.args.size() == 2 ? instruction.args[1] : "";
    const auto inserted = global_types.emplace(name, type);
    if (!inserted.second && inserted.first->second != type)
      throw std::runtime_error("NIL global '" + name +
          "' has conflicting or missing storage type metadata");
  }
}

void validate_global_storage_contract(const nil_module &module) {
  std::unordered_map<std::string, std::string> types;
  for (const auto &function : module.functions)
    validate_global_storage_contract(function.body, types);
}

void validate_global_storage_contract(const std::vector<Instruction> &program) {
  std::unordered_map<std::string, std::string> types;
  validate_global_storage_contract(program, types);
}

std::string function_skip_label(std::size_t function_index) {
  // '$' is not legal in a Ny identifier, so source-level function and label
  // names cannot collide with this loader-owned control-flow namespace.
  return "$ny.internal.function-end." + std::to_string(function_index);
}

void append_function_begin(std::vector<Instruction> &program,
                           const std::string &function_name,
                           std::size_t function_index,
                           std::size_t parameter_count,
                           const std::string &return_type) {
  program.push_back(
      {Opcode::JMP, {function_skip_label(function_index)}, 0});
  program.push_back(
      {Opcode::LABEL, {function_name}, 0, true, parameter_count, return_type});
}

void append_function_end(std::vector<Instruction> &program,
                         std::size_t function_index) {
  program.push_back(
      {Opcode::LABEL, {function_skip_label(function_index)}, 0, false, 0, "", true});
}

} // namespace

namespace {

bool is_fault_opcode(Opcode opcode) {
  return opcode >= Opcode::FAULT_ENTER && opcode <= Opcode::FAULT_RETHROW;
}

struct fault_flow_state {
  bool pending = false;
  std::vector<std::string> stack;
  std::map<std::string, std::string> locals;
  std::vector<nil_fault_region> regions;
  std::vector<std::vector<std::string>> prefixes;
  std::vector<std::string> saved;
  std::map<std::string, std::set<std::string>> initialized_fields;
};

static std::optional<std::pair<std::vector<std::string>, std::string>>
fault_callable_signature(const std::string &raw);

bool class_sensitive_type(const std::string &raw, const nil_module &module) {
  std::unordered_set<std::string> visited;
  std::vector<std::string> pending{compact_binding_type(raw)};
  while (!pending.empty()) {
    auto type = pending.back(); pending.pop_back();
    if (type.rfind("ref<", 0) == 0 && type.back() == '>') type = type.substr(4, type.size() - 5);
    if (!visited.insert(type).second) continue;
    // Compiler-owned binding cells carry stack storage in native code. Use
    // the existing exact-provenance CFG analysis for these handles too, so
    // casts, unknown calls, globals, arrays and raw pointers cannot erase the
    // bounded lifetime proven by their reference-parameter contract.
    if (contains_binding_cell_type(type)) return true;
    if (find_nil_class(module, type)) return true;
    if (const auto signature = fault_callable_signature(type)) {
      pending.push_back(signature->second);
      pending.insert(pending.end(), signature->first.begin(), signature->first.end());
    }
    for (const auto &schema : module.structs) if (schema.name == type)
      for (const auto &field : schema.fields) pending.push_back(compact_binding_type(field.second));
  }
  return false;
}

constexpr const char *unproven_class_value = "#class-unproven";
bool class_provenance_type(const std::string &type, const nil_module &module) {
  if (type == unproven_class_value) return true;
  // An opaque cast of a callable must not disguise a class-returning target
  // as a scalar callback, including through nested callable signatures.
  if (type.rfind("#callable:", 0) == 0)
    return class_sensitive_type(type.substr(10), module);
  if (!type.empty() && (type.front() == '@' || type.front() == '!')) {
    const auto marker = type.find('@');
    if (marker != std::string::npos) return class_sensitive_type(type.substr(marker + 1), module);
  }
  return false;
}

bool same_regions(const std::vector<nil_fault_region> &a,
                  const std::vector<nil_fault_region> &b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (a[i].id != b[i].id || a[i].enter_pc != b[i].enter_pc ||
        a[i].dispatch_pc != b[i].dispatch_pc ||
        a[i].stack_height != b[i].stack_height ||
        a[i].saved_slots != b[i].saved_slots) return false;
  return true;
}

// Function values are VM strings at runtime, but their checked structural
// signature must survive this analysis. An ordinary callback in a function
// that also uses a binding reference still has its existing VM contract.
static std::optional<std::pair<std::vector<std::string>, std::string>>
fault_callable_signature(const std::string &raw) {
  const auto type = compact_binding_type(raw);
  if (type.rfind("fn(", 0) != 0) return std::nullopt;
  std::vector<std::string> parameters;
  std::size_t start = 3;
  unsigned parentheses = 1, angles = 0;
  for (std::size_t index = start; index < type.size(); ++index) {
    const char c = type[index];
    if (c == '(') ++parentheses;
    else if (c == ')') {
      if (--parentheses != 0) continue;
      if (index != start) parameters.push_back(type.substr(start, index - start));
      else if (!parameters.empty()) return std::nullopt;
      if (type.substr(index + 1, 2) != "->" || index + 3 == type.size()) return std::nullopt;
      return std::make_pair(std::move(parameters), type.substr(index + 3));
    } else if (c == '<') ++angles;
    else if (c == '>' && index && type[index - 1] != '-' && angles) --angles;
    else if (c == ',' && parentheses == 1 && angles == 0) {
      if (index == start) return std::nullopt;
      parameters.push_back(type.substr(start, index - start));
      start = index + 1;
    }
  }
  return std::nullopt;
}

static std::string fault_callable_type(const nil_function &function) {
  std::string signature = "fn(";
  for (const auto &parameter : function.params) {
    const auto colon = parameter.find(':');
    if (colon == std::string::npos) return "#text";
    if (signature.size() != 3) signature += ',';
    signature += compact_binding_type(parameter.substr(colon + 1));
  }
  signature += ")->" + compact_binding_type(function.return_type);
  return fault_callable_signature(signature) ? "#callable:" + signature : "#text";
}

std::string fault_stack_type(const std::string &type, const nil_module &module) {
  if (contains_binding_cell_type(type)) {
    const auto canonical = compact_binding_type(type);
    for (const auto &schema : module.structs)
      if (canonical == schema.name || canonical == "ref<" + schema.name + ">")
        return "@" + canonical;
    return unproven_class_value;
  }
  if (fault_callable_signature(type)) return "#callable:" + compact_binding_type(type);
  if (type == "i32" || type == "i64" || type == "u8" || type == "bool") return "#word";
  if (type == "f64") return "#float";
  if (type == "text") return "#text";
  if (type.rfind("ref<", 0) == 0 && type.back() == '>')
    for (const auto &schema : module.structs)
      if (schema.name == type.substr(4, type.size() - 5)) return "@" + type;
  for (const auto &schema : module.structs)
    if (schema.name == type) return "@" + type;
  // VM embedders may supply instruction objects without serialized schemas.
  if (!type.empty() && type.front() != '&' && type.front() != '*' &&
      type.find('[') == std::string::npos && type != "any" && type != "void") {
    for (const auto &schema : module.enums)
      if (schema.name == type) return "#enum";
    return "?";
  }
  return "?";
}

nil_fault_analysis analyze_fault_body(const nil_function &function,
                                     const nil_module &module,
                                     bool require_schemas,
                                     bool force_class_validation = false,
                                     const std::unordered_set<std::size_t> *source_returns = nullptr) {
  nil_fault_analysis result;
  result.instructions.resize(function.body.size());
  std::vector<Opcode> ops;
  ops.reserve(function.body.size());
  bool has_faults = false;
  for (const auto &instruction : function.body) {
    const auto opcode = decode_opcode(instruction.opname, " in function '" + function.name + "'");
    ops.push_back(opcode);
    has_faults = has_faults || is_fault_opcode(opcode);
  }
  if (!has_faults && !force_class_validation) return result;
  const auto fail = [&](const std::string &message, std::size_t pc) -> void {
    throw std::runtime_error("NIL fault contract in function '" + function.name +
                             "' at instruction " + std::to_string(pc + 1) + ": " + message);
  };
  if (function.return_type.empty()) fail("fault-bearing function requires a return signature", 0);
  std::unordered_map<std::string, std::size_t> labels;
  std::unordered_set<std::size_t> dispatches;
  for (std::size_t pc = 0; pc < ops.size(); ++pc) {
    const auto &instruction = function.body[pc];
    validate_instruction(ops[pc], instruction.opname, instruction.args, " in fault-bearing function");
    if (ops[pc] == Opcode::LABEL && !labels.emplace(instruction.args[0], pc).second)
      fail("duplicate label '" + instruction.args[0] + "'", pc);
  }
  for (std::size_t pc = 0; pc < ops.size(); ++pc) {
    const auto &args = function.body[pc].args;
    if (ops[pc] == Opcode::FAULT_ENTER) {
      const auto found = labels.find(args[1]);
      if (found == labels.end()) fail("dispatch label must exist in the owning function", pc);
      dispatches.insert(found->second);
    }
    if (ops[pc] == Opcode::JMP || ops[pc] == Opcode::JMP_IF || ops[pc] == Opcode::JMP_IF_NOT)
      if (!labels.count(args[0])) fail("jump target must exist in the owning function", pc);
    if (ops[pc] == Opcode::FAULT_RAISE || ops[pc] == Opcode::FAULT_MATCH || ops[pc] == Opcode::FAULT_BIND) {
      const auto &type = args.back();
      if (contains_binding_cell_type(type)) fail("binding-reference cells cannot be fault payloads", pc);
      if (find_nil_class(module, type)) fail("classes cannot be fault payloads", pc);
      if (type == "void" || type == "any" || type == "bool" || type == "i32" ||
          type == "i64" || type == "u8" || type == "f64" || type == "text" ||
          type.front() == '*' || type.front() == '&' || type.back() == ']')
        fail("fault type must be a concrete nominal struct, not a scalar, pointer, or array", pc);
      const auto count = std::count_if(module.structs.begin(), module.structs.end(),
                                       [&](const auto &schema) { return schema.name == type; });
      if (require_schemas && count != 1)
        fail("fault type '" + type + "' requires one exact concrete struct schema", pc);
    }
  }
  if (ops.empty()) return result;
  std::vector<std::optional<fault_flow_state>> incoming(ops.size());
  std::deque<std::size_t> queue;
  fault_flow_state entry;
  for (const auto &parameter : function.params) {
    const auto colon = parameter.find(':');
    const auto first = colon == std::string::npos ? std::string::npos : parameter.find_first_not_of(" \t", colon + 1);
    entry.stack.push_back(first == std::string::npos ? "?" : fault_stack_type(parameter.substr(first), module));
  }
  incoming[0] = entry;
  queue.push_back(0);
  const auto enqueue = [&](std::size_t destination, const fault_flow_state &state,
                           bool exceptional, std::size_t source) {
    if (destination >= ops.size()) {
      fail("control falls through the function without RET", source);
    }
    if (!exceptional && dispatches.count(destination))
      fail("ordinary control enters a fault dispatch label", source);
    if (!incoming[destination]) {
      incoming[destination] = state;
      queue.push_back(destination);
      return;
    }
    auto &old = *incoming[destination];
    if (old.pending != state.pending || old.saved != state.saved ||
        !same_regions(old.regions, state.regions) || old.stack.size() != state.stack.size() ||
        old.prefixes != state.prefixes)
      fail("inconsistent region, saved-fault, or operand-stack state at CFG merge", destination);
    bool changed = false;
    for (std::size_t i = 0; i < old.stack.size(); ++i) {
      if (old.stack[i] == state.stack[i]) continue;
      if ((old.stack[i] == "?" || state.stack[i] == "?" ||
           old.stack[i] == unproven_class_value || state.stack[i] == unproven_class_value) &&
          (class_provenance_type(old.stack[i], module) || class_provenance_type(state.stack[i], module))) {
        if (old.stack[i] != unproven_class_value) { old.stack[i] = unproven_class_value; changed = true; }
        continue;
      }
      if (old.stack[i] != "?" && state.stack[i] != "?")
        fail("incompatible operand types at CFG merge", destination);
      if (old.stack[i] != "?") { old.stack[i] = "?"; changed = true; }
    }
    for (auto &local : old.locals) {
      const auto found = state.locals.find(local.first);
      if (found == state.locals.end() || found->second != local.second) {
        const bool tainted = class_provenance_type(local.second, module) ||
            (found != state.locals.end() && class_provenance_type(found->second, module));
        const std::string merged = tainted ? unproven_class_value : "?";
        if (local.second != merged) { local.second = merged; changed = true; }
      }
    }
    for (const auto &local : state.locals)
      if (!old.locals.count(local.first) && class_provenance_type(local.second, module)) {
        old.locals[local.first] = unproven_class_value; changed = true;
      }
    for (auto &initialization : old.initialized_fields) {
      const auto other = state.initialized_fields.find(initialization.first);
      for (auto field = initialization.second.begin(); field != initialization.second.end();) {
        if (other == state.initialized_fields.end() || !other->second.count(*field)) {
          field = initialization.second.erase(field); changed = true;
        } else ++field;
      }
    }
    if (changed) queue.push_back(destination);
  };
  while (!queue.empty()) {
    const auto pc = queue.front(); queue.pop_front();
    auto state = *incoming[pc];
    auto &exported = result.instructions[pc];
    exported = {true, state.pending, state.stack.size(), state.regions, state.saved};
    const auto opcode = ops[pc];
    const auto &args = function.body[pc].args;
    if (state.pending && opcode != Opcode::LABEL && opcode != Opcode::FAULT_SAVE)
      fail("pending fault must be saved before ordinary execution", pc);
    const auto require_complete = [&](const std::string &type) {
      if (!type.empty() && type.front() == '!')
        fail("class or class-containing struct escapes before all fields are initialized", pc);
    };
    const auto pop = [&](std::size_t count, bool initializing_receiver = false) {
      const auto floor = state.regions.empty() ? 0 : state.regions.back().stack_height;
      if (state.stack.size() < count || state.stack.size() - count < floor)
        fail("operand underflow or protected stack-prefix consumption", pc);
      for (std::size_t i = state.stack.size() - count; i < state.stack.size(); ++i)
        if (!initializing_receiver || i != state.stack.size() - count) require_complete(state.stack[i]);
      state.stack.resize(state.stack.size() - count);
    };
    const auto class_type = [&](const std::string &type) {
      return class_provenance_type(type, module);
    };
    const auto reject_class_operands = [&](std::size_t count) {
      if (state.stack.size() < count) fail("operand underflow", pc);
      for (std::size_t i = state.stack.size() - count; i < state.stack.size(); ++i)
        if (class_type(state.stack[i])) fail("class value cannot be used by this untyped, scalar, or raw-pointer operation", pc);
    };
    const auto check_class_argument = [&](const std::string &expected, const std::string &actual) {
      const auto checked = fault_stack_type(expected, module);
      if ((class_type(checked) || class_type(actual)) && checked != actual)
        fail("class argument or storage requires its exact checked static view", pc);
      if (checked.rfind("#callable:", 0) == 0 && checked != actual)
        fail("callable argument or storage requires its exact checked signature", pc);
    };
    const auto opaque_stack_type = [&](const std::string &raw) {
      const auto type = fault_stack_type(raw, module);
      if (type.rfind("#callable:", 0) == 0)
        fail("opaque result metadata cannot establish a checked callable signature", pc);
      return type;
    };
    const auto exception_edge = [&](fault_flow_state exceptional) {
      if (exceptional.regions.empty()) return;
      const auto region = exceptional.regions.back();
      for (const auto &slot : region.saved_slots)
        if (std::find(exceptional.saved.begin(), exceptional.saved.end(), slot) == exceptional.saved.end())
          fail("propagation would restore an absent saved fault slot", pc);
      exceptional.regions.pop_back();
      exceptional.stack = exceptional.prefixes.back();
      exceptional.prefixes.pop_back();
      exceptional.saved.erase(std::remove_if(exceptional.saved.begin(), exceptional.saved.end(),
          [&](const auto &slot) {
            return std::find(region.saved_slots.begin(), region.saved_slots.end(), slot) == region.saved_slots.end();
          }), exceptional.saved.end());
      exceptional.pending = true;
      enqueue(region.dispatch_pc, exceptional, true, pc);
    };
    const auto require_saved = [&]() {
      const auto found = std::find(state.saved.begin(), state.saved.end(), args[0]);
      if (found == state.saved.end()) fail("saved fault slot is absent or consumed", pc);
      return found;
    };
    bool terminal = false;
    switch (opcode) {
    case Opcode::FAULT_ENTER: {
      if (std::any_of(state.regions.begin(), state.regions.end(), [&](const auto &region) { return region.id == args[0]; }))
        fail("duplicate live fault region", pc);
      state.regions.push_back({args[0], pc, labels.at(args[1]), state.stack.size(), state.saved});
      state.prefixes.push_back(state.stack);
      // Landing paths are validated even for an empty protected body.
      exception_edge(state);
      break;
    }
    case Opcode::FAULT_LEAVE:
      if (state.regions.empty() || state.regions.back().id != args[0])
        fail("leave does not match the innermost region", pc);
      state.regions.pop_back(); state.prefixes.pop_back(); break;
    case Opcode::FAULT_SAVE:
      if (!state.pending) fail("save requires pending propagation", pc);
      if (std::find(state.saved.begin(), state.saved.end(), args[0]) != state.saved.end())
        fail("save would overwrite a live fault slot", pc);
      state.saved.push_back(args[0]); state.pending = false; break;
    case Opcode::FAULT_MATCH:
      require_saved(); state.stack.push_back("#word"); break;
    case Opcode::FAULT_BIND:
      require_saved(); state.stack.push_back("@" + args[1]); break;
    case Opcode::FAULT_DROP:
      state.saved.erase(require_saved()); break;
    case Opcode::FAULT_RESUME:
      state.saved.erase(require_saved()); exception_edge(state); terminal = true; break;
    case Opcode::FAULT_RETHROW:
      require_saved(); exception_edge(state); terminal = true; break;
    case Opcode::FAULT_RAISE: {
      if (state.stack.empty()) fail("raise requires a struct payload", pc);
      const auto type = state.stack.back(); pop(1);
      if (type != "@" + args[0] && (require_schemas || type != "?"))
        fail("raise payload does not have the exact concrete struct type", pc);
      exception_edge(state); terminal = true; break;
    }
    case Opcode::PUSH_CONST:
      state.stack.push_back(is_floating_literal_spelling(args[0]) ? "#float" : "#word"); break;
    case Opcode::PUSH_CONST_STR:
      state.stack.push_back("#text"); break;
    case Opcode::PUSH_FN_PTR: {
      const auto target = std::find_if(module.functions.begin(), module.functions.end(),
          [&](const auto &candidate) { return candidate.name == args[0]; });
      state.stack.push_back(target == module.functions.end() ? "#text" : fault_callable_type(*target));
      break;
    }
    case Opcode::LOAD_LOCAL: {
      const auto local = state.locals.find(args[0]);
      state.stack.push_back(local == state.locals.end() ? "?" : local->second); break;
    }
    case Opcode::STORE_LOCAL: {
      if (state.stack.empty()) fail("store requires an operand", pc);
      const auto type = state.stack.back(); pop(1); state.locals[args[0]] = type; break;
    }
    case Opcode::REF_LOCAL: {
      const auto local = state.locals.find(args[0]);
      if (local != state.locals.end() && local->second == unproven_class_value)
        state.stack.push_back(unproven_class_value);
      else if (local != state.locals.end() && local->second.size() > 1 && local->second[0] == '@' &&
          std::any_of(module.structs.begin(), module.structs.end(),
                      [&](const auto &schema) { return schema.name == local->second.substr(1); }))
        state.stack.push_back("@ref<" + local->second.substr(1) + ">");
      else if (local != state.locals.end() && class_provenance_type(local->second, module))
        state.stack.push_back(unproven_class_value);
      else state.stack.push_back("?");
      break;
    }
    case Opcode::LOAD_GLOBAL: {
      const auto type = args.size() == 2 ? opaque_stack_type(args[1]) : "?";
      state.stack.push_back(type); break;
    }
    case Opcode::STORE_GLOBAL:
      reject_class_operands(1); pop(1); break;
    case Opcode::POP: pop(1); break;
    case Opcode::FREE:
    case Opcode::CONSOLE_WRITE: case Opcode::CONSOLE_WRITELN: case Opcode::TIME_SLEEP:
      reject_class_operands(1); pop(1); break;
    case Opcode::NEW_STRUCT: {
      if (const auto *klass = find_nil_class(module, args[0]); klass && klass->is_abstract)
        fail("cannot construct an abstract class", pc);
      const auto schema = std::find_if(module.structs.begin(), module.structs.end(),
                                      [&](const auto &value) { return value.name == args[0]; });
      if (class_sensitive_type(args[0], module) && schema != module.structs.end() && !schema->fields.empty()) {
        const auto construction = "!" + std::to_string(pc) + "@" + args[0];
        state.initialized_fields[construction].clear();
        state.stack.push_back(construction);
      } else state.stack.push_back("@" + args[0]);
      break;
    }
    case Opcode::GET_FIELD: {
      if (state.stack.empty()) fail("field read requires an operand", pc);
      auto owner = state.stack.back(); pop(1);
      if (owner.rfind("@ref<", 0) == 0 && owner.back() == '>') owner = "@" + owner.substr(5, owner.size() - 6);
      std::string field_type = "?";
      bool known_field = false;
      if (!owner.empty() && owner[0] == '@')
        for (const auto &schema : module.structs)
          if (schema.name == owner.substr(1))
            for (const auto &field : schema.fields)
              if (field.first == args[0]) { field_type = opaque_stack_type(field.second); known_field = true; }
      if (class_type(owner) && !known_field) fail("class field read requires a declared field", pc);
      state.stack.push_back(field_type); break;
    }
    case Opcode::SET_FIELD: {
      if (state.stack.size() < 2) fail("field write requires two operands", pc);
      const auto owner = state.stack[state.stack.size() - 2];
      const auto type_start = owner.find('@');
      auto nominal = type_start != std::string::npos ? owner.substr(type_start + 1) : "";
      if (nominal.rfind("ref<", 0) == 0 && nominal.back() == '>') nominal = nominal.substr(4, nominal.size() - 5);
      bool known_field = false;
      for (const auto &schema : module.structs) if (schema.name == nominal)
        for (const auto &field : schema.fields) if (field.first == args[0]) {
          if (binding_cell_value_type(nominal) &&
              fault_stack_type(field.second, module) != state.stack.back())
            fail("binding-reference cell write requires its exact scalar value category", pc);
          check_class_argument(field.second, state.stack.back()); known_field = true;
        }
      if ((class_type(owner) || class_type(state.stack.back())) && !known_field)
        fail("class field write requires a declared field and receiver", pc);
      auto result = owner;
      if (!owner.empty() && owner[0] == '!') {
        auto &initialized = state.initialized_fields[owner];
        initialized.insert(args[0]);
        for (const auto &schema : module.structs) if (schema.name == nominal &&
            std::all_of(schema.fields.begin(), schema.fields.end(), [&](const auto &field) { return initialized.count(field.first) != 0; }))
          result = "@" + nominal;
      }
      pop(2, true); state.stack.push_back(result); break;
    }
    case Opcode::SET_FIELD_PTR: case Opcode::WRITE_PTR: reject_class_operands(2); pop(2); break;
    case Opcode::GET_INDEX: reject_class_operands(2); pop(2); state.stack.push_back("?"); break;
    case Opcode::SET_INDEX: reject_class_operands(3); pop(3); break;
    case Opcode::NEW_ARRAY_LITERAL:
      reject_class_operands(static_cast<std::size_t>(std::stoull(args[0])));
      pop(static_cast<std::size_t>(std::stoull(args[0]))); state.stack.push_back("?"); break;
    case Opcode::NEW_ARRAY:
      if (args.empty()) { reject_class_operands(1); pop(1); }
      state.stack.push_back("?"); break;
    case Opcode::OWN_ENTER: case Opcode::OWN_LEAVE: case Opcode::OWN_BIND: break;
    case Opcode::OWN_MOVE:
      state.stack.push_back(state.locals.count(args[0]) ? state.locals.at(args[0]) : "?"); break;
    case Opcode::PARALLEL_MAP:
      reject_class_operands(1); pop(1);
      state.stack.push_back(fault_stack_type("array<" + args[2] + ">", module)); break;
    case Opcode::OWN_ALLOC:
    case Opcode::ALLOC:
      reject_class_operands(1); pop(1); state.stack.push_back("?"); break;
    case Opcode::NEW_VARIANT:
      reject_class_operands(static_cast<std::size_t>(std::stoull(args[1])));
      pop(static_cast<std::size_t>(std::stoull(args[1]))); state.stack.push_back("#enum"); break;
    case Opcode::IS_VARIANT: reject_class_operands(1); pop(1); state.stack.push_back("#word"); break;
    case Opcode::UNWRAP_VARIANT: {
      reject_class_operands(1);
      if (args.size() == 3 && class_sensitive_type(args[2], module))
        fail("enum payload metadata cannot fabricate class provenance", pc);
      pop(1); const auto count = args.empty() ? 0 : std::stoull(args[0]);
      for (std::size_t i = 0; i < count; ++i) state.stack.push_back(args.size() == 3 ? opaque_stack_type(args[2]) : "?");
      break;
    }
    case Opcode::CALL: {
      exception_edge(state);
      const auto count = static_cast<std::size_t>(std::stoull(args[1]));
      if (state.stack.size() < count) fail("call argument underflow", pc);
      const auto local_callable = state.locals.find(args[0]);
      if (local_callable != state.locals.end() && local_callable->second.rfind("#callable:", 0) == 0) {
        const auto signature = fault_callable_signature(local_callable->second.substr(10));
        if (!signature || signature->first.size() != count)
          fail("indirect call requires its exact callable signature and argument count", pc);
        // Reference parameters deliberately require direct calls. A known
        // ordinary callback signature can establish its result category but
        // cannot authorize passing caller storage or class provenance.
        reject_class_operands(count);
        for (const auto &type : signature->first)
          if (class_sensitive_type(type, module))
            fail("indirect calls cannot receive binding-reference or class storage", pc);
        if (class_sensitive_type(signature->second, module))
          fail("indirect calls cannot return binding-reference or class storage", pc);
        pop(count);
        if (signature->second != "void")
          state.stack.push_back(fault_stack_type(signature->second, module));
        break;
      }
      if (local_callable != state.locals.end())
        fail("indirect call lacks a checked callable signature", pc);
      const auto callee = std::find_if(module.functions.begin(), module.functions.end(), [&](const auto &fn) { return fn.name == args[0]; });
      if (callee != module.functions.end()) {
        if (callee->return_type.empty() || callee->params.size() != count)
          fail("call requires an exact signature and matching arity", pc);
        for (std::size_t i = 0; i < count; ++i) {
          const auto colon = callee->params[i].find(':');
          auto type = colon == std::string::npos ? "" : callee->params[i].substr(colon + 1);
          const auto first = type.find_first_not_of(" \t");
          if (first != std::string::npos) type.erase(0, first);
          check_class_argument(type, state.stack[state.stack.size() - count + i]);
        }
      } else reject_class_operands(count);
      pop(count);
      if (callee != module.functions.end()) {
        if (callee->return_type != "void") {
          const auto result_type = fault_stack_type(callee->return_type, module);
          if (result_type.rfind("#callable:", 0) == 0 &&
              (callee->body.empty() || std::any_of(callee->annotations.begin(), callee->annotations.end(),
                  [](const auto &annotation) {
                    const auto dispatch = compact_binding_type(annotation);
                    return dispatch.rfind("@intrinsic", 0) == 0 || dispatch.rfind("@interop", 0) == 0 ||
                        dispatch.rfind("@ritual", 0) == 0 || dispatch.rfind("@native", 0) == 0;
                  })))
            fail("external calls cannot establish a checked callable result", pc);
          state.stack.push_back(result_type);
        }
      } else if (args[0] == "println" || args[0] == "core__print" ||
                 args[0] == "console__writeln" || args[0] == "console__write" ||
                 args[0] == "console__sys_write" || args[0] == "array__push" || args[0] == "array__set") {
        // Historical adapters with a fixed void result.
      } else if (args[0] == "panic" || args[0] == "ny__lang__core__panic") terminal = true;
      else if (args[0] == "String__substr" || args[0] == "String__substring" ||
               args[0] == "String__to_uppercase" || args[0] == "String__to_upper" ||
               args[0] == "String__to_lowercase" || args[0] == "String__to_lower" ||
               args[0] == "String__trim" || args[0] == "String__replace" ||
               args[0] == "core__to_string" || args[0] == "core__to_string_i64" ||
               args[0] == "core__to_string_f64")
        state.stack.push_back("#text");
      else if (args[0] == "String__len" || args[0] == "String__length" ||
               args[0] == "String__contains" || args[0] == "String__starts_with" ||
               args[0] == "String__ends_with" || args[0] == "String__find")
        state.stack.push_back("#word");
      else if (args[0].rfind("__ritual_", 0) == 0 || args[0].rfind("String__", 0) == 0 ||
               args[0].rfind("array__", 0) == 0 || args[0].rfind("core__to_string", 0) == 0)
        state.stack.push_back("?");
      else fail("call in a fault-bearing function lacks a supported exact result signature", pc);
      break;
    }
    case Opcode::CLASS_UPCAST: {
      if (state.stack.empty() || state.stack.back().empty() || state.stack.back()[0] != '@' ||
          !nil_class_assignable(module, args[0], state.stack.back().substr(1)))
        fail("upcast requires a proven plain class and declared ancestor", pc);
      pop(1); state.stack.push_back("@" + args[0]); break;
    }
    case Opcode::CALL_METHOD: {
      if (!find_nil_class(module, args[0])) fail("virtual call requires explicit static class metadata", pc);
      const auto method = resolve_nil_class_method(module, args[0], args[1]);
      if (!method.method) fail("method call requires a declared slot", pc);
      const auto count = static_cast<std::size_t>(std::stoull(args[2]));
      if (count != method.method->parameter_types.size() + 1 || state.stack.size() < count)
        fail("virtual call argument count mismatch", pc);
      const bool reference = method.method->receiver_type.rfind("ref<", 0) == 0;
      const auto expected = reference ? "@ref<" + args[0] + ">" : "@" + args[0];
      if (state.stack[state.stack.size() - count] != expected)
        fail("virtual call receiver lacks its exact checked static view", pc);
      for (std::size_t i = 0; i < method.method->parameter_types.size(); ++i)
        check_class_argument(method.method->parameter_types[i], state.stack[state.stack.size() - count + i + 1]);
      exception_edge(state); pop(count);
      if (method.method->return_type != "void") state.stack.push_back(fault_stack_type(method.method->return_type, module));
      break;
    }
    case Opcode::INTEROP_CALL: {
      reject_class_operands(static_cast<std::size_t>(std::stoull(args[2])));
      pop(static_cast<std::size_t>(std::stoull(args[2])));
      if (args.size() < 4) fail("interop in a fault-bearing function requires explicit result metadata", pc);
      if (class_sensitive_type(args[3], module)) fail("interop cannot synthesize class provenance", pc);
      if (args[3] != "void") state.stack.push_back(opaque_stack_type(args[3]));
      break;
    }
    case Opcode::INLINE_ASM:
      reject_class_operands(static_cast<std::size_t>(std::stoull(args[2])));
      pop(static_cast<std::size_t>(std::stoull(args[2])));
      if (args[3] == "1") state.stack.push_back("#word");
      break;
    case Opcode::CAST: {
      if (state.stack.empty()) fail("cast requires an operand", pc);
      const auto source_type = state.stack.back();
      const auto target_type = fault_stack_type(args[0], module);
      if (class_type(source_type) || class_type(target_type))
        fail("CAST cannot fabricate or erase class provenance; use CLASS_UPCAST", pc);
      if (target_type.rfind("#callable:", 0) == 0 && source_type != target_type)
        fail("CAST cannot fabricate or change a callable signature", pc);
      if (!target_type.empty() && target_type.front() == '@' && source_type != target_type)
        fail("cast cannot fabricate a concrete fault payload", pc);
      pop(1); state.stack.push_back(target_type); break;
    }
    case Opcode::READ_PTR: reject_class_operands(1); pop(1); state.stack.push_back("?"); break;
    case Opcode::TO_STRING: reject_class_operands(1); pop(1); state.stack.push_back("#text"); break;
    case Opcode::STR_CONCAT: reject_class_operands(2); pop(2); state.stack.push_back("#text"); break;
    case Opcode::LEN: case Opcode::STRLEN: case Opcode::NOT:
      reject_class_operands(1); pop(1); state.stack.push_back("#word"); break;
    case Opcode::TIME_NOW: state.stack.push_back("#word"); break;
    case Opcode::RET:
      if (!state.regions.empty() || !state.saved.empty() || state.pending)
        fail("return bypasses live fault state", pc);
      if (args.empty()) {
        const std::size_t expected = function.return_type == "void" ? 0U : 1U;
        const bool compiler_source_return = source_returns && source_returns->count(pc);
        if (state.stack.size() < expected ||
            (state.stack.size() != expected && !compiler_source_return))
          fail("return has the wrong operand stack height", pc);
      }
      if (args.empty() && function.return_type != "void") {
        require_complete(state.stack.back()); check_class_argument(function.return_type, state.stack.back());
      }
      terminal = true; break;
    case Opcode::JMP:
      enqueue(labels.at(args[0]), state, false, pc); terminal = true; break;
    case Opcode::JMP_IF: case Opcode::JMP_IF_NOT:
      reject_class_operands(1); pop(1); enqueue(labels.at(args[0]), state, false, pc); break;
    case Opcode::LABEL: case Opcode::NOP: break;
    case Opcode::NEG:
      reject_class_operands(1);
      pop(1); state.stack.push_back(args.empty() ? "?" : opaque_stack_type(args[0])); break;
    case Opcode::MATH_SIN: case Opcode::MATH_COS: case Opcode::MATH_TAN:
    case Opcode::MATH_ASIN: case Opcode::MATH_ACOS: case Opcode::MATH_ATAN:
    case Opcode::MATH_SQRT: case Opcode::MATH_EXP: case Opcode::MATH_LOG:
    case Opcode::MATH_LOG10: case Opcode::MATH_ABS: case Opcode::MATH_FLOOR:
    case Opcode::MATH_CEIL: case Opcode::MATH_ROUND: case Opcode::MATH_TRUNC:
    case Opcode::MATH_CBRT:
      reject_class_operands(1); pop(1); state.stack.push_back("#float"); break;
    case Opcode::MATH_ATAN2: case Opcode::MATH_POW: case Opcode::MATH_MIN: case Opcode::MATH_MAX:
      reject_class_operands(2); pop(2); state.stack.push_back("#float"); break;
    case Opcode::CMP_EQ: case Opcode::CMP_NE: case Opcode::CMP_LT:
    case Opcode::CMP_LE: case Opcode::CMP_GT: case Opcode::CMP_GE:
      reject_class_operands(2); pop(2); state.stack.push_back("#word"); break;
    case Opcode::ADD: case Opcode::SUB: case Opcode::MUL: case Opcode::DIV:
    case Opcode::MOD: case Opcode::BIT_AND: case Opcode::BIT_OR: case Opcode::BIT_XOR:
    case Opcode::SHL: case Opcode::SHR: case Opcode::PTR_ADD: case Opcode::PTR_SUB:
      reject_class_operands(2);
      pop(2); state.stack.push_back(args.empty() ? "?" : opaque_stack_type(args[0])); break;
    case Opcode::COUNT: fail("invalid opcode", pc); break;
    }
    if (!terminal) enqueue(pc + 1, state, false, pc);
  }
  return result;
}

} // namespace

nil_fault_analysis analyze_nil_fault_function(const nil_function &function,
                                              const nil_module &module) {
  return analyze_fault_body(function, module, true);
}

void finalize_nil_switch_returns(nil_module &module) {
  for (auto &function : module.functions) {
    std::unordered_set<std::size_t> source_returns;
    for (std::size_t pc = 0; pc < function.body.size(); ++pc) {
      const auto &instruction = function.body[pc];
      if (instruction.opname == "RET" && instruction.args.size() == 1 &&
          instruction.args.front() == nil_switch_return_marker)
        source_returns.insert(pc);
    }
    if (source_returns.empty()) continue;
    // Analyze a complete CFG with ordinary RET spelling. Only these exact
    // compiler-owned source sites may temporarily carry extra operands;
    // generated returns, underflow, region state, and class provenance retain
    // their normal validation. No executable loader invokes this preparation.
    auto prepared = function;
    for (const auto pc : source_returns) prepared.body[pc].args.clear();
    const auto analysis = analyze_fault_body(prepared, module, true, true, &source_returns);
    std::vector<nil_instruction> body;
    body.reserve(prepared.body.size());
    for (std::size_t pc = 0; pc < prepared.body.size(); ++pc) {
      const auto &instruction = prepared.body[pc];
      const auto &state = analysis.instructions[pc];
      if (source_returns.count(pc) && state.reachable) {
        const std::size_t expected = function.return_type == "void" ? 0 : 1;
        const auto surplus = state.stack_height - expected;
        if (surplus) {
          const auto saved = "$nil.switch.return." + std::to_string(pc);
          const auto append = [&](const std::string &op, std::vector<std::string> args = {}) {
            auto adjustment = instruction;
            adjustment.opname = op;
            adjustment.args = std::move(args);
            body.push_back(std::move(adjustment));
          };
          // Cleanup has already completed. The old fault-protected operand
          // prefix may now be retired, preserving the typed return value.
          if (expected) append("STORE_LOCAL", {saved});
          for (std::size_t count = 0; count < surplus; ++count) append("POP");
          if (expected) append("LOAD_LOCAL", {saved});
        }
      }
      body.push_back(instruction);
    }
    prepared.body = std::move(body);
    // Assert the exact executable contract after finalization, including
    // functions that would otherwise not need fault/class validation.
    (void)analyze_fault_body(prepared, module, true, true);
    function.body = std::move(prepared.body);
  }
}

void validate_nil_fault_contract(const nil_module &module) {
  for (const auto &function : module.functions)
    (void)analyze_fault_body(function, module, true);
}

void validate_nil_class_provenance(const nil_module &module) {
  std::unordered_set<std::string> related;
  const auto invalid_binding = [](const std::string &message) {
    throw std::runtime_error("NIL binding-reference contract: " + message);
  };
  std::set<std::string> binding_schemas;
  for (const auto &schema : module.structs) {
    if (contains_binding_cell_type(schema.name)) {
      const auto type = binding_cell_value_type(schema.name);
      if (!type || compact_binding_type(schema.name) != schema.name ||
          schema.fields != std::vector<std::pair<std::string, std::string>>{{"value", *type}} ||
          std::find(schema.annotations.begin(), schema.annotations.end(), "@nil_binding_cell") == schema.annotations.end() ||
          !binding_schemas.insert(schema.name).second || find_nil_class(module, schema.name))
        invalid_binding("storage requires one exact compiler-owned scalar cell schema");
      related.insert(schema.name);
      related.insert("ref<" + schema.name + ">");
    }
    for (const auto &field : schema.fields)
      if (contains_binding_cell_type(field.second))
        invalid_binding("a binding-reference cell cannot be stored in an aggregate field");
  }
  for (const auto &enumeration : module.enums)
    for (const auto &variant : enumeration.variants)
      if (contains_binding_cell_type(variant.associated_type))
        invalid_binding("a binding-reference cell cannot be stored in an enum payload");
  for (const auto &klass : module.classes)
    for (const auto &method : klass.methods) {
      if (contains_binding_cell_type(method.receiver_type) || contains_binding_cell_type(method.return_type))
        invalid_binding("binding references are unavailable in virtual method signatures");
      for (const auto &type : method.parameter_types)
        if (contains_binding_cell_type(type))
          invalid_binding("binding references are unavailable in virtual method signatures");
    }
  std::set<std::string> binding_callable_names;
  for (const auto &function : module.functions) {
    if (!binding_schemas.empty() && !binding_callable_names.insert(function.name).second)
      invalid_binding("function names must be unambiguous when binding references are present");
    if (contains_binding_cell_type(function.return_type))
      invalid_binding("a function cannot return binding-reference storage");
    bool has_reference_parameter = false;
    for (const auto &parameter : function.params) {
      const auto colon = parameter.find(':');
      const auto type = colon == std::string::npos ? std::string{} : compact_binding_type(parameter.substr(colon + 1));
      if (!contains_binding_cell_type(type)) continue;
      const auto scalar = binding_reference_value_type(type);
      if (!scalar || !binding_schemas.count(std::string(nil_binding_cell_prefix) + *scalar))
        invalid_binding("parameters require an exact ref<cell> scalar storage contract");
      has_reference_parameter = true;
    }
    if (has_reference_parameter)
      for (const auto &annotation : function.annotations) {
        const auto dispatch = compact_binding_type(annotation);
        if (dispatch.rfind("@intrinsic", 0) == 0 || dispatch.rfind("@interop", 0) == 0 ||
            dispatch.rfind("@ritual", 0) == 0 || dispatch.rfind("@native", 0) == 0)
          invalid_binding("binding references cannot cross an external or intrinsic boundary");
      }
    // Native storage is one alloca per constructor. Re-entering a constructor
    // in a loop would reuse its address while an earlier alias is still live.
    // Compiler constructors occur in the straight-line invocation prologue.
    bool in_prologue = true;
    for (const auto &instruction : function.body) {
      if (binding_schemas.empty() &&
          std::any_of(instruction.args.begin(), instruction.args.end(),
                      [](const auto &argument) { return contains_binding_cell_type(argument); }))
        invalid_binding("binding-reference instructions require explicit cell schemas");
      if (instruction.opname == "LABEL" || instruction.opname == "JMP" ||
          instruction.opname == "JMP_IF" || instruction.opname == "JMP_IF_NOT" ||
          instruction.opname == "CALL" || instruction.opname == "CALL_METHOD" ||
          instruction.opname == "RET" || instruction.opname.rfind("FAULT_", 0) == 0)
        in_prologue = false;
      if (instruction.opname == "NEW_STRUCT" && !instruction.args.empty() &&
          contains_binding_cell_type(instruction.args[0]) &&
          (!in_prologue || !binding_schemas.count(instruction.args[0])))
        invalid_binding("cells must be constructed from an exact schema in the invocation prologue");
    }
  }
  for (const auto &klass : module.classes) {
    related.insert(klass.name);
    related.insert("ref<" + klass.name + ">");
  }
  // Most modules have neither classes nor binding cells. Their ordinary call
  // graphs need no provenance walk after the metadata checks above.
  if (related.empty()) return;
  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto &schema : module.structs)
      if (!related.count(schema.name) && std::any_of(schema.fields.begin(), schema.fields.end(),
              [&](const auto &field) {
                return related.count(compact_binding_type(field.second)) != 0 ||
                    class_sensitive_type(field.second, module) || fault_callable_signature(field.second).has_value();
              }))
        changed = related.insert(schema.name).second || changed;
  }
  const auto related_type = [&](const std::string &raw) {
    const auto type = compact_binding_type(raw);
    return related.count(type) != 0 || contains_binding_cell_type(type) ||
        fault_callable_signature(type).has_value();
  };
  for (const auto &function : module.functions) {
    bool relevant = related_type(function.return_type);
    for (const auto &parameter : function.params) {
      const auto colon = parameter.find(':');
      const auto start = colon == std::string::npos ? std::string::npos : parameter.find_first_not_of(" \t", colon + 1);
      if (start != std::string::npos && related_type(parameter.substr(start))) relevant = true;
    }
    for (const auto &instruction : function.body) {
      if (instruction.opname == "CLASS_UPCAST" || instruction.opname == "CALL_METHOD") relevant = true;
      for (const auto &argument : instruction.args)
        if (related_type(argument)) relevant = true;
      if ((instruction.opname == "CALL" || instruction.opname == "PUSH_FN_PTR") && !instruction.args.empty())
        for (const auto &target : module.functions) if (target.name == instruction.args[0]) {
          if (related_type(target.return_type)) relevant = true;
          for (const auto &parameter : target.params) {
            const auto colon = parameter.find(':');
            const auto start = colon == std::string::npos ? std::string::npos : parameter.find_first_not_of(" \t", colon + 1);
            if (start != std::string::npos && related_type(parameter.substr(start))) relevant = true;
          }
        }
    }
    if (relevant) (void)analyze_fault_body(function, module, true, true);
  }
}

static void validate_fault_program(const std::vector<Instruction> &program,
                                   const nil_module *metadata) {
  if (std::none_of(program.begin(), program.end(), [](const auto &instruction) {
        return is_fault_opcode(instruction.op);
      })) return;
  nil_module module;
  if (metadata) {
    module.structs = metadata->structs;
    module.enums = metadata->enums;
    module.classes = metadata->classes;
  }
  nil_function *function = nullptr;
  for (const auto &instruction : program) {
    if (instruction.is_function_entry) {
      if (instruction.args.empty()) throw std::runtime_error("fault function entry lacks a name");
      module.functions.push_back({});
      function = &module.functions.back();
      function->name = instruction.args[0];
      function->return_type = instruction.function_return_type;
      function->annotations = instruction.function_annotations;
      for (std::size_t i = 0; i < instruction.function_param_count; ++i)
        function->params.push_back("arg" + std::to_string(i) + ":" +
            (i < instruction.function_parameter_types.size() ? instruction.function_parameter_types[i] : "any"));
      continue;
    }
    if (instruction.is_function_end) { function = nullptr; continue; }
    if (!function) {
      if (is_fault_opcode(instruction.op)) throw std::runtime_error("fault instruction outside a declared function");
      continue;
    }
    const auto ordinal = static_cast<std::size_t>(instruction.op);
    if (ordinal >= opcode_table_size) throw std::runtime_error("invalid NIL opcode in fault program");
    function->body.push_back({std::string(opcode_table[ordinal].name), instruction.args, instruction.line});
    if (!metadata && instruction.op == Opcode::NEW_STRUCT && instruction.args.size() == 1 &&
        std::none_of(module.structs.begin(), module.structs.end(), [&](const auto &schema) { return schema.name == instruction.args[0]; }))
      module.structs.push_back({instruction.args[0], {}});
  }
  for (const auto &fn : module.functions) (void)analyze_fault_body(fn, module, metadata != nullptr);
}

void validate_nil_fault_contract(const std::vector<Instruction> &program) {
  validate_fault_program(program, program.empty() ? nullptr : program.front().module_metadata.get());
}

static void attach_class_metadata(std::vector<Instruction> &program, nil_module metadata) {
  validate_nil_parallel_contract(program);
  validate_nil_owned_contract(program);
  nil_function *function = nullptr;
  for (const auto &instruction : program) {
    if (instruction.is_function_entry) {
      metadata.functions.push_back({});
      function = &metadata.functions.back();
      function->name = instruction.args.at(0);
      function->return_type = instruction.function_return_type;
      function->annotations = instruction.function_annotations;
      for (std::size_t i = 0; i < instruction.function_param_count; ++i)
        function->params.push_back("arg" + std::to_string(i) + ":" +
            (i < instruction.function_parameter_types.size() ? instruction.function_parameter_types[i] : "any"));
    } else if (instruction.is_function_end) function = nullptr;
    else if (function) {
      const auto ordinal = static_cast<std::size_t>(instruction.op);
      if (ordinal >= opcode_table_size) throw std::runtime_error("Invalid opcode in class program");
      function->body.push_back({std::string(opcode_table[ordinal].name), instruction.args, instruction.line});
    } else if (instruction.op == Opcode::CLASS_UPCAST || instruction.op == Opcode::CALL_METHOD)
      throw std::runtime_error("Class instruction outside a declared function");
  }
  validate_nil_class_contract(metadata);
  // Tuple arrays also use exact record schemas at execution time, including
  // modules that declare no classes at all.
  if (!program.empty() && (!metadata.classes.empty() || !metadata.structs.empty() ||
                           !metadata.enums.empty()))
    program.front().module_metadata = std::make_shared<const nil_module>(std::move(metadata));
}

void validate_nil_class_contract(const std::vector<Instruction> &program) {
  if (program.empty()) return;
  const auto metadata = program.front().module_metadata;
  if (!metadata) {
    for (const auto &instruction : program)
      if (instruction.op == Opcode::CLASS_UPCAST || instruction.op == Opcode::CALL_METHOD)
        throw std::runtime_error("NIL class instructions require explicit module metadata");
      else {
        for (const auto &argument : instruction.args)
          if (contains_binding_cell_type(argument))
            throw std::runtime_error("NIL binding-reference instructions require explicit module metadata");
        for (const auto &type : instruction.function_parameter_types)
          if (contains_binding_cell_type(type))
            throw std::runtime_error("NIL binding-reference parameters require explicit module metadata");
      }
    return;
  }
  auto declarations = *metadata;
  declarations.functions.clear();
  auto checked = program;
  attach_class_metadata(checked, std::move(declarations));
}

static std::string unescape_string(const std::string &raw) {
  std::string out;
  out.reserve(raw.size());
  for (size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] == '\\' && i + 1 < raw.size()) {
      char c = raw[i+1];
      switch (c) {
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'v': out += '\v'; break;
        case 'f': out += '\f'; break;
        case '\\': out += '\\'; break;
        case '"': out += '"'; break;
        case '0': out += '\0'; break;
        default: out += '\\'; out += c; break;
      }
      i++;
    } else {
      out += raw[i];
    }
  }
  return out;
}

namespace {

std::vector<std::string>
parse_textual_operands(const std::string &line, std::size_t position,
                       std::size_t input_line, std::string &comment) {
  std::vector<std::string> operands;

  const auto skip_whitespace = [&]() {
    while (position < line.size() &&
           (line[position] == ' ' || line[position] == '\t')) {
      ++position;
    }
  };

  skip_whitespace();
  while (position < line.size()) {
    if (line[position] == ';') {
      comment = line.substr(position + 1);
      break;
    }

    std::string operand;
    if (line[position] == '"') {
      ++position;
      std::string escaped;
      bool closed = false;
      while (position < line.size()) {
        const char character = line[position++];
        if (character == '"') {
          closed = true;
          break;
        }
        if (character == '\\' && position < line.size()) {
          escaped.push_back(character);
          escaped.push_back(line[position++]);
          continue;
        }
        escaped.push_back(character);
      }
      if (!closed) {
        throw std::runtime_error(
            "Unterminated quoted operand at textual NIL line " +
            std::to_string(input_line));
      }
      operand = unescape_string(escaped);

      if (position < line.size() && line[position] != ' ' &&
          line[position] != '\t' && line[position] != ',' &&
          line[position] != ';') {
        throw std::runtime_error(
            "Unexpected character after quoted operand at textual NIL line " +
            std::to_string(input_line));
      }
      if (position < line.size() && line[position] == ',')
        ++position;
    } else {
      const std::size_t begin = position;
      while (position < line.size() && line[position] != ' ' &&
             line[position] != '\t' && line[position] != ';') {
        ++position;
      }
      operand = line.substr(begin, position - begin);
      if (!operand.empty() && operand.back() == ',')
        operand.pop_back();
    }

    operands.push_back(std::move(operand));
    skip_whitespace();
  }

  return operands;
}

int parse_textual_debug_line(const std::string &comment,
                             std::size_t input_line) {
  std::istringstream metadata(comment);
  std::string marker;
  if (!(metadata >> marker) || marker != "line")
    return 0;

  std::string value;
  if (!(metadata >> value) || !is_nonnegative_count(value, INT_MAX)) {
    throw std::runtime_error("Invalid debug line at textual NIL line " +
                             std::to_string(input_line));
  }
  return static_cast<int>(std::stoul(value));
}

std::string trim_textual_field(std::string value) {
  const auto first = value.find_first_not_of(" \t\r");
  if (first == std::string::npos)
    return {};
  const auto last = value.find_last_not_of(" \t\r");
  return value.substr(first, last - first + 1);
}

// Backward-compatible reserved comments carry function metadata without
// changing function headers or claiming a source-file mapping. Both textual
// readers enforce the same bounded placement rules.
class textual_source_debug {
public:
  bool comment(const std::string &line, std::size_t input_line,
               bool in_function, std::size_t instruction_index) {
    const auto contents = trim_textual_field(line.substr(1));
    std::istringstream fields(contents);
    std::string marker;
    fields >> marker;
    if (marker != "source_debug") return false;
    if (!in_function || pending_) fail(input_line);
    std::string trailing;
    const auto values = parse_textual_operands(contents, marker.size(), input_line, trailing);
    const auto count_value = [](const std::string &value, std::uint64_t maximum) {
      return !value.empty() && std::all_of(value.begin(), value.end(),
          [](unsigned char c) { return c >= '0' && c <= '9'; }) &&
          is_nonnegative_count(value, maximum);
    };
    if (!trailing.empty() || values.size() < 3 ||
        !count_value(values[0], max_instruction_count) ||
        std::stoull(values[0]) != instruction_index ||
        !count_value(values[2], 1024)) fail(input_line);
    const auto count = std::stoull(values[2]);
    if (values.size() != 3 + count * 4 || values[1].empty()) fail(input_line);
    nil_instruction metadata;
    metadata.source_file = values[1];
    for (std::size_t i = 0; i < count; ++i) {
      const auto offset = 3 + i * 4;
      if (!count_value(values[offset + 3], 1024)) fail(input_line);
      metadata.debug_locals.push_back({values[offset], values[offset + 1], values[offset + 2],
          static_cast<int>(std::stoul(values[offset + 3]))});
    }
    validate_source_debug(metadata.source_file, metadata.debug_locals);
    pending_ = std::move(metadata);
    return true;
  }
  void record(const std::string &line, std::size_t input_line) const {
    if (pending_ && (line.rfind("end ", 0) == 0 || line.rfind("function ", 0) == 0 ||
        line.rfind("module ", 0) == 0 || line.rfind("const ", 0) == 0 ||
        line.rfind("struct ", 0) == 0 || line.rfind("enum ", 0) == 0 || line.front() == '@'))
      fail(input_line);
  }
  template <typename T> void apply(T &instruction) {
    if (!pending_) return;
    instruction.source_file = std::move(pending_->source_file);
    instruction.debug_locals = std::move(pending_->debug_locals);
    pending_.reset();
  }
  void finish(std::size_t line) const { if (pending_) fail(line); }
private:
  [[noreturn]] static void fail(std::size_t line) {
    throw std::runtime_error("Invalid or orphan source debug record at textual NIL line " + std::to_string(line));
  }
  std::optional<nil_instruction> pending_;
};

class textual_declaration_line {
public:
  std::optional<std::string> comment(const std::string &line,
                                     std::size_t input_line,
                                     bool at_module_scope) {
    const std::string contents = trim_textual_field(line.substr(1));
    std::istringstream fields(contents);
    std::string marker;
    if (!(fields >> marker))
      return std::nullopt;
    if (marker == "function_annotation" || marker == "declaration_annotation") {
      if (!at_module_scope)
        fail("Misplaced function annotation", input_line);
      if (pending_)
        fail("Orphan declaration line", input_line);
      const auto split = contents.find_first_of(" \t\r");
      const std::string encoded = split == std::string::npos
          ? std::string{} : trim_textual_field(contents.substr(split));
      if (encoded.size() < 2 || encoded.front() != '"' || encoded.back() != '"')
        fail("Invalid quoted function annotation", input_line);
      std::string trailing;
      const auto operands = parse_textual_operands(encoded, 0, input_line, trailing);
      if (operands.size() != 1 || !trailing.empty())
        fail("Invalid quoted function annotation", input_line);
      annotation(operands.front(), input_line);
      if (marker == "function_annotation") quoted_annotation_pending_ = true;
      else quoted_declaration_annotation_pending_ = true;
      return operands.front();
    }
    if (marker != "declaration_line")
      return std::nullopt;
    if (!at_module_scope)
      fail("Misplaced declaration line", input_line);
    if (pending_)
      fail("Duplicate declaration line", input_line);
    std::string number;
    std::string trailing;
    if (!(fields >> number) ||
        !std::all_of(number.begin(), number.end(),
                     [](char value) { return value >= '0' && value <= '9'; }) ||
        !is_nonnegative_count(number, 2147483647ULL) || (fields >> trailing)) {
      fail("Invalid declaration line", input_line);
    }
    pending_ = static_cast<int>(std::stoul(number));
    return std::nullopt;
  }

  void annotation(const std::string &value, std::size_t input_line) {
    if (value.empty() || value.front() != '@' ||
        value.find('\0') != std::string::npos ||
        value.size() > max_binary_string_size)
      fail("Invalid function annotation", input_line);
    if (++annotation_count_ > max_parameter_count)
      fail("Function annotation count exceeds safety limit", input_line);
  }

  void record(const std::string &line, std::size_t input_line) {
    if (pending_ && line.rfind("function ", 0) != 0)
      fail("Orphan declaration line", input_line);
    if (quoted_annotation_pending_ && line.rfind("function ", 0) != 0 &&
        (line.front() != '@' || line.rfind("@test ", 0) == 0))
      fail("Orphan function annotation", input_line);
    const bool type_header = line.rfind("struct ", 0) == 0 || line.rfind("enum ", 0) == 0 ||
                             line.rfind("class ", 0) == 0;
    if (quoted_declaration_annotation_pending_ && !type_header && line.rfind("function ", 0) != 0 &&
        (line.front() != '@' || line.rfind("@test ", 0) == 0))
      fail("Orphan declaration annotation", input_line);
    if (type_header) {
      quoted_declaration_annotation_pending_ = false;
      annotation_count_ = 0;
    }
  }

  int consume() {
    const int result = pending_.value_or(0);
    pending_.reset();
    annotation_count_ = 0;
    quoted_annotation_pending_ = false;
    quoted_declaration_annotation_pending_ = false;
    return result;
  }

  void finish(std::size_t input_line) const {
    if (pending_)
      fail("Orphan declaration line", input_line);
    if (quoted_annotation_pending_)
      fail("Orphan function annotation", input_line);
    if (quoted_declaration_annotation_pending_)
      fail("Orphan declaration annotation", input_line);
  }

private:
  [[noreturn]] static void fail(const char *message, std::size_t input_line) {
    throw std::runtime_error(std::string(message) +
                             " at textual NIL line " +
                             std::to_string(input_line));
  }
  std::optional<int> pending_;
  std::size_t annotation_count_ = 0;
  bool quoted_annotation_pending_ = false;
  bool quoted_declaration_annotation_pending_ = false;
};

bool is_serialized_identifier(const std::string &value) {
  if (value.empty())
    return false;
  const auto identifier_start = [](unsigned char character) {
    return std::isalpha(character) != 0 || character == '_';
  };
  const auto identifier_continue = [&](unsigned char character) {
    return identifier_start(character) || std::isdigit(character) != 0;
  };
  if (!identifier_start(static_cast<unsigned char>(value.front())))
    return false;
  return std::all_of(value.begin() + 1, value.end(), [&](unsigned char character) {
    return identifier_continue(character);
  });
}

void validate_serialized_type_spelling(const std::string &type,
                                       const std::string &subject) {
  std::vector<char> delimiters;
  for (std::size_t index = 0; index < type.size(); ++index) {
    const char character = type[index];
    if (character == '(' || character == '<' || character == '[') {
      delimiters.push_back(character);
      continue;
    }
    if (character != ')' && character != '>' && character != ']')
      continue;
    if (character == '>' && index > 0 && type[index - 1] == '-')
      continue;
    const char expected = character == ')' ? '(' : character == '>' ? '<' : '[';
    if (delimiters.empty() || delimiters.back() != expected) {
      throw std::runtime_error("Malformed " + subject +
                               ": unbalanced type delimiter '" + character +
                               "'");
    }
    delimiters.pop_back();
  }
  if (!delimiters.empty()) {
    throw std::runtime_error("Malformed " + subject +
                             ": unterminated type delimiter");
  }
  if (type.size() >= 2U && type.compare(type.size() - 2U, 2U, "->") == 0) {
    throw std::runtime_error("Malformed " + subject +
                             ": function type has an empty return type");
  }
}

void validate_serialized_function_signature(const nil_function &function,
                                            const std::string &context) {
  std::unordered_set<std::string> parameter_names;
  for (std::size_t index = 0; index < function.params.size(); ++index) {
    const std::string &serialized = function.params[index];
    const std::size_t colon = serialized.find(':');
    const std::string subject =
        "parameter " + std::to_string(index + 1) + " in " + context;
    if (colon == std::string::npos) {
      throw std::runtime_error("Malformed " + subject +
                               ": expected 'name: exact_type'");
    }

    const std::string name =
        trim_textual_field(serialized.substr(0, colon));
    const std::string type =
        trim_textual_field(serialized.substr(colon + 1));
    if (name.empty()) {
      throw std::runtime_error("Malformed " + subject +
                               ": parameter name before ':' is empty");
    }
    if (!is_serialized_identifier(name)) {
      throw std::runtime_error("Malformed " + subject + ": '" + name +
                               "' is not a valid identifier");
    }
    if (type.empty()) {
      throw std::runtime_error("Malformed " + subject +
                               ": exact type after ':' is empty");
    }
    std::string normalized_type = type;
    normalized_type.erase(
        std::remove_if(normalized_type.begin(), normalized_type.end(),
                       [](unsigned char character) {
                         return std::isspace(character) != 0;
                       }),
        normalized_type.end());
    std::transform(normalized_type.begin(), normalized_type.end(),
                   normalized_type.begin(), [](unsigned char character) {
                     return static_cast<char>(std::tolower(character));
                   });
    if (normalized_type == "void") {
      throw std::runtime_error("Malformed " + subject +
                               ": parameter type 'void' has no native ABI "
                               "value representation");
    }
    validate_serialized_type_spelling(type, subject + " exact type");
    if (!parameter_names.insert(name).second) {
      throw std::runtime_error("Malformed " + context +
                               ": duplicate parameter name '" + name + "'");
    }
  }

  if (!function.generic_params.empty()) {
    throw std::runtime_error(
        "Cannot native-assemble " + context +
        ": unresolved serialized generic parameters require a concrete "
        "specialization");
  }
  if (!function.return_type.empty()) {
    validate_serialized_type_spelling(function.return_type,
                                      context + " return type");
  }
}

std::vector<std::string> split_textual_parameters(const std::string &parameters,
                                                  std::size_t input_line) {
  std::vector<std::string> result;
  std::size_t start = 0;
  std::size_t parens = 0;
  std::size_t angles = 0;
  std::size_t brackets = 0;
  const auto append_parameter = [&](std::size_t end) {
    std::string parameter =
        trim_textual_field(parameters.substr(start, end - start));
    if (parameter.empty()) {
      throw std::runtime_error("Empty function parameter at textual NIL line " +
                               std::to_string(input_line));
    }
    result.push_back(std::move(parameter));
    if (result.size() > max_parameter_count) {
      throw std::runtime_error(
          "Textual NIL parameter count exceeds safety limit at line " +
          std::to_string(input_line));
    }
  };

  for (std::size_t i = 0; i < parameters.size(); ++i) {
    const char c = parameters[i];
    if (c == '(') {
      ++parens;
    } else if (c == ')') {
      if (parens == 0) {
        throw std::runtime_error(
            "Unbalanced nested parameter type at textual NIL line " +
            std::to_string(input_line));
      }
      --parens;
    } else if (c == '<') {
      ++angles;
    } else if (c == '>' && i > 0 && parameters[i - 1] == '-') {
      // Function types use `->`; this greater-than sign is not a generic
      // delimiter.
    } else if (c == '>') {
      if (angles == 0) {
        throw std::runtime_error(
            "Unbalanced generic parameter type at textual NIL line " +
            std::to_string(input_line));
      }
      --angles;
    } else if (c == '[') {
      ++brackets;
    } else if (c == ']') {
      if (brackets == 0) {
        throw std::runtime_error(
            "Unbalanced array parameter type at textual NIL line " +
            std::to_string(input_line));
      }
      --brackets;
    }

    if (c != ',' || parens != 0 || angles != 0 || brackets != 0)
      continue;
    append_parameter(i);
    start = i + 1;
  }

  if (parens != 0 || angles != 0 || brackets != 0) {
    throw std::runtime_error(
        "Unterminated nested parameter type at textual NIL line " +
        std::to_string(input_line));
  }
  if (!parameters.empty())
    append_parameter(parameters.size());
  return result;
}

struct textual_function_header {
  std::string name;
  std::vector<std::string> parameters;
  std::string return_type;
};

textual_function_header parse_textual_function_header(const std::string &line,
                                                      std::size_t input_line) {
  std::string header = trim_textual_field(line.substr(9));
  if (header.empty() || header.back() != ':') {
    throw std::runtime_error("Malformed function header at textual NIL line " +
                             std::to_string(input_line));
  }
  header.pop_back();
  header = trim_textual_field(std::move(header));

  textual_function_header result;
  std::string tail;
  const auto arrow = header.find("->");
  const auto open = header.find('(');
  // Parentheses following the signature's return arrow belong to the return
  // type (for example `fn(i32) -> i32` or a tuple spelling), not to a function
  // parameter list.  A parameter list is present only when its opening
  // parenthesis precedes the signature arrow.
  const bool has_parameter_list =
      open != std::string::npos &&
      (arrow == std::string::npos || open < arrow);
  if (!has_parameter_list) {
    result.name = trim_textual_field(header.substr(0, arrow));
    if (arrow != std::string::npos)
      tail = trim_textual_field(header.substr(arrow));
  } else {
    result.name = trim_textual_field(header.substr(0, open));
    std::size_t depth = 0;
    std::size_t close = std::string::npos;
    for (std::size_t i = open; i < header.size(); ++i) {
      if (header[i] == '(') {
        ++depth;
      } else if (header[i] == ')') {
        if (depth == 0) {
          throw std::runtime_error(
              "Unbalanced function parameter list at textual NIL line " +
              std::to_string(input_line));
        }
        --depth;
        if (depth == 0) {
          close = i;
          break;
        }
      }
    }
    if (close == std::string::npos) {
      throw std::runtime_error(
          "Unterminated function parameter list at textual NIL line " +
          std::to_string(input_line));
    }
    result.parameters = split_textual_parameters(
        header.substr(open + 1, close - open - 1), input_line);
    tail = trim_textual_field(header.substr(close + 1));
  }

  if (result.name.empty()) {
    throw std::runtime_error("Empty function name at textual NIL line " +
                             std::to_string(input_line));
  }
  if (!tail.empty()) {
    if (tail.rfind("->", 0) != 0) {
      throw std::runtime_error(
          "Unexpected function header suffix at textual NIL line " +
          std::to_string(input_line));
    }
    result.return_type = trim_textual_field(tail.substr(2));
    if (result.return_type.empty()) {
      throw std::runtime_error(
          "Empty function return type at textual NIL line " +
          std::to_string(input_line));
    }
  }
  return result;
}

bool parse_class_record(const std::string &line, std::size_t input_line,
                        nil_module &module, nil_class_def *&klass,
                        std::vector<std::string> *pending_annotations) {
  if (!klass && line.rfind("class ", 0) != 0) return false;
  if (line == "end class") { klass = nullptr; return true; }
  const auto split = line.find_first_of(" \t");
  const auto record = line.substr(0, split);
  std::string comment;
  const auto args = parse_textual_operands(line, split == std::string::npos ? line.size() : split,
                                          input_line, comment);
  const auto fail = [&]() { throw std::runtime_error("Malformed class metadata at textual NIL line " + std::to_string(input_line)); };
  if (!klass) {
    if (record != "class" || args.size() != 2 || (args[1] != "0" && args[1] != "1")) fail();
    if (module.classes.size() >= max_function_count) fail();
    // A class keeps whatever was written above it, as a record does.
    module.classes.push_back({args[0], args[1] == "1", {}, {},
                              pending_annotations ? std::move(*pending_annotations)
                                                  : std::vector<std::string>{}});
    if (pending_annotations) pending_annotations->clear();
    klass = &module.classes.back();
  } else if (record == "base") {
    if (args.size() != 1 || klass->bases.size() >= max_parameter_count) fail();
    klass->bases.push_back(args[0]);
  } else if (record == "method") {
    if (args.size() < 7 || (args[4] != "0" && args[4] != "1") ||
        (args[5] != "0" && args[5] != "1") || !is_nonnegative_count(args[6], max_parameter_count) ||
        std::stoull(args[6]) != args.size() - 7 || klass->methods.size() >= max_parameter_count) fail();
    nil_class_method method;
    method.name = args[0]; method.implementation = args[1] == "-" ? "" : args[1];
    method.receiver_type = args[2]; method.return_type = args[3];
    method.is_virtual = args[4] == "1"; method.is_abstract = args[5] == "1";
    method.parameter_types.assign(args.begin() + 7, args.end());
    klass->methods.push_back(std::move(method));
  } else fail();
  return true;
}

} // namespace

std::unique_ptr<nil_module> parseTextNilModule(std::istream &is) {
  auto module = std::make_unique<nil_module>();
  std::string line;
  std::size_t input_line = 0;
  std::size_t parsed_constants = 0;
  std::size_t parsed_functions = 0;
  std::size_t parsed_instructions = 0;
  nil_function *function = nullptr;
  nil_struct_def *structure = nullptr;
  nil_enum_def *enumeration = nullptr;
  nil_class_def *klass = nullptr;
  std::vector<std::string> pending_annotations;
  textual_declaration_line declaration_line;
  textual_source_debug source_debug;

  while (std::getline(is, line)) {
    ++input_line;
    if (line.size() > max_textual_line_size) {
      throw std::runtime_error("Textual NIL line exceeds the 16 MiB safety "
                               "limit at line " +
                               std::to_string(input_line));
    }
    line = trim_textual_field(std::move(line));
    if (line.empty())
      continue;
    if (line[0] == '#') {
      if (source_debug.comment(line, input_line, function != nullptr,
                               function ? function->body.size() : 0)) continue;
      const auto annotation = declaration_line.comment(
          line, input_line, !function && !structure && !enumeration && !klass);
      if (annotation)
        pending_annotations.push_back(*annotation);
      continue;
    }
    declaration_line.record(line, input_line);
    source_debug.record(line, input_line);

    if (function != nullptr) {
      if (line == "end function") {
        function = nullptr;
        continue;
      }
      if (line.rfind("function ", 0) == 0) {
        throw std::runtime_error("Nested function at textual NIL line " +
                                 std::to_string(input_line));
      }
      if (++parsed_instructions > max_instruction_count) {
        throw std::runtime_error(
            "Textual NIL instruction count exceeds safety limit");
      }
      const std::size_t opname_end = line.find_first_of(" \t;");
      const std::string opname = line.substr(0, opname_end);
      std::string comment;
      const std::vector<std::string> args = parse_textual_operands(
          line, opname_end == std::string::npos ? line.size() : opname_end,
          input_line, comment);
      const std::string context =
          " at textual NIL line " + std::to_string(input_line);
      const Opcode opcode = decode_opcode(opname, context);
      validate_instruction(opcode, opname, args, context);
      function->body.push_back(
          {opname, args, parse_textual_debug_line(comment, input_line)});
      source_debug.apply(function->body.back());
      continue;
    }

    if (structure != nullptr) {
      if (line == "end struct") {
        structure = nullptr;
        continue;
      }
      const auto colon = line.find(':');
      if (colon == std::string::npos) {
        throw std::runtime_error("Malformed struct field at textual NIL line " +
                                 std::to_string(input_line));
      }
      std::string name = trim_textual_field(line.substr(0, colon));
      std::string type = trim_textual_field(line.substr(colon + 1));
      if (name.empty() || type.empty()) {
        throw std::runtime_error("Malformed struct field at textual NIL line " +
                                 std::to_string(input_line));
      }
      structure->fields.emplace_back(std::move(name), std::move(type));
      if (structure->fields.size() > max_parameter_count) {
        throw std::runtime_error(
            "Textual NIL struct field count exceeds safety limit");
      }
      continue;
    }

    if (enumeration != nullptr) {
      if (line == "end enum") {
        enumeration = nullptr;
        continue;
      }
      const auto colon = line.find(':');
      nil_enum_def::variant variant;
      variant.name = trim_textual_field(line.substr(0, colon));
      if (colon != std::string::npos)
        variant.associated_type = trim_textual_field(line.substr(colon + 1));
      if (variant.name.empty()) {
        throw std::runtime_error("Malformed enum variant at textual NIL line " +
                                 std::to_string(input_line));
      }
      enumeration->variants.push_back(std::move(variant));
      if (enumeration->variants.size() > max_parameter_count) {
        throw std::runtime_error(
            "Textual NIL enum variant count exceeds safety limit");
      }
      continue;
    }

    if (parse_class_record(line, input_line, *module, klass, &pending_annotations)) continue;
    if (line.rfind("module ", 0) == 0) {
      if (!module->module_name.empty()) {
        throw std::runtime_error("Duplicate module declaration at textual NIL "
                                 "line " +
                                 std::to_string(input_line));
      }
      module->module_name = trim_textual_field(line.substr(7));
      if (module->module_name.empty()) {
        throw std::runtime_error("Empty module name at textual NIL line " +
                                 std::to_string(input_line));
      }
      continue;
    }
    if (line.rfind("const ", 0) == 0) {
      if (++parsed_constants > max_constant_count) {
        throw std::runtime_error(
            "Textual NIL constant count exceeds safety limit");
      }
      const auto equals = line.find('=');
      if (equals == std::string::npos || equals <= 6) {
        throw std::runtime_error("Malformed constant at textual NIL line " +
                                 std::to_string(input_line));
      }
      std::string name = trim_textual_field(line.substr(6, equals - 6));
      std::string value = trim_textual_field(line.substr(equals + 1));
      if (name.empty() || value.size() < 2 || value.front() != '"' ||
          value.back() != '"') {
        throw std::runtime_error("Malformed constant at textual NIL line " +
                                 std::to_string(input_line));
      }
      module->string_constants.emplace_back(
          std::move(name), unescape_string(value.substr(1, value.size() - 2)));
      continue;
    }
    if (line.rfind("struct ", 0) == 0) {
      // A record carries its own annotations now. Under NIL-0093 an artifact
      // states what its declarations are for, and a statement the loader threw
      // away would be a statement the manifest could not read back.
      module->structs.push_back({trim_textual_field(line.substr(7)), {},
                                 std::move(pending_annotations)});
      pending_annotations.clear();
      if (module->structs.back().name.empty()) {
        throw std::runtime_error("Empty struct name at textual NIL line " +
                                 std::to_string(input_line));
      }
      structure = &module->structs.back();
      continue;
    }
    if (line.rfind("enum ", 0) == 0) {
      // A choice states what it is for, as a record and a class do.
      module->enums.push_back({trim_textual_field(line.substr(5)), {},
                               std::move(pending_annotations)});
      pending_annotations.clear();
      if (module->enums.back().name.empty()) {
        throw std::runtime_error("Empty enum name at textual NIL line " +
                                 std::to_string(input_line));
      }
      enumeration = &module->enums.back();
      continue;
    }
    if (line.rfind("@test ", 0) == 0) {
      std::string test = trim_textual_field(line.substr(6));
      if (test.empty()) {
        throw std::runtime_error(
            "Empty test registry entry at textual NIL line " +
            std::to_string(input_line));
      }
      module->tests.push_back(std::move(test));
      continue;
    }
    if (line[0] == '@') {
      declaration_line.annotation(line, input_line);
      pending_annotations.push_back(line);
      continue;
    }
    if (line.rfind("function ", 0) == 0) {
      if (++parsed_functions > max_function_count) {
        throw std::runtime_error(
            "Textual NIL function count exceeds safety limit");
      }
      const auto header = parse_textual_function_header(line, input_line);
      nil_function parsed;
      parsed.name = header.name;
      parsed.params = header.parameters;
      parsed.return_type = header.return_type;
      parsed.source_line = declaration_line.consume();
      parsed.annotations = std::move(pending_annotations);
      pending_annotations.clear();
      for (const auto &annotation : parsed.annotations) {
        if (annotation_name_matches(annotation, "inline"))
          parsed.is_inline = true;
        if (annotation_name_matches(annotation, "test"))
          parsed.is_test = true;
      }
      validate_serialized_function_signature(
          parsed, "textual function '" + parsed.name + "' at line " +
                      std::to_string(input_line));
      module->functions.push_back(std::move(parsed));
      function = &module->functions.back();
      continue;
    }
    if (line == "end function" || line == "end struct" || line == "end enum") {
      throw std::runtime_error("Unexpected '" + line +
                               "' at textual NIL line " +
                               std::to_string(input_line));
    }
    throw std::runtime_error("Unexpected textual NIL record '" + line +
                             "' at line " + std::to_string(input_line));
  }

  declaration_line.finish(input_line);
  source_debug.finish(input_line);
  if (function != nullptr)
    throw std::runtime_error("Unterminated function in textual NIL");
  if (structure != nullptr)
    throw std::runtime_error("Unterminated struct in textual NIL");
  if (enumeration != nullptr)
    throw std::runtime_error("Unterminated enum in textual NIL");
  if (klass) throw std::runtime_error("Unterminated class in textual NIL");
  if (!pending_annotations.empty())
    throw std::runtime_error(
        "Function annotations do not precede a function in textual NIL");
  validate_global_storage_contract(*module);
  validate_nil_fault_contract(*module);
  validate_nil_class_contract(*module);
  validate_nil_parallel_contract(*module);
  validate_nil_owned_contract(*module);
  return module;
}

std::vector<Instruction>
parseProgram(std::istream &is,
             std::unordered_map<std::string, Value> &constants) {
  std::vector<Instruction> program;
  std::string line;
  // Counter for generating unique skip-labels around function bodies.
  // Each function body is wrapped with an internal JMP/LABEL pair
  // so that sequential module-level execution (execute(true) with no 'main')
  // skips over function bodies instead of falling through into their preambles.
  std::size_t fn_skip_counter = 0;
  std::size_t current_function_index = 0;
  std::size_t input_line = 0;
  std::size_t parsed_functions = 0;
  std::size_t parsed_constants = 0;
  std::size_t parsed_instructions = 0;
  bool in_function = false;
  bool in_struct_definition = false;
  bool in_enum_definition = false;
  nil_module fault_metadata;
  nil_class_def *klass = nullptr;
  textual_declaration_line declaration_line;
  textual_source_debug source_debug;
  std::size_t function_instruction_index = 0;
  std::vector<std::string> pending_annotations;
  while (std::getline(is, line)) {
    ++input_line;
    if (line.size() > max_textual_line_size) {
      throw std::runtime_error("Textual NIL line exceeds the 16 MiB safety "
                               "limit at line " +
                               std::to_string(input_line));
    }
    // Trim leading spaces
    size_t start = line.find_first_not_of(" \t");
    if (start != std::string::npos) {
      line = line.substr(start);
    } else {
      continue; // empty line
    }
    while (!line.empty() &&
           (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) {
      line.pop_back();
    }
    if (line.empty())
      continue;
    if (line[0] == '#') {
      if (source_debug.comment(line, input_line, in_function, function_instruction_index)) continue;
      const auto annotation = declaration_line.comment(line, input_line,
                               !in_function && !in_struct_definition &&
                                   !in_enum_definition && !klass);
      if (annotation) pending_annotations.push_back(*annotation);
      continue;
    }
    declaration_line.record(line, input_line);
    source_debug.record(line, input_line);
    if (!in_function && !in_struct_definition && !in_enum_definition &&
        parse_class_record(line, input_line, fault_metadata, klass, &pending_annotations)) continue;
    if (line.find("const ") == 0) {
      if (++parsed_constants > max_constant_count) {
        throw std::runtime_error(
            "Textual NIL constant count exceeds safety limit");
      }
      // const S1 = "value"
      size_t eq = line.find('=');
      if (eq == std::string::npos || eq <= 6) {
        throw std::runtime_error("Malformed constant at textual NIL line " +
                                 std::to_string(input_line));
      }
      std::string name = line.substr(6, eq - 6);
      const auto name_start = name.find_first_not_of(" \t");
      const auto name_end = name.find_last_not_of(" \t");
      if (name_start == std::string::npos) {
        throw std::runtime_error("Empty constant name at textual NIL line " +
                                 std::to_string(input_line));
      }
      name = name.substr(name_start, name_end - name_start + 1);
      std::string val_str = line.substr(eq + 1);
      // Trim leading whitespace
      size_t val_start = val_str.find_first_not_of(" \t");
      if (val_start != std::string::npos) {
        val_str = val_str.substr(val_start);
      }
      // Trim trailing whitespace and carriage returns
      while (!val_str.empty() &&
             (val_str.back() == ' ' || val_str.back() == '\t' ||
              val_str.back() == '\r')) {
        val_str.pop_back();
      }
      // remove quotes
      if (val_str.size() >= 2 && val_str[0] == '"' && val_str.back() == '"') {
        val_str = unescape_string(val_str.substr(1, val_str.size() - 2));
      }
      constants[name] = val_str;
      continue;
    }
    if (line.find("module ") == 0)
      continue;
    if (line.find("struct ") == 0) {
      if (in_struct_definition) {
        throw std::runtime_error("Nested struct at textual NIL line " +
                                 std::to_string(input_line));
      }
      in_struct_definition = true;
      fault_metadata.structs.push_back({trim_textual_field(line.substr(7)), {},
                                       std::move(pending_annotations)});
      pending_annotations.clear();
      continue;
    }
    if (line == "end struct") {
      if (!in_struct_definition) {
        throw std::runtime_error(
            "Unexpected 'end struct' at textual NIL line " +
            std::to_string(input_line));
      }
      in_struct_definition = false;
      continue;
    }
    if (line.find("enum ") == 0) {
      if (in_enum_definition) {
        throw std::runtime_error("Nested enum at textual NIL line " +
                                 std::to_string(input_line));
      }
      in_enum_definition = true;
      fault_metadata.enums.push_back({trim_textual_field(line.substr(5)), {},
                                     std::move(pending_annotations)});
      pending_annotations.clear();
      continue;
    }
    if (line == "end enum") {
      if (!in_enum_definition) {
        throw std::runtime_error("Unexpected 'end enum' at textual NIL line " +
                                 std::to_string(input_line));
      }
      in_enum_definition = false;
      continue;
    }
    if (in_struct_definition) {
      const auto colon = line.find(':');
      if (colon != std::string::npos)
        fault_metadata.structs.back().fields.emplace_back(
            trim_textual_field(line.substr(0, colon)), trim_textual_field(line.substr(colon + 1)));
      continue;
    }
    if (in_enum_definition) {
      const auto colon = line.find(':');
      nil_enum_def::variant variant;
      variant.name = trim_textual_field(line.substr(0, colon));
      if (colon != std::string::npos)
        variant.associated_type = trim_textual_field(line.substr(colon + 1));
      if (variant.name.empty())
        throw std::runtime_error("Malformed enum variant at textual NIL line " +
                                 std::to_string(input_line));
      auto &variants = fault_metadata.enums.back().variants;
      variants.push_back(std::move(variant));
      if (variants.size() > max_parameter_count)
        throw std::runtime_error("Textual NIL enum variant count exceeds safety limit");
      continue;
    }
    if (line[0] == '@') {
      if (!in_function && line.rfind("@test ", 0) != 0) {
        declaration_line.annotation(line, input_line);
        pending_annotations.push_back(line);
      }
      continue;
    }
    if (line == "end function") {
      if (!in_function) {
        throw std::runtime_error(
            "Unexpected 'end function' at textual NIL line " +
            std::to_string(input_line));
      }
      append_function_end(program, current_function_index);
      in_function = false;
      continue;
    }
    if (line.find("function ") == 0) {
      if (in_function) {
        throw std::runtime_error("Nested function at textual NIL line " +
                                 std::to_string(input_line));
      }
      if (++parsed_functions > max_function_count) {
        throw std::runtime_error(
            "Textual NIL function count exceeds safety limit");
      }
      const auto parsed_header =
          parse_textual_function_header(line, input_line);
      (void)declaration_line.consume();
      std::vector<std::string> param_names;
      for (const auto &parameter : parsed_header.parameters) {
        const auto colon = parameter.find(':');
        std::string name = trim_textual_field(parameter.substr(0, colon));
        if (!name.empty())
          param_names.push_back(std::move(name));
      }
      // Emit JMP to skip over the function body during sequential module init.
      // Without this, execute(true) with no 'main' falls through LABELs into
      // function preambles and causes stack underflow.
      //
      // NOTE: STORE_LOCAL preamble for param binding is emitted by the codegen
      // into the function body — nil_loader does NOT add it here to avoid
      // double-pop with the body's existing STORE_LOCAL instructions.
      current_function_index = fn_skip_counter++;
      append_function_begin(program, parsed_header.name, current_function_index,
                            param_names.size(), parsed_header.return_type);
      program.back().function_annotations = std::move(pending_annotations);
      pending_annotations.clear();
      for (const auto &parameter : parsed_header.parameters) {
        const auto colon = parameter.find(':');
        program.back().function_parameter_types.push_back(
            colon == std::string::npos ? "" : trim_textual_field(parameter.substr(colon + 1)));
      }
      in_function = true;
      function_instruction_index = 0;
      continue;
    }
    // instruction
    if (++parsed_instructions > max_instruction_count) {
      throw std::runtime_error(
          "Textual NIL instruction count exceeds safety limit");
    }
    const std::size_t opname_end = line.find_first_of(" \t;");
    const std::string opname = line.substr(0, opname_end);
    std::string comment;
    const std::vector<std::string> args = parse_textual_operands(
        line, opname_end == std::string::npos ? line.size() : opname_end,
        input_line, comment);
    const int line_num = parse_textual_debug_line(comment, input_line);
    Instruction instr;
    const std::string context =
        " at textual NIL line " + std::to_string(input_line);
    instr.op = decode_opcode(opname, context);
    validate_instruction(instr.op, opname, args, context);
    instr.args = args;
    instr.line = line_num;
    source_debug.apply(instr);
    program.push_back(instr);
    if (in_function) ++function_instruction_index;
  }
  declaration_line.finish(input_line);
  source_debug.finish(input_line);
  if (in_function) {
    throw std::runtime_error("Unterminated function in textual NIL");
  }
  if (in_struct_definition) {
    throw std::runtime_error("Unterminated struct in textual NIL");
  }
  if (in_enum_definition)
    throw std::runtime_error("Unterminated enum in textual NIL");
  if (klass) throw std::runtime_error("Unterminated class in textual NIL");
  validate_global_storage_contract(program);
  validate_fault_program(program, &fault_metadata);
  attach_class_metadata(program, std::move(fault_metadata));
  return program;
}

namespace {

std::vector<std::uint8_t> read_binary_bytes(std::istream &input) {
  constexpr std::size_t chunk_size = 64U * 1024U;
  std::array<char, chunk_size> chunk{};
  std::vector<std::uint8_t> bytes;
  bytes.reserve(chunk_size);

  while (true) {
    input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    const auto count = input.gcount();
    if (count < 0) {
      throw std::runtime_error("Failed to read binary NIL input");
    }
    const auto read_count = static_cast<std::size_t>(count);
    if (read_count > max_binary_nil_size - bytes.size()) {
      throw std::runtime_error("Binary NIL exceeds the 256 MiB safety limit");
    }
    bytes.insert(bytes.end(), chunk.data(), chunk.data() + read_count);

    if (input.eof())
      break;
    if (!input) {
      throw std::runtime_error("Failed to read complete binary NIL input");
    }
  }
  return bytes;
}

class binary_cursor {
public:
  binary_cursor(const std::vector<std::uint8_t> &bytes, std::size_t begin,
                std::size_t end)
      : bytes_(bytes), position_(begin), end_(end) {
    if (begin > end || end > bytes.size())
      throw std::runtime_error("Invalid binary NIL cursor bounds");
  }

  std::size_t remaining() const { return end_ - position_; }
  std::size_t position() const { return position_; }

  std::string bytes(std::size_t count, const std::string &field) {
    require(count, field);
    const auto *start = bytes_.data() + position_;
    position_ += count;
    return std::string(reinterpret_cast<const char *>(start), count);
  }

  std::uint32_t u32(const std::string &field) {
    require(4, field);
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
      value |= static_cast<std::uint32_t>(bytes_[position_ + i]) << (8 * i);
    position_ += 4;
    return value;
  }

  std::uint64_t u64(const std::string &field) {
    require(8, field);
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i)
      value |= static_cast<std::uint64_t>(bytes_[position_ + i]) << (8 * i);
    position_ += 8;
    return value;
  }

  std::string string(const std::string &field) {
    const auto length = u32(field + " length");
    if (length > max_binary_string_size) {
      throw std::runtime_error("Binary NIL " + field +
                               " exceeds the 16 MiB string limit");
    }
    return bytes(length, field);
  }

private:
  void require(std::size_t count, const std::string &field) const {
    if (count > remaining()) {
      throw std::runtime_error("Truncated binary NIL while reading " + field);
    }
  }

  const std::vector<std::uint8_t> &bytes_;
  std::size_t position_;
  std::size_t end_;
};

void validate_count(std::uint32_t count, std::uint32_t maximum,
                    const std::string &field) {
  if (count > maximum) {
    throw std::runtime_error("Binary NIL " + field + " exceeds safety limit");
  }
}

void require_section_consumed(const binary_cursor &section,
                              const std::string &name) {
  if (section.remaining() != 0) {
    throw std::runtime_error("Binary NIL " + name +
                             " section contains trailing data");
  }
}

std::string metadata_string(binary_cursor &section, const std::string &field,
                            bool allow_empty = false) {
  auto value = section.string(field);
  if ((!allow_empty && value.empty()) || value.find('\0') != std::string::npos)
    throw std::runtime_error("Binary NIL contains an invalid " + field);
  return value;
}

void read_executable_types(binary_cursor &section, nil_module &module,
                           bool with_annotations = false) {
  const auto type_annotation = [&](binary_cursor &from, const char *field) {
    auto annotation = metadata_string(from, field);
    if (annotation.front() != '@')
      throw std::runtime_error("Binary NIL type annotation must start with '@'");
    return annotation;
  };
  const auto structures = section.u32("struct count");
  validate_count(structures, max_function_count, "struct count");
  std::uint64_t total_members = 0;
  const auto member_count = [&](const std::string &field) {
    const auto count = section.u32(field);
    validate_count(count, max_parameter_count, field);
    total_members += count;
    if (total_members > max_instruction_count)
      throw std::runtime_error("Binary NIL total type member count exceeds safety limit");
    return count;
  };
  for (std::uint32_t i = 0; i < structures; ++i) {
    nil_struct_def structure;
    structure.name = metadata_string(section, "struct name");
    const auto fields = member_count("struct field count");
    for (std::uint32_t j = 0; j < fields; ++j) {
      auto name = metadata_string(section, "field name");
      auto type = metadata_string(section, "field type");
      structure.fields.emplace_back(std::move(name), std::move(type));
    }
    if (with_annotations) {
      const auto annotations = member_count("struct annotation count");
      for (std::uint32_t j = 0; j < annotations; ++j)
        structure.annotations.push_back(type_annotation(section, "struct annotation"));
    }
    module.structs.push_back(std::move(structure));
  }
  const auto enums = section.u32("enum count");
  validate_count(enums, max_function_count, "enum count");
  for (std::uint32_t i = 0; i < enums; ++i) {
    nil_enum_def enumeration;
    enumeration.name = metadata_string(section, "enum name");
    const auto variants = member_count("enum variant count");
    for (std::uint32_t j = 0; j < variants; ++j) {
      auto name = metadata_string(section, "variant name");
      auto payload = metadata_string(section, "variant payload type", true);
      enumeration.variants.push_back({std::move(name), std::move(payload)});
    }
    if (with_annotations) {
      const auto annotations = member_count("enum annotation count");
      for (std::uint32_t j = 0; j < annotations; ++j)
        enumeration.annotations.push_back(type_annotation(section, "enum annotation"));
    }
    module.enums.push_back(std::move(enumeration));
  }
}

void read_function_metadata(binary_cursor &section, nil_function &function) {
  const auto flags = section.u32("function flags");
  if ((flags & ~3U) != 0)
    throw std::runtime_error("Unsupported binary NIL function flags");
  function.is_inline = (flags & 1U) != 0;
  function.is_test = (flags & 2U) != 0;
  const auto line = section.u32("function source line");
  if (line > static_cast<std::uint32_t>(INT_MAX))
    throw std::runtime_error("Binary NIL function source line exceeds INT_MAX");
  function.source_line = static_cast<int>(line);
  const auto count = section.u32("annotation count");
  validate_count(count, max_parameter_count, "annotation count");
  for (std::uint32_t i = 0; i < count; ++i) {
    auto annotation = metadata_string(section, "function annotation");
    if (annotation.front() != '@')
      throw std::runtime_error("Binary NIL function annotation must start with '@'");
    function.annotations.push_back(std::move(annotation));
  }
}

void read_class_metadata(binary_cursor &section, nil_module &module, bool allow_empty = false,
                         bool with_annotations = false) {
  const auto count = section.u32("class count");
  validate_count(count, max_function_count, "class count");
  if (count == 0 && !allow_empty) throw std::runtime_error("Binary NIL v4 requires class metadata");
  std::uint64_t total_methods = 0;
  for (std::uint32_t i = 0; i < count; ++i) {
    nil_class_def klass;
    klass.name = metadata_string(section, "class name");
    const auto flags = section.u32("class flags");
    if (flags > 1) throw std::runtime_error("Unsupported binary NIL class flags");
    klass.is_abstract = flags != 0;
    if (with_annotations) {
      const auto annotations = section.u32("class annotation count");
      validate_count(annotations, max_parameter_count, "class annotation count");
      for (std::uint32_t j = 0; j < annotations; ++j) {
        auto annotation = metadata_string(section, "class annotation");
        if (annotation.front() != '@')
          throw std::runtime_error("Binary NIL type annotation must start with '@'");
        klass.annotations.push_back(std::move(annotation));
      }
    }
    const auto bases = section.u32("class base count");
    validate_count(bases, max_parameter_count, "class base count");
    for (std::uint32_t j = 0; j < bases; ++j) klass.bases.push_back(metadata_string(section, "class base"));
    const auto methods = section.u32("class method count");
    validate_count(methods, max_parameter_count, "class method count");
    total_methods += methods;
    if (total_methods > max_instruction_count) throw std::runtime_error("Binary NIL total class method count exceeds safety limit");
    for (std::uint32_t j = 0; j < methods; ++j) {
      nil_class_method method;
      method.name = metadata_string(section, "method name");
      method.implementation = metadata_string(section, "method implementation", true);
      method.receiver_type = metadata_string(section, "method receiver type");
      method.return_type = metadata_string(section, "method return type");
      const auto method_flags = section.u32("method flags");
      if ((method_flags & ~3U) != 0) throw std::runtime_error("Unsupported binary NIL method flags");
      method.is_virtual = (method_flags & 1U) != 0;
      method.is_abstract = (method_flags & 2U) != 0;
      const auto parameters = section.u32("method parameter count");
      validate_count(parameters, max_parameter_count, "method parameter count");
      for (std::uint32_t k = 0; k < parameters; ++k) method.parameter_types.push_back(metadata_string(section, "method parameter type"));
      klass.methods.push_back(std::move(method));
    }
    module.classes.push_back(std::move(klass));
  }
}

} // namespace

std::vector<Instruction>
parseBinaryProgram(std::istream &is,
                   std::unordered_map<std::string, Value> &constants) {
  const auto bytes = read_binary_bytes(is);
  if (bytes.size() < 4) {
    throw std::runtime_error("Truncated binary NIL header");
  }

  const bool current_magic = std::memcmp(bytes.data(), "NIL\0", 4) == 0;
  const bool legacy_magic = std::memcmp(bytes.data(), "MIL\0", 4) == 0 ||
                            std::memcmp(bytes.data(), "\0LIM", 4) == 0;
  if (!current_magic && !legacy_magic) {
    throw std::runtime_error("Invalid NIL binary magic");
  }

  // NIL 0.1 was historically serialized in little-endian host order. Both
  // legacy magic spellings identify that same payload encoding; the reversed
  // spelling is not an endianness flag.
  binary_cursor header(bytes, 4, bytes.size());
  const std::uint32_t version = header.u32("version");
  if (version == source_debug_binary_nil_version) {
    // Reuse the strict module reader for v5 and preserve per-instruction maps
    // while creating the VM's usual function-boundary wrappers.
    std::istringstream module_input(std::string(bytes.begin(), bytes.end()),
                                    std::ios::in | std::ios::binary);
    auto module = parseBinaryNilModule(module_input);
    std::vector<Instruction> program;
    for (const auto &constant : module->string_constants) constants[constant.first] = constant.second;
    for (std::size_t index = 0; index < module->functions.size(); ++index) {
      const auto &function = module->functions[index];
      append_function_begin(program, function.name, index, function.params.size(), function.return_type);
      program.back().function_annotations = function.annotations;
      for (const auto &parameter : function.params) {
        const auto colon = parameter.find(':');
        program.back().function_parameter_types.push_back(colon == std::string::npos ? "" : trim_textual_field(parameter.substr(colon + 1)));
      }
      for (const auto &instruction : function.body) {
        Instruction value;
        value.op = decode_opcode(instruction.opname, " in source-debug NIL");
        value.args = instruction.args;
        value.line = instruction.debug_line;
        value.source_file = instruction.source_file;
        value.debug_locals = instruction.debug_locals;
        program.push_back(std::move(value));
      }
      append_function_end(program, index);
    }
    validate_global_storage_contract(program);
    validate_fault_program(program, module.get());
    module->functions.clear();
    attach_class_metadata(program, std::move(*module));
    return program;
  }
  const std::uint32_t flags = header.u32("flags");
  const std::uint32_t section_count = header.u32("section count");
  if (version != executable_binary_nil_version &&
      version != legacy_executable_binary_nil_version &&
      version != library_binary_nil_version && version != class_binary_nil_version) {
    throw std::runtime_error("Unsupported binary NIL version " +
                             std::to_string(version));
  }
  // The type-annotation bit is orthogonal to the library bit, so it is masked
  // off before the historical per-version flag rules are applied. An unknown
  // bit still refuses.
  const bool annotates_types = binary_nil_annotates_types(flags);
  const std::uint32_t structural_flags = flags & ~binary_nil_type_annotation_flag;
  const bool is_library =
      (version == library_binary_nil_version || version == class_binary_nil_version) &&
      structural_flags == 1U;
  if ((flags & ~binary_nil_known_flags) != 0U ||
      (version != library_binary_nil_version && version != class_binary_nil_version &&
       structural_flags != 0U) ||
      (version == class_binary_nil_version && structural_flags > 1U) ||
      (version == library_binary_nil_version && !is_library)) {
    throw std::runtime_error("Unsupported binary NIL flags " +
                             std::to_string(flags));
  }
  validate_count(section_count, max_section_count, "section count");

  // 2. Section Table
  struct SectionEntry {
    uint32_t type;
    uint64_t offset;
    uint64_t size;
  };
  std::vector<SectionEntry> sections;
  sections.reserve(section_count);
  for (uint32_t i = 0; i < section_count; ++i) {
    SectionEntry section;
    section.type = header.u32("section type");
    section.offset = header.u64("section offset");
    section.size = header.u64("section size");
    if (section.offset > bytes.size() ||
        section.size > bytes.size() - section.offset) {
      throw std::runtime_error("Binary NIL section lies outside the file");
    }
    sections.push_back(section);
  }

  const std::size_t section_table_end = header.position();
  std::unordered_set<std::uint32_t> section_types;
  std::vector<std::pair<std::uint64_t, std::uint64_t>> section_ranges;
  section_ranges.reserve(sections.size());
  for (const auto &section : sections) {
    const std::uint32_t maximum_section_type = version == class_binary_nil_version ? 7U : is_library ? 6U : 5U;
    if (section.type < 1 || section.type > maximum_section_type || (!is_library && section.type == 6U)) {
      throw std::runtime_error("Unknown binary NIL section type " +
                               std::to_string(section.type));
    }
    if (!section_types.insert(section.type).second) {
      throw std::runtime_error("Duplicate binary NIL section type " +
                               std::to_string(section.type));
    }
    if (section.size == 0) {
      throw std::runtime_error("Binary NIL section has an empty payload");
    }
    if (section.offset < section_table_end) {
      throw std::runtime_error(
          "Binary NIL section overlaps the header or section table");
    }
    section_ranges.emplace_back(section.offset, section.offset + section.size);
  }
  std::sort(section_ranges.begin(), section_ranges.end());
  if (version == executable_binary_nil_version || version == class_binary_nil_version) {
    const auto required_count = version == class_binary_nil_version ? (is_library ? 7U : 6U) : 5U;
    if (section_count != required_count || section_ranges.front().first != section_table_end ||
        section_ranges.back().second != bytes.size())
      throw std::runtime_error("Binary NIL executable requires complete versioned sections");
    for (std::size_t i = 1; i < section_ranges.size(); ++i) {
      if (section_ranges[i - 1].second != section_ranges[i].first)
        throw std::runtime_error("Binary NIL executable sections overlap or contain gaps");
    }
  }
  for (std::size_t i = 1; i < section_ranges.size(); ++i) {
    if (section_ranges[i - 1].second > section_ranges[i].first) {
      throw std::runtime_error("Binary NIL sections overlap");
    }
  }

  std::vector<Instruction> program;
  std::size_t next_function_index = 0;
  std::uint64_t total_function_count = 0;
  std::uint64_t total_constant_count = 0;
  std::uint64_t total_instruction_count = 0;
  nil_module fault_metadata;

  // 3. Process Sections
  for (const auto &sec : sections) {
    binary_cursor section(bytes, static_cast<std::size_t>(sec.offset),
                          static_cast<std::size_t>(sec.offset + sec.size));

    if (sec.type == 1) { // Type Section (reserved in NIL 0.1)
      if (version == executable_binary_nil_version || version == class_binary_nil_version) {
        read_executable_types(section, fault_metadata, annotates_types);
      } else if (section.u32("type count") != 0) {
        throw std::runtime_error(
            "Binary NIL 0.1 does not define encoded type records");
      }
      require_section_consumed(section, "type");
    } else if (sec.type == 3) { // Constant Section
      const std::uint32_t count = section.u32("constant count");
      validate_count(count, max_constant_count, "constant count");
      total_constant_count += count;
      if (total_constant_count > max_constant_count) {
        throw std::runtime_error(
            "Binary NIL total constant count exceeds safety limit");
      }
      for (uint32_t i = 0; i < count; ++i) {
        std::string name = section.string("constant name");
        std::string val = section.string("constant value");
        constants[name] = val;
      }
      require_section_consumed(section, "constant");
    } else if (sec.type == 2) { // Function Section
      const std::uint32_t fn_count = section.u32("function count");
      validate_count(fn_count, max_function_count, "function count");
      total_function_count += fn_count;
      if (total_function_count > max_function_count) {
        throw std::runtime_error(
            "Binary NIL total function count exceeds safety limit");
      }
      for (uint32_t i = 0; i < fn_count; ++i) {
        std::string name = section.string("function name");
        if (name.empty()) {
          throw std::runtime_error("Binary NIL contains an empty function name");
        }
        const std::uint32_t param_count = section.u32("parameter count");
        validate_count(param_count, max_parameter_count, "parameter count");
        std::vector<std::string> parameter_types;
        for (uint32_t j = 0; j < param_count; ++j) {
          const auto parameter = section.string("parameter");
          const auto colon = parameter.find(':');
          parameter_types.push_back(colon == std::string::npos ? "" : trim_textual_field(parameter.substr(colon + 1)));
        }
        const std::uint32_t gp_count =
            section.u32("generic parameter count");
        validate_count(gp_count, max_parameter_count,
                       "generic parameter count");
        for (uint32_t j = 0; j < gp_count; ++j)
          (void)section.string("generic parameter");
        const std::string return_type = section.string("return type");
        nil_function metadata;
        if (version == executable_binary_nil_version || version == class_binary_nil_version) {
          read_function_metadata(section, metadata);
        }

        const std::size_t function_index = next_function_index++;
        append_function_begin(program, name, function_index, param_count,
                              return_type);
        program.back().function_parameter_types = std::move(parameter_types);
        program.back().function_annotations = std::move(metadata.annotations);

        // Body
        const std::uint32_t instr_count = section.u32("instruction count");
        validate_count(instr_count, max_instruction_count,
                       "instruction count");
        total_instruction_count += instr_count;
        if (total_instruction_count > max_instruction_count) {
          throw std::runtime_error(
              "Binary NIL total instruction count exceeds safety limit");
        }
        for (uint32_t j = 0; j < instr_count; ++j) {
          std::string opname = section.string("opcode");
          const std::string context =
              " in binary function '" + name + "', instruction " +
              std::to_string(j + 1);
          const Opcode opcode = decode_opcode(opname, context);
          const std::uint32_t arg_count = section.u32("operand count");
          validate_count(arg_count, max_operand_count, "operand count");
          validate_instruction_arity(opcode, opname, arg_count, context);
          std::vector<std::string> args;
          args.reserve(arg_count);
          for (uint32_t k = 0; k < arg_count; ++k)
            args.push_back(section.string("operand"));
          const std::uint32_t dbg = section.u32("debug line");
          if (dbg > static_cast<std::uint32_t>(INT_MAX)) {
            throw std::runtime_error("Binary NIL debug line exceeds INT_MAX" +
                                     context);
          }

          Instruction instr;
          instr.op = opcode;
          validate_instruction(instr.op, opname, args, context);
          instr.args = args;
          instr.line = static_cast<int>(dbg);
          program.push_back(instr);
        }
        append_function_end(program, function_index);
      }
      require_section_consumed(section, "function");
    } else if (sec.type == 4) { // Symbol Section (reserved in NIL 0.1)
      const std::uint32_t count = section.u32("symbol count");
      if (count != 0) {
        throw std::runtime_error(
            "Binary NIL 0.1 does not define encoded symbol records");
      }
      require_section_consumed(section, "symbol");
    } else if (sec.type == 5) { // Module Section
      (void)section.string("module name");
      const std::uint32_t test_count = section.u32("test count");
      validate_count(test_count, max_function_count, "test count");
      for (std::uint32_t i = 0; i < test_count; ++i)
        (void)section.string("test name");
      require_section_consumed(section, "module");
    } else if (sec.type == 7) {
      read_class_metadata(section, fault_metadata, false, annotates_types);
      require_section_consumed(section, "class");
    } else if (sec.type == 6) { // Embedded source interface
      const std::uint32_t source_count = section.u32("source count");
      validate_count(source_count, max_embedded_source_count, "source count");
      for (std::uint32_t i = 0; i < source_count; ++i) {
        (void)section.string("source path");
        (void)section.string("source contents");
      }
      const std::uint32_t dependency_count = section.u32("dependency count");
      validate_count(dependency_count, max_function_count, "dependency count");
      for (std::uint32_t i = 0; i < dependency_count; ++i)
        (void)section.string("dependency name");
      require_section_consumed(section, "source interface");
    }
  }
  validate_global_storage_contract(program);
  validate_fault_program(program, &fault_metadata);
  attach_class_metadata(program, std::move(fault_metadata));
  return program;
}

std::unique_ptr<nil_module> parseBinaryNilModule(std::istream &is) {
  const auto bytes = read_binary_bytes(is);
  if (bytes.size() < 16)
    throw std::runtime_error("Truncated binary NIL header");
  if (std::memcmp(bytes.data(), "NIL\0", 4) != 0) {
    throw std::runtime_error(
        "Native assembly requires current compiler-emitted NIL binary magic");
  }

  binary_cursor header(bytes, 4, bytes.size());
  const std::uint32_t version = header.u32("version");
  const std::uint32_t flags = header.u32("flags");
  const std::uint32_t section_count = header.u32("section count");
  // The type-annotation bit says how the type records are laid out; it does
  // not make an artifact a library, so it is masked off before the structural
  // rules. An unknown bit still refuses.
  const bool annotates_types = binary_nil_annotates_types(flags);
  const std::uint32_t structural_flags = flags & ~binary_nil_type_annotation_flag;
  if ((version == library_binary_nil_version || version == class_binary_nil_version) &&
      structural_flags == 1U) {
    throw std::runtime_error(
        "NIL v2 libraries cannot be assembled as executable objects");
  }
  if ((flags & ~binary_nil_known_flags) != 0U ||
      (version != executable_binary_nil_version &&
       version != legacy_executable_binary_nil_version && version != class_binary_nil_version &&
       version != source_debug_binary_nil_version) || structural_flags != 0U) {
    throw std::runtime_error("Native assembly requires a binary NIL executable "
                             "(version 1 or 3, flags 0)");
  }
  if (section_count != (version == source_debug_binary_nil_version ? 7U : version == class_binary_nil_version ? 6U : 5U)) {
    throw std::runtime_error(
        "Binary NIL executable must contain exactly five sections");
  }

  struct SectionEntry {
    std::uint32_t type;
    std::uint64_t offset;
    std::uint64_t size;
  };
  std::vector<SectionEntry> sections;
  sections.reserve(section_count);
  std::unordered_set<std::uint32_t> section_types;
  for (std::uint32_t i = 0; i < section_count; ++i) {
    SectionEntry section{header.u32("section type"),
                         header.u64("section offset"),
                         header.u64("section size")};
    if (section.type < 1U || (section.type > 5U &&
        !((version == class_binary_nil_version || version == source_debug_binary_nil_version) && section.type == 7U) &&
        !(version == source_debug_binary_nil_version && section.type == 8U))) {
      throw std::runtime_error("Unknown binary NIL section type " +
                               std::to_string(section.type));
    }
    if (!section_types.insert(section.type).second) {
      throw std::runtime_error("Duplicate binary NIL section type " +
                               std::to_string(section.type));
    }
    if (section.size == 0U || section.offset > bytes.size() ||
        section.size > bytes.size() - section.offset) {
      throw std::runtime_error(
          "Binary NIL section is empty or lies outside the file");
    }
    sections.push_back(section);
  }
  for (std::uint32_t type = 1; type <= 5; ++type) {
    if (section_types.count(type) == 0U) {
      throw std::runtime_error(
          "Binary NIL executable is missing section type " +
          std::to_string(type));
    }
  }

  const std::size_t table_end = header.position();
  std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
  ranges.reserve(sections.size());
  for (const auto &section : sections) {
    if (section.offset < table_end) {
      throw std::runtime_error(
          "Binary NIL section overlaps the header or section table");
    }
    ranges.emplace_back(section.offset, section.offset + section.size);
  }
  std::sort(ranges.begin(), ranges.end());
  if (ranges.front().first != table_end ||
      ranges.back().second != bytes.size()) {
    throw std::runtime_error(
        "Binary NIL executable contains unclaimed bytes between its sections");
  }
  for (std::size_t i = 1; i < ranges.size(); ++i) {
    if (ranges[i - 1].second != ranges[i].first) {
      throw std::runtime_error(
          "Binary NIL executable sections overlap or contain gaps");
    }
  }

  const auto find_section = [&](std::uint32_t type) -> const SectionEntry & {
    const auto found = std::find_if(
        sections.begin(), sections.end(),
        [type](const SectionEntry &section) { return section.type == type; });
    if (found == sections.end())
      throw std::runtime_error("Internal NIL section lookup failed");
    return *found;
  };
  const auto cursor_for = [&](std::uint32_t type) {
    const auto &section = find_section(type);
    return binary_cursor(
        bytes, static_cast<std::size_t>(section.offset),
        static_cast<std::size_t>(section.offset + section.size));
  };

  auto module = std::make_unique<nil_module>();
  if (version == class_binary_nil_version || version == source_debug_binary_nil_version) {
    auto section = cursor_for(7U);
    read_class_metadata(section, *module, version == source_debug_binary_nil_version,
                        annotates_types);
    require_section_consumed(section, "class");
  }

  {
    auto section = cursor_for(1U);
    if (version == executable_binary_nil_version || version == class_binary_nil_version || version == source_debug_binary_nil_version) {
      read_executable_types(section, *module, annotates_types);
    } else if (section.u32("type count") != 0U) {
      throw std::runtime_error(
          "Binary NIL version 1 does not define encoded type records");
    }
    require_section_consumed(section, "type");
  }
  {
    auto section = cursor_for(2U);
    const std::uint32_t function_count = section.u32("function count");
    validate_count(function_count, max_function_count, "function count");
    std::uint64_t total_instruction_count = 0;
    for (std::uint32_t i = 0; i < function_count; ++i) {
      nil_function function;
      function.name = section.string("function name");
      if (function.name.empty())
        throw std::runtime_error("Binary NIL contains an empty function name");
      const std::uint32_t parameter_count = section.u32("parameter count");
      validate_count(parameter_count, max_parameter_count, "parameter count");
      for (std::uint32_t parameter = 0; parameter < parameter_count;
           ++parameter)
        function.params.push_back(section.string("parameter"));
      const std::uint32_t generic_count =
          section.u32("generic parameter count");
      validate_count(generic_count, max_parameter_count,
                     "generic parameter count");
      for (std::uint32_t generic = 0; generic < generic_count; ++generic)
        function.generic_params.push_back(section.string("generic parameter"));
      function.return_type = section.string("return type");
      validate_serialized_function_signature(
          function, "binary function '" + function.name + "'");
      if (version == executable_binary_nil_version || version == class_binary_nil_version || version == source_debug_binary_nil_version)
        read_function_metadata(section, function);

      const std::uint32_t instruction_count = section.u32("instruction count");
      validate_count(instruction_count, max_instruction_count,
                     "instruction count");
      total_instruction_count += instruction_count;
      if (total_instruction_count > max_instruction_count) {
        throw std::runtime_error(
            "Binary NIL total instruction count exceeds safety limit");
      }
      for (std::uint32_t instruction = 0; instruction < instruction_count;
           ++instruction) {
        nil_instruction parsed;
        parsed.opname = section.string("opcode");
        const std::string context = " in binary function '" + function.name +
                                    "', instruction " +
                                    std::to_string(instruction + 1);
        const Opcode opcode = decode_opcode(parsed.opname, context);
        const std::uint32_t argument_count = section.u32("operand count");
        validate_count(argument_count, max_operand_count, "operand count");
        for (std::uint32_t argument = 0; argument < argument_count; ++argument)
          parsed.args.push_back(section.string("operand"));
        validate_instruction(opcode, parsed.opname, parsed.args, context);
        const std::uint32_t debug_line = section.u32("debug line");
        if (debug_line > static_cast<std::uint32_t>(INT_MAX)) {
          throw std::runtime_error("Binary NIL debug line exceeds INT_MAX" +
                                   context);
        }
        parsed.debug_line = static_cast<int>(debug_line);
        function.body.push_back(std::move(parsed));
      }
      module->functions.push_back(std::move(function));
    }
    require_section_consumed(section, "function");
  }
  if (version == source_debug_binary_nil_version) {
    auto section = cursor_for(8U);
    if (section.u32("debug function count") != module->functions.size())
      throw std::runtime_error("Binary NIL source debug function count mismatch");
    for (auto &function : module->functions) {
      if (section.u32("debug instruction count") != function.body.size())
        throw std::runtime_error("Binary NIL source debug instruction count mismatch");
      for (auto &instruction : function.body) {
        instruction.source_file = section.string("debug source file");
        const auto count = section.u32("debug local count");
        validate_count(count, 1024U, "debug local count");
        for (std::uint32_t i = 0; i < count; ++i) {
          DebugLocalBinding local;
          local.name = section.string("debug local name");
          local.storage = section.string("debug local storage");
          local.type = section.string("debug local type");
          const auto depth = section.u32("debug local scope depth");
          validate_count(depth, 1024U, "debug local scope depth");
          local.scope_depth = static_cast<int>(depth);
          instruction.debug_locals.push_back(std::move(local));
        }
        validate_source_debug(instruction.source_file, instruction.debug_locals);
      }
    }
    require_section_consumed(section, "source debug");
  }
  {
    auto section = cursor_for(3U);
    const std::uint32_t constant_count = section.u32("constant count");
    validate_count(constant_count, max_constant_count, "constant count");
    for (std::uint32_t i = 0; i < constant_count; ++i) {
      std::string name = section.string("constant name");
      std::string value = section.string("constant value");
      if (name.empty())
        throw std::runtime_error("Binary NIL contains an empty constant name");
      module->string_constants.emplace_back(std::move(name), std::move(value));
    }
    require_section_consumed(section, "constant");
  }
  {
    auto section = cursor_for(4U);
    if (section.u32("symbol count") != 0U) {
      throw std::runtime_error(
          "Binary NIL version 1 does not define encoded symbol records");
    }
    require_section_consumed(section, "symbol");
  }
  {
    auto section = cursor_for(5U);
    module->module_name = section.string("module name");
    const std::uint32_t test_count = section.u32("test count");
    validate_count(test_count, max_function_count, "test count");
    for (std::uint32_t i = 0; i < test_count; ++i)
      module->tests.push_back(section.string("test name"));
    require_section_consumed(section, "module");
  }
  validate_global_storage_contract(*module);
  validate_nil_fault_contract(*module);
  validate_nil_class_contract(*module);
  validate_nil_parallel_contract(*module);
  validate_nil_owned_contract(*module);
  return module;
}

nil_library_metadata parseBinaryLibraryMetadata(std::istream &is, std::string *content_digest) {
  const auto bytes = read_binary_bytes(is);
  if (bytes.size() < 16 || std::memcmp(bytes.data(), "NIL\0", 4) != 0)
    throw std::runtime_error("Invalid NIL library header");

  binary_cursor header(bytes, 4, bytes.size());
  const std::uint32_t version = header.u32("version");
  const std::uint32_t flags = header.u32("flags");
  const std::uint32_t section_count = header.u32("section count");
  if ((version != library_binary_nil_version && version != class_binary_nil_version) || flags != 1U)
    throw std::runtime_error("NIL artifact is not a version 2 library or version 4 class library");
  validate_count(section_count, max_section_count, "section count");

  struct section_entry {
    std::uint32_t type;
    std::uint64_t offset;
    std::uint64_t size;
  };
  std::vector<section_entry> sections;
  sections.reserve(section_count);
  std::unordered_set<std::uint32_t> types;
  for (std::uint32_t i = 0; i < section_count; ++i) {
    section_entry entry{header.u32("section type"),
                        header.u64("section offset"),
                        header.u64("section size")};
    if (entry.type < 1U || entry.type > (version == class_binary_nil_version ? 7U : 6U) ||
        !types.insert(entry.type).second)
      throw std::runtime_error("Invalid or duplicate NIL library section");
    if (entry.size == 0 || entry.offset > bytes.size() ||
        entry.size > bytes.size() - entry.offset)
      throw std::runtime_error("NIL library section lies outside the file");
    sections.push_back(entry);
  }
  if (!types.count(5U) || !types.count(6U))
    throw std::runtime_error("NIL library lacks module or source metadata");

  std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
  for (const auto &entry : sections) {
    if (entry.offset < header.position())
      throw std::runtime_error("NIL library section overlaps its header");
    ranges.emplace_back(entry.offset, entry.offset + entry.size);
  }
  std::sort(ranges.begin(), ranges.end());
  for (std::size_t i = 1; i < ranges.size(); ++i) {
    if (ranges[i - 1].second > ranges[i].first)
      throw std::runtime_error("NIL library sections overlap");
  }

  nil_library_metadata result;
  for (const auto &entry : sections) {
    binary_cursor section(bytes, static_cast<std::size_t>(entry.offset),
                          static_cast<std::size_t>(entry.offset + entry.size));
    if (entry.type == 5U) {
      result.module_name = section.string("module name");
      const auto test_count = section.u32("test count");
      validate_count(test_count, max_function_count, "test count");
      for (std::uint32_t i = 0; i < test_count; ++i)
        (void)section.string("test name");
      require_section_consumed(section, "module");
    } else if (entry.type == 6U) {
      const auto source_count = section.u32("source count");
      validate_count(source_count, max_embedded_source_count, "source count");
      std::unordered_set<std::string> source_paths;
      for (std::uint32_t i = 0; i < source_count; ++i) {
        nil_library_source source{section.string("source path"),
                                  section.string("source contents")};
        const std::filesystem::path path(source.path);
        if (source.path.empty() ||
            source.path.find('\0') != std::string::npos ||
            source.path.find("//") != std::string::npos ||
            source.path.back() == '/' || path.is_absolute() ||
            path.has_root_path() ||
            source.path.find('\\') != std::string::npos ||
            std::find(path.begin(), path.end(), std::filesystem::path(".")) !=
                path.end() ||
            std::find(path.begin(), path.end(), std::filesystem::path("..")) !=
                path.end() ||
            source.contents.find('\0') != std::string::npos ||
            !source_paths.insert(source.path).second) {
          throw std::runtime_error("NIL library contains an unsafe source entry");
        }
        result.sources.push_back(std::move(source));
      }
      const auto dependency_count = section.u32("dependency count");
      validate_count(dependency_count, max_function_count,
                     "dependency count");
      std::unordered_set<std::string> dependencies;
      for (std::uint32_t i = 0; i < dependency_count; ++i) {
        std::string dependency = section.string("dependency name");
        if (dependency.empty() || !dependencies.insert(dependency).second)
          throw std::runtime_error(
              "NIL library contains an invalid dependency entry");
        result.dependencies.push_back(std::move(dependency));
      }
      require_section_consumed(section, "source interface");
    }
  }
  if (result.module_name.empty() || result.sources.empty())
    throw std::runtime_error("NIL library metadata is incomplete");
  if (version == class_binary_nil_version) {
    std::istringstream checked(std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size()));
    std::unordered_map<std::string, Value> constants;
    (void)parseBinaryProgram(checked, constants);
  }
  if (content_digest)
    *content_digest = nil::detail::content_digest(
        std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size()));
  return result;
}

} // namespace ny
