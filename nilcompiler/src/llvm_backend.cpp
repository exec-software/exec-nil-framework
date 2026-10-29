#include "llvm_backend.hpp"
#include "array_intrinsic.hpp"
#include "binding_reference.hpp"
#include "codegen.hpp"
#include "concurrency_contract.hpp"
#include "owned_contract.hpp"
#include "diagnostics.hpp"
#include "dynamic_type.hpp"
#include "ui_intrinsic.hpp"
#include "executable_context.hpp"
#include "native_runtime.hpp"
#include "owned_runtime.hpp"
#include "nil_contract.hpp"
#include "numeric_literal.hpp"
#include "scalar_text.hpp"
#include "inline_asm.hpp"

#ifdef NY_HAS_LLVM

// LLVM Headers
#include "llvm/Config/llvm-config.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/Mangling.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <deque>
#include <iostream>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace ny {

static llvm::Intrinsic::ID lookupIntrinsicID(llvm::StringRef name) {
  // LLVM 20 moved intrinsic lookup from Function to the Intrinsic namespace.
#if LLVM_VERSION_MAJOR >= 20
  return llvm::Intrinsic::lookupIntrinsicID(name);
#else
  return llvm::Function::lookupIntrinsicID(name);
#endif
}

// Map string opcodes (from NIL) to enum Opcode
static Opcode stringToOpcode(const std::string &opname) {
  // Uppercase names are the canonical NIL spelling. A few lowercase/dotted
  // aliases remain readable for early hand-written NIL fixtures.
  static const std::unordered_map<std::string, Opcode> map = {
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
      {"CLASS_UPCAST", Opcode::CLASS_UPCAST},
      {"CALL_METHOD", Opcode::CALL_METHOD},
      {"PARALLEL_MAP", Opcode::PARALLEL_MAP},
      {"OWN_ENTER", Opcode::OWN_ENTER},
      {"OWN_LEAVE", Opcode::OWN_LEAVE},
      {"OWN_BIND", Opcode::OWN_BIND},
      {"OWN_MOVE", Opcode::OWN_MOVE},
      {"OWN_ALLOC", Opcode::OWN_ALLOC},
      {"RET", Opcode::RET},
      {"FAULT_ENTER", Opcode::FAULT_ENTER},
      {"FAULT_LEAVE", Opcode::FAULT_LEAVE},
      {"FAULT_RAISE", Opcode::FAULT_RAISE},
      {"FAULT_SAVE", Opcode::FAULT_SAVE},
      {"FAULT_MATCH", Opcode::FAULT_MATCH},
      {"FAULT_BIND", Opcode::FAULT_BIND},
      {"FAULT_DROP", Opcode::FAULT_DROP},
      {"FAULT_RESUME", Opcode::FAULT_RESUME},
      {"FAULT_RETHROW", Opcode::FAULT_RETHROW},
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
      {"BOX_ANY", Opcode::BOX_ANY},
      {"UNBOX_ANY", Opcode::UNBOX_ANY},
      {"IS_ANY_TYPE", Opcode::IS_ANY_TYPE},
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
      {"push.const", Opcode::PUSH_CONST},
      {"push.string", Opcode::PUSH_CONST_STR},
      {"pop", Opcode::POP},
      {"add.i32", Opcode::ADD},
      {"sub.i32", Opcode::SUB},
      {"call", Opcode::CALL},
      {"ret", Opcode::RET},
      {"alloc", Opcode::ALLOC},
      {"free", Opcode::FREE},
      {"store_local", Opcode::STORE_LOCAL},
      {"load_local", Opcode::LOAD_LOCAL},
      {"load_global", Opcode::LOAD_GLOBAL},
      {"store_global", Opcode::STORE_GLOBAL},
      {"jmp", Opcode::JMP},
      {"jmp_if_not", Opcode::JMP_IF_NOT},
      {"label", Opcode::LABEL},
      {"cmp_lt", Opcode::CMP_LT},
      {"cmp_eq", Opcode::CMP_EQ},
      {"mul", Opcode::MUL},
      {"div", Opcode::DIV},
      {"mod", Opcode::MOD},
      {"neg", Opcode::NEG},
      {"bit_and", Opcode::BIT_AND},
      {"bit_or", Opcode::BIT_OR},
      {"bit_xor", Opcode::BIT_XOR},
      {"shl", Opcode::SHL},
      {"shr", Opcode::SHR},
      {"cmp_gt", Opcode::CMP_GT},
      {"cmp_ge", Opcode::CMP_GE},
      {"cmp_le", Opcode::CMP_LE},
      {"cmp_ne", Opcode::CMP_NE},
      {"new_struct", Opcode::NEW_STRUCT},
      {"get_field", Opcode::GET_FIELD},
      {"set_field", Opcode::SET_FIELD},
      {"new_array", Opcode::NEW_ARRAY},
      {"new_array_literal", Opcode::NEW_ARRAY_LITERAL},
      {"get_index", Opcode::GET_INDEX},
      {"set_index", Opcode::SET_INDEX},
      {"len", Opcode::LEN},
      {"new_variant", Opcode::NEW_VARIANT},
      {"is_variant", Opcode::IS_VARIANT},
      {"unwrap_variant", Opcode::UNWRAP_VARIANT},
      {"cast", Opcode::CAST},
      {"interop_call", Opcode::INTEROP_CALL},
  };
  auto it = map.find(opname);
  if (it != map.end())
    return it->second;
  return Opcode::COUNT;
}

static bool native_opcode_supported(Opcode opcode) {
  switch (opcode) {
  case Opcode::PUSH_CONST_STR:
  case Opcode::PUSH_CONST:
  case Opcode::POP:
  case Opcode::LOAD_LOCAL:
  case Opcode::REF_LOCAL:
  case Opcode::STORE_LOCAL:
  case Opcode::LOAD_GLOBAL:
  case Opcode::STORE_GLOBAL:
  case Opcode::CALL:
  case Opcode::RET:
  case Opcode::FAULT_ENTER:
  case Opcode::FAULT_LEAVE:
  case Opcode::FAULT_RAISE:
  case Opcode::FAULT_SAVE:
  case Opcode::FAULT_MATCH:
  case Opcode::FAULT_BIND:
  case Opcode::FAULT_DROP:
  case Opcode::FAULT_RESUME:
  case Opcode::FAULT_RETHROW:
  case Opcode::NOP:
  case Opcode::JMP_IF_NOT:
  case Opcode::JMP:
  case Opcode::JMP_IF:
  case Opcode::LABEL:
  case Opcode::CMP_EQ:
  case Opcode::CMP_NE:
  case Opcode::CMP_LT:
  case Opcode::CMP_LE:
  case Opcode::CMP_GT:
  case Opcode::CMP_GE:
  case Opcode::NOT:
  case Opcode::ADD:
  case Opcode::SUB:
  case Opcode::PTR_ADD:
  case Opcode::PTR_SUB:
  case Opcode::MUL:
  case Opcode::DIV:
  case Opcode::MOD:
  case Opcode::NEG:
  case Opcode::BIT_AND:
  case Opcode::BIT_OR:
  case Opcode::BIT_XOR:
  case Opcode::SHL:
  case Opcode::SHR:
  case Opcode::ALLOC:
  case Opcode::FREE:
  case Opcode::READ_PTR:
  case Opcode::WRITE_PTR:
  case Opcode::NEW_STRUCT:
  case Opcode::CLASS_UPCAST:
  case Opcode::CALL_METHOD:
  case Opcode::PARALLEL_MAP:
  case Opcode::OWN_ENTER:
  case Opcode::OWN_LEAVE:
  case Opcode::OWN_BIND:
  case Opcode::OWN_MOVE:
  case Opcode::OWN_ALLOC:
  case Opcode::GET_FIELD:
  case Opcode::SET_FIELD:
  case Opcode::NEW_ARRAY:
  case Opcode::GET_INDEX:
  case Opcode::SET_INDEX:
  case Opcode::LEN:
  case Opcode::NEW_VARIANT:
  case Opcode::IS_VARIANT:
  case Opcode::UNWRAP_VARIANT:
  case Opcode::NEW_ARRAY_LITERAL:
  case Opcode::STR_CONCAT:
  case Opcode::TO_STRING:
  case Opcode::CAST:
  case Opcode::BOX_ANY:
  case Opcode::UNBOX_ANY:
  case Opcode::IS_ANY_TYPE:
  case Opcode::STRLEN:
  case Opcode::INTEROP_CALL:
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
  case Opcode::INLINE_ASM:
  case Opcode::TIME_NOW:
  case Opcode::TIME_SLEEP:
    return true;
  case Opcode::PUSH_FN_PTR:
  case Opcode::SET_FIELD_PTR:
  case Opcode::CONSOLE_WRITE:
  case Opcode::CONSOLE_WRITELN:
  case Opcode::COUNT:
    return false;
  }
  return false;
}

// PIMPL implementation structure

static std::string trim_copy(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos)
    return {};
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

static std::string nil_parameter_name(const std::string &parameter) {
  const size_t colon = parameter.find(':');
  return trim_copy(parameter.substr(0, colon));
}

static std::string nil_parameter_type(const std::string &parameter) {
  const size_t colon = parameter.find(':');
  if (colon == std::string::npos)
    return {};
  return trim_copy(parameter.substr(colon + 1));
}

static bool is_pointer_like_nil_type(std::string type) {
  type = trim_copy(std::move(type));
  std::transform(type.begin(), type.end(), type.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });

  return type == "text" || type == "string" || type == "str" ||
         type == "pointer" || type == "array" || type == "slice" ||
         type.rfind("ptr<", 0) == 0 || type.rfind("ref<", 0) == 0 ||
         type.rfind("array<", 0) == 0 || type.rfind("slice<", 0) == 0 ||
         (!type.empty() && type.front() == '[') ||
         (type.size() >= 2 && type.compare(type.size() - 2, 2, "[]") == 0);
}

enum class native_handle_kind {
  none,
  text,
  anything,
  scalar_array,
  integer_pointer,
  unsupported,
};

// Whitespace is presentation, but case and qualification are nominal identity.
// Only scalar/ABI classification may use the lowercased normalization below.
static std::string compact_nil_type(std::string type) {
  type = trim_copy(std::move(type));
  type.erase(
      std::remove_if(type.begin(), type.end(),
                     [](unsigned char ch) { return std::isspace(ch) != 0; }),
      type.end());
  return type;
}

static std::string normalize_nil_type(std::string type) {
  type = compact_nil_type(std::move(type));
  std::transform(type.begin(), type.end(), type.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return type;
}

static bool is_native_word_element_type(const std::string &type) {
  const std::string normalized = normalize_nil_type(type);
  // Native arrays are stable runtime descriptors. The descriptor carries the
  // concrete element kind and stride, so byte arrays do not masquerade as
  // arrays of target words.
  return normalized == "i32" || normalized == "i64" || normalized == "bool" ||
         normalized == "u8";
}

static std::optional<std::uint32_t>
native_array_kind_for_element(const std::string &type) {
  const std::string normalized = normalize_nil_type(type);
  if (normalized == "i32")
    return NYRT_ARRAY_I32;
  if (normalized == "i64")
    return NYRT_ARRAY_I64;
  if (normalized == "bool")
    return NYRT_ARRAY_BOOL;
  if (normalized == "u8")
    return NYRT_ARRAY_U8;
  if (normalized == "f64")
    return NYRT_ARRAY_F64;
  if (normalized == "anything")
    return NYRT_ARRAY_ANYTHING;
  if (normalized == "text" || normalized == "string" || normalized == "str")
    return NYRT_ARRAY_TEXT;
  if (is_array_tuple_type(type))
    return NYRT_ARRAY_TUPLE;
  return std::nullopt;
}

static bool is_native_scalar_abi_type(const std::string &type) {
  const std::string normalized = normalize_nil_type(type);
  return normalized == "i8" || normalized == "i16" || normalized == "i32" ||
         normalized == "i64" || normalized == "u8" || normalized == "u16" ||
         normalized == "u32" || normalized == "u64" || normalized == "f32" ||
         normalized == "f64" || normalized == "bool" || normalized == "char";
}

static std::optional<std::vector<std::string>>
native_generic_arguments(const std::string &type, const std::string &name) {
  const std::string normalized = compact_nil_type(type);
  const auto open = normalized.find('<');
  if (open == std::string::npos || normalized.back() != '>')
    return std::nullopt;
  std::string base = normalized.substr(0, open);
  if (const auto separator = base.rfind("::"); separator != std::string::npos)
    base = base.substr(separator + 2);
  if (normalize_nil_type(base) != normalize_nil_type(name))
    return std::nullopt;

  std::vector<std::string> arguments;
  std::size_t start = open + 1;
  int depth = 0;
  for (std::size_t index = start; index + 1 < normalized.size(); ++index) {
    const char character = normalized[index];
    if (character == '<') {
      ++depth;
    } else if (character == '>') {
      if (depth == 0)
        return std::nullopt;
      --depth;
    } else if (character == ',' && depth == 0) {
      if (index == start)
        return std::nullopt;
      arguments.push_back(normalized.substr(start, index - start));
      start = index + 1;
    }
  }
  if (depth != 0 || start + 1 > normalized.size())
    return std::nullopt;
  const std::size_t finish = normalized.size() - 1;
  if (finish == start)
    return std::nullopt;
  arguments.push_back(normalized.substr(start, finish - start));
  return arguments;
}

static std::optional<std::string>
native_generic_argument(const std::string &type, const std::string &name) {
  // A container has one argument even when that argument contains a nested
  // tuple or generic record. Nominal case must survive extraction.
  const auto compact = compact_nil_type(type);
  if (normalize_nil_type(compact.substr(0, name.size() + 1)) != name + "<")
    return std::nullopt;
  const auto arguments = native_generic_arguments(type, name);
  return arguments && arguments->size() == 1
             ? std::optional<std::string>(arguments->front()) : std::nullopt;
}

static native_handle_kind
native_handle_kind_for_nil_type(const std::string &type) {
  const std::string normalized = normalize_nil_type(type);
  if (normalized == "text" || normalized == "string" || normalized == "str")
    return native_handle_kind::text;
  if (normalized == "anything")
    return native_handle_kind::anything;

  if (const auto element = native_generic_argument(type, "array")) {
    return native_array_kind_for_element(*element).has_value()
               ? native_handle_kind::scalar_array
               : native_handle_kind::unsupported;
  }
  if (const auto element = native_generic_argument(type, "ptr")) {
    return is_native_word_element_type(*element)
               ? native_handle_kind::integer_pointer
               : native_handle_kind::unsupported;
  }

  return is_pointer_like_nil_type(normalized) ? native_handle_kind::unsupported
                                              : native_handle_kind::none;
}

static bool native_handle_types_compatible(const std::string &actual,
                                           const std::string &expected) {
  const auto actual_kind = native_handle_kind_for_nil_type(actual);
  const auto expected_kind = native_handle_kind_for_nil_type(expected);
  if (actual_kind != expected_kind)
    return false;
  if (expected_kind == native_handle_kind::text)
    return true; // text, String, and str are intentional language aliases.
  if (expected_kind == native_handle_kind::anything)
    return true;
  if (expected_kind == native_handle_kind::scalar_array) {
    const auto actual_element = native_generic_argument(actual, "array");
    const auto expected_element = native_generic_argument(expected, "array");
    if (actual_element && expected_element &&
        (is_array_tuple_type(*actual_element) || is_array_tuple_type(*expected_element)))
      return canonical_array_type(*actual_element) == canonical_array_type(*expected_element);
    return actual_element && expected_element &&
           native_array_kind_for_element(*actual_element) ==
               native_array_kind_for_element(*expected_element);
  }
  if (expected_kind == native_handle_kind::integer_pointer) {
    return normalize_nil_type(actual) == normalize_nil_type(expected);
  }
  return false;
}

static bool nil_type_name_matches(const std::string &qualified_name,
                                  const std::string &type) {
  const std::string qualified = compact_nil_type(qualified_name);
  const std::string normalized = compact_nil_type(type);
  // An untyped stack value is never a nominal type. In particular, do not
  // let empty metadata alias an accidentally empty schema name while checking
  // scalar returns and assignments.
  if (qualified.empty() || normalized.empty())
    return false;
  if (qualified == normalized)
    return true;
  const std::string suffix = "::" + normalized;
  if (qualified.size() > suffix.size() &&
      qualified.compare(qualified.size() - suffix.size(), suffix.size(),
                        suffix) == 0) {
    return true;
  }

  auto symbol_spelling = [](std::string name) {
    for (size_t separator = name.find("::"); separator != std::string::npos;
         separator = name.find("::", separator + 2)) {
      name.replace(separator, 2, "__");
    }
    return name;
  };
  const std::string qualified_symbol = symbol_spelling(qualified);
  const std::string normalized_symbol = symbol_spelling(normalized);
  if (qualified_symbol == normalized_symbol)
    return true;
  const std::string symbol_suffix = "__" + normalized_symbol;
  return qualified_symbol.size() > symbol_suffix.size() &&
         qualified_symbol.compare(qualified_symbol.size() -
                                      symbol_suffix.size(),
                                  symbol_suffix.size(), symbol_suffix) == 0;
}

static const nil_struct_def *
find_nil_struct_definition(const nil_module &mod, const std::string &type) {
  const auto exact_name = compact_nil_type(type);
  const nil_struct_def *exact = nullptr;
  for (const auto &definition : mod.structs) {
    if (compact_nil_type(definition.name) == exact_name) {
      if (exact)
        return nullptr;
      exact = &definition;
    }
  }
  if (exact)
    return exact;
  const nil_struct_def *match = nullptr;
  for (const auto &definition : mod.structs) {
    if (!nil_type_name_matches(definition.name, type))
      continue;
    if (match != nullptr)
      return nullptr;
    match = &definition;
  }
  return match;
}

static const nil_enum_def *find_nil_enum_definition(const nil_module &mod,
                                                    const std::string &type) {
  const auto exact_name = compact_nil_type(type);
  const nil_enum_def *exact = nullptr;
  for (const auto &definition : mod.enums) {
    if (compact_nil_type(definition.name) == exact_name) {
      if (exact)
        return nullptr;
      exact = &definition;
    }
  }
  if (exact)
    return exact;
  const nil_enum_def *match = nullptr;
  for (const auto &definition : mod.enums) {
    if (!nil_type_name_matches(definition.name, type))
      continue;
    if (match != nullptr)
      return nullptr;
    match = &definition;
  }
  return match;
}

static std::optional<std::vector<std::string>>
native_enum_variant_payload_types(const nil_enum_def::variant &variant) {
  const std::string associated_type = trim_copy(variant.associated_type);
  if (associated_type.empty())
    return std::vector<std::string>{};
  if (associated_type.front() != '(' || associated_type.back() != ')')
    return std::vector<std::string>{associated_type};

  const std::string payloads =
      trim_copy(associated_type.substr(1, associated_type.size() - 2));
  if (payloads.empty())
    return std::vector<std::string>{};

  std::vector<std::string> types;
  std::size_t begin = 0;
  int angle_depth = 0;
  int paren_depth = 0;
  int bracket_depth = 0;
  for (std::size_t index = 0; index < payloads.size(); ++index) {
    const char character = payloads[index];
    switch (character) {
    case '<':
      ++angle_depth;
      break;
    case '>':
      if (--angle_depth < 0)
        return std::nullopt;
      break;
    case '(':
      ++paren_depth;
      break;
    case ')':
      if (--paren_depth < 0)
        return std::nullopt;
      break;
    case '[':
      ++bracket_depth;
      break;
    case ']':
      if (--bracket_depth < 0)
        return std::nullopt;
      break;
    case ',':
      if (angle_depth == 0 && paren_depth == 0 && bracket_depth == 0) {
        types.push_back(trim_copy(payloads.substr(begin, index - begin)));
        begin = index + 1;
      }
      break;
    default:
      break;
    }
  }
  if (angle_depth != 0 || paren_depth != 0 || bracket_depth != 0)
    return std::nullopt;
  types.push_back(trim_copy(payloads.substr(begin)));
  for (const auto &type : types) {
    if (type.empty())
      return std::nullopt;
  }
  return types;
}

static std::optional<std::size_t>
native_enum_variant_payload_count(const nil_enum_def::variant &variant) {
  const auto types = native_enum_variant_payload_types(variant);
  return types ? std::optional<std::size_t>(types->size()) : std::nullopt;
}

static const nil_struct_def *
find_native_aggregate_definition(const nil_module &mod,
                                const std::string &type) {
  if (const auto *definition = find_nil_struct_definition(mod, type))
    return definition;
  // Struct references are the same shared object handles as struct values in
  // the interpreter. This is not a pointer to a local storage cell and does
  // not extend native support to primitive references or arbitrary pointers.
  if (normalize_nil_type(type).rfind("ref<", 0) != 0)
    return nullptr;
  const auto arguments = native_generic_arguments(type, "ref");
  return arguments && arguments->size() == 1
             ? find_nil_struct_definition(mod, arguments->front())
             : nullptr;
}

static bool is_native_aggregate_nil_type(const nil_module &mod,
                                         const std::string &type) {
  return find_native_aggregate_definition(mod, type) != nullptr;
}

static bool native_aggregate_types_compatible(const nil_module &mod,
                                              const std::string &actual,
                                              const std::string &expected) {
  const auto *actual_definition = find_native_aggregate_definition(mod, actual);
  const auto *expected_definition = find_native_aggregate_definition(mod, expected);
  if (actual_definition != nullptr && actual_definition == expected_definition)
    return true;
  // Only plain class views are covariant. A reference/container does not gain
  // subtyping merely because its referent is a class.
  return find_nil_class(mod, compact_nil_type(actual)) &&
         find_nil_class(mod, compact_nil_type(expected)) &&
         nil_class_assignable(mod, compact_nil_type(expected),
                              compact_nil_type(actual));
}

static bool native_pointer_types_compatible(const nil_module &mod,
                                            const std::string &actual,
                                            const std::string &expected) {
  if (is_native_aggregate_nil_type(mod, actual) ||
      is_native_aggregate_nil_type(mod, expected)) {
    return native_aggregate_types_compatible(mod, actual, expected);
  }
  return native_handle_types_compatible(actual, expected);
}

static const nil_function *find_nil_function(const nil_module &mod,
                                             const std::string &name) {
  const nil_function *match = nullptr;
  for (const auto &candidate : mod.functions) {
    if (candidate.name != name)
      continue;
    if (match != nullptr)
      return nullptr;
    match = &candidate;
  }
  return match;
}

std::optional<std::size_t>
native_builtin_argument_count(const nil_module &mod, const std::string &name) {
  // A real Ny definition wins over a legacy builtin spelling. Check presence
  // rather than uniqueness here so duplicate definitions still reach the
  // ordinary ambiguity validation instead of disappearing behind a builtin.
  for (const auto &function : mod.functions) {
    if (function.name == name)
      return std::nullopt;
  }
  if (name == "println" || name == "alloc" || name == "free" ||
      name == "read_i32" || name == "panic" || name == "ny__lang__core__panic" ||
      name == "array__len" || name == "array__length" ||
      name == "String__len" || name == "String__length") {
    return 1;
  }
  if (name == "write_i32" || name == "array__push" || name == "array__get")
    return 2;
  if (name == "array__set" || name == "String__substr")
    return 3;
  return std::nullopt;
}

static std::optional<std::size_t>
parse_call_argument_count(const nil_instruction &instruction,
                          const std::string &function_name) {
  if (instruction.args.size() != 2 || instruction.args[0].empty()) {
    std::cerr << "[NativeCodegen] CALL in function '" << function_name
              << "' requires a callee and argument count\n";
    return std::nullopt;
  }

  try {
    if (instruction.args[1].empty() ||
        !std::all_of(instruction.args[1].begin(), instruction.args[1].end(),
                     [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
      throw std::invalid_argument("invalid argument count");
    }
    std::size_t consumed = 0;
    const auto count = std::stoull(instruction.args[1], &consumed, 10);
    if (consumed != instruction.args[1].size() ||
        count > std::numeric_limits<std::size_t>::max()) {
      throw std::invalid_argument("invalid argument count");
    }
    return static_cast<std::size_t>(count);
  } catch (const std::exception &) {
    std::cerr << "[NativeCodegen] CALL to '" << instruction.args[0]
              << "' in function '" << function_name
              << "' has invalid argument count '" << instruction.args[1]
              << "'\n";
    return std::nullopt;
  }
}

static std::optional<std::unordered_set<const nil_function *>>
collect_native_reachable_functions(const nil_module &mod) {
  std::unordered_set<const nil_function *> reachable;
  std::vector<const nil_function *> worklist;
  std::unordered_map<std::string, std::vector<const nil_function *>>
      functions_by_name;
  for (const auto &function : mod.functions)
    functions_by_name[function.name].push_back(&function);

  bool valid = true;
  auto resolve_unique_function = [&](const std::string &name,
                                     const std::string &reference)
      -> const nil_function * {
    const auto definitions = functions_by_name.find(name);
    if (definitions == functions_by_name.end())
      return nullptr;
    if (definitions->second.size() != 1) {
      std::cerr << "[NativeCodegen] " << reference << " function name '" << name
                << "' is ambiguous (" << definitions->second.size()
                << " NIL definitions)\n";
      valid = false;
      return nullptr;
    }
    return definitions->second.front();
  };

  auto add_root = [&](const std::string &name) {
    if (const auto *function = resolve_unique_function(name, "root")) {
      if (reachable.insert(function).second)
        worklist.push_back(function);
    }
  };

  // Native executables enter through main. Global initialization is also a
  // platform root: older and hand-written NIL does not necessarily carry the
  // synthetic CALL that current source lowering inserts into main.
  add_root("main");
  add_root("__globals_init__");
  if (!valid)
    return std::nullopt;

  for (std::size_t index = 0; index < worklist.size(); ++index) {
    const auto *function = worklist[index];
    for (const auto &instruction : function->body) {
      const auto opcode = stringToOpcode(instruction.opname);
      if (opcode == Opcode::PUSH_FN_PTR ||
          ((opcode == Opcode::BOX_ANY || opcode == Opcode::UNBOX_ANY ||
            opcode == Opcode::IS_ANY_TYPE) && !instruction.args.empty() &&
           instruction.args[0].rfind("fn(", 0) == 0)) {
        std::cerr << "[NativeCodegen] native function values and their "
                     "invocation/boxing ABI are not supported in function '"
                  << function->name << "'\n";
        return std::nullopt;
      }
      if (opcode == Opcode::PARALLEL_MAP) {
        if (instruction.args.size() != 3) return std::nullopt;
        const auto *worker = resolve_unique_function(instruction.args[0], "parallel worker");
        if (!worker || !valid) return std::nullopt;
        if (reachable.insert(worker).second) worklist.push_back(worker);
        continue;
      }
      if (stringToOpcode(instruction.opname) == Opcode::CALL_METHOD) {
        if (instruction.args.size() != 3)
          return std::nullopt;
        const auto declaration = resolve_nil_class_method(
            mod, instruction.args[0], instruction.args[1]);
        if (!declaration.method) return std::nullopt;
        for (const auto &dynamic_class : mod.classes) {
          if (dynamic_class.is_abstract ||
              !nil_class_assignable(mod, instruction.args[0], dynamic_class.name))
            continue;
          const auto target = declaration.method->is_virtual
              ? resolve_nil_class_method(mod, dynamic_class.name, instruction.args[1])
              : declaration;
          if (!target.method || target.method->is_abstract)
            return std::nullopt;
          const auto *callee = resolve_unique_function(
              target.method->implementation, "class method target");
          if (!callee || !valid)
            return std::nullopt;
          if (reachable.insert(callee).second)
            worklist.push_back(callee);
        }
        continue;
      }
      if (stringToOpcode(instruction.opname) != Opcode::CALL)
        continue;

      const auto encoded_argument_count =
          parse_call_argument_count(instruction, function->name);
      if (!encoded_argument_count)
        return std::nullopt;

      const std::string &callee_name = instruction.args[0];
      if (const auto builtin_count =
              native_builtin_argument_count(mod, callee_name)) {
        if (*encoded_argument_count != *builtin_count) {
          std::cerr << "[NativeCodegen] CALL to builtin '" << callee_name
                    << "' in function '" << function->name << "' encodes "
                    << *encoded_argument_count << " argument(s), expected "
                    << *builtin_count << '\n';
          return std::nullopt;
        }
        continue;
      }

      const auto *callee =
          resolve_unique_function(callee_name, "directly called");
      if (!valid)
        return std::nullopt;
      if (!callee) {
        std::cerr << "[NativeCodegen] CALL in function '" << function->name
                  << "' names undefined callee '" << callee_name << "'\n";
        return std::nullopt;
      }
      if (*encoded_argument_count != callee->params.size()) {
        std::cerr << "[NativeCodegen] CALL to '" << callee_name
                  << "' in function '" << function->name << "' encodes "
                  << *encoded_argument_count
                  << " argument(s), but the callee declares "
                  << callee->params.size() << '\n';
        return std::nullopt;
      }

      if (reachable.insert(callee).second)
        worklist.push_back(callee);
    }
  }

  return reachable;
}

static bool has_nil_parameter_prologue(const nil_function &fn) {
  if (fn.params.empty() || fn.body.size() < fn.params.size())
    return false;

  // AST-to-NIL codegen pushes call arguments left-to-right, then begins each
  // ordinary function with STORE_LOCAL instructions in reverse parameter
  // order. Only recognize that exact prefix; a STORE_LOCAL in the user's body
  // must continue to consume a value produced by the body itself.
  for (size_t i = 0; i < fn.params.size(); ++i) {
    const auto &instruction = fn.body[i];
    const std::string expected =
        nil_parameter_name(fn.params[fn.params.size() - 1 - i]);
    if (stringToOpcode(instruction.opname) != Opcode::STORE_LOCAL ||
        instruction.args.size() != 1 || instruction.args[0] != expected) {
      return false;
    }
  }
  return true;
}

static std::string canonicalize_variant_name(std::string name) {
  // Ny accepts both Enum.Variant and Enum::Variant in source. NIL uses the
  // namespace separator so construction and matching have one representation.
  for (size_t dot = name.find('.'); dot != std::string::npos;
       dot = name.find('.', dot + 2)) {
    name.replace(dot, 1, "::");
  }

  const auto variant_separator = name.rfind("::");
  if (variant_separator == std::string::npos)
    return name;
  const auto enum_separator = variant_separator == 0
                                  ? std::string::npos
                                  : name.rfind("::", variant_separator - 1);
  const std::size_t enum_start =
      enum_separator == std::string::npos ? 0 : enum_separator + 2;
  std::string enum_name =
      name.substr(enum_start, variant_separator - enum_start);
  std::string variant_name = name.substr(variant_separator + 2);
  std::transform(
      enum_name.begin(), enum_name.end(), enum_name.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  std::transform(
      variant_name.begin(), variant_name.end(), variant_name.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

  std::string canonical_enum;
  std::string canonical_variant;
  if (enum_name == "option") {
    canonical_enum = "Option";
    if (variant_name == "some")
      canonical_variant = "Some";
    else if (variant_name == "none")
      canonical_variant = "None";
  } else if (enum_name == "result") {
    canonical_enum = "Result";
    if (variant_name == "ok")
      canonical_variant = "Ok";
    else if (variant_name == "error" || variant_name == "err")
      canonical_variant = "Error";
  }

  if (canonical_variant.empty())
    return name;
  name.replace(enum_start, variant_separator - enum_start, canonical_enum);
  const auto new_variant_separator = name.rfind("::");
  name.replace(new_variant_separator + 2, std::string::npos, canonical_variant);
  return name;
}

static bool variant_owner_matches_schema(const std::string &tag,
                                          const nil_enum_def &schema) {
  const auto separator = tag.rfind("::");
  if (separator == std::string::npos)
    return true; // Legacy unqualified tags get their owner from metadata.
  const std::string owner = tag.substr(0, separator);
  std::string schema_name = schema.name;
  // A source tag may omit generic arguments supplied by exact metadata, but
  // it may not name a different enum (or a different explicit instantiation).
  if (owner.find('<') == std::string::npos) {
    if (const auto parameters = schema_name.find('<');
        parameters != std::string::npos)
      schema_name.resize(parameters);
  }
  return nil_type_name_matches(schema_name, owner);
}

// Publish explicit shadow roots, not guessed machine-stack contents. A slot is
// reused on each execution of its SSA instruction, so allocation loops do not
// retain one root per iteration. Stale slots may conservatively retain values
// until the function returns; this is safe managed lifetime, not borrow checking.
static llvm::Value *emit_root_frame(llvm::Module &module,
                                    llvm::IRBuilder<> &builder,
                                    const std::vector<llvm::Value *> &slots) {
  auto &ctx = module.getContext();
  auto *pointer = llvm::PointerType::getUnqual(ctx);
  auto *word = llvm::Type::getInt64Ty(ctx);
  auto *layout = llvm::ArrayType::get(pointer, std::max<std::size_t>(slots.size(), 1));
  auto *addresses = builder.CreateAlloca(layout, nullptr, "ny.gc.roots");
  for (std::size_t index = 0; index < slots.size(); ++index) {
    auto *element = builder.CreateInBoundsGEP(layout, addresses,
        {llvm::ConstantInt::get(word, 0), llvm::ConstantInt::get(word, index)});
    builder.CreateStore(slots[index], element);
  }
  auto enter = module.getOrInsertFunction("nyrt_roots_enter",
      llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), {pointer, word}, false));
  builder.CreateCall(enter, {addresses, llvm::ConstantInt::get(word, slots.size())});
  return addresses;
}

static void instrument_native_roots(llvm::Function &function, llvm::Module &module) {
  auto &ctx = module.getContext();
  auto *word = llvm::Type::getInt64Ty(ctx);
  const auto root_type = [](llvm::Type *type) {
    return type->isPointerTy() || type->isIntegerTy(64) || type->isDoubleTy();
  };
  auto *body_entry = &function.getEntryBlock();
  std::vector<llvm::Instruction *> values;
  std::vector<llvm::AllocaInst *> locals;
  std::vector<llvm::AllocaInst *> binding_cells;
  std::vector<llvm::ReturnInst *> returns;
  std::vector<llvm::Instruction *> backedges;
  std::unordered_map<llvm::BasicBlock *, std::size_t> order;
  for (auto &block : function) order[&block] = order.size();
  for (auto &block : function) {
    for (auto &instruction : block) {
      if (auto *local = llvm::dyn_cast<llvm::AllocaInst>(&instruction)) {
        if (&block == body_entry && local->isStaticAlloca()) {
          if (root_type(local->getAllocatedType())) locals.push_back(local);
          else if (local->getMetadata("nil.binding.cell")) binding_cells.push_back(local);
        }
      } else if (root_type(instruction.getType())) {
        values.push_back(&instruction);
      }
      if (auto *ret = llvm::dyn_cast<llvm::ReturnInst>(&instruction)) returns.push_back(ret);
    }
    // Every cycle has a backward edge in any total block ordering, including
    // irreducible hand-written NIL. Dominator-only loop detection misses those.
    for (auto *successor : llvm::successors(&block)) {
      if (order.at(successor) <= order.at(&block)) {
        backedges.push_back(block.getTerminator());
        break;
      }
    }
  }
  auto *entry = llvm::BasicBlock::Create(ctx, "ny.gc.entry", &function, body_entry);
  llvm::IRBuilder<> setup(entry);
  std::vector<llvm::Value *> slots;
  for (auto *local : locals) {
    local->moveBefore(*entry, entry->end());
    setup.CreateStore(llvm::Constant::getNullValue(local->getAllocatedType()), local);
    slots.push_back(local);
  }
  // A checked binding cell is exactly one word-sized value field. Publish
  // that stack slot itself: retaining the cell address cannot trace values
  // written through a caller reference, because the cell is not a heap object.
  for (auto *cell : binding_cells) {
    cell->moveBefore(*entry, entry->end());
    setup.CreateStore(llvm::Constant::getNullValue(cell->getAllocatedType()), cell);
    slots.push_back(cell);
  }
  const auto bits = [&](llvm::IRBuilder<> &builder, llvm::Value *value) -> llvm::Value * {
    if (value->getType()->isPointerTy()) return builder.CreatePtrToInt(value, word);
    if (value->getType()->isDoubleTy()) return builder.CreateBitCast(value, word);
    return value;
  };
  const auto slot = [&]() {
    auto *address = setup.CreateAlloca(word, nullptr, "ny.gc.word");
    setup.CreateStore(llvm::ConstantInt::get(word, 0), address);
    slots.push_back(address);
    return address;
  };
  for (auto &argument : function.args()) {
    if (root_type(argument.getType())) {
      auto *address = slot();
      setup.CreateStore(bits(setup, &argument), address);
    }
  }
  for (auto *value : values) {
    auto *address = slot();
    llvm::IRBuilder<> store(ctx);
    if (llvm::isa<llvm::PHINode>(value))
      store.SetInsertPoint(&*value->getParent()->getFirstInsertionPt());
    else
      store.SetInsertPoint(value->getNextNode());
    store.CreateStore(bits(store, value), address);
  }
  auto *frame = emit_root_frame(module, setup, slots);
  auto safepoint = module.getOrInsertFunction("nyrt_safepoint",
      llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), {}, false));
  setup.CreateCall(safepoint);
  setup.CreateBr(body_entry);
  for (auto *edge : backedges) {
    llvm::IRBuilder<> at(edge);
    at.CreateCall(safepoint);
  }
  auto leave = module.getOrInsertFunction("nyrt_roots_leave",
      llvm::FunctionType::get(llvm::Type::getVoidTy(ctx),
          {llvm::PointerType::getUnqual(ctx)}, false));
  for (auto *ret : returns) {
    llvm::IRBuilder<> at(ret);
    at.CreateCall(leave, {frame});
  }
}

static void instrument_native_fault_frame(llvm::Function &function, llvm::Module &module) {
  auto &ctx = module.getContext();
  auto *word = llvm::Type::getInt64Ty(ctx);
  auto enter = module.getOrInsertFunction("nyrt_fault_v1_frame_enter",
      llvm::FunctionType::get(word, {}, false));
  auto leave = module.getOrInsertFunction("nyrt_fault_v1_frame_leave",
      llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), {word}, false));
  llvm::IRBuilder<> entry(&*function.getEntryBlock().getFirstInsertionPt());
  auto *identity = entry.CreateCall(enter, {}, "ny.fault.frame");
  for (auto &block : function)
    if (auto *ret = llvm::dyn_cast_or_null<llvm::ReturnInst>(block.getTerminator())) {
      llvm::IRBuilder<> at(ret);
      at.CreateCall(leave, {identity});
    }
}

static void instrument_native_owned_frame(llvm::Function &function, llvm::Module &module) {
  auto &ctx = module.getContext();
  auto *word = llvm::Type::getInt64Ty(ctx);
  auto depth = module.getOrInsertFunction("nyrt_owned_v1_depth", llvm::FunctionType::get(word, {}, false));
  auto unwind = module.getOrInsertFunction("nyrt_owned_v1_unwind",
      llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), {word}, false));
  llvm::IRBuilder<> entry(&*function.getEntryBlock().getFirstInsertionPt());
  auto *saved = entry.CreateCall(depth, {}, "ny.owned.frame.depth");
  for (auto &block : function)
    if (auto *ret = llvm::dyn_cast_or_null<llvm::ReturnInst>(block.getTerminator())) {
      llvm::IRBuilder<> at(ret);
      at.CreateCall(unwind, {saved});
    }
}

struct NativeCodegen::Impl {
  std::unique_ptr<llvm::LLVMContext> context;
  std::unique_ptr<llvm::Module> module;
  std::unique_ptr<llvm::IRBuilder<>> builder;
  std::unique_ptr<llvm::orc::LLJIT> jit;
  std::unique_ptr<llvm::TargetMachine> target_machine;

  // Optimization
  std::unique_ptr<llvm::legacy::FunctionPassManager> fpm;

  MemoryMode memory_mode = MemoryMode::UNSAFE_MANUAL;
  bool embed_context = false;
  llvm::Function *current_function = nullptr;

  // State matching generateModule usage
  std::vector<llvm::Value *> value_stack;
  std::unordered_map<std::string, llvm::Value *> string_constants_map;
  std::unordered_map<std::string, llvm::AllocaInst *> locals_map;
  std::unordered_map<std::string, std::string> local_nil_types;
  std::unordered_map<const llvm::Value *, std::string> value_nil_types;
  std::unordered_map<std::string, llvm::GlobalVariable *> globals_map;
  std::unordered_map<std::string, std::string> global_nil_types;
  std::unordered_map<std::string, llvm::GlobalVariable *> global_initialized;
  std::unordered_map<std::string, llvm::BasicBlock *> label_map;

  // Control flow - Added these earlier, need to keep them if they were used
  struct LoopInfo {
    llvm::BasicBlock *cond_block;
    llvm::BasicBlock *end_block;
  };
  std::vector<LoopInfo> loop_stack;

  const nil_module *current_mod = nullptr; // For function lookup

  // Helpers
  void setNilType(const llvm::Value *value, const std::string &type) {
    if (value && !type.empty())
      value_nil_types[value] = compact_nil_type(type);
  }

  std::string nilType(const llvm::Value *value) const {
    const auto type = value_nil_types.find(value);
    return type == value_nil_types.end() ? std::string{} : type->second;
  }

  void pushValue(llvm::Value *v, const std::string &type = {}) {
    if (v && !type.empty() && !nilType(v).empty() &&
        nilType(v) != compact_nil_type(type)) {
      // Constants are interned and no-op casts may reuse an existing SSA
      // value. A different Ny type belongs to this occurrence, not every
      // pending occurrence of the same LLVM value on other control-flow
      // edges. Give the retyped value its own identity without changing bits.
      v = builder->CreateFreeze(v, "ny.type.identity");
    }
    setNilType(v, type);
    value_stack.push_back(v);
  }
  llvm::Value *popValue() {
    if (value_stack.empty()) {
      return nullptr;
    }
    auto v = value_stack.back();
    value_stack.pop_back();
    return v;
  }

  llvm::Value *castInteger(llvm::Value *value, llvm::Type *target) {
    const bool is_boolean = value->getType()->isIntegerTy(1);
    return builder->CreateIntCast(value, target, !is_boolean);
  }

  llvm::Value *coerceToDouble(llvm::Value *value) {
    if (!value)
      return nullptr;
    auto *double_type = llvm::Type::getDoubleTy(*context);
    if (value->getType() == double_type)
      return value;
    if (value->getType()->isFloatTy())
      return builder->CreateFPExt(value, double_type);
    if (value->getType()->isIntegerTy(1))
      return builder->CreateUIToFP(value, double_type);
    if (value->getType()->isIntegerTy())
      return builder->CreateSIToFP(value, double_type);
    return nullptr;
  }

  // Ensure both operands have the same type for numeric operations. Mixed
  // integer/floating expressions use f64, matching the interpreter's numeric
  // promotion rules.
  void ensureSymmetricTypes(llvm::Value *&lhs, llvm::Value *&rhs) {
    if (!lhs || !rhs)
      return;
    auto *lType = lhs->getType();
    auto *rType = rhs->getType();
    if (lType == rType)
      return;

    if ((lType->isFloatingPointTy() || rType->isFloatingPointTy()) &&
        (lType->isFloatingPointTy() || lType->isIntegerTy()) &&
        (rType->isFloatingPointTy() || rType->isIntegerTy())) {
      lhs = coerceToDouble(lhs);
      rhs = coerceToDouble(rhs);
      return;
    }

    if (lType->isIntegerTy() && rType->isIntegerTy()) {
      unsigned lWidth = lType->getIntegerBitWidth();
      unsigned rWidth = rType->getIntegerBitWidth();
      if (lWidth < rWidth) {
        lhs = castInteger(lhs, rType);
      } else if (rWidth < lWidth) {
        rhs = castInteger(rhs, lType);
      }
    }
  }

  llvm::Value *coerceToI64(llvm::Value *value) {
    if (!value)
      return nullptr;
    auto *i64_type = llvm::Type::getInt64Ty(*context);
    if (value->getType() == i64_type)
      return value;
    if (value->getType()->isIntegerTy())
      return castInteger(value, i64_type);
    return nullptr;
  }

  llvm::Value *truthy(llvm::Value *value) {
    if (!value)
      return nullptr;
    auto *type = value->getType();
    if (type->isIntegerTy(1))
      return value;
    if (type->isIntegerTy())
      return builder->CreateICmpNE(value, llvm::ConstantInt::get(type, 0));
    if (type->isFloatingPointTy())
      return builder->CreateFCmpUNE(value, llvm::ConstantFP::get(type, 0.0));
    if (type->isPointerTy())
      return builder->CreateICmpNE(
          value,
          llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(type)));
    return nullptr;
  }

  llvm::Value *get_or_add_const_string(const std::string &str) {
    if (string_constants_map.find(str) != string_constants_map.end()) {
      return string_constants_map[str];
    }
    auto *str_val = llvm::ConstantDataArray::getString(*context, str, true);
    auto *global_var = new llvm::GlobalVariable(
        *module, str_val->getType(), true, llvm::GlobalValue::PrivateLinkage,
        str_val, ".str");
    global_var->setAlignment(llvm::MaybeAlign(1));

    std::vector<llvm::Constant *> indices;
    indices.push_back(llvm::ConstantInt::get(*context, llvm::APInt(32, 0)));
    indices.push_back(llvm::ConstantInt::get(*context, llvm::APInt(32, 0)));

    auto *const_ptr = llvm::ConstantExpr::getInBoundsGetElementPtr(
        str_val->getType(), global_var, indices);

    string_constants_map[str] = const_ptr;
    return const_ptr;
  }

  Impl() {
    // Basic init, wait for initialize() for the rest
    context = std::make_unique<llvm::LLVMContext>();
    module = std::make_unique<llvm::Module>("native_module", *context);
    builder = std::make_unique<llvm::IRBuilder<>>(*context);
  }
};

NativeCodegen::NativeCodegen() : impl_(std::make_unique<Impl>()) {}
NativeCodegen::~NativeCodegen() = default;

void NativeCodegen::setMemoryMode(MemoryMode mode) {
  impl_->memory_mode = mode;
}
MemoryMode NativeCodegen::getMemoryMode() const { return impl_->memory_mode; }

void NativeCodegen::setEmbedContext(bool enabled) {
  impl_->embed_context = enabled;
}

bool NativeCodegen::initialize() {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  // Resolve and own the host target independently of ORC. Pure AOT emission
  // must remain available on hosts that cannot create an executable JIT
  // session, and the module must have its final target layout before any IR is
  // generated.
  const std::string target_triple = llvm::sys::getDefaultTargetTriple();
  std::string target_error;
  const llvm::Target *target =
      llvm::TargetRegistry::lookupTarget(target_triple, target_error);
  if (target == nullptr) {
    std::cerr << "[NativeCodegen] Target lookup failed: " << target_error
              << std::endl;
    return false;
  }
  llvm::TargetOptions target_options;
  // LLVM 21 uses Triple objects where earlier versions accepted strings.
#if LLVM_VERSION_MAJOR >= 21
  const llvm::Triple target_spec(target_triple);
#else
  const auto &target_spec = target_triple;
#endif
  auto target_machine = std::unique_ptr<llvm::TargetMachine>(
      target->createTargetMachine(target_spec, "generic", "", target_options,
                                  std::optional<llvm::Reloc::Model>()));
  if (!target_machine) {
    std::cerr << "[NativeCodegen] Failed to create target machine for '"
              << target_triple << "'" << std::endl;
    return false;
  }

  // Create Context & Module. Destroy the old module/builder before their
  // context, as they retain references to it.
  impl_->fpm.reset();
  impl_->builder.reset();
  impl_->module.reset();
  impl_->jit.reset();

  impl_->context = std::make_unique<llvm::LLVMContext>();
  impl_->module = std::make_unique<llvm::Module>("ny_native", *impl_->context);
  impl_->module->setTargetTriple(target_spec);
  impl_->module->setDataLayout(target_machine->createDataLayout());
  impl_->target_machine = std::move(target_machine);

  // Initialize Optimization Passes
  impl_->fpm =
      std::make_unique<llvm::legacy::FunctionPassManager>(impl_->module.get());
  // impl_->fpm->add(llvm::createInstructionCombiningPass());
  // impl_->fpm->add(llvm::createReassociatePass());
  // impl_->fpm->add(llvm::createGVNPass());
  // impl_->fpm->add(llvm::createCFGSimplificationPass());
  impl_->fpm->doInitialization();

  diag_debug("[llvm] Initialized host target with O2 optimizations");
  return true;
}

bool NativeCodegen::generateModule(const nil_module &mod) {
  diag_debug("[llvm] Generating module; structs=" +
             std::to_string(mod.structs.size()));
  if (!impl_->context || !impl_->module)
    return false;

  std::vector<std::uint8_t> context_manifest;
  if (impl_->embed_context) {
    const llvm::Triple target(impl_->module->getTargetTriple());
    const auto &layout = impl_->module->getDataLayout();
    if (!target.isOSLinux() || !target.isOSBinFormatELF() ||
        target.getArch() != llvm::Triple::x86_64 ||
        layout.getPointerSize() != 8 || !layout.isLittleEndian()) {
      std::cerr << "[NativeCodegen] --embed-context requires a Linux ELF64 "
                   "little-endian x86_64 target\n";
      return false;
    }
    try {
      context_manifest = encode_context_manifest(mod);
    } catch (const std::exception &error) {
      std::cerr << "[NativeCodegen] invalid executable context: "
                << error.what() << '\n';
      return false;
    }
  }

  impl_->current_mod = &mod;

  std::unordered_set<std::string> parallel_workers;
  try {
    parallel_workers = collect_nil_parallel_workers(mod);
    validate_nil_owned_contract(mod);
  } catch (const std::exception &error) {
    std::cerr << "[NativeCodegen] " << error.what() << '\n';
    return false;
  }

  try {
    validate_nil_class_contract(mod);
  } catch (const std::exception &error) {
    std::cerr << "[NativeCodegen] invalid NIL class contract: " << error.what() << '\n';
    return false;
  }

  const auto reachable_functions_result =
      collect_native_reachable_functions(mod);
  if (!reachable_functions_result)
    return false;
  const auto &reachable_functions = *reachable_functions_result;
  bool fault_protocol_enabled = false;
  for (const auto *function : reachable_functions)
    for (const auto &instruction : function->body)
      if (instruction.opname.rfind("FAULT_", 0) == 0)
        fault_protocol_enabled = true;
  if (fault_protocol_enabled) {
    try {
      validate_nil_fault_contract(mod);
    } catch (const std::exception &error) {
      std::cerr << "[NativeCodegen] invalid NIL fault contract: " << error.what() << '\n';
      return false;
    }
  }

  // Generated helpers and runtime/libc declarations occupy fixed linker
  // symbols. Reject a reachable Ny definition before constructing any LLVM
  // declaration so a user function can never replace one with an incompatible
  // ABI (or make getOrInsertFunction return a conflicting callee).
  static const std::unordered_set<std::string> reserved_native_symbols = {
      "strlen", "memcpy", "malloc", "calloc", "free",   "printf",
      "strcmp", "tan",    "asin",   "acos",   "atan",   "atan2",
      "cbrt"};
  for (const nil_function *function : reachable_functions) {
    if (function == nullptr || function->name == "main" ||
        function->name == "__globals_init__") {
      continue;
    }
    if (function->name.rfind("__ny_", 0) == 0 ||
        function->name.rfind("nyrt_", 0) == 0 ||
        reserved_native_symbols.count(function->name) != 0) {
      std::cerr << "[NativeCodegen] reachable function name '"
                << function->name
                << "' is reserved by the native runtime ABI\n";
      return false;
    }
  }

  impl_->builder = std::make_unique<llvm::IRBuilder<>>(*impl_->context);
  auto &ctx = *impl_->context;
  auto *native_size_type = impl_->module->getDataLayout().getIntPtrType(ctx);
  const unsigned native_size_bits = native_size_type->getIntegerBitWidth();
  std::uint64_t native_size_max = std::numeric_limits<std::uint64_t>::max();
  if (native_size_bits < 64)
    native_size_max = (std::uint64_t{1} << native_size_bits) - 1;
  auto native_size_constant = [&](std::uint64_t value) -> llvm::ConstantInt * {
    return llvm::ConstantInt::get(ctx,
                                  llvm::APInt(native_size_bits, value, false));
  };

  auto get_runtime_failure_function = [&]() -> llvm::Function * {
    if (auto *existing = impl_->module->getFunction("nyrt_fail"))
      return existing;

    auto *pointer_type = llvm::PointerType::getUnqual(ctx);
    auto *failure_type = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx),
                                                 {pointer_type}, false);
    return llvm::Function::Create(failure_type, llvm::Function::ExternalLinkage,
                                  "nyrt_fail", *impl_->module);
  };

  // Generate __ny_string_concat(i8* s1, i8* s2) -> i8*
  //    (Helper for string concatenation)
  {
    llvm::FunctionType *concat_type = llvm::FunctionType::get(
        llvm::PointerType::getUnqual(ctx),
        {llvm::PointerType::getUnqual(ctx), llvm::PointerType::getUnqual(ctx)},
        false);
    auto *concat_fn =
        llvm::Function::Create(concat_type, llvm::Function::InternalLinkage,
                               "__ny_string_concat", *impl_->module);

    auto *saved_ip = impl_->builder->GetInsertBlock();
    auto *entry = llvm::BasicBlock::Create(ctx, "entry", concat_fn);
    impl_->builder->SetInsertPoint(entry);

    auto *s1 = concat_fn->getArg(0);
    auto *s2 = concat_fn->getArg(1);

    // Declare dependencies
    auto *strlen_fn = impl_->module->getFunction("strlen");
    if (!strlen_fn) {
      auto *ft = llvm::FunctionType::get(
          native_size_type, {llvm::PointerType::getUnqual(ctx)}, false);
      strlen_fn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage,
                                         "strlen", *impl_->module);
    }

    auto *memcpy_fn = impl_->module->getFunction("memcpy");
    if (!memcpy_fn) {
      // void* memcpy(void* dest, const void* src, size_t n)
      auto *ft = llvm::FunctionType::get(llvm::PointerType::getUnqual(ctx),
                                         {llvm::PointerType::getUnqual(ctx),
                                          llvm::PointerType::getUnqual(ctx),
                                          native_size_type},
                                         false);
      memcpy_fn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage,
                                         "memcpy", *impl_->module);
    }

    // len1 = strlen(s1)
    auto *len1 = impl_->builder->CreateCall(strlen_fn, {s1});
    // len2 = strlen(s2)
    auto *len2 = impl_->builder->CreateCall(strlen_fn, {s2});

    // total = len1 + len2 + 1, rejecting size_t wrap before allocation.
    auto *sum = impl_->builder->CreateAdd(len1, len2);
    auto *total = impl_->builder->CreateAdd(sum, native_size_constant(1));
    auto *length_overflow =
        impl_->builder->CreateOr(impl_->builder->CreateICmpULT(sum, len1),
                                 impl_->builder->CreateICmpULT(total, sum));
    auto *length_failure =
        llvm::BasicBlock::Create(ctx, "length.failure", concat_fn);
    auto *length_success =
        llvm::BasicBlock::Create(ctx, "length.success", concat_fn);
    impl_->builder->CreateCondBr(length_overflow, length_failure,
                                 length_success);

    impl_->builder->SetInsertPoint(length_failure);
    auto *length_message = impl_->builder->CreateGlobalString(
        "NIL native runtime error: string concat length overflow\n");
    impl_->builder->CreateCall(get_runtime_failure_function(),
                               {length_message});
    impl_->builder->CreateUnreachable();

    impl_->builder->SetInsertPoint(length_success);

    llvm::Value *new_str = nullptr;
    if (impl_->memory_mode == MemoryMode::SAFE_GC) {
      auto *runtime_type =
          llvm::FunctionType::get(llvm::PointerType::getUnqual(ctx),
                                  {llvm::Type::getInt64Ty(ctx)}, false);
      auto runtime =
          impl_->module->getOrInsertFunction("nyrt_alloc", runtime_type);
      auto *runtime_size = impl_->builder->CreateZExtOrTrunc(
          total, llvm::Type::getInt64Ty(ctx), "ny.concat.size");
      new_str = impl_->builder->CreateCall(runtime_type, runtime.getCallee(),
                                           {runtime_size});
    } else {
      auto *calloc_type =
          llvm::FunctionType::get(llvm::PointerType::getUnqual(ctx),
                                  {native_size_type, native_size_type}, false);
      auto calloc_fn =
          impl_->module->getOrInsertFunction("calloc", calloc_type);
      new_str = impl_->builder->CreateCall(calloc_type, calloc_fn.getCallee(),
                                           {native_size_constant(1), total});
    }

    auto *allocation_failure =
        llvm::BasicBlock::Create(ctx, "allocation.failure", concat_fn);
    auto *allocation_success =
        llvm::BasicBlock::Create(ctx, "allocation.success", concat_fn);
    impl_->builder->CreateCondBr(
        impl_->builder->CreateICmpEQ(
            new_str, llvm::ConstantPointerNull::get(
                         llvm::cast<llvm::PointerType>(new_str->getType()))),
        allocation_failure, allocation_success);

    impl_->builder->SetInsertPoint(allocation_failure);
    auto *allocation_message = impl_->builder->CreateGlobalString(
        "NIL native runtime error: allocation failed during string concat\n");
    impl_->builder->CreateCall(get_runtime_failure_function(),
                               {allocation_message});
    impl_->builder->CreateUnreachable();

    impl_->builder->SetInsertPoint(allocation_success);

    // memcpy(new_str, s1, len1)
    impl_->builder->CreateCall(memcpy_fn, {new_str, s1, len1});

    // dest_offset = new_str + len1
    auto *dest_offset = impl_->builder->CreateInBoundsGEP(
        llvm::Type::getInt8Ty(ctx), new_str, {len1});

    // memcpy(dest_offset, s2, len2)
    impl_->builder->CreateCall(memcpy_fn, {dest_offset, s2, len2}); // Or strcpy

    // Null terminate
    auto *null_pos = impl_->builder->CreateInBoundsGEP(
        llvm::Type::getInt8Ty(ctx), dest_offset, {len2});
    impl_->builder->CreateStore(llvm::ConstantInt::get(ctx, llvm::APInt(8, 0)),
                                null_pos);

    impl_->builder->CreateRet(new_str);

    if (saved_ip)
      impl_->builder->SetInsertPoint(saved_ip);
  }

  auto coerce_to_native_size = [&](llvm::Value *value) -> llvm::Value * {
    if (!value || !value->getType()->isIntegerTy())
      return nullptr;
    if (value->getType() == native_size_type)
      return value;
    return impl_->builder->CreateIntCast(value, native_size_type, false,
                                         "ny.size");
  };

  auto valid_native_size = [&](llvm::Value *value) -> llvm::Value * {
    value = impl_->coerceToI64(value);
    llvm::Value *valid = impl_->builder->CreateICmpSGE(
        value, llvm::ConstantInt::get(ctx, llvm::APInt(64, 0)));
    if (native_size_bits < 64) {
      valid = impl_->builder->CreateAnd(
          valid, impl_->builder->CreateICmpULE(
                     value, llvm::ConstantInt::get(
                                ctx, llvm::APInt(64, native_size_max, false))));
    }
    return valid;
  };

  auto native_value_has_enum_semantics = [&](llvm::Value *value) {
    return value &&
           find_nil_enum_definition(mod, impl_->nilType(value)) != nullptr;
  };

  auto native_value_has_nominal_or_handle_semantics = [&](llvm::Value *value) {
    if (!value)
      return false;
    const std::string nil_type = impl_->nilType(value);
    return value->getType()->isPointerTy() ||
           native_handle_kind_for_nil_type(nil_type) !=
               native_handle_kind::none ||
           is_native_aggregate_nil_type(mod, nil_type) ||
           native_value_has_enum_semantics(value);
  };

  auto native_value_is_numeric_scalar = [&](llvm::Value *value) {
    return value && !native_value_has_nominal_or_handle_semantics(value) &&
           (value->getType()->isIntegerTy() ||
            value->getType()->isFloatingPointTy());
  };

  auto native_values_are_matching_raw_pointers = [&](llvm::Value *lhs,
                                                     llvm::Value *rhs) {
    if (!lhs || !rhs || !lhs->getType()->isPointerTy() ||
        !rhs->getType()->isPointerTy()) {
      return false;
    }

    const std::string lhs_type = normalize_nil_type(impl_->nilType(lhs));
    const std::string rhs_type = normalize_nil_type(impl_->nilType(rhs));
    return native_handle_kind_for_nil_type(lhs_type) ==
               native_handle_kind::integer_pointer &&
           native_handle_kind_for_nil_type(rhs_type) ==
               native_handle_kind::integer_pointer &&
           lhs_type == rhs_type;
  };

  auto reject_native_comparison_operands = [&](llvm::Value *lhs,
                                               llvm::Value *rhs,
                                               const std::string &opcode,
                                               const std::string &function_name,
                                               int debug_line) {
    if (native_values_are_matching_raw_pointers(lhs, rhs))
      return false;

    if (!native_value_has_nominal_or_handle_semantics(lhs) &&
        !native_value_has_nominal_or_handle_semantics(rhs)) {
      return false;
    }

    const bool enum_operand = native_value_has_enum_semantics(lhs) ||
                              native_value_has_enum_semantics(rhs);
    const auto lhs_kind = native_handle_kind_for_nil_type(impl_->nilType(lhs));
    const auto rhs_kind = native_handle_kind_for_nil_type(impl_->nilType(rhs));
    std::cerr << "[NativeCodegen] ";
    if (enum_operand) {
      std::cerr << "enum-valued comparisons are not implemented for " << opcode;
    } else if (lhs_kind == native_handle_kind::integer_pointer ||
               rhs_kind == native_handle_kind::integer_pointer) {
      std::cerr << opcode
                << " raw-pointer operands must have identical exact "
                   "ptr<i32>, ptr<i64>, ptr<bool>, or ptr<u8> metadata; "
                   "got '"
                << impl_->nilType(lhs) << "' and '" << impl_->nilType(rhs)
                << "'";
    } else {
      std::cerr << "pointer-valued comparisons are not implemented for "
                << opcode;
    }
    std::cerr << " in function '" << function_name << "'";
    if (debug_line > 0)
      std::cerr << " at Ny source line " << debug_line;
    std::cerr << '\n';
    return true;
  };

  auto native_value_type_description = [&](llvm::Value *value) {
    if (!value)
      return std::string("<missing>");
    const std::string nil_type = impl_->nilType(value);
    if (!nil_type.empty())
      return nil_type;
    return value->getType()->isPointerTy() ? std::string("<untyped pointer>")
                                           : std::string("<missing metadata>");
  };

  auto require_native_builtin_pointer = [&](llvm::Value *value,
                                            const std::string &builtin,
                                            const std::string &function_name) {
    const std::string nil_type =
        value ? normalize_nil_type(impl_->nilType(value)) : std::string{};
    const bool has_pointer_representation =
        value && value->getType()->isPointerTy();
    const bool valid =
        has_pointer_representation &&
        (builtin == "free" ? native_handle_kind_for_nil_type(nil_type) ==
                                 native_handle_kind::integer_pointer
                           : nil_type == "ptr<i32>");
    if (valid)
      return true;

    std::cerr << "[NativeCodegen] builtin '" << builtin << "' requires ";
    if (builtin == "free") {
      std::cerr << "exact raw-pointer metadata ptr<i32>, ptr<i64>, ptr<bool>, "
                   "or ptr<u8>";
    } else {
      std::cerr << "exact raw-pointer metadata ptr<i32>";
    }
    std::cerr << "; got '" << native_value_type_description(value)
              << "' in function '" << function_name << "'\n";
    return false;
  };

  auto emit_allocation_null_check = [&](llvm::Value *pointer,
                                        const std::string &operation) {
    auto *current_function = impl_->builder->GetInsertBlock()->getParent();
    auto *failure_block =
        llvm::BasicBlock::Create(ctx, "allocation.failure", current_function);
    auto *success_block =
        llvm::BasicBlock::Create(ctx, "allocation.success", current_function);
    auto *null_pointer = llvm::ConstantPointerNull::get(
        llvm::cast<llvm::PointerType>(pointer->getType()));
    impl_->builder->CreateCondBr(
        impl_->builder->CreateICmpEQ(pointer, null_pointer), failure_block,
        success_block);

    impl_->builder->SetInsertPoint(failure_block);
    auto *message = impl_->builder->CreateGlobalString(
        "NIL native runtime error: allocation failed during " + operation +
        "\n");
    impl_->builder->CreateCall(get_runtime_failure_function(), {message});
    impl_->builder->CreateUnreachable();

    impl_->builder->SetInsertPoint(success_block);
  };

  auto native_pointer_storage_type =
      [&](const std::string &element_type) -> llvm::Type * {
    const std::string normalized = normalize_nil_type(element_type);
    if (normalized == "u8" || normalized == "bool")
      return llvm::Type::getInt8Ty(ctx);
    if (normalized == "i32")
      return llvm::Type::getInt32Ty(ctx);
    if (normalized == "i64")
      return llvm::Type::getInt64Ty(ctx);
    return nullptr;
  };

  // Convert a typed Ny value to the stable word accepted by runtime entry
  // points. f64 travels as exact bits and text as a proven text address.
  // Metadata is mandatory: an arbitrary pointer cannot masquerade as text.
  auto native_storage_word =
      [&](llvm::Value *value, const std::string &element_type,
          const std::string &operation) -> llvm::Value * {
    if (!value)
      return nullptr;
    const std::string normalized = normalize_nil_type(element_type);
    auto *i64_type = llvm::Type::getInt64Ty(ctx);
    if (normalized == "anything") {
      if (!value->getType()->isPointerTy() ||
          impl_->nilType(value) != "anything") {
        std::cerr << "[NativeCodegen] " << operation
                  << " requires a checked anything box\n";
        return nullptr;
      }
      return impl_->builder->CreatePtrToInt(value, i64_type, "nil.anything.bits");
    }
    if (is_array_tuple_type(element_type)) {
      if (!value->getType()->isPointerTy() ||
          canonical_array_type(impl_->nilType(value)) != canonical_array_type(element_type) ||
          !is_native_aggregate_nil_type(mod, element_type)) {
        std::cerr << "[NativeCodegen] " << operation
                  << " requires an exact tuple element of type '" << element_type << "'\n";
        return nullptr;
      }
      return impl_->builder->CreatePtrToInt(value, i64_type, "ny.array.tuple.bits");
    }
    if (normalized == "f64") {
      if (normalize_nil_type(impl_->nilType(value)) != "f64") {
        std::cerr << "[NativeCodegen] " << operation
                  << " requires an exact f64 element\n";
        return nullptr;
      }
      if (value->getType()->isDoubleTy())
        return impl_->builder->CreateBitCast(value, i64_type, "ny.array.f64.bits");
      // Ordinary Ny function parameters use the established word ABI.
      return value->getType()->isIntegerTy(64) ? value : nullptr;
    }
    if (normalized == "text" || normalized == "string" || normalized == "str") {
      if (!value->getType()->isPointerTy() ||
          native_handle_kind_for_nil_type(impl_->nilType(value)) !=
              native_handle_kind::text) {
        std::cerr << "[NativeCodegen] " << operation
                  << " requires an exact text element\n";
        return nullptr;
      }
      return impl_->builder->CreatePtrToInt(value, i64_type, "ny.array.text.bits");
    }
    if (!value->getType()->isIntegerTy() ||
        !native_value_is_numeric_scalar(value) ||
        normalize_nil_type(impl_->nilType(value)) == "f64") {
      std::cerr << "[NativeCodegen] " << operation
                << " requires an integer element for '" << element_type << "'\n";
      return nullptr;
    }

    if (normalized == "bool") {
      auto runtime = impl_->module->getOrInsertFunction("nyrt_bool_i64",
          llvm::FunctionType::get(i64_type, {i64_type}, false));
      return impl_->builder->CreateCall(runtime,
          {impl_->castInteger(value, i64_type)}, "nil.checked.bool");
    }

    llvm::Value *word = value;
    if (value->getType() != i64_type) {
      const bool zero_extend = normalized == "u8";
      word = impl_->builder->CreateIntCast(value, i64_type, !zero_extend,
                                           "ny.pointer.word");
    }

    if (normalized == "u8")
      return impl_->builder->CreateAnd(
          word, llvm::ConstantInt::get(ctx, llvm::APInt(64, 255, false)),
          "ny.pointer.u8.word");

    if (normalized == "i32") {
      auto *narrow = impl_->builder->CreateTruncOrBitCast(
          word, llvm::Type::getInt32Ty(ctx), "ny.pointer.i32");
      return impl_->builder->CreateSExt(narrow, i64_type,
                                        "ny.pointer.i32.word");
    }
    return word;
  };

  auto native_array_loaded_value =
      [&](llvm::Value *word, const std::string &element_type) -> llvm::Value * {
    const auto kind = native_array_kind_for_element(element_type);
    if (kind == NYRT_ARRAY_TUPLE || kind == NYRT_ARRAY_ANYTHING)
      return impl_->builder->CreateIntToPtr(
          word, llvm::PointerType::getUnqual(ctx), "ny.array.tuple.value");
    if (kind == NYRT_ARRAY_F64)
      return impl_->builder->CreateBitCast(
          word, llvm::Type::getDoubleTy(ctx), "ny.array.f64.value");
    if (kind == NYRT_ARRAY_TEXT) {
      // Match typed default reads: a zero slot/out-of-bounds result is empty
      // text, never a null C string passed into text operations.
      auto *pointer = impl_->builder->CreateIntToPtr(
          word, llvm::PointerType::getUnqual(ctx), "ny.array.text.value");
      return impl_->builder->CreateSelect(
          impl_->builder->CreateICmpEQ(word, llvm::ConstantInt::get(word->getType(), 0)),
          impl_->builder->CreateGlobalString(""), pointer, "ny.array.text.default");
    }
    return word;
  };

  auto native_enum_payload_type_supported = [&](const std::string &type) {
    const auto kind = native_handle_kind_for_nil_type(type);
    return kind == native_handle_kind::text ||
           kind == native_handle_kind::anything ||
           kind == native_handle_kind::scalar_array ||
           kind == native_handle_kind::integer_pointer ||
           is_native_aggregate_nil_type(mod, type) ||
           find_nil_enum_definition(mod, type) != nullptr ||
           (is_native_scalar_abi_type(type) &&
            normalize_nil_type(type) != "f32");
  };

  auto native_enum_payload_compatible =
      [&](llvm::Value *value, const std::string &expected) {
    if (!value || !native_enum_payload_type_supported(expected))
      return false;
    const std::string actual = impl_->nilType(value);
    if (is_native_aggregate_nil_type(mod, expected))
      return value->getType()->isPointerTy() &&
             native_aggregate_types_compatible(mod, actual, expected);
    if (const auto *definition = find_nil_enum_definition(mod, expected))
      return value->getType()->isIntegerTy(64) &&
             find_nil_enum_definition(mod, actual) == definition;
    const auto kind = native_handle_kind_for_nil_type(expected);
    if (kind != native_handle_kind::none)
      return value->getType()->isPointerTy() &&
             native_handle_types_compatible(actual, expected);
    if (normalize_nil_type(expected) == "f64")
      return value->getType()->isDoubleTy() &&
             normalize_nil_type(actual) == "f64";
    return value->getType()->isIntegerTy() &&
           native_value_is_numeric_scalar(value);
  };

  auto emit_single_payload_variant =
      [&](const std::string &tag, llvm::Value *payload,
          const std::string &enum_type,
          const std::string &payload_type) -> llvm::Value * {
    if (!payload)
      return nullptr;
    llvm::Value *pointer = nullptr;
    if (impl_->memory_mode == MemoryMode::SAFE_GC) {
      auto *runtime_type =
          llvm::FunctionType::get(llvm::PointerType::getUnqual(ctx),
                                  {llvm::Type::getInt64Ty(ctx)}, false);
      auto runtime =
          impl_->module->getOrInsertFunction("nyrt_alloc", runtime_type);
      pointer = impl_->builder->CreateCall(
          runtime_type, runtime.getCallee(),
          {llvm::ConstantInt::get(ctx, llvm::APInt(64, 24, false))});
    } else {
      auto *malloc_type = llvm::FunctionType::get(
          llvm::PointerType::getUnqual(ctx), {native_size_type}, false);
      auto malloc_fn =
          impl_->module->getOrInsertFunction("malloc", malloc_type);
      pointer = impl_->builder->CreateCall(malloc_type, malloc_fn.getCallee(),
                                           {native_size_constant(24)});
    }
    emit_allocation_null_check(pointer, "enum variant");
    auto *pointer_i64 =
        impl_->builder->CreatePtrToInt(pointer, llvm::Type::getInt64Ty(ctx));

    auto *tag_pointer = impl_->builder->CreateIntToPtr(
        pointer_i64, llvm::PointerType::getUnqual(ctx));
    impl_->builder->CreateStore(
        impl_->get_or_add_const_string(canonicalize_variant_name(tag)),
        tag_pointer);

    auto *count_address = impl_->builder->CreateAdd(
        pointer_i64, llvm::ConstantInt::get(ctx, llvm::APInt(64, 8)));
    auto *count_pointer = impl_->builder->CreateIntToPtr(
        count_address,
        llvm::PointerType::getUnqual(llvm::Type::getInt64Ty(ctx)));
    impl_->builder->CreateStore(llvm::ConstantInt::get(ctx, llvm::APInt(64, 1)),
                                count_pointer);

    llvm::Value *payload_word = payload;
    if (payload->getType()->isPointerTy()) {
      payload_word =
          impl_->builder->CreatePtrToInt(payload, llvm::Type::getInt64Ty(ctx));
    } else if (payload->getType()->isIntegerTy()) {
      const unsigned width = payload->getType()->getIntegerBitWidth();
      if (width < 64) {
        const std::string normalized_payload = normalize_nil_type(payload_type);
        const bool unsigned_payload =
            normalized_payload == "bool" || normalized_payload == "u8" ||
            normalized_payload == "u16" || normalized_payload == "u32";
        payload_word = unsigned_payload
                           ? impl_->builder->CreateZExt(
                                 payload, llvm::Type::getInt64Ty(ctx))
                           : impl_->builder->CreateSExt(
                                 payload, llvm::Type::getInt64Ty(ctx));
      } else if (width > 64) {
        payload_word =
            impl_->builder->CreateTrunc(payload, llvm::Type::getInt64Ty(ctx));
      }
    } else if (payload->getType()->isDoubleTy() &&
               normalize_nil_type(payload_type) == "f64") {
      payload_word =
          impl_->builder->CreateBitCast(payload, llvm::Type::getInt64Ty(ctx));
    } else {
      return nullptr;
    }

    auto *payload_address = impl_->builder->CreateAdd(
        pointer_i64, llvm::ConstantInt::get(ctx, llvm::APInt(64, 16)));
    auto *payload_pointer = impl_->builder->CreateIntToPtr(
        payload_address,
        llvm::PointerType::getUnqual(llvm::Type::getInt64Ty(ctx)));
    impl_->builder->CreateStore(payload_word, payload_pointer);
    impl_->setNilType(pointer_i64, enum_type);
    return pointer_i64;
  };

  auto array_kind_constant =
      [&](const std::string &array_type) -> llvm::ConstantInt * {
    const auto element = native_generic_argument(array_type, "array");
    const auto kind =
        element ? native_array_kind_for_element(*element) : std::nullopt;
    if (!kind)
      return nullptr;
    return llvm::ConstantInt::get(ctx, llvm::APInt(32, *kind, false));
  };

  auto array_length_value = [&](llvm::Value *array_pointer) -> llvm::Value * {
    auto *kind = array_kind_constant(impl_->nilType(array_pointer));
    if (!kind)
      return nullptr;
    auto type = llvm::FunctionType::get(
        llvm::Type::getInt64Ty(ctx),
        {llvm::PointerType::getUnqual(ctx), llvm::Type::getInt32Ty(ctx)},
        false);
    auto function =
        impl_->module->getOrInsertFunction("nyrt_array_length", type);
    return impl_->builder->CreateCall(type, function.getCallee(),
                                      {array_pointer, kind}, "ny.array.length");
  };

  // 0. Process only string constants referenced by reachable code. Imported
  // dead functions must not make an otherwise valid native executable fail,
  // while a reachable missing/ambiguous reference remains a hard error.
  impl_->string_constants_map.clear();
  std::unordered_set<std::string> referenced_string_constants;
  for (const auto &function : mod.functions) {
    if (reachable_functions.count(&function) == 0)
      continue;
    for (const auto &instruction : function.body) {
      if (stringToOpcode(instruction.opname) != Opcode::PUSH_CONST_STR ||
          instruction.args.empty()) {
        continue;
      }
      // PUSH_CONST_STR names a constant in every NIL consumer. Quotes are
      // encoding syntax, not an alternate native-only literal instruction.
      referenced_string_constants.insert(instruction.args[0]);
    }
  }

  for (const auto &kv : mod.string_constants) {
    const std::string &name = kv.first;
    if (referenced_string_constants.count(name) == 0)
      continue;
    if (impl_->string_constants_map.count(name) != 0) {
      std::cerr << "[NativeCodegen] reachable text constant reference '" << name
                << "' is ambiguous\n";
      return false;
    }
    const std::string &val = kv.second;
    // Constants are semantic bytes, not source spelling: the lexer and NIL
    // readers have already decoded escapes. Decoding again changes literal
    // backslashes ("\\\\n", for example) into different program data.
    if (val.find('\0') != std::string::npos) {
      std::cerr << "[NativeCodegen] text constant '" << name
                << "' contains an embedded NUL byte; the native text ABI "
                   "requires NUL-free values\n";
      return false;
    }
    auto *val_ptr = impl_->builder->CreateGlobalString(val, name, 0,
                                                       impl_->module.get());
    impl_->string_constants_map[name] = val_ptr;
  }
  for (const auto &reference : referenced_string_constants) {
    if (impl_->string_constants_map.count(reference) == 0) {
      std::cerr << "[NativeCodegen] reachable text constant reference '"
                << reference << "' is undefined\n";
      return false;
    }
  }

  // NIL globals are initialized by the generated __globals_init__ function.
  // Only materialize globals read by reachable code. Imported modules can
  // contribute constants whose initializer types are outside the deliberately
  // bounded native-global ABI; an unread STORE_GLOBAL still evaluates and
  // discards its initializer, preserving side effects without making dead
  // data constrain the executable.
  //
  // Typed accesses carry their storage contract as a trailing NIL operand.
  // Historical one-operand integer globals retain their i64 ABI. In particular,
  // an opaque pointer is never accepted as text without exact text metadata.
  impl_->globals_map.clear();
  impl_->global_nil_types.clear();
  impl_->global_initialized.clear();
  for (const auto &fn : mod.functions) {
    if (reachable_functions.count(&fn) == 0)
      continue;
    for (const auto &instr : fn.body) {
      const Opcode op = stringToOpcode(instr.opname);
      if (op != Opcode::LOAD_GLOBAL && op != Opcode::STORE_GLOBAL)
        continue;
      if (instr.args.empty() || instr.args.size() > 2 ||
          instr.args[0].empty() ||
          (instr.args.size() == 2 &&
           !is_typed_global_storage_type(instr.args[1]))) {
        std::cerr << "[NativeCodegen] invalid global storage metadata\n";
        return false;
      }
      const auto type = instr.args.size() == 2 ? instr.args[1] : "";
      const auto inserted = impl_->global_nil_types.emplace(instr.args[0], type);
      if (!inserted.second && inserted.first->second != type) {
        std::cerr << "[NativeCodegen] global '" << instr.args[0]
                  << "' has conflicting or missing storage type metadata\n";
        return false;
      }
    }
  }
  for (const auto &fn : mod.functions) {
    if (reachable_functions.count(&fn) == 0)
      continue;
    for (const auto &instr : fn.body) {
      const Opcode op = stringToOpcode(instr.opname);
      if (op != Opcode::LOAD_GLOBAL || instr.args.empty()) {
        continue;
      }

      const std::string &name = instr.args[0];
      if (impl_->globals_map.count(name))
        continue;

      const auto &type = impl_->global_nil_types.at(name);
      llvm::Type *storage_type = type == "text" || type == "anything"
          ? static_cast<llvm::Type *>(llvm::PointerType::getUnqual(ctx))
          : type == "f64" ? llvm::Type::getDoubleTy(ctx)
                           : llvm::Type::getInt64Ty(ctx);
      auto *storage = new llvm::GlobalVariable(
          *impl_->module, storage_type, false,
          llvm::GlobalValue::InternalLinkage,
          llvm::Constant::getNullValue(storage_type), "ny.global." + name);
      impl_->globals_map[name] = storage;
      if (!type.empty()) {
        impl_->global_initialized[name] = new llvm::GlobalVariable(
            *impl_->module, llvm::Type::getInt1Ty(ctx), false,
            llvm::GlobalValue::InternalLinkage,
            llvm::ConstantInt::getFalse(ctx), "ny.global.initialized." + name);
      }
    }
  }

  // Native struct values are owning VM objects with reference semantics. The
  // bounded native representation mirrors that behavior with a heap pointer
  // to a schema-derived LLVM struct. Integer/bool and enum values use i64,
  // while supported handles and nested structs use opaque LLVM pointers.
  // Validate every schema used by reachable code before
  // declaring or lowering any Ny function so an unsupported layout can never
  // be emitted partially. Imported but unused schemas remain NIL metadata and
  // do not constrain this native executable.
  struct NativeStructLayout {
    const nil_struct_def *definition = nullptr;
    llvm::StructType *llvm_type = nullptr;
    std::size_t hidden_fields = 0;
    std::unordered_map<std::string, std::size_t> field_indices;
    std::vector<std::string> field_nil_types;
  };
  std::unordered_map<const nil_struct_def *, NativeStructLayout>
      native_struct_layouts;
  std::unordered_map<std::string, const nil_struct_def *> struct_names;
  std::unordered_map<std::string, std::uint64_t> native_class_tags;
  for (const auto &class_definition : mod.classes)
    native_class_tags.emplace(class_definition.name, native_class_tags.size() + 1);

  std::unordered_set<const nil_struct_def *> reachable_structs;
  std::vector<const nil_struct_def *> struct_worklist;
  std::unordered_set<std::string> visited_layout_types;
  bool valid_tuple_schemas = true;
  auto add_struct_type = [&](const std::string &type) {
    std::vector<std::string> pending{type};
    while (!pending.empty()) {
      const auto current = compact_nil_type(pending.back());
      pending.pop_back();
      if (!visited_layout_types.insert(current).second)
        continue;
      const auto *definition = find_native_aggregate_definition(mod, current);
      if (is_array_tuple_type(current)) {
        const auto fields = native_generic_arguments(current, "tuple");
        bool valid = definition && fields && definition->fields.size() == fields->size() &&
            compact_nil_type(definition->name) == current &&
            std::find(definition->annotations.begin(), definition->annotations.end(),
                      "@nil_tuple") != definition->annotations.end();
        if (valid)
          for (std::size_t i = 0; i < fields->size(); ++i)
            valid = valid && definition->fields[i].first == std::to_string(i) &&
                canonical_array_type(definition->fields[i].second) == canonical_array_type((*fields)[i]);
        if (!valid) {
          std::cerr << "[NativeCodegen] tuple type '" << current
                    << "' requires its exact ordered @nil_tuple schema\n";
          valid_tuple_schemas = false;
        }
      }
      if (definition && reachable_structs.insert(definition).second)
        struct_worklist.push_back(definition);
      if (const auto element = native_generic_argument(current, "array"))
        pending.push_back(*element);
      if (const auto *enumeration = find_nil_enum_definition(mod, current))
        for (const auto &variant : enumeration->variants)
          if (const auto payloads = native_enum_variant_payload_types(variant))
            pending.insert(pending.end(), payloads->begin(), payloads->end());
    }
  };

  for (const auto &fn : mod.functions) {
    if (reachable_functions.count(&fn) == 0)
      continue;
    for (const auto &parameter : fn.params)
      add_struct_type(nil_parameter_type(parameter));
    add_struct_type(fn.return_type);
    for (const auto &instruction : fn.body) {
      const auto opcode = stringToOpcode(instruction.opname);
      if ((opcode == Opcode::NEW_STRUCT || opcode == Opcode::FAULT_RAISE) &&
          !instruction.args.empty()) {
        add_struct_type(instruction.args[0]);
      } else if ((opcode == Opcode::FAULT_MATCH || opcode == Opcode::FAULT_BIND) &&
                 instruction.args.size() >= 2) {
        // A typed handler may mention a valid payload never constructed by
        // this executable (including a deliberately nonmatching clause).
        add_struct_type(instruction.args[1]);
      } else if ((opcode == Opcode::CLASS_UPCAST || opcode == Opcode::CALL_METHOD ||
                  opcode == Opcode::BOX_ANY || opcode == Opcode::UNBOX_ANY ||
                  opcode == Opcode::IS_ANY_TYPE) && !instruction.args.empty()) {
        add_struct_type(instruction.args[0]);
      }
      if (opcode == Opcode::NEW_ARRAY_LITERAL && instruction.args.size() >= 2)
        add_struct_type(instruction.args[1]);
      if (opcode == Opcode::INTEROP_CALL && instruction.args.size() == 5 &&
          instruction.args[0] == "intrinsic" && is_array_intrinsic(instruction.args[1])) {
        add_struct_type(instruction.args[3]);
        add_struct_type(instruction.args[4]);
      }
      if (opcode == Opcode::NEW_VARIANT && instruction.args.size() >= 3)
        add_struct_type(instruction.args[2]);
    }
  }

  // A reachable aggregate owns the layout of its fields. Follow nominal field
  // types transitively, including typed references. Nested fields preserve
  // object identity rather than recursively embedding physical layouts.
  for (std::size_t index = 0; index < struct_worklist.size(); ++index) {
    // An ancestor view can carry any concrete descendant. Their exact layouts
    // are required even when allocation occurs in a different reachable body.
    const auto view_name = struct_worklist[index]->name;
    if (find_nil_class(mod, view_name))
      for (const auto &dynamic_class : mod.classes)
        if (!dynamic_class.is_abstract &&
            nil_class_assignable(mod, view_name, dynamic_class.name))
          add_struct_type(dynamic_class.name);
    for (const auto &[field_name, field_type] :
         struct_worklist[index]->fields) {
      (void)field_name;
      add_struct_type(field_type);
    }
  }

  if (!valid_tuple_schemas)
    return false;

  // Acyclic nested objects are supported. Keep recursive schema shapes closed
  // until nullable/recursive initialization has a defined language contract.
  // Peel leaves iteratively so malformed deeply nested schemas cannot consume
  // the compiler's C++ call stack.
  std::unordered_map<const nil_struct_def *, std::size_t> pending_dependencies;
  std::unordered_map<const nil_struct_def *,
                     std::vector<const nil_struct_def *>> dependent_structs;
  std::deque<const nil_struct_def *> ready_structs;
  for (const auto *definition : struct_worklist) {
    auto &pending = pending_dependencies[definition];
    for (const auto &[field_name, field_type] : definition->fields) {
      (void)field_name;
      // An array is a handle, but its tuple schema still contributes a
      // dependency. Recursive aggregate graphs keep the existing rejection.
      const auto element = native_generic_argument(field_type, "array");
      if (const auto *nested = find_native_aggregate_definition(
              mod, element && is_array_tuple_type(*element) ? *element : field_type)) {
        ++pending;
        dependent_structs[nested].push_back(definition);
      }
    }
    if (pending == 0)
      ready_structs.push_back(definition);
  }
  std::size_t completed_structs = 0;
  while (!ready_structs.empty()) {
    const auto *definition = ready_structs.front();
    ready_structs.pop_front();
    ++completed_structs;
    for (const auto *dependent : dependent_structs[definition]) {
      if (--pending_dependencies[dependent] == 0)
        ready_structs.push_back(dependent);
    }
  }
  if (completed_structs != struct_worklist.size()) {
    for (const auto *definition : struct_worklist) {
      if (pending_dependencies[definition] != 0) {
        std::cerr << "[NativeCodegen] native struct schema '" << definition->name
                  << "' is a recursive aggregate or depends on one; native "
                     "recursive struct initialization is not implemented\n";
        break;
      }
    }
    return false;
  }

  for (const auto &definition : mod.structs) {
    if (reachable_structs.count(&definition) == 0)
      continue;
    const std::string normalized_name = compact_nil_type(definition.name);
    if (normalized_name.empty() || struct_names.count(normalized_name) != 0) {
      std::cerr << "[NativeCodegen] native struct schema contains duplicate "
                   "or empty type name '"
                << definition.name << "'\n";
      return false;
    }
    struct_names.emplace(normalized_name, &definition);

    NativeStructLayout layout;
    layout.definition = &definition;
    layout.llvm_type =
        llvm::StructType::create(ctx, "ny.struct." + definition.name);

    std::vector<llvm::Type *> field_llvm_types;
    field_llvm_types.reserve(definition.fields.size());
    layout.field_nil_types.reserve(definition.fields.size());
    if (find_nil_class(mod, definition.name)) {
      // This header is not a Ny field, pointer, or user-writeable vtable. Every
      // class allocation receives a compiler-owned tag and dispatch branches
      // access the actual class's own flattened layout.
      layout.hidden_fields = 1;
      field_llvm_types.push_back(llvm::Type::getInt64Ty(ctx));
      layout.field_nil_types.push_back({});
    }

    for (const auto &[field_name, field_type] : definition.fields) {
      if (field_name.empty() || layout.field_indices.count(field_name) != 0) {
        std::cerr << "[NativeCodegen] native struct schema '" << definition.name
                  << "' contains duplicate or empty field name '" << field_name
                  << "'\n";
        return false;
      }

      const std::size_t field_index = field_llvm_types.size();
      layout.field_indices.emplace(field_name, field_index);
      layout.field_nil_types.push_back(compact_nil_type(field_type));

      if (is_native_aggregate_nil_type(mod, field_type)) {
        field_llvm_types.push_back(llvm::PointerType::getUnqual(ctx));
        continue;
      }

      const auto handle_kind = native_handle_kind_for_nil_type(field_type);
      if (handle_kind == native_handle_kind::text ||
          handle_kind == native_handle_kind::anything ||
          handle_kind == native_handle_kind::scalar_array ||
          handle_kind == native_handle_kind::integer_pointer) {
        field_llvm_types.push_back(llvm::PointerType::getUnqual(ctx));
        continue;
      }
      if (handle_kind == native_handle_kind::unsupported) {
        std::cerr << "[NativeCodegen] native struct schema '" << definition.name
                  << "' field '" << field_name << "' of type '" << field_type
                  << "' has an unsupported native handle layout\n";
        return false;
      }

      if (find_nil_enum_definition(mod, field_type) != nullptr) {
        field_llvm_types.push_back(llvm::Type::getInt64Ty(ctx));
        continue;
      }

      const std::string scalar_type = normalize_nil_type(field_type);
      if (scalar_type == "i32" || scalar_type == "i64" ||
          scalar_type == "bool" || scalar_type == "u8") {
        field_llvm_types.push_back(llvm::Type::getInt64Ty(ctx));
        continue;
      }
      if (scalar_type == "f64") {
        field_llvm_types.push_back(llvm::Type::getDoubleTy(ctx));
        continue;
      }

      std::cerr << "[NativeCodegen] native struct schema '" << definition.name
                << "' field '" << field_name
                << "' has unsupported or opaque type '" << field_type
                << "'; native fields currently accept i32, i64, bool, u8, f64, "
                   "known enums, acyclic structs, text, scalar arrays, and "
                   "ptr<i32/i64/bool/u8>\n";
      return false;
    }

    layout.llvm_type->setBody(field_llvm_types, false);
    native_struct_layouts.emplace(&definition, std::move(layout));
  }

  auto llvm_type_for_nil_abi = [&](const std::string &type) -> llvm::Type * {
    const auto handle_kind = native_handle_kind_for_nil_type(type);
    if (handle_kind == native_handle_kind::text ||
        handle_kind == native_handle_kind::anything ||
        handle_kind == native_handle_kind::scalar_array ||
        handle_kind == native_handle_kind::integer_pointer ||
        is_native_aggregate_nil_type(mod, type)) {
      return llvm::PointerType::getUnqual(ctx);
    }
    return llvm::Type::getInt64Ty(ctx);
  };

  auto validate_signature_type = [&](const nil_function &fn,
                                     const std::string &type,
                                     const std::string &subject) -> bool {
    if (type.empty() || normalize_nil_type(type) == "void")
      return true;
    const auto handle_kind = native_handle_kind_for_nil_type(type);
    if (handle_kind == native_handle_kind::unsupported &&
        !is_native_aggregate_nil_type(mod, type)) {
      std::cerr << "[NativeCodegen] pointer/text " << subject << " of type '"
                << type << "' in function '" << fn.name
                << "' has an unsupported native handle shape; this backend "
                   "currently accepts text, array<i32>, array<i64>, "
                   "array<bool>, array<u8>, ptr<i32>, ptr<i64>, and "
                   "ptr<bool>\n";
      return false;
    }
    if (handle_kind == native_handle_kind::text ||
        handle_kind == native_handle_kind::anything ||
        handle_kind == native_handle_kind::scalar_array ||
        handle_kind == native_handle_kind::integer_pointer ||
        is_native_aggregate_nil_type(mod, type) ||
        find_nil_enum_definition(mod, type) != nullptr ||
        is_native_scalar_abi_type(type)) {
      return true;
    }

    std::cerr << "[NativeCodegen] " << subject << " of type '" << type
              << "' in function '" << fn.name
              << "' has no exact native ABI metadata; generic and unknown "
                 "nominal types are not lowered as scalar words\n";
    return false;
  };

  // 1. Declare all functions first. Ordinary Ny functions are private,
  // backend-named implementation details; only the generated platform `main`
  // is exported. Besides giving Ny a stable namespace boundary, this prevents
  // a source function named like a libc/runtime dependency from interposing on
  // that dependency when the final executable is linked.
  std::unordered_map<std::string, llvm::Function *> native_functions;
  std::size_t native_function_index = 0;
  for (const auto &fn : mod.functions) {
    if (reachable_functions.count(&fn) == 0)
      continue;
    // Native entry points are called directly by the platform/JIT with no Ny
    // arguments. Keep this invariant in the backend as well as the type
    // checker so --no-typecheck cannot manufacture an invalid main ABI.
    if (fn.name == "main" && !fn.params.empty()) {
      std::cerr << "[NativeCodegen] entry function 'main' must declare 0 "
                   "parameters, found "
                << fn.params.size() << "\n";
      return false;
    }

    for (std::size_t index = 0; index < fn.params.size(); ++index) {
      const std::string &parameter = fn.params[index];
      const std::string parameter_name = nil_parameter_name(parameter);
      const std::string parameter_type = nil_parameter_type(parameter);
      if (!validate_signature_type(fn, parameter_type,
                                   "parameter '" + parameter_name + "'")) {
        return false;
      }
    }
    if (!validate_signature_type(fn, fn.return_type, "return"))
      return false;

    llvm::Type *ret_type = llvm::Type::getVoidTy(ctx);
    if (fn.name == "main") {
      ret_type = llvm::Type::getInt32Ty(ctx);
    } else if (!fn.return_type.empty() && fn.return_type != "void") {
      ret_type = llvm_type_for_nil_abi(fn.return_type);
    }

    std::vector<llvm::Type *> param_types;
    for (const auto &parameter : fn.params)
      param_types.push_back(
          llvm_type_for_nil_abi(nil_parameter_type(parameter)));

    llvm::FunctionType *ft =
        llvm::FunctionType::get(ret_type, param_types, false);
    auto *native_function = llvm::Function::Create(
        ft, llvm::Function::InternalLinkage,
        "__ny_function." + std::to_string(native_function_index++),
        *impl_->module);
    if (!native_functions.emplace(fn.name, native_function).second) {
      std::cerr << "[NativeCodegen] reachable function name '" << fn.name
                << "' is ambiguous\n";
      return false;
    }
  }

  std::unordered_map<std::string, llvm::Function *> parallel_callbacks;
  for (const auto *function : reachable_functions) {
    for (const auto &instruction : function->body) {
      if (instruction.opname != "PARALLEL_MAP" || parallel_callbacks.count(instruction.args[0])) continue;
      auto *word = llvm::Type::getInt64Ty(ctx);
      auto *callback = llvm::Function::Create(llvm::FunctionType::get(word, {word}, false),
          llvm::Function::InternalLinkage, "__ny_parallel_callback." + std::to_string(parallel_callbacks.size()), *impl_->module);
      parallel_callbacks.emplace(instruction.args[0], callback);
    }
  }

  // 2. Generate bodies
  for (const auto &fn : mod.functions) {
    if (reachable_functions.count(&fn) == 0)
      continue;
    const auto native_function = native_functions.find(fn.name);
    if (native_function == native_functions.end())
      continue;
    impl_->current_function = native_function->second;

    auto entry =
        llvm::BasicBlock::Create(ctx, "entry", impl_->current_function);
    impl_->builder->SetInsertPoint(entry);
    impl_->value_stack.clear();
    impl_->locals_map.clear();
    impl_->local_nil_types.clear();
    impl_->value_nil_types.clear();
    impl_->label_map.clear();

    // Source capsules are opt-in; programs without them still identify the
    // generated function and available line, without inventing a source path.
    // The runtime makes this a no-op unless memory recording was requested.
    const auto emit_profile_site = [&](const std::string &file, int line) {
      auto *pointer = llvm::PointerType::getUnqual(ctx);
      auto *word = llvm::Type::getInt64Ty(ctx);
      auto site = impl_->module->getOrInsertFunction("nyrt_profile_site",
          llvm::FunctionType::get(llvm::Type::getVoidTy(ctx),
                                 {pointer, pointer, word}, false));
      impl_->builder->CreateCall(site,
          {impl_->builder->CreateGlobalString(file),
           impl_->builder->CreateGlobalString(fn.name),
           llvm::ConstantInt::get(word, std::max(0, line))});
    };
    emit_profile_site(fn.source_file, fn.source_line);

    struct NativeStructInitialization {
      const nil_struct_def *definition = nullptr;
    };
    std::unordered_map<const llvm::Value *, NativeStructInitialization>
        native_struct_initializations;
    enum class StructInitializationAction { allocate, write, use };
    struct StructInitializationEvent {
      StructInitializationAction action;
      const llvm::Value *value;
      std::string field;
      std::string operation;
    };
    std::unordered_map<llvm::BasicBlock *,
                       std::vector<StructInitializationEvent>> struct_events;
    const auto require_initialized_struct =
        [&](const llvm::Value *value, const std::string &operation) {
      if (value && is_native_aggregate_nil_type(mod, impl_->nilType(value)))
        struct_events[impl_->builder->GetInsertBlock()].push_back(
            {StructInitializationAction::use, value, {}, operation});
    };

    // Lower parameters to local variables
    size_t arg_idx = 0;
    for (auto &arg : impl_->current_function->args()) {
      if (arg_idx < fn.params.size()) {
        const std::string p_name = nil_parameter_name(fn.params[arg_idx]);
        const std::string p_type = nil_parameter_type(fn.params[arg_idx]);

        // f64 crosses the established Ny word ABI as exact bits, but local
        // computations and stores retain a real double representation.
        llvm::Value *local_value = &arg;
        if (normalize_nil_type(p_type) == "f64")
          local_value = impl_->builder->CreateBitCast(
              &arg, llvm::Type::getDoubleTy(ctx), "ny.parameter.f64");
        auto *alloca =
            impl_->builder->CreateAlloca(local_value->getType(), nullptr, p_name);
        impl_->builder->CreateStore(local_value, alloca);
        impl_->locals_map[p_name] = alloca;
        impl_->local_nil_types[p_name] = compact_nil_type(p_type);
        impl_->setNilType(&arg, p_type);
      }
      arg_idx++;
    }

    // LLVM receives parameters as SSA arguments and has already made them
    // addressable above. NIL function bodies also carry the interpreter's
    // operand-stack parameter prologue. Seed the virtual stack only when that
    // exact generated prologue is present so its STORE_LOCAL instructions
    // consume the arguments with the same ordering as nilinterpreter. Functions
    // without that prologue keep the direct-local convention and gain no stale
    // operand-stack values.
    if (has_nil_parameter_prologue(fn)) {
      for (auto &arg : impl_->current_function->args()) {
        const auto type = impl_->nilType(&arg);
        llvm::Value *value = &arg;
        if (type == "f64")
          value = impl_->builder->CreateBitCast(
              value, llvm::Type::getDoubleTy(ctx), "ny.parameter.stack.f64");
        impl_->pushValue(value, type);
      }
    }

    // Check for intrinsics
    bool handled_intrinsic = false;
    for (const auto &annotation : fn.annotations) {
      bool annotation_handled = false;
      const std::string dispatch_text = trim_copy(annotation);
      std::string dispatch_name;
      for (const std::string candidate : {"@intrinsic", "@interop"}) {
        if (dispatch_text.compare(0, candidate.size(), candidate) != 0)
          continue;
        if (dispatch_text.size() != candidate.size() &&
            dispatch_text[candidate.size()] != '(' &&
            std::isspace(static_cast<unsigned char>(
                dispatch_text[candidate.size()])) == 0) {
          continue;
        }
        dispatch_name = candidate;
        break;
      }
      const bool dispatch_annotation = !dispatch_name.empty();
      if (!dispatch_annotation)
        continue;

      std::size_t cursor = dispatch_name.size();
      const auto skip_space = [&]() {
        while (cursor < dispatch_text.size() &&
               std::isspace(
                   static_cast<unsigned char>(dispatch_text[cursor])) != 0) {
          ++cursor;
        }
      };
      skip_space();
      bool dispatch_syntax_valid =
          cursor < dispatch_text.size() && dispatch_text[cursor] == '(';
      if (dispatch_syntax_valid)
        ++cursor;
      skip_space();

      std::string dispatch_provider;
      if (dispatch_syntax_valid && cursor < dispatch_text.size() &&
          dispatch_text[cursor] == '"') {
        const std::size_t start = ++cursor;
        while (cursor < dispatch_text.size() && dispatch_text[cursor] != '"')
          ++cursor;
        dispatch_syntax_valid = cursor < dispatch_text.size();
        if (dispatch_syntax_valid) {
          dispatch_provider = dispatch_text.substr(start, cursor - start);
          ++cursor;
        }
      } else if (dispatch_syntax_valid) {
        const std::size_t start = cursor;
        while (cursor < dispatch_text.size() && dispatch_text[cursor] != ')' &&
               std::isspace(
                   static_cast<unsigned char>(dispatch_text[cursor])) == 0) {
          ++cursor;
        }
        dispatch_provider = dispatch_text.substr(start, cursor - start);
      }

      const bool valid_provider =
          !dispatch_provider.empty() &&
          (std::isalpha(
               static_cast<unsigned char>(dispatch_provider.front())) != 0 ||
           dispatch_provider.front() == '_') &&
          std::all_of(dispatch_provider.begin(), dispatch_provider.end(),
                      [](unsigned char character) {
                        return std::isalnum(character) != 0 ||
                               character == '_' || character == '.';
                      });
      skip_space();
      dispatch_syntax_valid = dispatch_syntax_valid && valid_provider &&
                              cursor < dispatch_text.size() &&
                              dispatch_text[cursor] == ')';
      if (dispatch_syntax_valid)
        ++cursor;
      skip_space();
      dispatch_syntax_valid =
          dispatch_syntax_valid && cursor == dispatch_text.size();
      if (!dispatch_syntax_valid) {
        std::cerr << "[NativeCodegen] malformed native dispatch annotation '"
                  << dispatch_text << "' in reachable function '" << fn.name
                  << "'\n";
        return false;
      }

      // Existing handlers consume one normalized spelling. Construct it only
      // after parsing the complete annotation so substring matches can never
      // admit trailing tokens or a different annotation kind.
      const std::string ann = dispatch_name + "(\"" + dispatch_provider + "\")";
      const bool std_sys =
          ann.find("@interop(\"std_sys\")") != std::string::npos ||
          ann.find("@interop(std_sys)") != std::string::npos;
      const bool console_write =
          ann.find("@intrinsic(\"io.console.write\")") != std::string::npos ||
          ann.find("@intrinsic(io.console.write)") != std::string::npos ||
          ann.find("@intrinsic(\"console.write\")") != std::string::npos ||
          ann.find("@intrinsic(console.write)") != std::string::npos;
      const bool console_writeln =
          ann.find("@intrinsic(\"console.writeln\")") != std::string::npos ||
          ann.find("@intrinsic(console.writeln)") != std::string::npos;
      const bool console_error =
          ann.find("@intrinsic(\"io.console.error\")") != std::string::npos ||
          ann.find("@intrinsic(io.console.error)") != std::string::npos ||
          ann.find("@intrinsic(\"console.error\")") != std::string::npos ||
          ann.find("@intrinsic(console.error)") != std::string::npos;
      if (dispatch_name == "@intrinsic" && dispatch_provider.rfind("ui.v1.", 0) == 0) {
        const auto contract = ui::intrinsic(dispatch_provider);
        if (!contract) {
          std::cerr << "[NativeCodegen] unknown UI intrinsic '" << dispatch_provider << "'\n";
          return false;
        }
        std::vector<std::string> parameter_types;
        for (const auto &parameter : fn.params) parameter_types.push_back(nil_parameter_type(parameter));
        bool valid = ui::signature_matches(*contract, parameter_types, fn.return_type) &&
            impl_->current_function->arg_size() == contract->parameters.size();
        for (std::size_t index = 0; valid && index < contract->parameters.size(); ++index) {
          auto *actual = impl_->current_function->getArg(index)->getType();
          valid = contract->parameters[index] == "i64" ? actual->isIntegerTy(64) : actual->isPointerTy();
        }
        const bool capability = contract->output == ui::Output::capability;
        const bool text_result = contract->output == ui::Output::text;
        valid = valid && (capability ? impl_->current_function->getReturnType()->isIntegerTy()
                                    : impl_->current_function->getReturnType()->isIntegerTy(64));
        if (!capability) {
          std::size_t schemas = 0;
          for (const auto &definition : mod.enums)
            if (ui::result_schema_matches(*contract, definition)) ++schemas;
          valid = valid && schemas == 1;
        }
        if (!valid) {
          std::cerr << "[NativeCodegen] " << dispatch_provider << " requires the exact UI ABI "
                    << ui::signature(*contract) << " and the standard Result schema\n";
          return false;
        }
        auto *word_type = llvm::Type::getInt64Ty(ctx);
        auto *pointer_type = llvm::PointerType::getUnqual(ctx);
        llvm::Type *payload_type = text_result ? static_cast<llvm::Type *>(pointer_type) : word_type;
        std::vector<llvm::Value *> arguments;
        for (auto &argument : impl_->current_function->args()) arguments.push_back(&argument);
        llvm::AllocaInst *value_out = nullptr, *error_out = nullptr;
        if (!capability) {
          // Entry allocas and loaded payloads are published by native root
          // instrumentation before any enum allocation can collect them.
          value_out = impl_->builder->CreateAlloca(payload_type);
          error_out = impl_->builder->CreateAlloca(pointer_type);
          arguments.push_back(value_out); arguments.push_back(error_out);
        }
        std::vector<llvm::Type *> types;
        for (auto *argument : arguments) types.push_back(argument->getType());
        auto *runtime_type = llvm::FunctionType::get(capability ? word_type : llvm::Type::getInt32Ty(ctx), types, false);
        auto runtime = impl_->module->getOrInsertFunction("nyrt_ui_v1_" + contract->operation, runtime_type);
        auto *status = impl_->builder->CreateCall(runtime_type, runtime.getCallee(), arguments);
        if (capability) {
          impl_->builder->CreateRet(impl_->builder->CreateIntCast(status, impl_->current_function->getReturnType(), false));
        } else {
          auto *success = llvm::BasicBlock::Create(ctx, "ui.success", impl_->current_function);
          auto *failure = llvm::BasicBlock::Create(ctx, "ui.failure", impl_->current_function);
          impl_->builder->CreateCondBr(impl_->builder->CreateICmpNE(status, llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 0)), success, failure);
          impl_->builder->SetInsertPoint(success);
          auto *value = impl_->builder->CreateLoad(payload_type, value_out);
          auto *ok = emit_single_payload_variant("Result::Ok", value, fn.return_type, text_result ? "text" : "i64");
          if (!ok) return false;
          impl_->builder->CreateRet(ok);
          impl_->builder->SetInsertPoint(failure);
          auto *error = impl_->builder->CreateLoad(pointer_type, error_out);
          auto *failed = emit_single_payload_variant("Result::Error", error, fn.return_type, "text");
          if (!failed) return false;
          impl_->builder->CreateRet(failed);
        }
        annotation_handled = true;
      } else if (dispatch_name == "@intrinsic" && dispatch_provider.rfind("db2.", 0) == 0) {
        std::vector<std::string> expected;
        bool capability = false, text_result = false;
        int operation = 0;
        std::string runtime_name;
        if (dispatch_provider == "db2.available") {
          expected = {"text"}; capability = true; runtime_name = "nyrt_db2_available";
        } else if (dispatch_provider == "db2.open") {
          expected = {"text", "text", "i64"}; runtime_name = "nyrt_db2_open";
        } else if (dispatch_provider == "db2.execute") {
          expected = {"i64", "text", "array<text>", "array<bool>", "i64"}; runtime_name = "nyrt_db2_execute";
        } else {
          if (dispatch_provider == "db2.rows") operation = 1;
          else if (dispatch_provider == "db2.columns") operation = 2;
          else if (dispatch_provider == "db2.affected") operation = 3;
          else if (dispatch_provider == "db2.is_null") operation = 4;
          else if (dispatch_provider == "db2.release") operation = 5;
          else if (dispatch_provider == "db2.close") operation = 6;
          else if (dispatch_provider == "db2.column_name") operation = 7;
          else if (dispatch_provider == "db2.cell") operation = 8;
          if (!operation) {
            std::cerr << "[NativeCodegen] unsupported database intrinsic\n";
            return false;
          }
          expected = {"i64"};
          if (operation == 7) expected.push_back("i64");
          if (operation == 4 || operation == 8) { expected.push_back("i64"); expected.push_back("i64"); }
          text_result = operation >= 7;
          runtime_name = text_result ? "nyrt_db2_read_text" : "nyrt_db2_read_i64";
        }
        bool valid = fn.params.size() == expected.size() && impl_->current_function->arg_size() == expected.size();
        for (std::size_t i = 0; valid && i < expected.size(); ++i) {
          valid = normalize_nil_type(nil_parameter_type(fn.params[i])) == expected[i];
          auto *actual = impl_->current_function->getArg(i)->getType();
          valid = valid && (expected[i] == "i64" ? actual->isIntegerTy(64) : actual->isPointerTy());
        }
        const auto result_arguments = native_generic_arguments(fn.return_type, "result");
        if (capability) {
          valid = valid && normalize_nil_type(fn.return_type) == "bool" && impl_->current_function->getReturnType()->isIntegerTy();
        } else {
          valid = valid && result_arguments && result_arguments->size() == 2 &&
                  (*result_arguments)[0] == (text_result ? "text" : "i64") &&
                  (*result_arguments)[1] == "text" && impl_->current_function->getReturnType()->isIntegerTy(64);
        }
        if (!valid) {
          std::cerr << "[NativeCodegen] " << dispatch_provider << " has an invalid native ABI\n";
          return false;
        }
        auto *word_type = llvm::Type::getInt64Ty(ctx);
        auto *pointer_type = llvm::PointerType::getUnqual(ctx);
        std::vector<llvm::Value *> arguments;
        if (operation) {
          arguments = {llvm::ConstantInt::get(word_type, operation), impl_->current_function->getArg(0),
                       llvm::ConstantInt::get(word_type, 0), llvm::ConstantInt::get(word_type, 0)};
          if (operation == 7) arguments[3] = impl_->current_function->getArg(1);
          if (operation == 4 || operation == 8) { arguments[2] = impl_->current_function->getArg(1); arguments[3] = impl_->current_function->getArg(2); }
        } else {
          for (auto &arg : impl_->current_function->args()) arguments.push_back(&arg);
        }
        llvm::Type *payload_type = text_result ? static_cast<llvm::Type *>(pointer_type) : word_type;
        llvm::AllocaInst *value_out = nullptr, *error_out = nullptr;
        if (!capability) {
          value_out = impl_->builder->CreateAlloca(payload_type);
          error_out = impl_->builder->CreateAlloca(pointer_type);
          arguments.push_back(value_out); arguments.push_back(error_out);
        }
        std::vector<llvm::Type *> types;
        for (auto *argument : arguments) types.push_back(argument->getType());
        auto *runtime_type = llvm::FunctionType::get(capability ? word_type : llvm::Type::getInt32Ty(ctx), types, false);
        auto runtime = impl_->module->getOrInsertFunction(runtime_name, runtime_type);
        auto *status = impl_->builder->CreateCall(runtime_type, runtime.getCallee(), arguments);
        if (capability) {
          impl_->builder->CreateRet(impl_->builder->CreateIntCast(status, impl_->current_function->getReturnType(), false));
        } else {
          auto *success = llvm::BasicBlock::Create(ctx, "database.success", impl_->current_function);
          auto *failure = llvm::BasicBlock::Create(ctx, "database.failure", impl_->current_function);
          impl_->builder->CreateCondBr(impl_->builder->CreateICmpNE(status, llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 0)), success, failure);
          impl_->builder->SetInsertPoint(success);
          auto *value = impl_->builder->CreateLoad(payload_type, value_out);
          auto *ok = emit_single_payload_variant("Result::Ok", value, fn.return_type, text_result ? "text" : "i64");
          if (!ok) return false;
          impl_->builder->CreateRet(ok);
          impl_->builder->SetInsertPoint(failure);
          auto *error = impl_->builder->CreateLoad(pointer_type, error_out);
          auto *failed = emit_single_payload_variant("Result::Error", error, fn.return_type, "text");
          if (!failed) return false;
          impl_->builder->CreateRet(failed);
        }
        annotation_handled = true;
      } else if (std_sys) {
        const auto separator = fn.name.rfind("__");
        const std::string operation = separator == std::string::npos
                                          ? fn.name
                                          : fn.name.substr(separator + 2);
        if (operation == "arg_count") {
          if (!fn.params.empty() ||
              !impl_->current_function->getReturnType()->isIntegerTy(64)) {
            std::cerr << "[NativeCodegen] std_sys.arg_count requires the exact "
                         "() -> i64 ABI\n";
            return false;
          }
          auto runtime_type =
              llvm::FunctionType::get(llvm::Type::getInt64Ty(ctx), {}, false);
          auto runtime = impl_->module->getOrInsertFunction("nyrt_arg_count",
                                                            runtime_type);
          impl_->builder->CreateRet(impl_->builder->CreateCall(
              runtime_type, runtime.getCallee(), {}));
          annotation_handled = true;
        } else if (operation == "arg") {
          if (fn.params.size() != 1 ||
              normalize_nil_type(nil_parameter_type(fn.params[0])) != "i64" ||
              impl_->current_function->arg_size() != 1 ||
              !impl_->current_function->getReturnType()->isPointerTy()) {
            std::cerr << "[NativeCodegen] std_sys.arg requires the exact (i64) "
                         "-> text ABI\n";
            return false;
          }
          auto runtime_type =
              llvm::FunctionType::get(llvm::PointerType::getUnqual(ctx),
                                      {llvm::Type::getInt64Ty(ctx)}, false);
          auto runtime =
              impl_->module->getOrInsertFunction("nyrt_arg", runtime_type);
          auto *value =
              impl_->builder->CreateCall(runtime_type, runtime.getCallee(),
                                         {impl_->current_function->getArg(0)});
          auto *success = llvm::BasicBlock::Create(ctx, "arg.success",
                                                   impl_->current_function);
          auto *failure = llvm::BasicBlock::Create(ctx, "arg.failure",
                                                   impl_->current_function);
          impl_->builder->CreateCondBr(
              impl_->builder->CreateICmpNE(
                  value, llvm::ConstantPointerNull::get(
                             llvm::cast<llvm::PointerType>(value->getType()))),
              success, failure);
          impl_->builder->SetInsertPoint(failure);
          auto *message = impl_->builder->CreateGlobalString(
              "NIL native runtime error: std_sys.arg index is outside the "
              "argument list\n");
          impl_->builder->CreateCall(get_runtime_failure_function(), {message});
          impl_->builder->CreateUnreachable();
          impl_->builder->SetInsertPoint(success);
          impl_->builder->CreateRet(value);
          annotation_handled = true;
        } else if (operation == "exit") {
          if (fn.params.size() != 1 ||
              normalize_nil_type(nil_parameter_type(fn.params[0])) != "i32" ||
              impl_->current_function->arg_size() != 1 ||
              !impl_->current_function->getReturnType()->isVoidTy()) {
            std::cerr
                << "[NativeCodegen] std_sys.exit requires the exact (i32) "
                   "-> void ABI\n";
            return false;
          }
          auto runtime_type = llvm::FunctionType::get(
              llvm::Type::getVoidTy(ctx), {llvm::Type::getInt64Ty(ctx)}, false);
          auto runtime =
              impl_->module->getOrInsertFunction("nyrt_exit", runtime_type);
          llvm::Value *code =
              impl_->coerceToI64(impl_->current_function->getArg(0));
          impl_->builder->CreateCall(runtime_type, runtime.getCallee(), {code});
          impl_->builder->CreateUnreachable();
          annotation_handled = true;
        } else {
          std::cerr << "[NativeCodegen] unsupported std_sys operation '"
                    << operation << "' in function '" << fn.name << "'\n";
          return false;
        }
      } else if (dispatch_name == "@intrinsic" && dispatch_provider.rfind("nildata.", 0) == 0) {
        const std::vector<std::string> expected{"text", "text", "text", "i64"};
        bool valid = dispatch_provider == "nildata.request" && fn.params.size() == expected.size() &&
            impl_->current_function->arg_size() == expected.size() &&
            normalize_nil_type(fn.return_type) == "array<text>";
        for (std::size_t i = 0; valid && i < expected.size(); ++i)
          valid = normalize_nil_type(nil_parameter_type(fn.params[i])) == expected[i];
        if (!valid) {
          std::cerr << "[NativeCodegen] nildata.request requires (text, text, text, i64) -> array<text>\n";
          return false;
        }
        auto *pointer = llvm::PointerType::getUnqual(ctx);
        auto *type = llvm::FunctionType::get(pointer,
            {pointer, pointer, pointer, llvm::Type::getInt64Ty(ctx)}, false);
        auto runtime = impl_->module->getOrInsertFunction("nyrt_nildata_v1_request", type);
        impl_->builder->CreateRet(impl_->builder->CreateCall(type, runtime.getCallee(),
            {impl_->current_function->getArg(0), impl_->current_function->getArg(1),
             impl_->current_function->getArg(2), impl_->coerceToI64(impl_->current_function->getArg(3))}));
        annotation_handled = true;
      } else if (dispatch_name == "@intrinsic" && dispatch_provider.rfind("regex.", 0) == 0) {
        const bool validation = dispatch_provider == "regex.validate";
        const bool searching = dispatch_provider == "regex.search";
        const bool group = dispatch_provider == "regex.group_index";
        const std::vector<std::string> expected = validation ? std::vector<std::string>{"text"}
            : searching ? std::vector<std::string>{"text", "text", "i64", "bool"}
                        : std::vector<std::string>{"text", "text"};
        const std::string result_type = validation ? "text" : searching ? "array<i64>" : "i64";
        bool valid = (validation || searching || group) && fn.params.size() == expected.size() &&
            impl_->current_function->arg_size() == expected.size() &&
            normalize_nil_type(fn.return_type) == result_type;
        for (std::size_t i = 0; valid && i < expected.size(); ++i)
          valid = normalize_nil_type(nil_parameter_type(fn.params[i])) == expected[i];
        if (!valid) {
          std::cerr << "[NativeCodegen] " << dispatch_provider << " has an invalid native ABI\n";
          return false;
        }
        auto *pointer = llvm::PointerType::getUnqual(ctx);
        auto *word = llvm::Type::getInt64Ty(ctx);
        std::vector<llvm::Type *> types;
        std::vector<llvm::Value *> arguments;
        for (std::size_t i = 0; i < expected.size(); ++i) {
          auto *value = impl_->current_function->getArg(i);
          types.push_back(expected[i] == "text" ? static_cast<llvm::Type *>(pointer) : word);
          arguments.push_back(expected[i] == "text" ? value : impl_->coerceToI64(value));
        }
        auto *type = llvm::FunctionType::get(group ? static_cast<llvm::Type *>(word) : pointer, types, false);
        auto runtime = impl_->module->getOrInsertFunction(validation ? "nyrt_regex_validate" :
            searching ? "nyrt_regex_search" : "nyrt_regex_group_index", type);
        impl_->builder->CreateRet(impl_->builder->CreateCall(type, runtime.getCallee(), arguments));
        annotation_handled = true;
      } else if (dispatch_name == "@intrinsic" &&
                 (dispatch_provider == "console.read_line" ||
                  dispatch_provider == "console.flush")) {
        const bool reading = dispatch_provider == "console.read_line";
        if (!fn.params.empty() || impl_->current_function->arg_size() != 0 ||
            normalize_nil_type(fn.return_type) != (reading ? "text" : "void") ||
            (reading ? !impl_->current_function->getReturnType()->isPointerTy()
                     : !impl_->current_function->getReturnType()->isVoidTy())) {
          std::cerr << "[NativeCodegen] " << dispatch_provider
                    << " requires the exact () -> "
                    << (reading ? "text" : "void") << " ABI\n";
          return false;
        }
        auto *runtime_type = llvm::FunctionType::get(
            reading ? static_cast<llvm::Type *>(llvm::PointerType::getUnqual(ctx))
                    : llvm::Type::getVoidTy(ctx), {}, false);
        auto runtime = impl_->module->getOrInsertFunction(
            reading ? "nyrt_read_line" : "nyrt_flush_stdout", runtime_type);
        auto *value = impl_->builder->CreateCall(runtime_type, runtime.getCallee(), {});
        if (reading) impl_->builder->CreateRet(value);
        else impl_->builder->CreateRetVoid();
        annotation_handled = true;
      } else if (console_write || console_writeln || console_error) {
        if (fn.params.size() != 1 ||
            native_handle_kind_for_nil_type(nil_parameter_type(fn.params[0])) !=
                native_handle_kind::text ||
            impl_->current_function->arg_size() != 1 ||
            !impl_->current_function->getReturnType()->isVoidTy()) {
          std::cerr << "[NativeCodegen] console intrinsic '" << fn.name
                    << "' requires the exact (text) -> void ABI\n";
          return false;
        }
        auto runtime_type = llvm::FunctionType::get(
            llvm::Type::getVoidTy(ctx),
            {llvm::PointerType::getUnqual(ctx), llvm::Type::getInt32Ty(ctx)},
            false);
        auto runtime = impl_->module->getOrInsertFunction(
            console_error ? "nyrt_write_stderr" : "nyrt_write_stdout",
            runtime_type);
        impl_->builder->CreateCall(
            runtime_type, runtime.getCallee(),
            {impl_->current_function->getArg(0),
             llvm::ConstantInt::get(
                 ctx, llvm::APInt(32, console_writeln ? 1 : 0, false))});
        impl_->builder->CreateRetVoid();
        annotation_handled = true;
      } else if (ann.find("@intrinsic(\"convert.i32_to_text\")") !=
                     std::string::npos ||
                 ann.find("@intrinsic(convert.i32_to_text)") !=
                     std::string::npos ||
                 ann.find("@intrinsic(\"convert.i64_to_text\")") !=
                     std::string::npos ||
                 ann.find("@intrinsic(convert.i64_to_text)") !=
                     std::string::npos) {
        const bool i32_conversion =
            ann.find("i32_to_text") != std::string::npos;
        if (fn.params.size() != 1 ||
            normalize_nil_type(nil_parameter_type(fn.params[0])) !=
                (i32_conversion ? "i32" : "i64") ||
            impl_->current_function->arg_size() != 1 ||
            !impl_->current_function->getReturnType()->isPointerTy()) {
          std::cerr << "[NativeCodegen] integer-to-text intrinsic '" << fn.name
                    << "' has an invalid native ABI\n";
          return false;
        }
        llvm::Value *argument = impl_->current_function->getArg(0);
        auto *runtime_argument_type = i32_conversion
                                          ? llvm::Type::getInt32Ty(ctx)
                                          : llvm::Type::getInt64Ty(ctx);
        if (argument->getType() != runtime_argument_type)
          argument = impl_->builder->CreateIntCast(argument,
                                                   runtime_argument_type, true);
        auto runtime_type = llvm::FunctionType::get(
            llvm::PointerType::getUnqual(ctx), {runtime_argument_type}, false);
        auto runtime = impl_->module->getOrInsertFunction(
            i32_conversion ? "nyrt_i32_to_text" : "nyrt_i64_to_text",
            runtime_type);
        auto *value = impl_->builder->CreateCall(
            runtime_type, runtime.getCallee(), {argument});
        auto *success = llvm::BasicBlock::Create(ctx, "format.success",
                                                 impl_->current_function);
        auto *failure = llvm::BasicBlock::Create(ctx, "format.failure",
                                                 impl_->current_function);
        impl_->builder->CreateCondBr(
            impl_->builder->CreateICmpNE(
                value, llvm::ConstantPointerNull::get(
                           llvm::cast<llvm::PointerType>(value->getType()))),
            success, failure);
        impl_->builder->SetInsertPoint(failure);
        auto *message = impl_->builder->CreateGlobalString(
            "NIL native runtime error: integer formatting failed\n");
        impl_->builder->CreateCall(get_runtime_failure_function(), {message});
        impl_->builder->CreateUnreachable();
        impl_->builder->SetInsertPoint(success);
        impl_->builder->CreateRet(value);
        annotation_handled = true;
      } else if (ann.find("@intrinsic(\"convert.try_to_i32\")") !=
                     std::string::npos ||
                 ann.find("@intrinsic(convert.try_to_i32)") !=
                     std::string::npos ||
                 ann.find("@intrinsic(\"convert.try_to_i64\")") !=
                     std::string::npos ||
                 ann.find("@intrinsic(convert.try_to_i64)") !=
                     std::string::npos) {
        const bool i32_conversion = ann.find("try_to_i32") != std::string::npos;
        const std::string payload_type = i32_conversion ? "i32" : "i64";
        const auto result_arguments =
            native_generic_arguments(fn.return_type, "result");
        if (fn.params.size() != 1 ||
            native_handle_kind_for_nil_type(nil_parameter_type(fn.params[0])) !=
                native_handle_kind::text ||
            !result_arguments || result_arguments->size() != 2 ||
            (*result_arguments)[0] != payload_type ||
            native_handle_kind_for_nil_type((*result_arguments)[1]) !=
                native_handle_kind::text ||
            impl_->current_function->arg_size() != 1 ||
            !impl_->current_function->getReturnType()->isIntegerTy(64)) {
          std::cerr << "[NativeCodegen] convert.try_to_" << payload_type
                    << " requires the exact (text) -> Result<" << payload_type
                    << ",text> ABI\n";
          return false;
        }
        auto *parsed_type = i32_conversion ? llvm::Type::getInt32Ty(ctx)
                                          : llvm::Type::getInt64Ty(ctx);
        auto *value_out = impl_->builder->CreateAlloca(parsed_type);
        auto *error_out =
            impl_->builder->CreateAlloca(llvm::PointerType::getUnqual(ctx));
        auto runtime_type =
            llvm::FunctionType::get(llvm::Type::getInt32Ty(ctx),
                                    {llvm::PointerType::getUnqual(ctx),
                                     llvm::PointerType::getUnqual(ctx),
                                     llvm::PointerType::getUnqual(ctx)},
                                    false);
        auto runtime = impl_->module->getOrInsertFunction(
            i32_conversion ? "nyrt_try_i32" : "nyrt_try_i64", runtime_type);
        auto *status = impl_->builder->CreateCall(
            runtime_type, runtime.getCallee(),
            {impl_->current_function->getArg(0), value_out, error_out});
        auto *success = llvm::BasicBlock::Create(ctx, "parse.success",
                                                 impl_->current_function);
        auto *failure = llvm::BasicBlock::Create(ctx, "parse.failure",
                                                 impl_->current_function);
        impl_->builder->CreateCondBr(
            impl_->builder->CreateICmpNE(
                status, llvm::ConstantInt::get(ctx, llvm::APInt(32, 0))),
            success, failure);
        impl_->builder->SetInsertPoint(success);
        auto *parsed = impl_->builder->CreateLoad(parsed_type, value_out);
        auto *ok_variant = emit_single_payload_variant("Result::Ok", parsed,
                                                       fn.return_type, payload_type);
        if (!ok_variant)
          return false;
        impl_->builder->CreateRet(ok_variant);
        impl_->builder->SetInsertPoint(failure);
        auto *error = impl_->builder->CreateLoad(
            llvm::PointerType::getUnqual(ctx), error_out);
        auto *error_variant = emit_single_payload_variant(
            "Result::Error", error, fn.return_type, "text");
        if (!error_variant)
          return false;
        impl_->builder->CreateRet(error_variant);
        annotation_handled = true;
      } else if (ann.find("@intrinsic(\"file.read_regular_bytes\")") !=
                     std::string::npos ||
                 ann.find("@intrinsic(file.read_regular_bytes)") !=
                     std::string::npos) {
        const auto result_arguments =
            native_generic_arguments(fn.return_type, "result");
        if (fn.params.size() != 2 ||
            native_handle_kind_for_nil_type(nil_parameter_type(fn.params[0])) !=
                native_handle_kind::text ||
            normalize_nil_type(nil_parameter_type(fn.params[1])) != "i64" ||
            !result_arguments || result_arguments->size() != 2 ||
            (*result_arguments)[0] != "array<u8>" ||
            native_handle_kind_for_nil_type((*result_arguments)[1]) !=
                native_handle_kind::text ||
            impl_->current_function->arg_size() != 2 ||
            !impl_->current_function->getReturnType()->isIntegerTy(64)) {
          std::cerr << "[NativeCodegen] file.read_regular_bytes has an invalid "
                       "native ABI\n";
          return false;
        }
        auto *bytes_out =
            impl_->builder->CreateAlloca(llvm::PointerType::getUnqual(ctx));
        auto *error_out =
            impl_->builder->CreateAlloca(llvm::PointerType::getUnqual(ctx));
        auto runtime_type = llvm::FunctionType::get(
            llvm::Type::getInt32Ty(ctx),
            {llvm::PointerType::getUnqual(ctx), llvm::Type::getInt64Ty(ctx),
             llvm::PointerType::getUnqual(ctx),
             llvm::PointerType::getUnqual(ctx)},
            false);
        auto runtime = impl_->module->getOrInsertFunction(
            "nyrt_read_regular_bytes", runtime_type);
        auto *status = impl_->builder->CreateCall(
            runtime_type, runtime.getCallee(),
            {impl_->current_function->getArg(0),
             impl_->current_function->getArg(1), bytes_out, error_out});
        auto *success = llvm::BasicBlock::Create(ctx, "file.read.success",
                                                 impl_->current_function);
        auto *failure = llvm::BasicBlock::Create(ctx, "file.read.failure",
                                                 impl_->current_function);
        impl_->builder->CreateCondBr(
            impl_->builder->CreateICmpNE(
                status, llvm::ConstantInt::get(ctx, llvm::APInt(32, 0))),
            success, failure);
        impl_->builder->SetInsertPoint(success);
        auto *bytes = impl_->builder->CreateLoad(
            llvm::PointerType::getUnqual(ctx), bytes_out);
        auto *ok_variant = emit_single_payload_variant(
            "Result::Ok", bytes, fn.return_type, "array<u8>");
        if (!ok_variant)
          return false;
        impl_->builder->CreateRet(ok_variant);
        impl_->builder->SetInsertPoint(failure);
        auto *error = impl_->builder->CreateLoad(
            llvm::PointerType::getUnqual(ctx), error_out);
        auto *error_variant = emit_single_payload_variant(
            "Result::Error", error, fn.return_type, "text");
        if (!error_variant)
          return false;
        impl_->builder->CreateRet(error_variant);
        annotation_handled = true;
      } else if (ann.find("@intrinsic(\"file.write_bytes_atomic\")") !=
                     std::string::npos ||
                 ann.find("@intrinsic(file.write_bytes_atomic)") !=
                     std::string::npos) {
        const auto result_arguments =
            native_generic_arguments(fn.return_type, "result");
        if (fn.params.size() != 3 ||
            native_handle_kind_for_nil_type(nil_parameter_type(fn.params[0])) !=
                native_handle_kind::text ||
            normalize_nil_type(nil_parameter_type(fn.params[1])) !=
                "array<u8>" ||
            normalize_nil_type(nil_parameter_type(fn.params[2])) != "bool" ||
            !result_arguments || result_arguments->size() != 2 ||
            (*result_arguments)[0] != "bool" ||
            native_handle_kind_for_nil_type((*result_arguments)[1]) !=
                native_handle_kind::text ||
            impl_->current_function->arg_size() != 3 ||
            !impl_->current_function->getReturnType()->isIntegerTy(64)) {
          std::cerr << "[NativeCodegen] file.write_bytes_atomic has an invalid "
                       "native ABI\n";
          return false;
        }
        llvm::Value *replace = impl_->current_function->getArg(2);
        if (!replace->getType()->isIntegerTy(32))
          replace = impl_->builder->CreateIntCast(
              replace, llvm::Type::getInt32Ty(ctx), false);
        auto *error_out =
            impl_->builder->CreateAlloca(llvm::PointerType::getUnqual(ctx));
        auto runtime_type = llvm::FunctionType::get(
            llvm::Type::getInt32Ty(ctx),
            {llvm::PointerType::getUnqual(ctx),
             llvm::PointerType::getUnqual(ctx), llvm::Type::getInt32Ty(ctx),
             llvm::PointerType::getUnqual(ctx)},
            false);
        auto runtime = impl_->module->getOrInsertFunction(
            "nyrt_write_bytes_atomic", runtime_type);
        auto *status = impl_->builder->CreateCall(
            runtime_type, runtime.getCallee(),
            {impl_->current_function->getArg(0),
             impl_->current_function->getArg(1), replace, error_out});
        auto *success = llvm::BasicBlock::Create(ctx, "file.write.success",
                                                 impl_->current_function);
        auto *failure = llvm::BasicBlock::Create(ctx, "file.write.failure",
                                                 impl_->current_function);
        impl_->builder->CreateCondBr(
            impl_->builder->CreateICmpNE(
                status, llvm::ConstantInt::get(ctx, llvm::APInt(32, 0))),
            success, failure);
        impl_->builder->SetInsertPoint(success);
        auto *written = llvm::ConstantInt::get(ctx, llvm::APInt(1, 1));
        auto *ok_variant = emit_single_payload_variant("Result::Ok", written,
                                                       fn.return_type, "bool");
        if (!ok_variant)
          return false;
        impl_->builder->CreateRet(ok_variant);
        impl_->builder->SetInsertPoint(failure);
        auto *error = impl_->builder->CreateLoad(
            llvm::PointerType::getUnqual(ctx), error_out);
        auto *error_variant = emit_single_payload_variant(
            "Result::Error", error, fn.return_type, "text");
        if (!error_variant)
          return false;
        impl_->builder->CreateRet(error_variant);
        annotation_handled = true;
      }
      // --- Math Intrinsics ---
      else if (ann.find("@intrinsic(\"math.") != std::string::npos ||
               ann.find("@intrinsic(math.") != std::string::npos) {
        const size_t start = ann.find("math.") + 5;
        const size_t end = ann.find_first_of("\")", start);
        if (end == std::string::npos || end == start) {
          std::cerr << "[NativeCodegen] malformed math annotation '" << ann
                    << "' in function '" << fn.name << "'\n";
          return false;
        }
        const std::string math_name = ann.substr(start, end - start);

        std::string llvm_intrinsic;
        bool use_libc = false;
        bool two_args = false;

        if (math_name == "sqrt")
          llvm_intrinsic = "llvm.sqrt.f64";
        else if (math_name == "sin")
          llvm_intrinsic = "llvm.sin.f64";
        else if (math_name == "cos")
          llvm_intrinsic = "llvm.cos.f64";
        else if (math_name == "exp")
          llvm_intrinsic = "llvm.exp.f64";
        else if (math_name == "log")
          llvm_intrinsic = "llvm.log.f64";
        else if (math_name == "log10")
          llvm_intrinsic = "llvm.log10.f64";
        else if (math_name == "log2")
          llvm_intrinsic = "llvm.log2.f64";
        else if (math_name == "fabs")
          llvm_intrinsic = "llvm.fabs.f64";
        else if (math_name == "floor")
          llvm_intrinsic = "llvm.floor.f64";
        else if (math_name == "ceil")
          llvm_intrinsic = "llvm.ceil.f64";
        else if (math_name == "round")
          llvm_intrinsic = "llvm.round.f64";
        else if (math_name == "trunc")
          llvm_intrinsic = "llvm.trunc.f64";
        else if (math_name == "pow") {
          llvm_intrinsic = "llvm.pow.f64";
          two_args = true;
        } else if (math_name == "fmin") {
          llvm_intrinsic = "llvm.minnum.f64";
          two_args = true;
        } else if (math_name == "fmax") {
          llvm_intrinsic = "llvm.maxnum.f64";
          two_args = true;
        } else if (math_name == "tan" || math_name == "asin" ||
                   math_name == "acos" || math_name == "atan" ||
                   math_name == "cbrt") {
          use_libc = true;
          llvm_intrinsic = math_name;
        } else if (math_name == "atan2") {
          use_libc = true;
          two_args = true;
          llvm_intrinsic = math_name;
        } else {
          std::cerr << "[NativeCodegen] unsupported math intrinsic 'math."
                    << math_name << "' in function '" << fn.name << "'\n";
          return false;
        }

        const std::size_t expected_arguments = two_args ? 2U : 1U;
        bool exact_f64_signature =
            fn.params.size() == expected_arguments &&
            impl_->current_function->arg_size() == expected_arguments &&
            normalize_nil_type(fn.return_type) == "f64" &&
            impl_->current_function->getReturnType()->isIntegerTy(64);
        for (const auto &parameter : fn.params) {
          exact_f64_signature =
              exact_f64_signature &&
              normalize_nil_type(nil_parameter_type(parameter)) == "f64";
        }
        for (const auto &argument : impl_->current_function->args()) {
          exact_f64_signature =
              exact_f64_signature && argument.getType()->isIntegerTy(64);
        }
        if (!exact_f64_signature) {
          std::cerr << "[NativeCodegen] math intrinsic 'math." << math_name
                    << "' requires the exact "
                    << (two_args ? "(f64, f64) -> f64" : "(f64) -> f64")
                    << " ABI in function '" << fn.name << "'\n";
          return false;
        }

        llvm::Type *f64_ty = llvm::Type::getDoubleTy(ctx);
        llvm::FunctionType *math_ft = llvm::FunctionType::get(
            f64_ty,
            two_args ? std::vector<llvm::Type *>{f64_ty, f64_ty}
                     : std::vector<llvm::Type *>{f64_ty},
            false);

        llvm::Function *math_fn = nullptr;
        if (use_libc) {
          math_fn = llvm::cast_or_null<llvm::Function>(
              impl_->module->getOrInsertFunction(llvm_intrinsic, math_ft)
                  .getCallee());
        } else {
          std::string lookup_name = llvm_intrinsic;
          if (lookup_name.size() > 4 &&
              lookup_name.substr(lookup_name.size() - 4) == ".f64") {
            lookup_name = lookup_name.substr(0, lookup_name.size() - 4);
          }
          const auto id = lookupIntrinsicID(lookup_name);
          if (id != llvm::Intrinsic::not_intrinsic) {
            math_fn = llvm::Intrinsic::getDeclaration(impl_->module.get(), id,
                                                      {f64_ty});
          }
        }

        if (!math_fn) {
          std::cerr << "[NativeCodegen] math intrinsic 'math." << math_name
                    << "' is unavailable in the configured LLVM target\n";
          return false;
        }

        std::vector<llvm::Value *> arguments;
        arguments.reserve(expected_arguments);
        for (std::size_t index = 0; index < expected_arguments; ++index) {
          arguments.push_back(impl_->builder->CreateBitCast(
              impl_->current_function->getArg(index), f64_ty));
        }
        auto *result = impl_->builder->CreateCall(math_fn, arguments);
        impl_->builder->CreateRet(
            impl_->builder->CreateBitCast(result, llvm::Type::getInt64Ty(ctx)));
        annotation_handled = true;
      }
      // --- Array Intrinsics ---
      else if (ann.find("@intrinsic(\"array.new_with_value\")") !=
                   std::string::npos ||
               ann.find("@intrinsic(\"array.new_u8\")") != std::string::npos ||
               ann.find("@intrinsic(\"array.new_f32\")") != std::string::npos) {
        // array.new_with_value(size: i32, val: T) -> array<T>
        if (fn.params.size() != 2 || impl_->current_function->arg_size() != 2) {
          std::cerr << "[NativeCodegen] array.new_with_value intrinsic '"
                    << fn.name
                    << "' requires a supported word-array signature\n";
          return false;
        }
        {
          auto *size_arg = impl_->current_function->getArg(0);
          auto *val_arg = impl_->current_function->getArg(1);
          const auto element_type =
              native_generic_argument(fn.return_type, "array");
          const std::string size_type = nil_parameter_type(fn.params[0]);
          const std::string value_type = nil_parameter_type(fn.params[1]);
          if (!element_type || !native_array_kind_for_element(*element_type) ||
              normalize_nil_type(size_type) != "i32" ||
              canonical_array_type(value_type) !=
                  canonical_array_type(*element_type) ||
              native_handle_kind_for_nil_type(fn.return_type) !=
                  native_handle_kind::scalar_array ||
              !size_arg->getType()->isIntegerTy() ||
              ((native_handle_kind_for_nil_type(value_type) == native_handle_kind::text ||
                native_handle_kind_for_nil_type(value_type) == native_handle_kind::anything ||
                is_array_tuple_type(value_type))
                   ? !val_arg->getType()->isPointerTy()
                   : !val_arg->getType()->isIntegerTy()) ||
              !impl_->current_function->getReturnType()->isPointerTy()) {
            std::cerr << "[NativeCodegen] array.new_with_value intrinsic '"
                      << fn.name
                      << "' requires a supported word-array signature\n";
            return false;
          }

          const auto kind = native_array_kind_for_element(*element_type);
          auto *size_i64 = impl_->coerceToI64(size_arg);
          auto *value_i64 = native_storage_word(
              val_arg, *element_type, "array.new_with_value");
          if (!value_i64)
            return false;
          auto runtime_type = llvm::FunctionType::get(
              llvm::PointerType::getUnqual(ctx),
              {llvm::Type::getInt32Ty(ctx), llvm::Type::getInt64Ty(ctx),
               llvm::Type::getInt64Ty(ctx)},
              false);
          auto runtime = impl_->module->getOrInsertFunction(
              "nyrt_array_new_with_value", runtime_type);
          auto *pointer = impl_->builder->CreateCall(
              runtime_type, runtime.getCallee(),
              {llvm::ConstantInt::get(ctx, llvm::APInt(32, *kind, false)),
               size_i64, value_i64});
          emit_allocation_null_check(pointer, "array.new_with_value");
          impl_->builder->CreateRet(pointer);
          annotation_handled = true;
        }
      } else if (ann.find("@intrinsic(\"array.length\")") !=
                 std::string::npos) {
        if (fn.params.size() != 1 || impl_->current_function->arg_size() != 1 ||
            normalize_nil_type(fn.return_type) != "i32" ||
            !impl_->current_function->getReturnType()->isIntegerTy(64)) {
          std::cerr << "[NativeCodegen] array.length intrinsic '" << fn.name
                    << "' requires a supported (array<T>) -> i32 signature\n";
          return false;
        }

        auto *array = impl_->current_function->getArg(0);
        const std::string array_type = nil_parameter_type(fn.params[0]);
        const auto element_type = native_generic_argument(array_type, "array");
        if (!element_type || !native_array_kind_for_element(*element_type) ||
            native_handle_kind_for_nil_type(array_type) !=
                native_handle_kind::scalar_array ||
            !array->getType()->isPointerTy()) {
          std::cerr << "[NativeCodegen] array.length intrinsic '" << fn.name
                    << "' requires a supported (array<T>) -> i32 signature\n";
          return false;
        }

        const auto kind = native_array_kind_for_element(*element_type);
        auto *runtime_type = llvm::FunctionType::get(
            llvm::Type::getInt64Ty(ctx),
            {llvm::PointerType::getUnqual(ctx), llvm::Type::getInt32Ty(ctx)},
            false);
        auto runtime = impl_->module->getOrInsertFunction("nyrt_array_length",
                                                          runtime_type);
        auto *length = impl_->builder->CreateCall(
            runtime_type, runtime.getCallee(),
            {array,
             llvm::ConstantInt::get(ctx, llvm::APInt(32, *kind, false))});
        auto *valid = impl_->builder->CreateAnd(
            impl_->builder->CreateICmpSGE(
                length, llvm::ConstantInt::get(ctx, llvm::APInt(64, 0))),
            impl_->builder->CreateICmpSLE(
                length,
                llvm::ConstantInt::get(
                    ctx, llvm::APInt(
                             64, std::numeric_limits<std::int32_t>::max()))));
        auto *failure = llvm::BasicBlock::Create(ctx, "array.length.failure",
                                                 impl_->current_function);
        auto *success = llvm::BasicBlock::Create(ctx, "array.length.success",
                                                 impl_->current_function);
        impl_->builder->CreateCondBr(valid, success, failure);
        impl_->builder->SetInsertPoint(failure);
        auto *message = impl_->builder->CreateGlobalString(
            "NIL native runtime error: array.length received an invalid "
            "array or a length outside the i32 range\n");
        impl_->builder->CreateCall(get_runtime_failure_function(), {message});
        impl_->builder->CreateUnreachable();
        impl_->builder->SetInsertPoint(success);
        impl_->builder->CreateRet(length);
        annotation_handled = true;
      } else if (ann.find("@intrinsic(\"array.get\")") != std::string::npos ||
                 ann.find("@intrinsic(\"array.get_u8\")") !=
                     std::string::npos ||
                 ann.find("@intrinsic(\"array.get_f32\")") !=
                     std::string::npos) {
        if (fn.params.size() != 2 || impl_->current_function->arg_size() != 2) {
          std::cerr << "[NativeCodegen] array.get intrinsic '" << fn.name
                    << "' requires a supported word-array signature\n";
          return false;
        }
        {
          auto *arr_arg = impl_->current_function->getArg(0);
          auto *idx_arg = impl_->current_function->getArg(1);
          const std::string array_type = nil_parameter_type(fn.params[0]);
          const std::string index_type = nil_parameter_type(fn.params[1]);
          const auto element_type =
              native_generic_argument(array_type, "array");
          if (!element_type || !native_array_kind_for_element(*element_type) ||
              normalize_nil_type(index_type) != "i32" ||
              canonical_array_type(fn.return_type) !=
                  canonical_array_type(*element_type) ||
              native_handle_kind_for_nil_type(array_type) !=
                  native_handle_kind::scalar_array ||
              !arr_arg->getType()->isPointerTy() ||
              !idx_arg->getType()->isIntegerTy() ||
              ((native_handle_kind_for_nil_type(fn.return_type) == native_handle_kind::text ||
                native_handle_kind_for_nil_type(fn.return_type) == native_handle_kind::anything ||
                is_array_tuple_type(fn.return_type))
                   ? !impl_->current_function->getReturnType()->isPointerTy()
                   : !impl_->current_function->getReturnType()->isIntegerTy())) {
            std::cerr << "[NativeCodegen] array.get intrinsic '" << fn.name
                      << "' requires a supported word-array signature\n";
            return false;
          }

          const auto kind = native_array_kind_for_element(*element_type);
          auto runtime_type = llvm::FunctionType::get(
              llvm::Type::getInt64Ty(ctx),
              {llvm::PointerType::getUnqual(ctx), llvm::Type::getInt32Ty(ctx),
               llvm::Type::getInt64Ty(ctx)},
              false);
          auto runtime = impl_->module->getOrInsertFunction("nyrt_array_get",
                                                            runtime_type);
          llvm::Value *value = impl_->builder->CreateCall(
              runtime_type, runtime.getCallee(),
              {arr_arg,
               llvm::ConstantInt::get(ctx, llvm::APInt(32, *kind, false)),
               impl_->coerceToI64(idx_arg)});
          if (*kind == NYRT_ARRAY_TEXT || *kind == NYRT_ARRAY_TUPLE || *kind == NYRT_ARRAY_ANYTHING)
            value = native_array_loaded_value(value, *element_type);
          else
            value = impl_->castInteger(value,
                                       impl_->current_function->getReturnType());
          impl_->builder->CreateRet(value);
          annotation_handled = true;
        }
      } else if (ann.find("@intrinsic(\"array.set\")") != std::string::npos ||
                 ann.find("@intrinsic(\"array.set_u8\")") !=
                     std::string::npos ||
                 ann.find("@intrinsic(\"array.set_f32\")") !=
                     std::string::npos) {
        if (fn.params.size() != 3 || impl_->current_function->arg_size() != 3) {
          std::cerr << "[NativeCodegen] array.set intrinsic '" << fn.name
                    << "' requires a supported word-array signature\n";
          return false;
        }
        {
          auto *arr_arg = impl_->current_function->getArg(0);
          auto *idx_arg = impl_->current_function->getArg(1);
          auto *val_arg = impl_->current_function->getArg(2);
          const std::string array_type = nil_parameter_type(fn.params[0]);
          const std::string index_type = nil_parameter_type(fn.params[1]);
          const auto element_type =
              native_generic_argument(array_type, "array");
          const std::string value_type = nil_parameter_type(fn.params[2]);
          if (!element_type || !native_array_kind_for_element(*element_type) ||
              normalize_nil_type(index_type) != "i32" ||
              canonical_array_type(value_type) !=
                  canonical_array_type(*element_type) ||
              native_handle_kind_for_nil_type(array_type) !=
                  native_handle_kind::scalar_array ||
              !arr_arg->getType()->isPointerTy() ||
              !idx_arg->getType()->isIntegerTy() ||
              ((native_handle_kind_for_nil_type(value_type) == native_handle_kind::text ||
                native_handle_kind_for_nil_type(value_type) == native_handle_kind::anything ||
                is_array_tuple_type(value_type))
                   ? !val_arg->getType()->isPointerTy()
                   : !val_arg->getType()->isIntegerTy()) ||
              !impl_->current_function->getReturnType()->isVoidTy()) {
            std::cerr << "[NativeCodegen] array.set intrinsic '" << fn.name
                      << "' requires a supported word-array signature\n";
            return false;
          }

          const auto kind = native_array_kind_for_element(*element_type);
          auto runtime_type = llvm::FunctionType::get(
              llvm::Type::getInt32Ty(ctx),
              {llvm::PointerType::getUnqual(ctx), llvm::Type::getInt32Ty(ctx),
               llvm::Type::getInt64Ty(ctx), llvm::Type::getInt64Ty(ctx)},
              false);
          auto runtime = impl_->module->getOrInsertFunction("nyrt_array_set",
                                                            runtime_type);
          auto *stored_value = native_storage_word(val_arg, *element_type, "array.set");
          if (!stored_value)
            return false;
          // Ny arrays define an out-of-bounds write as a no-op, matching the
          // interpreter. The runtime validates the opaque descriptor and
          // reports both an invalid handle and an out-of-range index as a
          // rejected write; neither case may mutate memory.
          impl_->builder->CreateCall(
              runtime_type, runtime.getCallee(),
              {arr_arg,
               llvm::ConstantInt::get(ctx, llvm::APInt(32, *kind, false)),
               impl_->coerceToI64(idx_arg),
               stored_value});
          impl_->builder->CreateRetVoid();
          annotation_handled = true;
        }
      }

      if (annotation_handled) {
        handled_intrinsic = true;
      } else if (dispatch_annotation) {
        std::cerr << "[NativeCodegen] unsupported native annotation '" << ann
                  << "' in reachable function '" << fn.name << "'\n";
        return false;
      }
    }

    if (handled_intrinsic)
      continue;

    nil_fault_analysis fault_analysis;
    bool function_has_fault_protocol = false;
    for (const auto &instruction : fn.body)
      function_has_fault_protocol = function_has_fault_protocol ||
          instruction.opname.rfind("FAULT_", 0) == 0;
    if (function_has_fault_protocol) {
      try {
        fault_analysis = analyze_nil_fault_function(fn, mod);
      } catch (const std::exception &error) {
        std::cerr << "[NativeCodegen] invalid fault state in '" << fn.name
                  << "': " << error.what() << '\n';
        return false;
      }
      if (fault_analysis.instructions.size() != fn.body.size()) {
        std::cerr << "[NativeCodegen] incomplete fault analysis in '" << fn.name << "'\n";
        return false;
      }
    }

    // Never treat an unknown or unimplemented NIL opcode as a no-op. Silent
    // omission produces native executables that appear valid while behaving
    // differently from nilinterpreter.
    for (const auto &instruction : fn.body) {
      const Opcode opcode = stringToOpcode(instruction.opname);
      if (!native_opcode_supported(opcode)) {
        std::cerr << "[NativeCodegen] unsupported NIL opcode '"
                  << instruction.opname << "' in function '" << fn.name << "'";
        if (instruction.debug_line > 0)
          std::cerr << " at Ny source line " << instruction.debug_line;
        std::cerr << '\n';
        return false;
      }

      if (opcode != Opcode::CALL)
        continue;

      const auto encoded_argument_count =
          parse_call_argument_count(instruction, fn.name);
      if (!encoded_argument_count)
        return false;

      const std::string &callee_name = instruction.args[0];
      if (const auto builtin_count =
              native_builtin_argument_count(mod, callee_name)) {
        if (*encoded_argument_count != *builtin_count) {
          std::cerr << "[NativeCodegen] CALL to builtin '" << callee_name
                    << "' in function '" << fn.name << "' encodes "
                    << *encoded_argument_count << " argument(s), expected "
                    << *builtin_count << '\n';
          return false;
        }
        continue;
      }

      const nil_function *callee = find_nil_function(mod, callee_name);
      if (!callee) {
        std::cerr << "[NativeCodegen] CALL in function '" << fn.name
                  << "' names undefined callee '" << callee_name << "'\n";
        return false;
      }
      if (*encoded_argument_count != callee->params.size()) {
        std::cerr << "[NativeCodegen] CALL to '" << callee_name
                  << "' in function '" << fn.name << "' encodes "
                  << *encoded_argument_count << " argument(s), but the callee "
                  << "declares " << callee->params.size() << '\n';
        return false;
      }
    }

    // Resolve labels up front; missing/duplicate targets
    // must never become silently dropped branches or fabricated returns.
    for (const auto &minstr : fn.body) {
      const Opcode opcode = stringToOpcode(minstr.opname);
      if (opcode == Opcode::LABEL || opcode == Opcode::JMP ||
          opcode == Opcode::JMP_IF || opcode == Opcode::JMP_IF_NOT) {
        if (minstr.args.size() != 1 || minstr.args[0].empty()) {
          std::cerr << "[NativeCodegen] " << minstr.opname
                    << " requires one nonempty label in function '" << fn.name
                    << "'\n";
          return false;
        }
      }
      if (opcode == Opcode::LABEL) {
        std::string label_name = minstr.args[0];
        if (impl_->label_map.count(label_name) != 0) {
          std::cerr << "[NativeCodegen] duplicate label '" << label_name
                    << "' in function '" << fn.name << "'\n";
          return false;
        }
        impl_->label_map[label_name] =
            llvm::BasicBlock::Create(ctx, label_name, impl_->current_function);
      }
    }
    for (const auto &minstr : fn.body) {
      const Opcode opcode = stringToOpcode(minstr.opname);
      if ((opcode == Opcode::JMP || opcode == Opcode::JMP_IF ||
           opcode == Opcode::JMP_IF_NOT) &&
          impl_->label_map.count(minstr.args[0]) == 0) {
        std::cerr << "[NativeCodegen] " << minstr.opname << " target '"
                  << minstr.args[0] << "' is undefined in function '" << fn.name
                  << "'\n";
        return false;
      }
    }

    // Each NIL edge carries its complete operand stack. Materialize that
    // stack as PHIs at labels rather than requiring source expressions to
    // discard or reorder values which were evaluated before a branch.
    struct IncomingOperandStack {
      bool established = false;
      std::vector<llvm::PHINode *> values;
      std::vector<std::string> types;
    };
    std::unordered_map<llvm::BasicBlock *, IncomingOperandStack> incoming_stacks;
    const auto stack_value_type = [&](llvm::Value *value) {
      std::string type = impl_->nilType(value);
      // Arithmetic sometimes has only its LLVM scalar type. Recover that
      // scalar identity, never infer nominal/handle identity from a pointer.
      if (type.empty()) {
        if (value->getType()->isIntegerTy(1))
          type = "bool";
        else if (value->getType()->isIntegerTy())
          type = "i" + std::to_string(value->getType()->getIntegerBitWidth());
        else if (value->getType()->isDoubleTy())
          type = "f64";
        else if (value->getType()->isFloatTy())
          type = "f32";
      }
      return type;
    };
    const auto record_stack_edge = [&](llvm::BasicBlock *target,
                                        llvm::BasicBlock *predecessor,
                                        const std::vector<llvm::Value *> *exception_prefix = nullptr) {
      const auto &edge_stack = exception_prefix ? *exception_prefix : impl_->value_stack;
      auto &incoming = incoming_stacks[target];
      if (!incoming.established) {
        incoming.established = true;
        for (llvm::Value *value : edge_stack) {
          // A target with instructions has already been lowered using its
          // established stack. Never invent a shape retroactively.
          auto *phi = llvm::PHINode::Create(
              value->getType(), 2, "ny.stack", target);
          const auto type = stack_value_type(value);
          impl_->setNilType(phi, type);
          incoming.values.push_back(phi);
          incoming.types.push_back(type);
        }
      }
      if (incoming.values.size() != edge_stack.size()) {
        std::cerr << "[NativeCodegen] incompatible operand-stack height at label '"
                  << target->getName().str() << "' in function '" << fn.name
                  << "': expected " << incoming.values.size() << ", found "
                  << edge_stack.size() << '\n';
        return false;
      }
      for (std::size_t slot = 0; slot < incoming.values.size(); ++slot) {
        llvm::Value *value = edge_stack[slot];
        const auto type = stack_value_type(value);
        if (incoming.values[slot]->getType() != value->getType() ||
            incoming.types[slot] != type) {
          std::cerr << "[NativeCodegen] incompatible operand-stack type at label '"
                    << target->getName().str() << "' in function '" << fn.name
                    << "' slot " << slot << ": expected '"
                    << incoming.types[slot] << "', found '" << type << "'\n";
          return false;
        }
        incoming.values[slot]->addIncoming(value, predecessor);
      }
      return true;
    };

    struct NativeFaultRegion {
      llvm::Value *saved_slots = nullptr;
      std::uint64_t saved_count = 0;
      llvm::AllocaInst *owned_depth = nullptr;
    };
    std::unordered_map<std::size_t, NativeFaultRegion> fault_regions;
    std::unordered_map<std::string, std::uint64_t> fault_slot_numbers;
    for (const auto &instruction : fn.body) {
      const auto opcode = stringToOpcode(instruction.opname);
      if (opcode == Opcode::FAULT_SAVE || opcode == Opcode::FAULT_MATCH ||
          opcode == Opcode::FAULT_BIND || opcode == Opcode::FAULT_DROP ||
          opcode == Opcode::FAULT_RESUME || opcode == Opcode::FAULT_RETHROW)
        if (!instruction.args.empty())
          fault_slot_numbers.emplace(instruction.args[0], fault_slot_numbers.size());
    }
    auto *fault_word = llvm::Type::getInt64Ty(ctx);
    auto *fault_pointer = llvm::PointerType::getUnqual(ctx);
    auto *fault_void = llvm::Type::getVoidTy(ctx);
    auto *fault_i32 = llvm::Type::getInt32Ty(ctx);
    const auto fault_helper = [&](const char *name, llvm::Type *result,
                                  const std::vector<llvm::Type *> &arguments) {
      return impl_->module->getOrInsertFunction(name,
          llvm::FunctionType::get(result, arguments, false));
    };
    const auto fault_slot = [&](const std::string &name) {
      return llvm::ConstantInt::get(fault_word, fault_slot_numbers.at(name));
    };
    // Saved-slot identities come from CFG analysis, not textual lowering
    // order. A valid region can dominate a body written earlier in the file.
    if (function_has_fault_protocol) {
      for (std::size_t pc = 0; pc < fn.body.size(); ++pc) {
        if (stringToOpcode(fn.body[pc].opname) != Opcode::FAULT_ENTER ||
            !fault_analysis.instructions[pc].reachable)
          continue;
        std::vector<llvm::Constant *> saved_ids;
        for (const auto &slot : fault_analysis.instructions[pc].saved_slots)
          saved_ids.push_back(fault_slot(slot));
        const auto count = saved_ids.size();
        if (saved_ids.empty()) saved_ids.push_back(llvm::ConstantInt::get(fault_word, 0));
        auto *layout = llvm::ArrayType::get(fault_word, saved_ids.size());
        auto *snapshot = new llvm::GlobalVariable(*impl_->module, layout, true,
            llvm::GlobalValue::PrivateLinkage, llvm::ConstantArray::get(layout, saved_ids),
            "ny.fault.saved.slots");
        llvm::IRBuilder<> entry(ctx);
        entry.SetInsertPoint(&impl_->current_function->getEntryBlock(),
                             impl_->current_function->getEntryBlock().begin());
        auto *owned_depth = entry.CreateAlloca(fault_word, nullptr, "ny.owned.region.depth");
        fault_regions.emplace(pc, NativeFaultRegion{snapshot, count, owned_depth});
      }
    }
    llvm::BasicBlock *fault_return = nullptr;
    const auto emit_fault_transfer = [&](std::size_t instruction_index) {
      const nil_fault_instruction_state *state = function_has_fault_protocol
          ? &fault_analysis.instructions[instruction_index] : nullptr;
      if (state && !state->regions.empty()) {
        const auto &region = state->regions.back();
        const auto snapshot = fault_regions.find(region.enter_pc);
        if (snapshot == fault_regions.end() ||
            impl_->value_stack.size() < region.stack_height ||
            region.dispatch_pc >= fn.body.size() ||
            fn.body[region.dispatch_pc].args.empty()) {
          std::cerr << "[NativeCodegen] fault edge has no exact protected operand prefix in '"
                    << fn.name << "'\n";
          return false;
        }
        const auto destination = impl_->label_map.find(fn.body[region.dispatch_pc].args[0]);
        if (destination == impl_->label_map.end()) return false;
        auto unwind = fault_helper("nyrt_fault_v1_unwind", fault_void, {fault_pointer, fault_word});
        impl_->builder->CreateCall(unwind, {snapshot->second.saved_slots,
            llvm::ConstantInt::get(fault_word, snapshot->second.saved_count)});
        auto owned_unwind = fault_helper("nyrt_owned_v1_unwind", fault_void, {fault_word});
        impl_->builder->CreateCall(owned_unwind,
            {impl_->builder->CreateLoad(fault_word, snapshot->second.owned_depth)});
        // The validator forbids consuming/replacing operands below every
        // active region's floor. Even after CALL arguments or a RAISE payload
        // are popped, these SSA values are exactly the original ENTER prefix.
        const std::vector<llvm::Value *> prefix(impl_->value_stack.begin(),
            impl_->value_stack.begin() + region.stack_height);
        if (!record_stack_edge(destination->second, impl_->builder->GetInsertBlock(),
                               &prefix)) return false;
        impl_->builder->CreateBr(destination->second);
      } else {
        if (!fault_return) {
          fault_return = llvm::BasicBlock::Create(ctx, "ny.fault.propagate", impl_->current_function);
          llvm::IRBuilder<> propagation(fault_return);
          const auto type = impl_->current_function->getReturnType();
          if (type->isVoidTy()) propagation.CreateRetVoid();
          else propagation.CreateRet(llvm::Constant::getNullValue(type));
        }
        impl_->builder->CreateBr(fault_return);
      }
      return true;
    };
    const auto check_call_fault = [&](std::size_t instruction_index) {
      if (!fault_protocol_enabled) return true;
      auto pending = fault_helper("nyrt_fault_v1_pending", fault_i32, {});
      auto *status = impl_->builder->CreateCall(pending, {}, "ny.fault.pending");
      auto *failure = llvm::BasicBlock::Create(ctx, "ny.fault.call", impl_->current_function);
      auto *success = llvm::BasicBlock::Create(ctx, "ny.fault.call.success", impl_->current_function);
      impl_->builder->CreateCondBr(impl_->builder->CreateICmpNE(status,
          llvm::ConstantInt::get(fault_i32, 0)), failure, success);
      impl_->builder->SetInsertPoint(failure);
      if (!emit_fault_transfer(instruction_index)) return false;
      impl_->builder->SetInsertPoint(success);
      return true;
    };

    const auto emit_invalid_class_tag = [&]() {
      auto *message = impl_->builder->CreateGlobalString(
          "NIL native runtime error: invalid class identity\n");
      impl_->builder->CreateCall(get_runtime_failure_function(), {message});
      impl_->builder->CreateUnreachable();
    };
    const auto class_field_pointer = [&](llvm::Value *aggregate,
                                         const NativeStructLayout &view,
                                         const std::string &field) -> llvm::Value * {
      if (view.hidden_fields == 0)
        return impl_->builder->CreateStructGEP(
            view.llvm_type, aggregate, view.field_indices.at(field),
            "ny.field." + field);
      auto *tag = impl_->builder->CreateLoad(
          llvm::Type::getInt64Ty(ctx), aggregate, "ny.class.tag");
      auto *failure = llvm::BasicBlock::Create(
          ctx, "ny.class.field.invalid", impl_->current_function);
      auto *merge = llvm::BasicBlock::Create(
          ctx, "ny.class.field", impl_->current_function);
      auto *dispatch = impl_->builder->CreateSwitch(tag, failure);
      auto *result = llvm::PHINode::Create(
          llvm::PointerType::getUnqual(ctx), 0, "ny.class.field.address", merge);
      for (const auto &dynamic_class : mod.classes) {
        if (dynamic_class.is_abstract ||
            !nil_class_assignable(mod, view.definition->name, dynamic_class.name))
          continue;
        const auto *definition = find_nil_struct_definition(mod, dynamic_class.name);
        const auto layout = native_struct_layouts.find(definition);
        if (layout == native_struct_layouts.end() ||
            layout->second.field_indices.count(field) == 0)
          return nullptr;
        auto *branch = llvm::BasicBlock::Create(
            ctx, "ny.class.field." + dynamic_class.name, impl_->current_function);
        dispatch->addCase(llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx),
            native_class_tags.at(dynamic_class.name)), branch);
        impl_->builder->SetInsertPoint(branch);
        auto *address = impl_->builder->CreateStructGEP(
            layout->second.llvm_type, aggregate, layout->second.field_indices.at(field),
            "ny.field." + field);
        result->addIncoming(address, branch);
        impl_->builder->CreateBr(merge);
      }
      impl_->builder->SetInsertPoint(failure);
      emit_invalid_class_tag();
      impl_->builder->SetInsertPoint(merge);
      return result;
    };

    // Emit instructions
    bool skip_until_label =
        false; // Track if we're in unreachable code after RET
    for (size_t i = 0; i < fn.body.size(); ++i) {
      const auto &minstr = fn.body[i];
      Opcode op = stringToOpcode(minstr.opname);
      const bool preceding_unreachable = skip_until_label;

      // If we hit a LABEL, stop skipping - we're at a valid entry point
      if (op == Opcode::LABEL) {
        skip_until_label = false;
      }

      // Skip unreachable instructions (after RET, before next LABEL)
      if (skip_until_label && op != Opcode::LABEL) {
        continue;
      }

      // Legacy method spellings emitted by source and packaged NIL use the
      // same checked lowering as canonical indexing/length opcodes. Their
      // exact arity has already been verified above, and a real Ny function
      // with the same name always takes precedence over a builtin alias.
      bool scalar_array_method = false;
      if (op == Opcode::CALL && minstr.args.size() == 2 &&
          native_builtin_argument_count(mod, minstr.args[0])) {
        const auto &callee = minstr.args[0];
        if (callee == "array__len" || callee == "array__length") {
          op = Opcode::LEN;
        } else if (callee == "array__get" || callee == "array__set") {
          op = callee == "array__get" ? Opcode::GET_INDEX : Opcode::SET_INDEX;
          scalar_array_method = true;
        } else if (callee == "String__len" || callee == "String__length") {
          op = Opcode::STRLEN;
        }
      }

      // Check the stack before lowering, including cases whose old handlers
      // merely skipped work when popValue() returned null. Variable-arity
      // CALL/array/variant operations perform their exact checks below.
      std::size_t required_stack = 0;
      switch (op) {
      case Opcode::POP:
      case Opcode::STORE_LOCAL:
      case Opcode::STORE_GLOBAL:
      case Opcode::JMP_IF:
      case Opcode::JMP_IF_NOT:
      case Opcode::NEG:
      case Opcode::NOT:
      case Opcode::ALLOC:
      case Opcode::OWN_ALLOC:
      case Opcode::FREE:
      case Opcode::READ_PTR:
      case Opcode::GET_FIELD:
      case Opcode::LEN:
      case Opcode::IS_VARIANT:
      case Opcode::UNWRAP_VARIANT:
      case Opcode::CAST:
      case Opcode::BOX_ANY:
      case Opcode::UNBOX_ANY:
      case Opcode::IS_ANY_TYPE:
      case Opcode::TO_STRING:
      case Opcode::STRLEN:
      case Opcode::TIME_SLEEP:
      case Opcode::PARALLEL_MAP:
      case Opcode::FAULT_RAISE:
      case Opcode::CLASS_UPCAST:
      case Opcode::MATH_SIN:
      case Opcode::MATH_COS:
      case Opcode::MATH_TAN:
      case Opcode::MATH_ASIN:
      case Opcode::MATH_ACOS:
      case Opcode::MATH_ATAN:
      case Opcode::MATH_SQRT:
      case Opcode::MATH_EXP:
      case Opcode::MATH_LOG:
      case Opcode::MATH_LOG10:
      case Opcode::MATH_ABS:
      case Opcode::MATH_FLOOR:
      case Opcode::MATH_CEIL:
      case Opcode::MATH_ROUND:
      case Opcode::MATH_TRUNC:
      case Opcode::MATH_CBRT:
        required_stack = 1;
        break;
      case Opcode::ADD:
      case Opcode::SUB:
      case Opcode::MUL:
      case Opcode::DIV:
      case Opcode::MOD:
      case Opcode::BIT_AND:
      case Opcode::BIT_OR:
      case Opcode::BIT_XOR:
      case Opcode::SHL:
      case Opcode::SHR:
      case Opcode::PTR_ADD:
      case Opcode::PTR_SUB:
      case Opcode::CMP_EQ:
      case Opcode::CMP_NE:
      case Opcode::CMP_LT:
      case Opcode::CMP_LE:
      case Opcode::CMP_GT:
      case Opcode::CMP_GE:
      case Opcode::WRITE_PTR:
      case Opcode::SET_FIELD:
      case Opcode::GET_INDEX:
      case Opcode::STR_CONCAT:
      case Opcode::MATH_ATAN2:
      case Opcode::MATH_POW:
      case Opcode::MATH_MIN:
      case Opcode::MATH_MAX:
        required_stack = 2;
        break;
      case Opcode::SET_INDEX:
        required_stack = 3;
        break;
      default:
        break;
      }
      if (impl_->value_stack.size() < required_stack) {
        std::cerr << "[NativeCodegen] stack underflow at " << minstr.opname
                  << " in function '" << fn.name << "': requires "
                  << required_stack << " value(s), found "
                  << impl_->value_stack.size() << '\n';
        return false;
      }

      // Builtins dispatched directly from a call can allocate in the runtime;
      // publish their call site as well as explicit allocating instructions.
      switch (op) {
      case Opcode::ALLOC:
      case Opcode::OWN_ALLOC:
      case Opcode::NEW_ARRAY:
      case Opcode::NEW_ARRAY_LITERAL:
      case Opcode::NEW_STRUCT:
      case Opcode::BOX_ANY:
      case Opcode::NEW_VARIANT:
      case Opcode::STR_CONCAT:
      case Opcode::TO_STRING:
      case Opcode::CALL:
      case Opcode::CALL_METHOD:
      case Opcode::INTEROP_CALL:
      case Opcode::PARALLEL_MAP:
        emit_profile_site(minstr.source_file.empty() ? fn.source_file : minstr.source_file,
                          minstr.debug_line);
        break;
      default:
        break;
      }

      switch (op) {
      case Opcode::PUSH_CONST:
        if (!minstr.args.empty()) {
          std::string arg = minstr.args[0];
          // Strip quotes if present
          if (arg.size() >= 2 && arg.front() == '"' && arg.back() == '"') {
            arg = arg.substr(1, arg.size() - 2);
          }
          try {
            std::size_t consumed = 0;
            if (is_floating_literal_spelling(arg)) {
              double value = 0.0;
              if (const auto *message = parse_floating_literal(arg, value))
                throw std::invalid_argument(message);
              impl_->pushValue(
                  llvm::ConstantFP::get(llvm::Type::getDoubleTy(ctx), value),
                  "f64");
            } else {
              const bool hexadecimal = has_hexadecimal_integer_prefix(arg);
              const std::int64_t value =
                  std::stoll(arg, &consumed, hexadecimal ? 0 : 10);
              if (consumed != arg.size())
                throw std::invalid_argument("trailing numeric characters");
              impl_->pushValue(
                  llvm::ConstantInt::get(ctx, llvm::APInt(64, value)), "i64");
            }
          } catch (const std::exception &error) {
            std::cerr << "[NativeCodegen] invalid numeric literal '" << arg
                      << "' in function '" << fn.name << "': " << error.what()
                      << '\n';
            return false;
          }
        }
        break;

      case Opcode::PUSH_CONST_STR:
        if (!minstr.args.empty()) {
          const std::string &arg = minstr.args[0];
          auto it = impl_->string_constants_map.find(arg);
          if (it == impl_->string_constants_map.end()) {
            std::cerr << "[NativeCodegen] unknown string constant '" << arg
                      << "'\n";
            return false;
          }
          impl_->pushValue(it->second, "text");
        }
        break;

      case Opcode::POP:
        if (!impl_->value_stack.empty())
          require_initialized_struct(impl_->popValue(), "POP");
        break;

      case Opcode::NOP:
        break;

      case Opcode::PARALLEL_MAP: {
        auto *inputs = impl_->popValue();
        if (minstr.args.size() != 3 || !inputs || !inputs->getType()->isPointerTy() ||
            normalize_nil_type(impl_->nilType(inputs)) != "array<" + minstr.args[1] + ">") {
          std::cerr << "[NativeCodegen] PARALLEL_MAP requires the exact input array type\n";
          return false;
        }
        auto *pointer = llvm::PointerType::getUnqual(ctx);
        auto *i32 = llvm::Type::getInt32Ty(ctx);
        auto *type = llvm::FunctionType::get(pointer, {pointer, i32, i32, pointer}, false);
        auto runtime = impl_->module->getOrInsertFunction("nyrt_parallel_map_v1", type);
        auto *result = impl_->builder->CreateCall(runtime,
            {inputs, llvm::ConstantInt::get(i32, *native_array_kind_for_element(minstr.args[1])),
             llvm::ConstantInt::get(i32, *native_array_kind_for_element(minstr.args[2])),
             parallel_callbacks.at(minstr.args[0])});
        impl_->pushValue(result, "array<" + minstr.args[2] + ">");
        break;
      }

      case Opcode::OWN_ENTER:
      case Opcode::OWN_LEAVE: {
        if (!minstr.args.empty()) {
          std::cerr << "[NativeCodegen] affine scope opcode accepts no operands\n";
          return false;
        }
        const char *name = op == Opcode::OWN_ENTER ? "nyrt_owned_v1_enter" : "nyrt_owned_v1_leave";
        impl_->builder->CreateCall(impl_->module->getOrInsertFunction(name,
            llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), {}, false)));
        break;
      }
      case Opcode::OWN_BIND:
      case Opcode::OWN_MOVE: {
        if (minstr.args.size() != 1 || !impl_->locals_map.count(minstr.args[0])) {
          std::cerr << "[NativeCodegen] affine opcode requires an existing local slot\n";
          return false;
        }
        auto *local = impl_->locals_map.at(minstr.args[0]);
        const auto type = impl_->local_nil_types.at(minstr.args[0]);
        const auto element = native_generic_argument(type, "ptr");
        if (!local->getAllocatedType()->isPointerTy() || !element ||
            (*element != "i32" && *element != "i64" && *element != "u8")) {
          std::cerr << "[NativeCodegen] affine local must hold a scalar allocation pointer\n";
          return false;
        }
        auto *word = llvm::Type::getInt64Ty(ctx);
        auto *pointer = llvm::PointerType::getUnqual(ctx);
        auto *identity = impl_->builder->CreatePtrToInt(local, word);
        if (op == Opcode::OWN_BIND) {
          auto runtime = impl_->module->getOrInsertFunction("nyrt_owned_v1_bind",
              llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), {word, pointer}, false));
          impl_->builder->CreateCall(runtime, {identity, impl_->builder->CreateLoad(pointer, local)});
        } else {
          auto runtime = impl_->module->getOrInsertFunction("nyrt_owned_v1_take",
              llvm::FunctionType::get(pointer, {word}, false));
          auto *value = impl_->builder->CreateCall(runtime, {identity});
          impl_->builder->CreateStore(llvm::ConstantPointerNull::get(pointer), local);
          impl_->pushValue(value, type);
        }
        break;
      }

      case Opcode::TIME_NOW: {
        if (!minstr.args.empty()) {
          std::cerr << "[NativeCodegen] TIME_NOW accepts no operands\n";
          return false;
        }
        auto *type = llvm::FunctionType::get(llvm::Type::getInt64Ty(ctx), {}, false);
        auto runtime = impl_->module->getOrInsertFunction("nyrt_time_now_ms", type);
        impl_->pushValue(impl_->builder->CreateCall(type, runtime.getCallee(), {}), "i64");
        break;
      }

      case Opcode::TIME_SLEEP: {
        auto *duration = impl_->popValue();
        const auto type_name = duration ? normalize_nil_type(impl_->nilType(duration)) : "";
        if (!minstr.args.empty() || !duration || !duration->getType()->isIntegerTy() ||
            (type_name != "i32" && type_name != "i64" && type_name != "u8")) {
          std::cerr << "[NativeCodegen] TIME_SLEEP requires one integer duration\n";
          return false;
        }
        auto *type = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx),
                                            {llvm::Type::getInt64Ty(ctx)}, false);
        auto runtime = impl_->module->getOrInsertFunction("nyrt_sleep_ms", type);
        impl_->builder->CreateCall(type, runtime.getCallee(), {impl_->coerceToI64(duration)});
        break;
      }

      case Opcode::INLINE_ASM: {
        if (minstr.args.size() != 4) {
          std::cerr << "[NativeCodegen] INLINE_ASM requires four operands in "
                       "function '" << fn.name << "'\n";
          return false;
        }
        std::size_t inputs = 0;
        std::size_t outputs = 0;
        try {
          std::size_t consumed = 0;
          inputs = std::stoull(minstr.args[2], &consumed, 10);
          if (consumed != minstr.args[2].size() || inputs > 8)
            throw std::invalid_argument("input count");
          outputs = std::stoull(minstr.args[3], &consumed, 10);
          if (consumed != minstr.args[3].size() || outputs > 1)
            throw std::invalid_argument("output count");
        } catch (const std::exception &) {
          std::cerr << "[NativeCodegen] invalid INLINE_ASM operand counts in "
                       "function '" << fn.name << "'\n";
          return false;
        }
        const std::string *assembly = nullptr;
        for (const auto &constant : mod.string_constants) {
          if (constant.first != minstr.args[1])
            continue;
          if (assembly) {
            std::cerr << "[NativeCodegen] ambiguous INLINE_ASM template '"
                      << minstr.args[1] << "'\n";
            return false;
          }
          assembly = &constant.second;
        }
        if (!assembly) {
          std::cerr << "[NativeCodegen] undefined INLINE_ASM template '"
                    << minstr.args[1] << "'\n";
          return false;
        }
        const std::string contract_error =
            inline_asm_contract_error(minstr.args[0], *assembly, inputs, outputs);
        if (!contract_error.empty()) {
          std::cerr << "[NativeCodegen] " << contract_error << " in function '"
                    << fn.name << "'\n";
          return false;
        }
        const llvm::Triple target(impl_->module->getTargetTriple());
        if (target.getArch() != llvm::Triple::x86_64) {
          std::cerr << "[NativeCodegen] INLINE_ASM target '"
                    << minstr.args[0] << "' does not match native target '"
                    << target.str() << "'\n";
          return false;
        }
        if (impl_->value_stack.size() < inputs) {
          std::cerr << "[NativeCodegen] stack underflow at INLINE_ASM in "
                       "function '" << fn.name << "'\n";
          return false;
        }
        auto *word = llvm::Type::getInt64Ty(ctx);
        std::vector<llvm::Value *> arguments(inputs);
        for (std::size_t index = inputs; index > 0; --index) {
          auto *value = impl_->popValue();
          const std::string value_type =
              normalize_nil_type(impl_->nilType(value));
          if (!value->getType()->isIntegerTy(64) ||
              (!value_type.empty() && value_type != "i64")) {
            std::cerr << "[NativeCodegen] INLINE_ASM inputs must have type i64 "
                         "in function '" << fn.name << "'\n";
            return false;
          }
          arguments[index - 1] = value;
        }
        std::vector<llvm::Type *> argument_types(inputs, word);
        llvm::Type *result_type = outputs ? static_cast<llvm::Type *>(word)
                                          : llvm::Type::getVoidTy(ctx);
        auto *signature =
            llvm::FunctionType::get(result_type, argument_types, false);
        std::string constraints = outputs ? "=&r" : "";
        for (std::size_t index = 0; index < inputs; ++index) {
          if (!constraints.empty())
            constraints += ',';
          constraints += 'r';
        }
        if (!constraints.empty())
          constraints += ',';
        constraints += "~{memory},~{dirflag},~{fpsr},~{flags}";
        auto *inline_assembly = llvm::InlineAsm::get(
            signature, *assembly, constraints, true, true,
            llvm::InlineAsm::AD_ATT);
        auto *result = impl_->builder->CreateCall(signature, inline_assembly,
                                                   arguments);
        if (outputs)
          impl_->pushValue(result, "i64");
        break;
      }

      case Opcode::PTR_ADD:
      case Opcode::PTR_SUB: {
        const bool subtract = op == Opcode::PTR_SUB;
        auto *rhs = impl_->popValue();
        auto *lhs = impl_->popValue();
        if (!lhs || !rhs) {
          std::cerr << "[NativeCodegen] " << minstr.opname
                    << " requires two operands in function '" << fn.name
                    << "'\n";
          return false;
        }

        const std::string lhs_nil_type = impl_->nilType(lhs);
        const auto pointer_element =
            native_generic_argument(lhs_nil_type, "ptr");
        const std::string rhs_nil_type =
            normalize_nil_type(impl_->nilType(rhs));
        const auto pointer_kind =
            pointer_element ? native_array_kind_for_element(*pointer_element)
                            : std::nullopt;
        auto *storage_type = pointer_element
                                 ? native_pointer_storage_type(*pointer_element)
                                 : nullptr;
        if (!pointer_element ||
            native_handle_kind_for_nil_type(lhs_nil_type) !=
                native_handle_kind::integer_pointer ||
            !pointer_kind || !storage_type ||
            !native_value_is_numeric_scalar(rhs) ||
            !rhs->getType()->isIntegerTy() ||
            (rhs_nil_type != "i32" && rhs_nil_type != "i64")) {
          std::cerr << "[NativeCodegen] " << minstr.opname
                    << " requires lhs ptr<i32/i64/bool/u8> and rhs i32/i64 "
                       "in function '"
                    << fn.name << "'\n";
          return false;
        }

        auto *offset = impl_->coerceToI64(rhs);
        llvm::Value *result = nullptr;
        if (impl_->memory_mode == MemoryMode::SAFE_GC) {
          auto *runtime_type = llvm::FunctionType::get(
              llvm::PointerType::getUnqual(ctx),
              {llvm::PointerType::getUnqual(ctx), llvm::Type::getInt32Ty(ctx),
               llvm::Type::getInt64Ty(ctx), llvm::Type::getInt32Ty(ctx)},
              false);
          auto runtime = impl_->module->getOrInsertFunction(
              "nyrt_buffer_offset", runtime_type);
          result = impl_->builder->CreateCall(
              runtime_type, runtime.getCallee(),
              {lhs,
               llvm::ConstantInt::get(ctx,
                                      llvm::APInt(32, *pointer_kind, false)),
               offset,
               llvm::ConstantInt::get(
                   ctx, llvm::APInt(32, subtract ? 1 : 0, false))},
              subtract ? "ny.pointer.subtract" : "ny.pointer.add");
        } else {
          llvm::Value *element_offset = offset;
          if (subtract) {
            element_offset = impl_->builder->CreateSub(
                llvm::ConstantInt::get(ctx, llvm::APInt(64, 0, false)), offset,
                "ny.pointer.negative.offset");
          }
          result = impl_->builder->CreateGEP(
              storage_type, lhs, {element_offset},
              subtract ? "ny.pointer.subtract" : "ny.pointer.add");
        }
        impl_->pushValue(result, lhs_nil_type);
        break;
      }

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
      case Opcode::SHR: {
        const bool unary = op == Opcode::NEG;
        const bool shift = op == Opcode::SHL || op == Opcode::SHR;
        const bool bitwise = shift || op == Opcode::BIT_AND ||
                             op == Opcode::BIT_OR || op == Opcode::BIT_XOR;
        auto *rhs = unary ? nullptr : impl_->popValue();
        auto *lhs = impl_->popValue();
        if (!native_value_is_numeric_scalar(lhs) ||
            (!unary && !native_value_is_numeric_scalar(rhs))) {
          std::cerr << "[NativeCodegen] " << minstr.opname
                    << (unary ? " operand must be a numeric scalar in function '"
                              : " operands must be numeric scalars in function '")
                    << fn.name << "'\n";
          return false;
        }
        if (minstr.args.size() > 1) {
          std::cerr << "[NativeCodegen] arithmetic requires at most one scalar "
                       "type operand in function '" << fn.name << "'\n";
          return false;
        }
        // Legacy untyped NIL always operates on integer words or f64. Never
        // let an incidental LLVM i1/i32 value change its arithmetic contract.
        const std::string type = minstr.args.empty()
            ? (lhs->getType()->isFloatingPointTy() ||
               (rhs && rhs->getType()->isFloatingPointTy()) ? "f64" : "i64")
            : minstr.args.front();
        if (type != "i32" && type != "i64" && type != "u8" &&
            !(type == "f64" && !bitwise)) {
          std::cerr << "[NativeCodegen] unsupported scalar arithmetic type '"
                    << type << "' in function '" << fn.name << "'\n";
          return false;
        }
        const bool floating = type == "f64";
        const bool unsigned_integer = type == "u8";
        const unsigned width = type == "i32" ? 32 : unsigned_integer ? 8 : 64;
        llvm::Value *shift_count = nullptr;
        if (floating) {
          lhs = impl_->coerceToDouble(lhs);
          if (rhs) rhs = impl_->coerceToDouble(rhs);
        } else {
          if (!lhs->getType()->isIntegerTy() ||
              (rhs && !rhs->getType()->isIntegerTy())) {
            std::cerr << "[NativeCodegen] " << minstr.opname
                      << " operands must be integers in function '"
                      << fn.name << "'\n";
            return false;
          }
          // A count is validated at its full word width before converting it
          // to the left operand's width (e.g. u8 << 256 must not become << 0).
          if (shift) shift_count = impl_->coerceToI64(rhs);
          auto *integer_type = llvm::IntegerType::get(ctx, width);
          lhs = impl_->castInteger(lhs, integer_type);
          if (rhs) rhs = impl_->castInteger(rhs, integer_type);
        }
        const auto require_runtime_condition = [&](llvm::Value *valid,
                                                     const char *diagnostic) {
          auto *success = llvm::BasicBlock::Create(
              ctx, "arithmetic.success", impl_->current_function);
          auto *failure = llvm::BasicBlock::Create(
              ctx, "arithmetic.failure", impl_->current_function);
          impl_->builder->CreateCondBr(valid, success, failure);
          impl_->builder->SetInsertPoint(failure);
          auto *message = impl_->builder->CreateGlobalString(
              std::string("NIL interpreter runtime error: ") + diagnostic + "\n");
          impl_->builder->CreateCall(get_runtime_failure_function(), {message});
          impl_->builder->CreateUnreachable();
          impl_->builder->SetInsertPoint(success);
        };
        if (op == Opcode::DIV || op == Opcode::MOD) {
          auto *nonzero = floating
              ? impl_->builder->CreateFCmpUNE(
                    rhs, llvm::ConstantFP::get(rhs->getType(), 0.0))
              : impl_->builder->CreateICmpNE(
                    rhs, llvm::ConstantInt::get(rhs->getType(), 0));
          require_runtime_condition(nonzero, op == Opcode::DIV
              ? "Division by zero" : "Modulo by zero");
          if (!floating && !unsigned_integer) {
            auto *minimum = impl_->builder->CreateICmpEQ(
                lhs, llvm::ConstantInt::get(
                         lhs->getType(), llvm::APInt::getSignedMinValue(width)));
            auto *negative_one = impl_->builder->CreateICmpEQ(
                rhs, llvm::ConstantInt::getSigned(rhs->getType(), -1));
            require_runtime_condition(impl_->builder->CreateNot(
                impl_->builder->CreateAnd(minimum, negative_one)),
                op == Opcode::DIV ? "Integer division overflow"
                                  : "Integer modulo overflow");
          }
        }
        if (shift) {
          // Unsigned comparison also rejects every negative signed count.
          require_runtime_condition(impl_->builder->CreateICmpULT(
              shift_count, llvm::ConstantInt::get(shift_count->getType(), width)),
              "Shift count out of range");
        }
        llvm::Value *result = nullptr;
        switch (op) {
        case Opcode::ADD: result = floating ? impl_->builder->CreateFAdd(lhs, rhs)
                                           : impl_->builder->CreateAdd(lhs, rhs); break;
        case Opcode::SUB: result = floating ? impl_->builder->CreateFSub(lhs, rhs)
                                           : impl_->builder->CreateSub(lhs, rhs); break;
        case Opcode::MUL: result = floating ? impl_->builder->CreateFMul(lhs, rhs)
                                           : impl_->builder->CreateMul(lhs, rhs); break;
        case Opcode::DIV: result = floating ? impl_->builder->CreateFDiv(lhs, rhs)
                                 : unsigned_integer ? impl_->builder->CreateUDiv(lhs, rhs)
                                                    : impl_->builder->CreateSDiv(lhs, rhs); break;
        case Opcode::MOD: result = floating ? impl_->builder->CreateFRem(lhs, rhs)
                                 : unsigned_integer ? impl_->builder->CreateURem(lhs, rhs)
                                                    : impl_->builder->CreateSRem(lhs, rhs); break;
        case Opcode::NEG: result = floating ? impl_->builder->CreateFNeg(lhs)
                                           : impl_->builder->CreateNeg(lhs); break;
        case Opcode::BIT_AND: result = impl_->builder->CreateAnd(lhs, rhs); break;
        case Opcode::BIT_OR: result = impl_->builder->CreateOr(lhs, rhs); break;
        case Opcode::BIT_XOR: result = impl_->builder->CreateXor(lhs, rhs); break;
        case Opcode::SHL: result = impl_->builder->CreateShl(lhs, rhs); break;
        case Opcode::SHR: result = unsigned_integer
            ? impl_->builder->CreateLShr(lhs, rhs)
            : impl_->builder->CreateAShr(lhs, rhs); break;
        default: break;
        }
        // u8's existing ABI is a nonnegative integer word; keep the arithmetic
        // itself at eight bits, then restore that canonical representation.
        if (unsigned_integer)
          result = impl_->builder->CreateZExt(result, llvm::Type::getInt64Ty(ctx));
        impl_->pushValue(result, type);
        break;
      }

      case Opcode::FAULT_ENTER: {
        const auto &state = fault_analysis.instructions[i];
        if (impl_->value_stack.size() != state.stack_height) {
          std::cerr << "[NativeCodegen] FAULT_ENTER prefix disagrees with validated stack in '"
                    << fn.name << "'\n";
          return false;
        }
        const auto region = fault_regions.find(i);
        if (region != fault_regions.end()) {
          auto depth = fault_helper("nyrt_owned_v1_depth", fault_word, {});
          impl_->builder->CreateStore(impl_->builder->CreateCall(depth), region->second.owned_depth);
        }
        break;
      }

      case Opcode::FAULT_LEAVE:
        // Region stacks and every normal/exceptional CFG merge were validated
        // above. Native dispatch is direct CFG control flow, not host unwinding.
        break;

      case Opcode::FAULT_SAVE:
      case Opcode::FAULT_DROP: {
        const auto name = op == Opcode::FAULT_SAVE ? "nyrt_fault_v1_save" : "nyrt_fault_v1_drop";
        impl_->builder->CreateCall(fault_helper(name, fault_void, {fault_word}),
                                   {fault_slot(minstr.args[0])});
        break;
      }

      case Opcode::FAULT_MATCH:
      case Opcode::FAULT_BIND: {
        const auto *schema = find_nil_struct_definition(mod, minstr.args[1]);
        if (!schema || schema->name != minstr.args[1] ||
            native_struct_layouts.find(schema) == native_struct_layouts.end()) {
          std::cerr << "[NativeCodegen] " << minstr.opname
                    << " requires an exact supported struct schema in '" << fn.name << "'\n";
          return false;
        }
        auto *type = impl_->builder->CreateGlobalString(schema->name);
        auto *length = llvm::ConstantInt::get(fault_word, schema->name.size());
        if (op == Opcode::FAULT_MATCH) {
          auto *matched = impl_->builder->CreateCall(fault_helper("nyrt_fault_v1_match",
              fault_i32, {fault_word, fault_pointer, fault_word}),
              {fault_slot(minstr.args[0]), type, length});
          impl_->pushValue(impl_->builder->CreateICmpNE(matched,
              llvm::ConstantInt::get(fault_i32, 0)), "bool");
        } else {
          auto *payload = impl_->builder->CreateCall(fault_helper("nyrt_fault_v1_bind",
              fault_pointer, {fault_word, fault_pointer, fault_word}),
              {fault_slot(minstr.args[0]), type, length});
          impl_->pushValue(payload, schema->name);
        }
        break;
      }

      case Opcode::FAULT_RAISE: {
        auto *payload = impl_->popValue();
        const auto *schema = find_nil_struct_definition(mod, minstr.args[0]);
        if (!schema || schema->name != minstr.args[0] || !payload ||
            !payload->getType()->isPointerTy() ||
            impl_->nilType(payload) != compact_nil_type(schema->name) ||
            find_nil_struct_definition(mod, impl_->nilType(payload)) != schema ||
            native_struct_layouts.find(schema) == native_struct_layouts.end()) {
          std::cerr << "[NativeCodegen] FAULT_RAISE requires its exact initialized struct payload in '"
                    << fn.name << "'\n";
          return false;
        }
        require_initialized_struct(payload, "FAULT_RAISE " + schema->name);
        auto *type = impl_->builder->CreateGlobalString(schema->name);
        impl_->builder->CreateCall(fault_helper("nyrt_fault_v1_raise", fault_void,
            {fault_pointer, fault_word, fault_pointer, fault_word}),
            {type, llvm::ConstantInt::get(fault_word, schema->name.size()), payload,
             llvm::ConstantInt::get(fault_word, std::max(0, minstr.debug_line))});
        if (!emit_fault_transfer(i)) return false;
        skip_until_label = true;
        impl_->value_stack.clear();
        break;
      }

      case Opcode::FAULT_RESUME:
      case Opcode::FAULT_RETHROW: {
        impl_->builder->CreateCall(fault_helper("nyrt_fault_v1_resume", fault_void,
            {fault_word, fault_i32}), {fault_slot(minstr.args[0]),
             llvm::ConstantInt::get(fault_i32, op == Opcode::FAULT_RESUME ? 1 : 0)});
        if (!emit_fault_transfer(i)) return false;
        skip_until_label = true;
        impl_->value_stack.clear();
        break;
      }

      case Opcode::CALL: {
        const auto encoded_argument_count =
            parse_call_argument_count(minstr, fn.name);
        if (!encoded_argument_count)
          return false;

        const std::string &callee_name = minstr.args[0];
        const bool is_legacy_builtin =
            native_builtin_argument_count(mod, callee_name).has_value();
        if (impl_->value_stack.size() < *encoded_argument_count) {
          std::cerr << "[NativeCodegen] CALL to '" << callee_name
                    << "' in function '" << fn.name << "' requires "
                    << *encoded_argument_count << " stack argument(s), found "
                    << impl_->value_stack.size() << '\n';
          return false;
        }

        if (is_legacy_builtin && callee_name == "println") {
          auto printf_type = llvm::FunctionType::get(
              llvm::Type::getInt32Ty(ctx), {llvm::PointerType::getUnqual(ctx)},
              true);
          auto printf_fn =
              impl_->module->getOrInsertFunction("printf", printf_type);
          auto *value = impl_->popValue();
          if (value->getType()->isPointerTy()) {
            if (native_handle_kind_for_nil_type(impl_->nilType(value)) !=
                native_handle_kind::text) {
              std::cerr << "[NativeCodegen] println cannot consume non-text "
                           "opaque handle in function '"
                        << fn.name << "'\n";
              return false;
            }
            auto *fmt = impl_->builder->CreateGlobalString("%s\n");
            impl_->builder->CreateCall(printf_fn, {fmt, value});
          } else if (value->getType()->isIntegerTy()) {
            auto *integer = impl_->coerceToI64(value);
            auto *fmt = impl_->builder->CreateGlobalString("%lld\n");
            impl_->builder->CreateCall(printf_fn, {fmt, integer});
          } else if (value->getType()->isFloatingPointTy()) {
            auto *floating = value;
            if (floating->getType()->isFloatTy()) {
              floating = impl_->builder->CreateFPExt(
                  floating, llvm::Type::getDoubleTy(ctx));
            }
            auto *fmt = impl_->builder->CreateGlobalString("%g\n");
            impl_->builder->CreateCall(printf_fn, {fmt, floating});
          } else {
            std::cerr
                << "[NativeCodegen] println operand has unsupported native "
                   "type in function '"
                << fn.name << "'\n";
            return false;
          }
        } else if (is_legacy_builtin &&
                   (callee_name == "panic" || callee_name == "ny__lang__core__panic")) {
          auto *message = impl_->popValue();
          if (!message || !message->getType()->isPointerTy() ||
              native_handle_kind_for_nil_type(impl_->nilType(message)) != native_handle_kind::text) {
            std::cerr << "[NativeCodegen] panic requires a typed text message in function '"
                      << fn.name << "'\n";
            return false;
          }
          auto *type = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx),
              {llvm::PointerType::getUnqual(ctx)}, false);
          auto runtime = impl_->module->getOrInsertFunction("nyrt_panic", type);
          impl_->builder->CreateCall(type, runtime.getCallee(), {message});
          impl_->builder->CreateUnreachable();
          skip_until_label = true;
          impl_->value_stack.clear();
        } else if (is_legacy_builtin && callee_name == "String__substr") {
          auto *length = impl_->popValue();
          auto *start = impl_->popValue();
          auto *text = impl_->popValue();
          if (!text || !text->getType()->isPointerTy() ||
              native_handle_kind_for_nil_type(impl_->nilType(text)) != native_handle_kind::text ||
              !native_value_is_numeric_scalar(start) || !start->getType()->isIntegerTy() ||
              !native_value_is_numeric_scalar(length) || !length->getType()->isIntegerTy()) {
            std::cerr << "[NativeCodegen] String__substr requires text and two integer bounds in function '"
                      << fn.name << "'\n";
            return false;
          }
          auto *type = llvm::FunctionType::get(llvm::PointerType::getUnqual(ctx),
              {llvm::PointerType::getUnqual(ctx), llvm::Type::getInt64Ty(ctx),
               llvm::Type::getInt64Ty(ctx)}, false);
          auto runtime = impl_->module->getOrInsertFunction("nyrt_text_substr", type);
          auto *result = impl_->builder->CreateCall(type, runtime.getCallee(),
              {text, impl_->coerceToI64(start), impl_->coerceToI64(length)});
          emit_allocation_null_check(result, "text substring");
          impl_->pushValue(result, "text");
        } else if (is_legacy_builtin && callee_name == "alloc") {
          auto *size = impl_->popValue();
          if (!native_value_is_numeric_scalar(size) ||
              !size->getType()->isIntegerTy()) {
            std::cerr << "[NativeCodegen] alloc size must be an integer in "
                         "function '"
                      << fn.name << "'\n";
            return false;
          }
          size = impl_->coerceToI64(size);
          auto *size_failure = llvm::BasicBlock::Create(
              ctx, "alloc.size.failure", impl_->current_function);
          auto *size_success = llvm::BasicBlock::Create(
              ctx, "alloc.size.success", impl_->current_function);
          impl_->builder->CreateCondBr(
              impl_->builder->CreateNot(valid_native_size(size)), size_failure,
              size_success);

          impl_->builder->SetInsertPoint(size_failure);
          auto *size_message = impl_->builder->CreateGlobalString(
              "NIL native runtime error: invalid allocation size\n");
          impl_->builder->CreateCall(get_runtime_failure_function(),
                                     {size_message});
          impl_->builder->CreateUnreachable();

          impl_->builder->SetInsertPoint(size_success);
          auto *four = llvm::ConstantInt::get(ctx, llvm::APInt(64, 4));
          auto *cells = impl_->builder->CreateAdd(
              impl_->builder->CreateUDiv(size, four),
              impl_->builder->CreateZExt(
                  impl_->builder->CreateICmpNE(
                      impl_->builder->CreateURem(size, four),
                      llvm::ConstantInt::get(ctx, llvm::APInt(64, 0))),
                  llvm::Type::getInt64Ty(ctx)),
              "ny.alloc.i32.cells");
          auto *effective_cells = impl_->builder->CreateSelect(
              impl_->builder->CreateICmpEQ(
                  cells, llvm::ConstantInt::get(ctx, llvm::APInt(64, 0))),
              llvm::ConstantInt::get(ctx, llvm::APInt(64, 1)), cells);
          llvm::Value *pointer = nullptr;
          if (impl_->memory_mode == MemoryMode::SAFE_GC) {
            auto *runtime_type = llvm::FunctionType::get(
                llvm::PointerType::getUnqual(ctx),
                {llvm::Type::getInt32Ty(ctx), llvm::Type::getInt64Ty(ctx)},
                false);
            auto runtime = impl_->module->getOrInsertFunction(
                "nyrt_buffer_new", runtime_type);
            pointer = impl_->builder->CreateCall(
                runtime_type, runtime.getCallee(),
                {llvm::ConstantInt::get(
                     ctx, llvm::APInt(32, NYRT_ARRAY_I32, false)),
                 effective_cells});
          } else {
            auto *calloc_type = llvm::FunctionType::get(
                llvm::PointerType::getUnqual(ctx),
                {native_size_type, native_size_type}, false);
            auto calloc_fn =
                impl_->module->getOrInsertFunction("calloc", calloc_type);
            pointer = impl_->builder->CreateCall(
                calloc_type, calloc_fn.getCallee(),
                {coerce_to_native_size(effective_cells),
                 native_size_constant(sizeof(std::int32_t))});
          }
          emit_allocation_null_check(pointer, "alloc");
          // The raw memory builtins address i32 cells. Preserve that exact
          // contract in metadata so read_i32/write_i32 fail closed instead of
          // accepting any LLVM pointer representation.
          impl_->pushValue(pointer, "ptr<i32>");
        } else if (is_legacy_builtin && callee_name == "free") {
          auto *ptr = impl_->popValue();
          if (!require_native_builtin_pointer(ptr, callee_name, fn.name)) {
            return false;
          }
          if (impl_->memory_mode == MemoryMode::SAFE_GC) {
            auto *runtime_type = llvm::FunctionType::get(
                llvm::Type::getInt32Ty(ctx),
                {llvm::PointerType::getUnqual(ctx)}, false);
            auto runtime = impl_->module->getOrInsertFunction(
                "nyrt_buffer_free", runtime_type);
            impl_->builder->CreateCall(runtime_type, runtime.getCallee(),
                                       {ptr});
          } else {
            auto *free_type = llvm::FunctionType::get(
                llvm::Type::getVoidTy(ctx),
                {llvm::PointerType::getUnqual(ctx)}, false);
            auto free_fn =
                impl_->module->getOrInsertFunction("free", free_type);
            impl_->builder->CreateCall(free_type, free_fn.getCallee(), {ptr});
          }
        } else if (is_legacy_builtin && callee_name == "write_i32") {
          auto *val = impl_->popValue();
          auto *ptr = impl_->popValue();
          if (!native_value_is_numeric_scalar(val) ||
              !val->getType()->isIntegerTy()) {
            std::cerr << "[NativeCodegen] builtin 'write_i32' requires an "
                         "integer value in function '"
                      << fn.name << "'\n";
            return false;
          }
          if (!require_native_builtin_pointer(ptr, callee_name, fn.name))
            return false;
          auto *word = native_storage_word(val, "i32", "write_i32");
          if (!word)
            return false;
          if (impl_->memory_mode == MemoryMode::SAFE_GC) {
            auto *runtime_type = llvm::FunctionType::get(
                llvm::Type::getInt32Ty(ctx),
                {llvm::PointerType::getUnqual(ctx),
                 llvm::Type::getInt32Ty(ctx), llvm::Type::getInt64Ty(ctx),
                 llvm::Type::getInt64Ty(ctx)},
                false);
            auto runtime = impl_->module->getOrInsertFunction(
                "nyrt_buffer_set", runtime_type);
            impl_->builder->CreateCall(
                runtime_type, runtime.getCallee(),
                {ptr,
                 llvm::ConstantInt::get(
                     ctx, llvm::APInt(32, NYRT_ARRAY_I32, false)),
                 llvm::ConstantInt::get(ctx, llvm::APInt(64, 0, false)),
                 word});
          } else {
            auto *stored = impl_->builder->CreateTrunc(
                word, llvm::Type::getInt32Ty(ctx), "ny.write_i32.stored");
            impl_->builder->CreateStore(stored, ptr);
          }
        } else if (is_legacy_builtin && callee_name == "read_i32") {
          auto *ptr = impl_->popValue();
          if (!require_native_builtin_pointer(ptr, callee_name, fn.name)) {
            return false;
          }
          llvm::Value *val = nullptr;
          if (impl_->memory_mode == MemoryMode::SAFE_GC) {
            auto *runtime_type = llvm::FunctionType::get(
                llvm::Type::getInt64Ty(ctx),
                {llvm::PointerType::getUnqual(ctx),
                 llvm::Type::getInt32Ty(ctx), llvm::Type::getInt64Ty(ctx)},
                false);
            auto runtime = impl_->module->getOrInsertFunction(
                "nyrt_buffer_get", runtime_type);
            auto *word = impl_->builder->CreateCall(
                runtime_type, runtime.getCallee(),
                {ptr,
                 llvm::ConstantInt::get(
                     ctx, llvm::APInt(32, NYRT_ARRAY_I32, false)),
                 llvm::ConstantInt::get(ctx, llvm::APInt(64, 0, false))});
            val = impl_->builder->CreateTrunc(
                word, llvm::Type::getInt32Ty(ctx), "ny.read_i32.value");
          } else {
            val = impl_->builder->CreateLoad(llvm::Type::getInt32Ty(ctx), ptr);
          }
          impl_->pushValue(val, "i32");
        } else if (is_legacy_builtin && callee_name == "array__push") {
          auto *value = impl_->popValue();
          auto *array = impl_->popValue();
          if (!array || !array->getType()->isPointerTy() || !value) {
            std::cerr << "[NativeCodegen] builtin 'array__push' requires a "
                         "typed scalar array and matching value in function '"
                      << fn.name << "'\n";
            return false;
          }
          const std::string array_type = impl_->nilType(array);
          const auto element = native_generic_argument(array_type, "array");
          const auto kind =
              element ? native_array_kind_for_element(*element) : std::nullopt;
          if (!kind) {
            std::cerr << "[NativeCodegen] builtin 'array__push' cannot grow "
                         "array handle '"
                      << array_type << "' in function '" << fn.name << "'\n";
            return false;
          }
          require_initialized_struct(value, "array__push");
          auto *stored_value = native_storage_word(value, *element, "array__push");
          if (!stored_value)
            return false;

          auto runtime_type = llvm::FunctionType::get(
              llvm::Type::getInt32Ty(ctx),
              {llvm::PointerType::getUnqual(ctx), llvm::Type::getInt32Ty(ctx),
               llvm::Type::getInt64Ty(ctx)},
              false);
          auto runtime = impl_->module->getOrInsertFunction("nyrt_array_push",
                                                            runtime_type);
          auto *status = impl_->builder->CreateCall(
              runtime_type, runtime.getCallee(),
              {array,
               llvm::ConstantInt::get(ctx, llvm::APInt(32, *kind, false)),
               stored_value});
          auto *success = llvm::BasicBlock::Create(ctx, "array.push.success",
                                                   impl_->current_function);
          auto *failure = llvm::BasicBlock::Create(ctx, "array.push.failure",
                                                   impl_->current_function);
          impl_->builder->CreateCondBr(
              impl_->builder->CreateICmpNE(
                  status, llvm::ConstantInt::get(ctx, llvm::APInt(32, 0))),
              success, failure);
          impl_->builder->SetInsertPoint(failure);
          auto *message = impl_->builder->CreateGlobalString(
              "NIL native runtime error: array growth failed\n");
          impl_->builder->CreateCall(get_runtime_failure_function(), {message});
          impl_->builder->CreateUnreachable();
          impl_->builder->SetInsertPoint(success);
        } else {
          const auto callee_function = native_functions.find(callee_name);
          if (callee_function == native_functions.end()) {
            std::cerr << "[NativeCodegen] CALL in function '" << fn.name
                      << "' names undefined callee '" << callee_name << "'\n";
            return false;
          }
          auto *callee = callee_function->second;

          const size_t num_params = callee->arg_size();
          if (*encoded_argument_count != num_params) {
            std::cerr << "[NativeCodegen] CALL to '" << callee_name
                      << "' in function '" << fn.name << "' encodes "
                      << *encoded_argument_count << " argument(s), expected "
                      << num_params << '\n';
            return false;
          }

          const nil_function *nil_callee = find_nil_function(mod, callee_name);
          if (!nil_callee || nil_callee->params.size() != num_params) {
            std::cerr << "[NativeCodegen] CALL to '" << callee_name
                      << "' in function '" << fn.name
                      << "' has no matching typed NIL signature\n";
            return false;
          }

          std::vector<llvm::Value *> args(num_params);
          for (std::size_t reverse_index = 0; reverse_index < num_params;
               ++reverse_index) {
            const std::size_t index = num_params - 1 - reverse_index;
            auto *value = impl_->popValue();
            const std::string expected_nil_type =
                nil_parameter_type(nil_callee->params[index]);
            const std::string actual_nil_type = impl_->nilType(value);
            if (normalize_nil_type(expected_nil_type) == "f64") {
              if (normalize_nil_type(actual_nil_type) != "f64" ||
                  (!value->getType()->isDoubleTy() &&
                   !value->getType()->isIntegerTy(64))) {
                std::cerr << "[NativeCodegen] CALL to '" << callee_name
                          << "' requires an exact f64 argument\n";
                return false;
              }
              args[index] = value->getType()->isDoubleTy()
                  ? impl_->builder->CreateBitCast(
                        value, llvm::Type::getInt64Ty(ctx), "ny.call.f64.bits")
                  : value;
              continue;
            }
            if (normalize_nil_type(actual_nil_type) == "f64") {
              std::cerr << "[NativeCodegen] CALL to '" << callee_name
                        << "' cannot pass f64 to parameter '" << expected_nil_type
                        << "'\n";
              return false;
            }
            if (is_native_aggregate_nil_type(mod, expected_nil_type)) {
              if (!value->getType()->isPointerTy() ||
                  !native_aggregate_types_compatible(mod, actual_nil_type,
                                                     expected_nil_type)) {
                std::cerr << "[NativeCodegen] CALL to '" << callee_name
                          << "' in function '" << fn.name << "' passes '"
                          << (actual_nil_type.empty()
                                  ? std::string("untyped value")
                                  : actual_nil_type)
                          << "' to aggregate parameter '"
                          << nil_parameter_name(nil_callee->params[index])
                          << "' of type '" << expected_nil_type << "'\n";
                return false;
              }
              require_initialized_struct(value, "CALL " + callee_name);
              args[index] = value;
              continue;
            }
            if (const auto *expected_enum =
                    find_nil_enum_definition(mod, expected_nil_type)) {
              const auto *actual_enum =
                  find_nil_enum_definition(mod, actual_nil_type);
              if (!value->getType()->isIntegerTy() ||
                  actual_enum != expected_enum) {
                std::cerr << "[NativeCodegen] CALL to '" << callee_name
                          << "' in function '" << fn.name << "' passes '"
                          << (actual_nil_type.empty()
                                  ? std::string("untyped scalar")
                                  : actual_nil_type)
                          << "' to enum parameter '"
                          << nil_parameter_name(nil_callee->params[index])
                          << "' of type '" << expected_nil_type << "'\n";
                return false;
              }
              args[index] =
                  impl_->castInteger(value, callee->getArg(index)->getType());
              continue;
            }
            const auto expected_handle_kind =
                native_handle_kind_for_nil_type(expected_nil_type);
            if (expected_handle_kind == native_handle_kind::text ||
                expected_handle_kind == native_handle_kind::anything ||
                expected_handle_kind == native_handle_kind::scalar_array ||
                expected_handle_kind == native_handle_kind::integer_pointer) {
              if (!value->getType()->isPointerTy() ||
                  !native_handle_types_compatible(actual_nil_type,
                                                  expected_nil_type)) {
                std::cerr << "[NativeCodegen] CALL to '" << callee_name
                          << "' in function '" << fn.name << "' passes '"
                          << (actual_nil_type.empty()
                                  ? std::string("untyped value")
                                  : actual_nil_type)
                          << "' to handle parameter '"
                          << nil_parameter_name(nil_callee->params[index])
                          << "' of type '" << expected_nil_type << "'\n";
                return false;
              }
              args[index] = value;
              continue;
            }

            if (find_nil_enum_definition(mod, actual_nil_type) != nullptr) {
              std::cerr << "[NativeCodegen] CALL to '" << callee_name
                        << "' in function '" << fn.name << "' passes enum '"
                        << actual_nil_type << "' to scalar parameter '"
                        << nil_parameter_name(nil_callee->params[index])
                        << "' of type '" << expected_nil_type << "'\n";
              return false;
            }
            if (value->getType()->isPointerTy()) {
              std::cerr << "[NativeCodegen] CALL to '" << callee_name
                        << "' in function '" << fn.name
                        << "' passes opaque handle '" << impl_->nilType(value)
                        << "' to scalar parameter '"
                        << nil_parameter_name(nil_callee->params[index])
                        << "' of type '" << expected_nil_type << "'\n";
              return false;
            }
            if (value->getType()->isFloatingPointTy()) {
              std::cerr << "[NativeCodegen] floating-point parameters require "
                           "typed NIL function ABI metadata; cannot call '"
                        << callee_name << "' from function '" << fn.name
                        << "'\n";
              return false;
            }
            if (!value->getType()->isIntegerTy()) {
              std::cerr << "[NativeCodegen] unsupported argument type in CALL "
                           "to '"
                        << callee_name << "' from function '" << fn.name
                        << "'\n";
              return false;
            }
            if (normalize_nil_type(expected_nil_type) == "bool") {
              value = native_storage_word(value, "bool", "CALL");
              if (!value) return false;
            }
            args[index] =
                impl_->castInteger(value, callee->getArg(index)->getType());
          }

          auto *ret = impl_->builder->CreateCall(callee, args);
          // A propagation placeholder is never a Ny result. Check pending
          // state before decoding or pushing the returned value.
          if (!check_call_fault(i)) return false;
          if (!callee->getReturnType()->isVoidTy()) {
            llvm::Value *value = ret;
            if (normalize_nil_type(nil_callee->return_type) == "f64")
              value = impl_->builder->CreateBitCast(
                  value, llvm::Type::getDoubleTy(ctx), "ny.call.f64.value");
            impl_->pushValue(value, nil_callee->return_type);
          }
        }
        break;
      }

      case Opcode::CLASS_UPCAST: {
        if (minstr.args.size() != 1) return false;
        auto *value = impl_->popValue();
        const auto actual = impl_->nilType(value);
        if (!value || !value->getType()->isPointerTy() ||
            !find_nil_class(mod, actual) || !find_nil_class(mod, minstr.args[0]) ||
            !nil_class_assignable(mod, minstr.args[0], actual)) {
          std::cerr << "[NativeCodegen] CLASS_UPCAST requires a proven plain class ancestor view\n";
          return false;
        }
        require_initialized_struct(value, "CLASS_UPCAST");
        impl_->pushValue(value, minstr.args[0]);
        break;
      }

      case Opcode::CALL_METHOD: {
        if (minstr.args.size() != 3) return false;
        const auto declaration = resolve_nil_class_method(mod, minstr.args[0], minstr.args[1]);
        if (!declaration.method) return false;
        std::size_t argument_count = 0;
        try {
          std::size_t consumed = 0;
          argument_count = std::stoull(minstr.args[2], &consumed);
          if (consumed != minstr.args[2].size()) return false;
        } catch (const std::exception &) { return false; }
        if (argument_count != declaration.method->parameter_types.size() + 1 ||
            impl_->value_stack.size() < argument_count) return false;
        std::vector<llvm::Value *> arguments(argument_count);
        for (std::size_t n = argument_count; n != 0; --n)
          arguments[n - 1] = impl_->popValue();
        auto *receiver = arguments.front();
        const auto receiver_type = impl_->nilType(receiver);
        const auto reference = native_generic_arguments(receiver_type, "ref");
        const bool receiver_is_reference =
            normalize_nil_type(declaration.method->receiver_type).rfind("ref<", 0) == 0;
        const bool receiver_valid = receiver_is_reference
            ? reference && reference->size() == 1 && reference->front() == minstr.args[0]
            : find_nil_class(mod, receiver_type) &&
                  nil_class_assignable(mod, minstr.args[0], receiver_type);
        if (!receiver->getType()->isPointerTy() || !receiver_valid) {
          std::cerr << "[NativeCodegen] CALL_METHOD requires its checked class receiver shape\n";
          return false;
        }
        require_initialized_struct(receiver, "CALL_METHOD");
        for (std::size_t n = 1; n < argument_count; ++n) {
          auto *value = arguments[n];
          const auto &expected = declaration.method->parameter_types[n - 1];
          const auto actual = impl_->nilType(value);
          if (normalize_nil_type(expected) == "f64") {
            if (normalize_nil_type(actual) != "f64" ||
                (!value->getType()->isDoubleTy() && !value->getType()->isIntegerTy(64)))
              return false;
            if (value->getType()->isDoubleTy())
              value = impl_->builder->CreateBitCast(value, llvm::Type::getInt64Ty(ctx),
                                                     "ny.virtual.argument.f64");
          } else if (is_native_aggregate_nil_type(mod, expected)) {
            if (!value->getType()->isPointerTy() ||
                !native_aggregate_types_compatible(mod, actual, expected)) return false;
            require_initialized_struct(value, "CALL_METHOD argument");
          } else if (const auto *enumeration = find_nil_enum_definition(mod, expected)) {
            if (!value->getType()->isIntegerTy() ||
                find_nil_enum_definition(mod, actual) != enumeration) return false;
            value = impl_->castInteger(value, llvm::Type::getInt64Ty(ctx));
          } else if (native_handle_kind_for_nil_type(expected) != native_handle_kind::none) {
            if (!value->getType()->isPointerTy() ||
                !native_handle_types_compatible(actual, expected)) return false;
          } else {
            if (!value->getType()->isIntegerTy() ||
                !native_value_is_numeric_scalar(value)) return false;
            value = impl_->castInteger(value, llvm::Type::getInt64Ty(ctx));
          }
          if (normalize_nil_type(expected) == "bool") {
            value = native_storage_word(value, "bool", "CALL_METHOD");
            if (!value) return false;
          }
          arguments[n] = value;
        }
        auto *tag = impl_->builder->CreateLoad(llvm::Type::getInt64Ty(ctx), receiver,
                                                "ny.virtual.tag");
        auto *failure = llvm::BasicBlock::Create(ctx, "ny.virtual.invalid", impl_->current_function);
        auto *merge = llvm::BasicBlock::Create(ctx, "ny.virtual.result", impl_->current_function);
        auto *dispatch = impl_->builder->CreateSwitch(tag, failure);
        llvm::PHINode *result = nullptr;
        if (declaration.method->return_type != "void")
          result = llvm::PHINode::Create(llvm_type_for_nil_abi(declaration.method->return_type),
                                         0, "ny.virtual.value", merge);
        for (const auto &dynamic_class : mod.classes) {
          if (dynamic_class.is_abstract ||
              !nil_class_assignable(mod, minstr.args[0], dynamic_class.name)) continue;
          const auto target = declaration.method->is_virtual
              ? resolve_nil_class_method(mod, dynamic_class.name, minstr.args[1])
              : declaration;
          if (!target.method || target.method->is_abstract) return false;
          const auto native_target = native_functions.find(target.method->implementation);
          const auto *nil_target = find_nil_function(mod, target.method->implementation);
          if (native_target == native_functions.end() || !nil_target ||
              nil_target->params.size() != argument_count ||
              nil_target->return_type != declaration.method->return_type) return false;
          auto *branch = llvm::BasicBlock::Create(ctx, "ny.virtual." + dynamic_class.name,
                                                 impl_->current_function);
          dispatch->addCase(llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx),
              native_class_tags.at(dynamic_class.name)), branch);
          impl_->builder->SetInsertPoint(branch);
          auto *value = impl_->builder->CreateCall(native_target->second, arguments);
          // The typed branch proves the receiver's dynamic identity. The
          // declaring owner receives the same object handle, never a sliced
          // base prefix or an unchecked host function pointer.
          if (!check_call_fault(i)) return false;
          if (result) result->addIncoming(value, impl_->builder->GetInsertBlock());
          impl_->builder->CreateBr(merge);
        }
        impl_->builder->SetInsertPoint(failure);
        emit_invalid_class_tag();
        impl_->builder->SetInsertPoint(merge);
        if (result) {
          llvm::Value *value = result;
          if (normalize_nil_type(declaration.method->return_type) == "f64")
            value = impl_->builder->CreateBitCast(value, llvm::Type::getDoubleTy(ctx),
                                                   "ny.virtual.result.f64");
          impl_->pushValue(value, declaration.method->return_type);
        }
        break;
      }

      case Opcode::NEW_STRUCT: {
        if (minstr.args.empty()) {
          std::cerr << "[NativeCodegen] NEW_STRUCT in function '" << fn.name
                    << "' requires a struct type\n";
          return false;
        }

        const auto *definition =
            find_nil_struct_definition(mod, minstr.args[0]);
        const auto layout_it = definition == nullptr
                                   ? native_struct_layouts.end()
                                   : native_struct_layouts.find(definition);
        if (layout_it == native_struct_layouts.end()) {
          std::cerr << "[NativeCodegen] NEW_STRUCT in function '" << fn.name
                    << "' names unknown, ambiguous, or unsupported schema '"
                    << minstr.args[0] << "'\n";
          return false;
        }

        const auto type_size = impl_->module->getDataLayout().getTypeAllocSize(
            layout_it->second.llvm_type);
        if (type_size.isScalable()) {
          std::cerr << "[NativeCodegen] NEW_STRUCT schema '" << definition->name
                    << "' has a scalable native layout, which is unsupported\n";
          return false;
        }
        const std::uint64_t allocation_size =
            std::max<std::uint64_t>(1, type_size.getFixedValue());
        llvm::Value *pointer = nullptr;
        if (binding_cell_value_type(definition->name)) {
          // The shared NIL provenance validator proves that binding cells
          // stay within the active call chain and are constructed once in
          // the invocation prologue. Stack storage therefore has the exact
          // lifetime required by both memory modes, with no hidden manual-
          // mode allocation for the user to release.
          auto &entry = impl_->current_function->getEntryBlock();
          llvm::IRBuilder<> storage(&entry, entry.begin());
          auto *cell = storage.CreateAlloca(layout_it->second.llvm_type, nullptr,
                                            "nil.binding.cell");
          cell->setMetadata("nil.binding.cell", llvm::MDNode::get(ctx, {}));
          pointer = cell;
        } else if (impl_->memory_mode == MemoryMode::SAFE_GC) {
          auto *runtime_type =
              llvm::FunctionType::get(llvm::PointerType::getUnqual(ctx),
                                      {llvm::Type::getInt64Ty(ctx)}, false);
          auto runtime =
              impl_->module->getOrInsertFunction("nyrt_alloc", runtime_type);
          pointer = impl_->builder->CreateCall(
              runtime_type, runtime.getCallee(),
              {llvm::ConstantInt::get(
                  ctx, llvm::APInt(64, allocation_size, false))});
        } else {
          auto *calloc_type = llvm::FunctionType::get(
              llvm::PointerType::getUnqual(ctx),
              {native_size_type, native_size_type}, false);
          auto calloc_fn =
              impl_->module->getOrInsertFunction("calloc", calloc_type);
          pointer = impl_->builder->CreateCall(
              calloc_type, calloc_fn.getCallee(),
              {native_size_constant(1), native_size_constant(allocation_size)});
        }
        emit_allocation_null_check(pointer, "NEW_STRUCT " + definition->name);
        if (layout_it->second.hidden_fields != 0) {
          impl_->builder->CreateStore(llvm::ConstantInt::get(
              llvm::Type::getInt64Ty(ctx), native_class_tags.at(definition->name)), pointer);
        }
        native_struct_initializations.emplace(
            pointer, NativeStructInitialization{definition});
        struct_events[impl_->builder->GetInsertBlock()].push_back(
            {StructInitializationAction::allocate, pointer, {}, "NEW_STRUCT"});
        impl_->pushValue(pointer, definition->name);
        break;
      }

      case Opcode::GET_FIELD: {
        if (minstr.args.empty()) {
          std::cerr << "[NativeCodegen] GET_FIELD in function '" << fn.name
                    << "' requires a field name\n";
          return false;
        }
        auto *aggregate = impl_->popValue();
        const std::string aggregate_type = impl_->nilType(aggregate);
        const auto *definition =
            find_native_aggregate_definition(mod, aggregate_type);
        const auto layout_it = definition == nullptr
                                   ? native_struct_layouts.end()
                                   : native_struct_layouts.find(definition);
        if (!aggregate || !aggregate->getType()->isPointerTy() ||
            layout_it == native_struct_layouts.end()) {
          std::cerr << "[NativeCodegen] GET_FIELD '" << minstr.args[0]
                    << "' in function '" << fn.name
                    << "' requires a typed supported struct, got '"
                    << (aggregate_type.empty() ? std::string("untyped value")
                                               : aggregate_type)
                    << "'\n";
          return false;
        }

        const auto field_it =
            layout_it->second.field_indices.find(minstr.args[0]);
        if (field_it == layout_it->second.field_indices.end()) {
          std::cerr << "[NativeCodegen] GET_FIELD in function '" << fn.name
                    << "' names unknown field '" << minstr.args[0]
                    << "' on struct '" << definition->name << "'\n";
          return false;
        }

        const std::size_t field_index = field_it->second;
        require_initialized_struct(aggregate, "GET_FIELD " + minstr.args[0]);
        auto *field_pointer = class_field_pointer(aggregate, layout_it->second, minstr.args[0]);
        if (!field_pointer) return false;
        auto *field_value = impl_->builder->CreateLoad(
            layout_it->second.llvm_type->getElementType(field_index),
            field_pointer, "ny.field.value." + minstr.args[0]);
        impl_->pushValue(field_value,
                         layout_it->second.field_nil_types[field_index]);
        break;
      }

      case Opcode::SET_FIELD: {
        if (minstr.args.empty()) {
          std::cerr << "[NativeCodegen] SET_FIELD in function '" << fn.name
                    << "' requires a field name\n";
          return false;
        }
        auto *field_value = impl_->popValue();
        if (!field_value || impl_->value_stack.empty()) {
          std::cerr << "[NativeCodegen] SET_FIELD '" << minstr.args[0]
                    << "' in function '" << fn.name
                    << "' requires a struct and field value on the stack\n";
          return false;
        }

        // Like nilinterpreter's StructValue, the struct remains on the operand
        // stack after mutation. Local copies therefore retain deliberate
        // reference/alias semantics rather than implicit deep-copy behavior.
        auto *aggregate = impl_->value_stack.back();
        const std::string aggregate_type = impl_->nilType(aggregate);
        const auto *definition =
            find_native_aggregate_definition(mod, aggregate_type);
        const auto layout_it = definition == nullptr
                                   ? native_struct_layouts.end()
                                   : native_struct_layouts.find(definition);
        if (!aggregate->getType()->isPointerTy() ||
            layout_it == native_struct_layouts.end()) {
          std::cerr << "[NativeCodegen] SET_FIELD '" << minstr.args[0]
                    << "' in function '" << fn.name
                    << "' requires a typed supported struct, got '"
                    << (aggregate_type.empty() ? std::string("untyped value")
                                               : aggregate_type)
                    << "'\n";
          return false;
        }

        const auto field_it =
            layout_it->second.field_indices.find(minstr.args[0]);
        if (field_it == layout_it->second.field_indices.end()) {
          std::cerr << "[NativeCodegen] SET_FIELD in function '" << fn.name
                    << "' names unknown field '" << minstr.args[0]
                    << "' on struct '" << definition->name << "'\n";
          return false;
        }

        const std::size_t field_index = field_it->second;
        const std::string &expected_type =
            layout_it->second.field_nil_types[field_index];
        const std::string actual_type = impl_->nilType(field_value);
        const auto expected_handle_kind =
            native_handle_kind_for_nil_type(expected_type);
        const bool expects_handle =
            expected_handle_kind == native_handle_kind::text ||
            expected_handle_kind == native_handle_kind::anything ||
            expected_handle_kind == native_handle_kind::scalar_array ||
            expected_handle_kind == native_handle_kind::integer_pointer;

        if (is_native_aggregate_nil_type(mod, expected_type)) {
          if (!field_value->getType()->isPointerTy() ||
              !native_aggregate_types_compatible(mod, actual_type, expected_type)) {
            std::cerr << "[NativeCodegen] SET_FIELD '" << minstr.args[0]
                      << "' in function '" << fn.name
                      << "' expects aggregate type '" << expected_type
                      << "', got '"
                      << (actual_type.empty() ? std::string("untyped value")
                                              : actual_type)
                      << "'\n";
            return false;
          }
          require_initialized_struct(field_value, "SET_FIELD " + minstr.args[0]);
        } else if (expects_handle) {
          if (!field_value->getType()->isPointerTy() ||
              !native_handle_types_compatible(actual_type, expected_type)) {
            std::cerr << "[NativeCodegen] SET_FIELD '" << minstr.args[0]
                      << "' in function '" << fn.name
                      << "' expects native handle type '" << expected_type
                      << "', got '"
                      << (actual_type.empty() ? std::string("untyped value")
                                              : actual_type)
                      << "'\n";
            return false;
          }
        } else if (const auto *expected_enum =
                       find_nil_enum_definition(mod, expected_type)) {
          const auto *actual_enum = find_nil_enum_definition(mod, actual_type);
          if (!field_value->getType()->isIntegerTy() ||
              actual_enum != expected_enum) {
            std::cerr << "[NativeCodegen] SET_FIELD '" << minstr.args[0]
                      << "' in function '" << fn.name << "' expects enum '"
                      << expected_type << "', got '"
                      << (actual_type.empty() ? std::string("untyped value")
                                              : actual_type)
                      << "'\n";
            return false;
          }
        } else if (expected_type == "f64") {
          if (normalize_nil_type(actual_type) != "f64" ||
              (!field_value->getType()->isDoubleTy() &&
               !field_value->getType()->isIntegerTy(64))) {
            std::cerr << "[NativeCodegen] SET_FIELD '" << minstr.args[0]
                      << "' in function '" << fn.name
                      << "' requires an exact f64 field value, got '"
                      << (actual_type.empty() ? std::string("untyped value")
                                              : actual_type)
                      << "'\n";
            return false;
          }
          if (field_value->getType()->isIntegerTy(64))
            field_value = impl_->builder->CreateBitCast(
                field_value, llvm::Type::getDoubleTy(ctx), "ny.field.f64.value");
        } else {
          if (!field_value->getType()->isIntegerTy() ||
              !native_value_is_numeric_scalar(field_value) ||
              normalize_nil_type(actual_type) == "f64") {
            std::cerr << "[NativeCodegen] SET_FIELD '" << minstr.args[0]
                      << "' in function '" << fn.name
                      << "' expects scalar word type '" << expected_type
                      << "', got '"
                      << (actual_type.empty() ? std::string("untyped value")
                                              : actual_type)
                      << "'\n";
            return false;
          }
          if (expected_type == "u8" || expected_type == "bool") {
            field_value = native_storage_word(field_value, expected_type, "SET_FIELD");
            if (!field_value)
              return false;
          }
        }

        auto *storage_type =
            layout_it->second.llvm_type->getElementType(field_index);
        if (field_value->getType() != storage_type) {
          if (field_value->getType()->isIntegerTy() &&
              storage_type->isIntegerTy()) {
            field_value = impl_->castInteger(field_value, storage_type);
          } else if (field_value->getType()->isPointerTy() &&
                     storage_type->isPointerTy()) {
            field_value =
                impl_->builder->CreatePointerCast(field_value, storage_type);
          } else {
            std::cerr << "[NativeCodegen] SET_FIELD '" << minstr.args[0]
                      << "' in function '" << fn.name
                      << "' cannot lower its value to the schema field type\n";
            return false;
          }
        }

        auto *field_pointer = class_field_pointer(aggregate, layout_it->second, minstr.args[0]);
        if (!field_pointer) return false;
        impl_->builder->CreateStore(field_value, field_pointer);
        struct_events[impl_->builder->GetInsertBlock()].push_back(
            {StructInitializationAction::write, aggregate, minstr.args[0], "SET_FIELD"});
        break;
      }

      case Opcode::OWN_ALLOC:
      case Opcode::ALLOC: {
        const std::string element_type =
            minstr.args.empty() ? std::string{} : minstr.args[0];
        const auto buffer_kind = native_array_kind_for_element(element_type);
        auto *storage_type = native_pointer_storage_type(element_type);
        if (!buffer_kind || !storage_type) {
          std::cerr << "[NativeCodegen] ALLOC element type '" << element_type
                    << "' has no supported native buffer layout in function '"
                    << fn.name << "'\n";
          return false;
        }

        auto *count = impl_->popValue();
        if (!native_value_is_numeric_scalar(count) ||
            !count->getType()->isIntegerTy()) {
          std::cerr << "[NativeCodegen] ALLOC count must be an integer in "
                       "function '"
                    << fn.name << "'\n";
          return false;
        }
        count = impl_->coerceToI64(count);
        auto *one = llvm::ConstantInt::get(ctx, llvm::APInt(64, 1));
        auto *positive_count = impl_->builder->CreateSelect(
            impl_->builder->CreateICmpSLT(
                count, llvm::ConstantInt::get(ctx, llvm::APInt(64, 1))),
            one, count, "ny.alloc.count");

        const std::uint64_t stride = impl_->module->getDataLayout()
                                         .getTypeAllocSize(storage_type)
                                         .getFixedValue();
        const std::uint64_t maximum_element_count = native_size_max / stride;
        auto *size_failure = llvm::BasicBlock::Create(
            ctx, "alloc.elements.failure", impl_->current_function);
        auto *size_success = llvm::BasicBlock::Create(
            ctx, "alloc.elements.success", impl_->current_function);
        impl_->builder->CreateCondBr(
            impl_->builder->CreateICmpULE(
                positive_count,
                llvm::ConstantInt::get(
                    ctx, llvm::APInt(64, maximum_element_count, false))),
            size_success, size_failure);

        impl_->builder->SetInsertPoint(size_failure);
        auto *size_message = impl_->builder->CreateGlobalString(
            "NIL native runtime error: invalid typed allocation size\n");
        impl_->builder->CreateCall(get_runtime_failure_function(),
                                   {size_message});
        impl_->builder->CreateUnreachable();

        impl_->builder->SetInsertPoint(size_success);

        llvm::Value *pointer = nullptr;
        if (impl_->memory_mode == MemoryMode::SAFE_GC || op == Opcode::OWN_ALLOC) {
          auto *runtime_type = llvm::FunctionType::get(
              llvm::PointerType::getUnqual(ctx),
              {llvm::Type::getInt32Ty(ctx), llvm::Type::getInt64Ty(ctx)},
              false);
          auto runtime = impl_->module->getOrInsertFunction("nyrt_buffer_new",
                                                            runtime_type);
          pointer = impl_->builder->CreateCall(
              runtime_type, runtime.getCallee(),
              {llvm::ConstantInt::get(ctx,
                                      llvm::APInt(32, *buffer_kind, false)),
               positive_count});
        } else {
          auto *calloc_type = llvm::FunctionType::get(
              llvm::PointerType::getUnqual(ctx),
              {native_size_type, native_size_type}, false);
          auto calloc_fn =
              impl_->module->getOrInsertFunction("calloc", calloc_type);
          pointer =
              impl_->builder->CreateCall(calloc_type, calloc_fn.getCallee(),
                                         {coerce_to_native_size(positive_count),
                                          native_size_constant(stride)});
        }
        emit_allocation_null_check(pointer, "ALLOC");
        impl_->pushValue(pointer,
                         "ptr<" + normalize_nil_type(element_type) + ">");
        break;
      }

      case Opcode::FREE: {
        auto ptr = impl_->popValue();
        if (!ptr || !ptr->getType()->isPointerTy() ||
            native_handle_kind_for_nil_type(impl_->nilType(ptr)) !=
                native_handle_kind::integer_pointer) {
          std::cerr << "[NativeCodegen] FREE requires a typed ptr<i32>, "
                       "ptr<i64>, ptr<bool>, or ptr<u8> handle in function '"
                    << fn.name << "'\n";
          return false;
        }
        if (impl_->memory_mode == MemoryMode::SAFE_GC) {
          auto *runtime_type = llvm::FunctionType::get(
              llvm::Type::getInt32Ty(ctx), {llvm::PointerType::getUnqual(ctx)},
              false);
          auto runtime = impl_->module->getOrInsertFunction("nyrt_buffer_free",
                                                            runtime_type);
          impl_->builder->CreateCall(runtime_type, runtime.getCallee(), {ptr});
        } else {
          auto *free_type = llvm::FunctionType::get(
              llvm::Type::getVoidTy(ctx), {llvm::PointerType::getUnqual(ctx)},
              false);
          auto free_fn = impl_->module->getOrInsertFunction("free", free_type);
          impl_->builder->CreateCall(free_type, free_fn.getCallee(), {ptr});
        }
        break;
      }

      case Opcode::READ_PTR: {
        auto *pointer = impl_->popValue();
        const std::string pointer_type = impl_->nilType(pointer);
        const auto element_type = native_generic_argument(pointer_type, "ptr");
        const auto kind = element_type
                              ? native_array_kind_for_element(*element_type)
                              : std::nullopt;
        if (!pointer || !pointer->getType()->isPointerTy() || !kind) {
          std::cerr << "[NativeCodegen] READ_PTR requires a supported typed "
                       "raw pointer in function '"
                    << fn.name << "'\n";
          return false;
        }

        llvm::Value *value = nullptr;
        if (impl_->memory_mode == MemoryMode::SAFE_GC) {
          auto *runtime_type = llvm::FunctionType::get(
              llvm::Type::getInt64Ty(ctx),
              {llvm::PointerType::getUnqual(ctx), llvm::Type::getInt32Ty(ctx),
               llvm::Type::getInt64Ty(ctx)},
              false);
          auto runtime = impl_->module->getOrInsertFunction("nyrt_buffer_get",
                                                            runtime_type);
          value = impl_->builder->CreateCall(
              runtime_type, runtime.getCallee(),
              {pointer,
               llvm::ConstantInt::get(ctx, llvm::APInt(32, *kind, false)),
               llvm::ConstantInt::get(ctx, llvm::APInt(64, 0, false))});
        } else {
          auto *storage_type = native_pointer_storage_type(*element_type);
          auto *loaded = impl_->builder->CreateLoad(storage_type, pointer,
                                                    "ny.pointer.dereference");
          if (storage_type->isIntegerTy(64)) {
            value = loaded;
          } else if (normalize_nil_type(*element_type) == "i32") {
            value = impl_->builder->CreateSExt(
                loaded, llvm::Type::getInt64Ty(ctx), "ny.pointer.i32.word");
          } else {
            value = impl_->builder->CreateZExt(
                loaded, llvm::Type::getInt64Ty(ctx), "ny.pointer.byte.word");
          }
        }
        if (*element_type == "bool") {
          value = native_storage_word(value, "bool", "READ_PTR");
          if (!value) return false;
        }
        impl_->pushValue(value, *element_type);
        break;
      }

      case Opcode::WRITE_PTR: {
        auto *value = impl_->popValue();
        auto *pointer = impl_->popValue();
        const std::string pointer_type = impl_->nilType(pointer);
        const auto element_type = native_generic_argument(pointer_type, "ptr");
        const auto kind = element_type
                              ? native_array_kind_for_element(*element_type)
                              : std::nullopt;
        if (!pointer || !pointer->getType()->isPointerTy() || !kind ||
            !native_value_is_numeric_scalar(value) ||
            !value->getType()->isIntegerTy()) {
          std::cerr << "[NativeCodegen] WRITE_PTR requires a supported typed "
                       "raw pointer and integer value in function '"
                    << fn.name << "'\n";
          return false;
        }
        auto *word = native_storage_word(value, *element_type, "WRITE_PTR");
        if (!word)
          return false;

        if (impl_->memory_mode == MemoryMode::SAFE_GC) {
          auto *runtime_type = llvm::FunctionType::get(
              llvm::Type::getInt32Ty(ctx),
              {llvm::PointerType::getUnqual(ctx), llvm::Type::getInt32Ty(ctx),
               llvm::Type::getInt64Ty(ctx), llvm::Type::getInt64Ty(ctx)},
              false);
          auto runtime = impl_->module->getOrInsertFunction("nyrt_buffer_set",
                                                            runtime_type);
          impl_->builder->CreateCall(
              runtime_type, runtime.getCallee(),
              {pointer,
               llvm::ConstantInt::get(ctx, llvm::APInt(32, *kind, false)),
               llvm::ConstantInt::get(ctx, llvm::APInt(64, 0, false)), word});
        } else {
          auto *storage_type = native_pointer_storage_type(*element_type);
          llvm::Value *stored = word;
          if (!storage_type->isIntegerTy(64)) {
            stored = impl_->builder->CreateTrunc(word, storage_type,
                                                 "ny.pointer.stored");
          }
          impl_->builder->CreateStore(stored, pointer);
        }
        break;
      }

      case Opcode::BOX_ANY:
      case Opcode::UNBOX_ANY:
      case Opcode::IS_ANY_TYPE: {
        if (minstr.args.size() != 1 || minstr.args[0].empty()) {
          std::cerr << "[NativeCodegen] " << minstr.opname
                    << " requires exactly one canonical type descriptor\n";
          return false;
        }
        const auto &descriptor = minstr.args[0];
        try {
          validate_nil_dynamic_type(mod, descriptor);
        } catch (const std::exception &error) {
          std::cerr << "[NativeCodegen] " << minstr.opname << ": " << error.what() << '\n';
          return false;
        }
        if (canonical_dynamic_type(descriptor) != descriptor ||
            descriptor.rfind("fn(", 0) == 0 ||
            !native_enum_payload_type_supported(descriptor)) {
          std::cerr << "[NativeCodegen] " << minstr.opname
                    << " has no supported native value representation for '"
                    << descriptor << "'; native function values cannot be boxed\n";
          return false;
        }
        auto *value = impl_->popValue();
        auto *word_type = llvm::Type::getInt64Ty(ctx);
        auto *pointer_type = llvm::PointerType::getUnqual(ctx);
        auto *type_text = impl_->builder->CreateGlobalString(descriptor);
        auto *type_length = llvm::ConstantInt::get(word_type, descriptor.size());
        if (op == Opcode::BOX_ANY) {
          if (canonical_dynamic_type(impl_->nilType(value)) != descriptor ||
              !native_enum_payload_compatible(value, descriptor)) {
            std::cerr << "[NativeCodegen] BOX_ANY descriptor '" << descriptor
                      << "' disagrees with source value '" << impl_->nilType(value) << "'\n";
            return false;
          }
          require_initialized_struct(value, "BOX_ANY");
          llvm::Value *payload = value;
          if (value->getType()->isPointerTy())
            payload = impl_->builder->CreatePtrToInt(value, word_type);
          else if (value->getType()->isDoubleTy())
            payload = impl_->builder->CreateBitCast(value, word_type);
          else if (value->getType()->isIntegerTy())
            payload = impl_->castInteger(value, word_type);
          else return false;
          auto runtime = impl_->module->getOrInsertFunction("nyrt_any_v1_box",
              llvm::FunctionType::get(pointer_type, {pointer_type, word_type, word_type}, false));
          auto *box = impl_->builder->CreateCall(runtime, {type_text, type_length, payload});
          impl_->pushValue(box, "anything");
        } else {
          if (!value->getType()->isPointerTy() || impl_->nilType(value) != "anything") {
            std::cerr << "[NativeCodegen] " << minstr.opname
                      << " requires a checked anything box\n";
            return false;
          }
          if (op == Opcode::IS_ANY_TYPE) {
            auto runtime = impl_->module->getOrInsertFunction("nyrt_any_v1_is_type",
                llvm::FunctionType::get(llvm::Type::getInt32Ty(ctx),
                    {pointer_type, pointer_type, word_type}, false));
            auto *matches = impl_->builder->CreateCall(runtime, {value, type_text, type_length});
            impl_->pushValue(impl_->builder->CreateICmpNE(matches,
                llvm::ConstantInt::get(matches->getType(), 0)), "bool");
          } else {
            auto runtime = impl_->module->getOrInsertFunction("nyrt_any_v1_unbox",
                llvm::FunctionType::get(word_type, {pointer_type, pointer_type, word_type}, false));
            llvm::Value *payload = impl_->builder->CreateCall(runtime, {value, type_text, type_length});
            const auto kind = native_handle_kind_for_nil_type(descriptor);
            if (kind == native_handle_kind::text || kind == native_handle_kind::scalar_array ||
                kind == native_handle_kind::integer_pointer || is_native_aggregate_nil_type(mod, descriptor))
              payload = impl_->builder->CreateIntToPtr(payload, pointer_type);
            else if (descriptor == "f64")
              payload = impl_->builder->CreateBitCast(payload, llvm::Type::getDoubleTy(ctx));
            impl_->pushValue(payload, descriptor);
          }
        }
        break;
      }

      case Opcode::CAST: {
        if (minstr.args.size() != 1 || minstr.args[0].empty()) {
          std::cerr << "[NativeCodegen] CAST: exactly one nonempty target type "
                       "is required\n";
          return false;
        }
        std::string target_type = minstr.args[0];
        auto val = impl_->popValue();
        if (!val)
          break;

        llvm::Type *src_ty = val->getType();
        std::string normalized_target = normalize_nil_type(target_type);
        if (normalized_target == "int")
          normalized_target = "i32";

        // Handle conversions
        // If target is "text" or "ptr<...>" or "string" (if native), we want
        // Pointer If target is "i32", "i64", "any", we want Int

        const auto target_handle_kind =
            native_handle_kind_for_nil_type(target_type);
        const bool target_is_aggregate =
            is_native_aggregate_nil_type(mod, target_type);
        const bool target_is_ptr =
            target_handle_kind == native_handle_kind::text ||
            target_handle_kind == native_handle_kind::anything ||
            target_handle_kind == native_handle_kind::scalar_array ||
            target_handle_kind == native_handle_kind::integer_pointer;
        const bool target_is_float = normalized_target == "f64";
        const std::string source_type = impl_->nilType(val);
        const auto *target_enum =
            find_nil_enum_definition(mod, target_type);
        const auto *source_enum = find_nil_enum_definition(mod, source_type);

        if (normalized_target == "bool" && src_ty->isPointerTy() &&
            native_handle_kind_for_nil_type(source_type) == native_handle_kind::text) {
          auto runtime = impl_->module->getOrInsertFunction("nyrt_bool_text",
              llvm::FunctionType::get(llvm::Type::getInt64Ty(ctx),
                  {llvm::PointerType::getUnqual(ctx)}, false));
          impl_->pushValue(impl_->builder->CreateCall(runtime, {val}), "bool");
          break;
        }

        if (target_handle_kind == native_handle_kind::unsupported) {
          std::cerr << "[NativeCodegen] cannot cast to unsupported native "
                       "handle type '"
                    << target_type << "' in function '" << fn.name << "'\n";
          return false;
        }

        if (target_is_aggregate) {
          if (!src_ty->isPointerTy() ||
              !native_aggregate_types_compatible(mod, source_type,
                                                 target_type) ||
              (find_nil_class(mod, compact_nil_type(target_type)) &&
               compact_nil_type(source_type) != compact_nil_type(target_type))) {
            std::cerr << "[NativeCodegen] cannot cast value of type '"
                      << (source_type.empty() ? std::string("untyped value")
                                              : source_type)
                      << "' to aggregate type '" << target_type
                      << "' in function '" << fn.name << "'\n";
            return false;
          }
          impl_->pushValue(val, target_type);
        } else if (target_is_ptr) {
          if (!src_ty->isPointerTy() || source_type.empty() ||
              !native_handle_types_compatible(source_type, target_type)) {
            std::cerr << "[NativeCodegen] ordinary CAST cannot convert '"
                      << (source_type.empty() ? std::string("untyped scalar")
                                              : source_type)
                      << "' to native handle type '" << target_type
                      << "' in function '" << fn.name
                      << "'; only exact compatible handle casts are allowed\n";
            return false;
          }
          impl_->pushValue(val, canonical_array_type(target_type));
        } else if (target_enum != nullptr) {
          if (!src_ty->isIntegerTy() || source_enum != target_enum) {
            std::cerr << "[NativeCodegen] ordinary CAST cannot convert '"
                      << (source_type.empty() ? std::string("untyped scalar")
                                              : source_type)
                      << "' to enum type '" << target_type << "' in function '"
                      << fn.name << "'; exact enum metadata is required\n";
            return false;
          }
          impl_->pushValue(val, target_type);
        } else {
          if (src_ty->isPointerTy()) {
            std::cerr << "[NativeCodegen] ordinary CAST cannot convert native "
                         "handle '"
                      << (source_type.empty() ? std::string("untyped pointer")
                                              : source_type)
                      << "' to scalar type '" << target_type
                      << "' in function '" << fn.name << "'\n";
            return false;
          }
          if (source_enum != nullptr) {
            std::cerr << "[NativeCodegen] ordinary CAST cannot convert enum '"
                      << source_type << "' to scalar type '" << target_type
                      << "' in function '" << fn.name << "'\n";
            return false;
          }

          if (normalized_target != "bool" && normalized_target != "i32" &&
              normalized_target != "i64" && normalized_target != "u8" &&
              !target_is_float) {
            std::cerr << "[NativeCodegen] CAST: unsupported scalar target '"
                      << target_type
                      << "'; supported targets are bool, i32, i64, u8, f64\n";
            return false;
          }

          if (normalized_target == "bool") {
            llvm::Value *boolean = nullptr;
            if (src_ty->isIntegerTy()) {
              auto *word = llvm::Type::getInt64Ty(ctx);
              auto runtime = impl_->module->getOrInsertFunction("nyrt_bool_i64",
                  llvm::FunctionType::get(word, {word}, false));
              boolean = impl_->builder->CreateCall(runtime, {impl_->castInteger(val, word)});
            } else if (src_ty->isFloatingPointTy()) {
              auto runtime = impl_->module->getOrInsertFunction("nyrt_bool_f64",
                  llvm::FunctionType::get(llvm::Type::getInt64Ty(ctx),
                      {llvm::Type::getDoubleTy(ctx)}, false));
              boolean = impl_->builder->CreateCall(runtime, {impl_->coerceToDouble(val)});
            }
            if (!boolean) {
              std::cerr << "[NativeCodegen] cannot cast value to bool in function '"
                        << fn.name << "'\n";
              return false;
            }
            impl_->pushValue(boolean, "bool");
          } else if (target_is_float) {
            llvm::Type *dest_llvm_ty = llvm::Type::getDoubleTy(ctx);
            if (src_ty->isIntegerTy(1)) {
              impl_->pushValue(impl_->builder->CreateUIToFP(val, dest_llvm_ty),
                               normalized_target);
            } else if (src_ty->isIntegerTy()) {
              impl_->pushValue(impl_->builder->CreateSIToFP(val, dest_llvm_ty),
                               normalized_target);
            } else if (src_ty->isFloatingPointTy()) {
              if (src_ty == dest_llvm_ty)
                impl_->pushValue(val, normalized_target);
              else if (dest_llvm_ty->isDoubleTy())
                impl_->pushValue(impl_->builder->CreateFPExt(val, dest_llvm_ty),
                                 normalized_target);
              else
                impl_->pushValue(
                    impl_->builder->CreateFPTrunc(val, dest_llvm_ty),
                    normalized_target);
            } else {
              std::cerr << "[NativeCodegen] cannot cast value to "
                        << target_type << " in function '" << fn.name << "'\n";
              return false;
            }
          } else {
            // Scalar integer/floating conversions remain ordinary numeric
            // casts and never cross the native handle or enum boundary.
            if (src_ty->isFloatingPointTy()) {
              auto *minimum = llvm::ConstantFP::get(src_ty, -9223372036854775808.0);
              auto *limit = llvm::ConstantFP::get(src_ty, 9223372036854775808.0);
              auto *valid = impl_->builder->CreateAnd(
                  impl_->builder->CreateFCmpOGE(val, minimum),
                  impl_->builder->CreateFCmpOLT(val, limit), "ny.cast.in_range");
              auto *success = llvm::BasicBlock::Create(
                  ctx, "cast.range.success", impl_->current_function);
              auto *failure = llvm::BasicBlock::Create(
                  ctx, "cast.range.failure", impl_->current_function);
              impl_->builder->CreateCondBr(valid, success, failure);
              impl_->builder->SetInsertPoint(failure);
              auto *message = impl_->builder->CreateGlobalString(
                  "NIL native runtime error: CAST: floating-point value is "
                  "non-finite or outside i64 range\n");
              impl_->builder->CreateCall(get_runtime_failure_function(), {message});
              impl_->builder->CreateUnreachable();
              impl_->builder->SetInsertPoint(success);
              val = impl_->builder->CreateFPToSI(
                  val, llvm::Type::getInt64Ty(ctx), "ny.cast.integer_word");
              src_ty = val->getType();
            }
            if (normalized_target == "u8") {
              llvm::Value *word = nullptr;
              if (src_ty->isIntegerTy()) {
                word = src_ty->isIntegerTy(64)
                           ? val
                           : impl_->builder->CreateZExtOrTrunc(
                                 val, llvm::Type::getInt64Ty(ctx),
                                 "ny.cast.u8.source");
              } else {
                std::cerr << "[NativeCodegen] cannot cast value to "
                          << target_type << " in function '" << fn.name
                          << "'\n";
                return false;
              }
              impl_->pushValue(
                  impl_->builder->CreateAnd(
                      word,
                      llvm::ConstantInt::get(
                          ctx, llvm::APInt(64, 255, false)),
                      "ny.cast.u8"),
                  normalized_target);
              break;
            }

            llvm::Type *dest_llvm_ty = llvm::Type::getInt64Ty(ctx);
            if (normalized_target == "i32")
              dest_llvm_ty = llvm::Type::getInt32Ty(ctx);

            if (src_ty->isIntegerTy()) {
              if (src_ty != dest_llvm_ty) {
                impl_->pushValue(impl_->castInteger(val, dest_llvm_ty),
                                 normalized_target);
              } else {
                impl_->pushValue(val, normalized_target);
              }
            } else {
              std::cerr << "[NativeCodegen] cannot cast value to "
                        << target_type << " in function '" << fn.name << "'\n";
              return false;
            }
          }
        }
        break;
      }

      // Enum Support
      case Opcode::NEW_VARIANT: {
        // Legacy: NEW_VARIANT "Tag" count. New NIL also carries the exact
        // concrete enum and payload types so generic instances retain their
        // native ABI identity.
        if (minstr.args.size() != 2 && minstr.args.size() != 4) {
          std::cerr << "[NativeCodegen] NEW_VARIANT in function '" << fn.name
                    << "' requires either legacy tag/count operands or "
                       "complete concrete enum metadata\n";
          return false;
        }
        const std::string encoded_tag = minstr.args[0];
        std::string tag = canonicalize_variant_name(encoded_tag);
        std::size_t count = 0;
        try {
          std::size_t consumed = 0;
          count = std::stoull(minstr.args[1], &consumed, 10);
          if (consumed != minstr.args[1].size() ||
              count > (std::numeric_limits<std::uint64_t>::max() / 8U) - 2U) {
            throw std::out_of_range("variant payload count");
          }
        } catch (const std::exception &) {
          std::cerr << "[NativeCodegen] NEW_VARIANT '" << encoded_tag
                    << "' in function '" << fn.name
                    << "' has invalid payload count '" << minstr.args[1]
                    << "'\n";
          return false;
        }

        const auto variant_separator = tag.rfind("::");
        const std::string encoded_enum_type =
            minstr.args.size() >= 3 && !minstr.args[2].empty()
                ? minstr.args[2]
                : (variant_separator == std::string::npos
                       ? std::string{}
                       : tag.substr(0, variant_separator));
        const nil_enum_def *enum_definition =
            encoded_enum_type.empty()
                ? nullptr
                : find_nil_enum_definition(mod, encoded_enum_type);
        if (enum_definition == nullptr) {
          std::cerr << "[NativeCodegen] NEW_VARIANT '" << encoded_tag
                    << "' in function '" << fn.name
                    << "' has no exact non-generic enum schema metadata\n";
          return false;
        }

        if (!variant_owner_matches_schema(tag, *enum_definition)) {
          std::cerr << "[NativeCodegen] NEW_VARIANT tag owner disagrees with "
                       "enum schema in function '" << fn.name << "'\n";
          return false;
        }

        const std::string variant_name =
            variant_separator == std::string::npos
                ? tag
                : tag.substr(variant_separator + 2);
        const auto variant = std::find_if(
            enum_definition->variants.begin(), enum_definition->variants.end(),
            [&](const nil_enum_def::variant &candidate) {
              return candidate.name == variant_name;
            });
        if (variant == enum_definition->variants.end()) {
          std::cerr << "[NativeCodegen] NEW_VARIANT '" << encoded_tag
                    << "' in function '" << fn.name
                    << "' names unknown variant '" << variant_name
                    << "' on enum '" << enum_definition->name << "'\n";
          return false;
        }
        const auto expected_count = native_enum_variant_payload_count(*variant);
        if (!expected_count || count != *expected_count) {
          std::cerr << "[NativeCodegen] NEW_VARIANT '" << encoded_tag
                    << "' in function '" << fn.name << "' encodes " << count
                    << " payload value(s), but enum schema '"
                    << enum_definition->name << "' requires "
                    << (expected_count ? std::to_string(*expected_count)
                                       : std::string("a valid payload layout"))
                    << "\n";
          return false;
        }
        const auto payload_types = native_enum_variant_payload_types(*variant);
        if (minstr.args.size() >= 4) {
          const std::string encoded_payload =
              normalize_nil_type(minstr.args[3]);
          const std::string schema_payload = normalize_nil_type(
              variant->associated_type.empty() ? std::string("void")
                                               : variant->associated_type);
          if (encoded_payload != schema_payload) {
            std::cerr << "[NativeCodegen] NEW_VARIANT '" << encoded_tag
                      << "' in function '" << fn.name << "' payload metadata '"
                      << minstr.args[3]
                      << "' disagrees with concrete enum schema '"
                      << variant->associated_type << "'\n";
            return false;
          }
        }

        // Allocate (2 + count) * 8 bytes: [TagPtr, Count, Payload...]
        const std::uint64_t bytes = static_cast<std::uint64_t>(2 + count) * 8U;

        llvm::Value *ptr = nullptr;
        if (impl_->memory_mode == MemoryMode::SAFE_GC) {
          auto *runtime_type =
              llvm::FunctionType::get(llvm::PointerType::getUnqual(ctx),
                                      {llvm::Type::getInt64Ty(ctx)}, false);
          auto runtime =
              impl_->module->getOrInsertFunction("nyrt_alloc", runtime_type);
          ptr = impl_->builder->CreateCall(
              runtime_type, runtime.getCallee(),
              {llvm::ConstantInt::get(ctx, llvm::APInt(64, bytes, false))});
        } else {
          auto *malloc_type = llvm::FunctionType::get(
              llvm::PointerType::getUnqual(ctx), {native_size_type}, false);
          auto malloc_fn =
              impl_->module->getOrInsertFunction("malloc", malloc_type);
          ptr = impl_->builder->CreateCall(malloc_type, malloc_fn.getCallee(),
                                           {native_size_constant(bytes)});
        }
        emit_allocation_null_check(ptr, "NEW_VARIANT");
        auto ptr_i64 =
            impl_->builder->CreatePtrToInt(ptr, llvm::Type::getInt64Ty(ctx));

        // Store Tag (char*)
        auto tag_str = impl_->get_or_add_const_string(tag);
        auto tag_ptr_loc = impl_->builder->CreateIntToPtr(
            ptr_i64, llvm::PointerType::getUnqual(ctx));
        impl_->builder->CreateStore(tag_str, tag_ptr_loc);

        // Store Count
        auto count_loc = impl_->builder->CreateAdd(
            ptr_i64, llvm::ConstantInt::get(ctx, llvm::APInt(64, 8)));
        auto count_ptr = impl_->builder->CreateIntToPtr(
            count_loc,
            llvm::PointerType::getUnqual(llvm::Type::getInt64Ty(ctx)));
        impl_->builder->CreateStore(
            llvm::ConstantInt::get(ctx, llvm::APInt(64, count)), count_ptr);

        // Store Fields (popped from stack in reverse order of definition,
        // usually? Stack: [f1, f2]. Pop gives f2, f1. If we want [f1, f2] in
        // memory, we pop f2 -> index 1, pop f1 -> index 0.
        for (std::size_t reverse_index = 0; reverse_index < count;
             ++reverse_index) {
          const std::size_t i = count - 1 - reverse_index;
          auto val = impl_->popValue();
          if (!val) {
            std::cerr << "[NativeCodegen] NEW_VARIANT '" << encoded_tag
                      << "' in function '" << fn.name
                      << "' has fewer payload values on the NIL stack than its "
                         "encoded count\n";
            return false;
          }
          if (!native_enum_payload_compatible(val, payload_types->at(i))) {
            std::cerr << "[NativeCodegen] NEW_VARIANT '" << encoded_tag
                      << "' payload " << i << " requires exact type '"
                      << payload_types->at(i) << "', got '"
                      << native_value_type_description(val) << "'\n";
            return false;
          }
          require_initialized_struct(val, "NEW_VARIANT");
          if (payload_types->at(i) == "bool") {
            val = native_storage_word(val, "bool", "NEW_VARIANT");
            if (!val) return false;
          }
          auto offset = 16 + i * 8;
          auto field_loc = impl_->builder->CreateAdd(
              ptr_i64, llvm::ConstantInt::get(ctx, llvm::APInt(64, offset)));

          // Properly cast value to i64 for storage. Exact payload metadata is
          // retained on the instruction and restored by UNWRAP_VARIANT.
          llvm::Value *val_i64 = val;
          if (val->getType()->isPointerTy()) {
            val_i64 = impl_->builder->CreatePtrToInt(
                val, llvm::Type::getInt64Ty(ctx));
          } else if (val->getType()->isIntegerTy()) {
            const unsigned width = val->getType()->getIntegerBitWidth();
            if (width < 64) {
              const std::string payload_type =
                  minstr.args.size() >= 4 ? normalize_nil_type(minstr.args[3])
                                          : impl_->nilType(val);
              const bool unsigned_payload =
                  payload_type == "bool" || payload_type == "u8" ||
                  payload_type == "u16" || payload_type == "u32";
              val_i64 = unsigned_payload
                            ? impl_->builder->CreateZExt(
                                  val, llvm::Type::getInt64Ty(ctx))
                            : impl_->builder->CreateSExt(
                                  val, llvm::Type::getInt64Ty(ctx));
            } else if (width > 64) {
              val_i64 =
                  impl_->builder->CreateTrunc(val, llvm::Type::getInt64Ty(ctx));
            }
          } else if (val->getType()->isDoubleTy()) {
            // The enum ABI stores each payload in an eight-byte word. Preserve
            // the IEEE-754 bits rather than converting its numeric value.
            val_i64 = impl_->builder->CreateBitCast(
                val, llvm::Type::getInt64Ty(ctx));
          } else {
            std::cerr << "[NativeCodegen] NEW_VARIANT '" << encoded_tag
                      << "' in function '" << fn.name
                      << "' has a non-word payload value\n";
            return false;
          }

          auto field_ptr = impl_->builder->CreateIntToPtr(
              field_loc,
              llvm::PointerType::getUnqual(llvm::Type::getInt64Ty(ctx)));
          impl_->builder->CreateStore(val_i64, field_ptr);
        }

        // Enums retain the existing word representation, but carry their
        // schema type so storing one into a typed struct field can be checked
        // exactly rather than accepting an arbitrary integer word.
        impl_->pushValue(ptr_i64, enum_definition->name);
        break;
      }

      case Opcode::IS_VARIANT: {
        // Legacy: IS_VARIANT "Tag". Concrete metadata appends enum/payload.
        if (minstr.args.size() != 1 && minstr.args.size() != 3) {
          std::cerr << "[NativeCodegen] IS_VARIANT in function '" << fn.name
                    << "' requires either a legacy tag operand or complete "
                       "concrete enum metadata\n";
          return false;
        }
        auto ptr_i64 = impl_->popValue();
        const auto *enum_definition =
            ptr_i64 ? find_nil_enum_definition(mod, impl_->nilType(ptr_i64))
                    : nullptr;
        if (!ptr_i64 || !ptr_i64->getType()->isIntegerTy(64) ||
            !enum_definition) {
          std::cerr << "[NativeCodegen] IS_VARIANT requires a typed enum "
                       "value in function '" << fn.name << "'\n";
          return false;
        }

        if (minstr.args.size() >= 2 &&
            !nil_type_name_matches(minstr.args[1], impl_->nilType(ptr_i64))) {
          std::cerr << "[NativeCodegen] IS_VARIANT in function '" << fn.name
                    << "' expects enum '" << minstr.args[1] << "', got '"
                    << impl_->nilType(ptr_i64) << "'\n";
          return false;
        }

        std::string expected_tag = canonicalize_variant_name(minstr.args[0]);
        if (!variant_owner_matches_schema(expected_tag, *enum_definition)) {
          std::cerr << "[NativeCodegen] IS_VARIANT tag owner disagrees with "
                       "enum schema in function '" << fn.name << "'\n";
          return false;
        }
        const auto separator = expected_tag.rfind("::");
        const std::string variant_name =
            separator == std::string::npos ? expected_tag
                                          : expected_tag.substr(separator + 2);
        const auto variant = std::find_if(
            enum_definition->variants.begin(), enum_definition->variants.end(),
            [&](const nil_enum_def::variant &candidate) {
              return candidate.name == variant_name;
            });
        if (variant == enum_definition->variants.end() ||
            (minstr.args.size() == 3 &&
             normalize_nil_type(minstr.args[2]) !=
                 normalize_nil_type(variant->associated_type.empty()
                                        ? "void" : variant->associated_type))) {
          std::cerr << "[NativeCodegen] IS_VARIANT has incompatible enum "
                       "schema metadata in function '" << fn.name << "'\n";
          return false;
        }
        llvm::Value *expected_str =
            impl_->get_or_add_const_string(expected_tag);

        // Load actual tag from [ptr]
        auto ptr = impl_->builder->CreateIntToPtr(
            ptr_i64, llvm::PointerType::getUnqual(ctx));
        auto tag_ptr = impl_->builder->CreatePointerCast(
            ptr,
            llvm::PointerType::getUnqual(llvm::PointerType::getUnqual(ctx)));
        llvm::Value *actual_str = impl_->builder->CreateLoad(
            llvm::PointerType::getUnqual(ctx), tag_ptr);

        // Compare strings (pointer comparison might work if constants are
        // unique, but better use strcmp) For now, use strcmp
        auto strcmp_type =
            llvm::FunctionType::get(llvm::Type::getInt32Ty(ctx),
                                    {llvm::PointerType::getUnqual(ctx),
                                     llvm::PointerType::getUnqual(ctx)},
                                    false);
        auto strcmp_fn =
            impl_->module->getOrInsertFunction("strcmp", strcmp_type);

        std::vector<llvm::Value *> strcmp_args = {actual_str, expected_str};
        auto res = impl_->builder->CreateCall(
            strcmp_type, strcmp_fn.getCallee(), strcmp_args);
        auto is_eq = impl_->builder->CreateICmpEQ(
            res, llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 0));

        impl_->pushValue(is_eq, "bool");
        break;
      }

      case Opcode::UNWRAP_VARIANT: {
        // Legacy: no operands/count. Concrete metadata appends enum/payload.
        if (minstr.args.size() != 0 && minstr.args.size() != 1 &&
            minstr.args.size() != 3) {
          std::cerr << "[NativeCodegen] UNWRAP_VARIANT in function '" << fn.name
                    << "' requires legacy operands or complete concrete enum "
                       "metadata\n";
          return false;
        }
        if (minstr.args.empty()) {
          std::cerr << "[NativeCodegen] UNWRAP_VARIANT requires an exact "
                       "payload count in function '" << fn.name << "'\n";
          return false;
        }
        std::size_t count = 0;
        try {
          std::size_t consumed = 0;
          count = std::stoull(minstr.args[0], &consumed, 10);
          if (consumed != minstr.args[0].size() ||
              count > static_cast<std::size_t>(
                          std::numeric_limits<std::int64_t>::max() / 8 - 2)) {
            throw std::out_of_range("variant payload count");
          }
        } catch (const std::exception &) {
          std::cerr << "[NativeCodegen] UNWRAP_VARIANT in function '" << fn.name
                    << "' has invalid payload count '" << minstr.args[0]
                    << "'\n";
          return false;
        }

        // Pop variant pointer
        auto ptr_i64 = impl_->popValue();
        const auto *enum_definition =
            ptr_i64 ? find_nil_enum_definition(mod, impl_->nilType(ptr_i64))
                    : nullptr;
        if (!ptr_i64 || !ptr_i64->getType()->isIntegerTy(64) ||
            !enum_definition) {
          std::cerr << "[NativeCodegen] UNWRAP_VARIANT requires a typed enum "
                       "value in function '" << fn.name << "'\n";
          return false;
        }

        const std::string concrete_enum =
            minstr.args.size() >= 2 ? minstr.args[1] : impl_->nilType(ptr_i64);
        const std::string encoded_payload_type =
            minstr.args.size() >= 3 ? minstr.args[2] : std::string{};
        if (minstr.args.size() >= 2 &&
            !nil_type_name_matches(concrete_enum, impl_->nilType(ptr_i64))) {
          std::cerr << "[NativeCodegen] UNWRAP_VARIANT in function '" << fn.name
                    << "' expects enum '" << concrete_enum << "', got '"
                    << impl_->nilType(ptr_i64) << "'\n";
          return false;
        }

        // Only variants with this exact payload layout are valid here.
        // Legacy NIL can infer a layout only when all count-matching variants
        // agree; no native pointer/scalar type is guessed from a raw word.
        std::optional<std::vector<std::string>> payload_types;
        std::vector<std::string> allowed_variants;
        for (const auto &variant : enum_definition->variants) {
          const auto types = native_enum_variant_payload_types(variant);
          if (!types || types->size() != count)
            continue;
          const std::string schema_payload =
              variant.associated_type.empty() ? "void" : variant.associated_type;
          if (!encoded_payload_type.empty() &&
              normalize_nil_type(encoded_payload_type) !=
                  normalize_nil_type(schema_payload))
            continue;
          if (payload_types && *payload_types != *types) {
            std::cerr << "[NativeCodegen] UNWRAP_VARIANT requires exact "
                         "payload type metadata for ambiguous enum layout\n";
            return false;
          }
          payload_types = types;
          allowed_variants.push_back(variant.name);
        }
        if (!payload_types) {
          std::cerr << "[NativeCodegen] UNWRAP_VARIANT payload count/type "
                       "does not match enum schema in function '"
                    << fn.name << "'\n";
          return false;
        }
        for (const auto &type : *payload_types) {
          if (!native_enum_payload_type_supported(type)) {
            std::cerr << "[NativeCodegen] UNWRAP_VARIANT payload type '"
                      << type << "' has no exact native word layout\n";
            return false;
          }
        }

        // Proven enum provenance makes the header readable. Check both its
        // count and variant name before reading any payload, including when
        // a caller unwraps a different variant with the same payload count.
        auto *pointer_type = llvm::PointerType::getUnqual(ctx);
        auto *header = impl_->builder->CreateIntToPtr(ptr_i64, pointer_type);
        auto *actual_tag = impl_->builder->CreateLoad(pointer_type, header);
        auto *count_address = impl_->builder->CreateAdd(
            ptr_i64, llvm::ConstantInt::get(ctx, llvm::APInt(64, 8)));
        auto *actual_count = impl_->builder->CreateLoad(
            llvm::Type::getInt64Ty(ctx),
            impl_->builder->CreateIntToPtr(count_address, pointer_type));
        auto *last_colon_type = llvm::FunctionType::get(
            pointer_type, {pointer_type, llvm::Type::getInt32Ty(ctx)}, false);
        auto last_colon_function =
            impl_->module->getOrInsertFunction("strrchr", last_colon_type);
        auto *last_colon = impl_->builder->CreateCall(
            last_colon_type, last_colon_function.getCallee(),
            {actual_tag, llvm::ConstantInt::get(ctx, llvm::APInt(32, ':'))});
        auto *after_colon = impl_->builder->CreateGEP(
            llvm::Type::getInt8Ty(ctx), last_colon,
            llvm::ConstantInt::get(ctx, llvm::APInt(64, 1)));
        auto *variant_tag = impl_->builder->CreateSelect(
            impl_->builder->CreateICmpEQ(
                last_colon, llvm::ConstantPointerNull::get(pointer_type)),
            actual_tag, after_colon);
        auto *strcmp_type = llvm::FunctionType::get(
            llvm::Type::getInt32Ty(ctx), {pointer_type, pointer_type}, false);
        auto strcmp_function =
            impl_->module->getOrInsertFunction("strcmp", strcmp_type);
        llvm::Value *matching_tag =
            llvm::ConstantInt::getFalse(ctx);
        for (const auto &variant : allowed_variants) {
          auto *comparison = impl_->builder->CreateCall(
              strcmp_type, strcmp_function.getCallee(),
              {variant_tag, impl_->builder->CreateGlobalString(variant)});
          matching_tag = impl_->builder->CreateOr(
              matching_tag, impl_->builder->CreateICmpEQ(
                                comparison, llvm::ConstantInt::get(
                                                llvm::Type::getInt32Ty(ctx), 0)));
        }
        auto *valid_shape = impl_->builder->CreateAnd(
            matching_tag, impl_->builder->CreateICmpEQ(
                              actual_count, llvm::ConstantInt::get(
                                                ctx, llvm::APInt(64, count))));
        auto *shape_success = llvm::BasicBlock::Create(
            ctx, "enum.unwrap.success", impl_->current_function);
        auto *shape_failure = llvm::BasicBlock::Create(
            ctx, "enum.unwrap.failure", impl_->current_function);
        impl_->builder->CreateCondBr(valid_shape, shape_success, shape_failure);
        impl_->builder->SetInsertPoint(shape_failure);
        impl_->builder->CreateCall(
            get_runtime_failure_function(),
            {impl_->builder->CreateGlobalString(
                "NIL native runtime error: enum payload shape mismatch\n")});
        impl_->builder->CreateUnreachable();
        impl_->builder->SetInsertPoint(shape_success);

        // Push fields in reverse order (so pop retrieves f0, f1...)
        // Memory: [Tag, Count, f0, f1...]
        // Stack needs: [..., f1, f0] (Top)
        // Pop -> f0.

        // Loop from count-1 down to 0
        for (std::size_t reverse_index = 0; reverse_index < count;
             ++reverse_index) {
          const std::size_t i = count - 1 - reverse_index;
          // Offset = 16 + i * 8
          const std::uint64_t offset = 16 + i * 8;

          auto field_loc = impl_->builder->CreateAdd(
              ptr_i64, llvm::ConstantInt::get(ctx, llvm::APInt(64, offset)));
          auto field_ptr = impl_->builder->CreateIntToPtr(
              field_loc,
              llvm::PointerType::getUnqual(llvm::Type::getInt64Ty(ctx)));
          llvm::Value *field_val = impl_->builder->CreateLoad(
              llvm::Type::getInt64Ty(ctx), field_ptr);
          const std::string &payload_type = payload_types->at(i);
          if (native_handle_kind_for_nil_type(payload_type) ==
                   native_handle_kind::text ||
               native_handle_kind_for_nil_type(payload_type) ==
                   native_handle_kind::anything ||
               native_handle_kind_for_nil_type(payload_type) ==
                   native_handle_kind::scalar_array ||
               native_handle_kind_for_nil_type(payload_type) ==
                   native_handle_kind::integer_pointer ||
               is_native_aggregate_nil_type(mod, payload_type)) {
            field_val = impl_->builder->CreateIntToPtr(
                field_val, llvm::PointerType::getUnqual(ctx));
          } else if (normalize_nil_type(payload_type) == "f64") {
            field_val = impl_->builder->CreateBitCast(
                field_val, llvm::Type::getDoubleTy(ctx));
          }

          impl_->pushValue(field_val, payload_type);
        }
        break;
      }

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
      case Opcode::MATH_MAX: {
        if (!minstr.args.empty()) {
          std::cerr << "[NativeCodegen] " << minstr.opname
                    << " must not encode operands in function '" << fn.name
                    << "'\n";
          return false;
        }

        const bool binary = op == Opcode::MATH_ATAN2 ||
                            op == Opcode::MATH_POW || op == Opcode::MATH_MIN ||
                            op == Opcode::MATH_MAX;
        bool use_libc = false;
        std::string math_name;
        switch (op) {
        case Opcode::MATH_SIN:
          math_name = "llvm.sin.f64";
          break;
        case Opcode::MATH_COS:
          math_name = "llvm.cos.f64";
          break;
        case Opcode::MATH_TAN:
          math_name = "tan";
          use_libc = true;
          break;
        case Opcode::MATH_ASIN:
          math_name = "asin";
          use_libc = true;
          break;
        case Opcode::MATH_ACOS:
          math_name = "acos";
          use_libc = true;
          break;
        case Opcode::MATH_ATAN:
          math_name = "atan";
          use_libc = true;
          break;
        case Opcode::MATH_ATAN2:
          math_name = "atan2";
          use_libc = true;
          break;
        case Opcode::MATH_SQRT:
          math_name = "llvm.sqrt.f64";
          break;
        case Opcode::MATH_POW:
          math_name = "llvm.pow.f64";
          break;
        case Opcode::MATH_EXP:
          math_name = "llvm.exp.f64";
          break;
        case Opcode::MATH_LOG:
          math_name = "llvm.log.f64";
          break;
        case Opcode::MATH_LOG10:
          math_name = "llvm.log10.f64";
          break;
        case Opcode::MATH_ABS:
          math_name = "llvm.fabs.f64";
          break;
        case Opcode::MATH_FLOOR:
          math_name = "llvm.floor.f64";
          break;
        case Opcode::MATH_CEIL:
          math_name = "llvm.ceil.f64";
          break;
        case Opcode::MATH_ROUND:
          math_name = "llvm.round.f64";
          break;
        case Opcode::MATH_TRUNC:
          math_name = "llvm.trunc.f64";
          break;
        case Opcode::MATH_CBRT:
          math_name = "cbrt";
          use_libc = true;
          break;
        case Opcode::MATH_MIN:
          math_name = "llvm.minnum.f64";
          break;
        case Opcode::MATH_MAX:
          math_name = "llvm.maxnum.f64";
          break;
        default:
          return false;
        }

        auto exact_f64_operand = [&](llvm::Value *value) -> llvm::Value * {
          if (!value || normalize_nil_type(impl_->nilType(value)) != "f64")
            return nullptr;
          if (value->getType()->isDoubleTy())
            return value;
          if (value->getType()->isFloatTy())
            return impl_->builder->CreateFPExt(value,
                                               llvm::Type::getDoubleTy(ctx));
          // f64 values cross Ny function boundaries as their exact i64 bit
          // representation. They must be bit-cast, never numerically cast.
          if (value->getType()->isIntegerTy(64))
            return impl_->builder->CreateBitCast(value,
                                                 llvm::Type::getDoubleTy(ctx));
          return nullptr;
        };

        llvm::Value *rhs = nullptr;
        if (binary) {
          rhs = exact_f64_operand(impl_->popValue());
          if (!rhs) {
            std::cerr << "[NativeCodegen] " << minstr.opname
                      << " requires exact f64 operands in function '" << fn.name
                      << "'\n";
            return false;
          }
        }
        auto *lhs = exact_f64_operand(impl_->popValue());
        if (!lhs) {
          std::cerr << "[NativeCodegen] " << minstr.opname
                    << " requires exact f64 operands in function '" << fn.name
                    << "'\n";
          return false;
        }

        auto *f64_type = llvm::Type::getDoubleTy(ctx);
        auto *math_type = llvm::FunctionType::get(
            f64_type,
            binary ? std::vector<llvm::Type *>{f64_type, f64_type}
                   : std::vector<llvm::Type *>{f64_type},
            false);
        llvm::Function *math_function = nullptr;
        if (use_libc) {
          math_function = llvm::cast_or_null<llvm::Function>(
              impl_->module->getOrInsertFunction(math_name, math_type)
                  .getCallee());
        } else {
          std::string lookup_name = math_name;
          lookup_name.erase(lookup_name.size() - 4);
          const auto intrinsic_id =
              lookupIntrinsicID(lookup_name);
          if (intrinsic_id != llvm::Intrinsic::not_intrinsic) {
            math_function = llvm::Intrinsic::getDeclaration(
                impl_->module.get(), intrinsic_id, {f64_type});
          }
        }
        if (!math_function) {
          std::cerr << "[NativeCodegen] " << minstr.opname
                    << " is unavailable in the configured native target\n";
          return false;
        }

        std::vector<llvm::Value *> arguments = {lhs};
        if (binary)
          arguments.push_back(rhs);
        auto *result =
            impl_->builder->CreateCall(math_function, arguments, "ny.math");
        impl_->pushValue(result, "f64");
        break;
      }

      case Opcode::INTEROP_CALL: {
        // Function name is in args[1], args[0] is lib name
        std::string lib_name = minstr.args.size() > 0 ? minstr.args[0] : "";
        std::string func_name =
            minstr.args.size() > 1
                ? minstr.args[1]
                : (minstr.args.size() > 0 ? minstr.args[0] : "");

        // System intrinsics use the same versioned runtime ABI in JIT and AOT.
        if (lib_name == "std_sys") {
          const std::string encoded_count =
              minstr.args.size() >= 3 ? minstr.args[2] : std::string{};
          if (func_name == "arg_count" && encoded_count == "0" &&
              minstr.args.size() == 4 && minstr.args[3] == "i64") {
            auto runtime_type =
                llvm::FunctionType::get(llvm::Type::getInt64Ty(ctx), {}, false);
            auto runtime = impl_->module->getOrInsertFunction("nyrt_arg_count",
                                                              runtime_type);
            impl_->pushValue(impl_->builder->CreateCall(
                                 runtime_type, runtime.getCallee(), {}),
                             "i64");
            break;
          }
          const bool exact_arg_shape =
              func_name == "arg" && encoded_count == "1" &&
              (minstr.args.size() == 4 || minstr.args.size() == 5) &&
              minstr.args[3] == "text" &&
              (minstr.args.size() == 4 ||
               normalize_nil_type(minstr.args[4]) == "i64");
          if (exact_arg_shape) {
            auto *index = impl_->popValue();
            if (!index || !index->getType()->isIntegerTy() ||
                normalize_nil_type(impl_->nilType(index)) != "i64") {
              std::cerr << "[NativeCodegen] std_sys.arg requires one i64 "
                           "operand in function '"
                        << fn.name << "'\n";
              return false;
            }
            auto runtime_type =
                llvm::FunctionType::get(llvm::PointerType::getUnqual(ctx),
                                        {llvm::Type::getInt64Ty(ctx)}, false);
            auto runtime =
                impl_->module->getOrInsertFunction("nyrt_arg", runtime_type);
            auto *value = impl_->builder->CreateCall(
                runtime_type, runtime.getCallee(), {impl_->coerceToI64(index)});
            auto *success = llvm::BasicBlock::Create(ctx, "arg.success",
                                                     impl_->current_function);
            auto *failure = llvm::BasicBlock::Create(ctx, "arg.failure",
                                                     impl_->current_function);
            impl_->builder->CreateCondBr(
                impl_->builder->CreateICmpNE(
                    value,
                    llvm::ConstantPointerNull::get(
                        llvm::cast<llvm::PointerType>(value->getType()))),
                success, failure);
            impl_->builder->SetInsertPoint(failure);
            auto *message = impl_->builder->CreateGlobalString(
                "NIL native runtime error: std_sys.arg index is outside the "
                "argument list\n");
            impl_->builder->CreateCall(get_runtime_failure_function(),
                                       {message});
            impl_->builder->CreateUnreachable();
            impl_->builder->SetInsertPoint(success);
            impl_->pushValue(value, "text");
            break;
          }
          const bool exact_exit_shape =
              func_name == "exit" && encoded_count == "1" &&
              (minstr.args.size() == 4 || minstr.args.size() == 5) &&
              minstr.args[3] == "void" &&
              (minstr.args.size() == 4 ||
               normalize_nil_type(minstr.args[4]) == "i32");
          if (exact_exit_shape) {
            auto *code = impl_->popValue();
            if (!code || !code->getType()->isIntegerTy() ||
                normalize_nil_type(impl_->nilType(code)) != "i32") {
              std::cerr << "[NativeCodegen] std_sys.exit requires one i32 "
                           "operand in function '"
                        << fn.name << "'\n";
              return false;
            }
            code = impl_->coerceToI64(code);
            auto runtime_type =
                llvm::FunctionType::get(llvm::Type::getVoidTy(ctx),
                                        {llvm::Type::getInt64Ty(ctx)}, false);
            auto runtime =
                impl_->module->getOrInsertFunction("nyrt_exit", runtime_type);
            impl_->builder->CreateCall(runtime_type, runtime.getCallee(),
                                       {code});
            impl_->builder->CreateUnreachable();
            skip_until_label = true;
            break;
          }
          std::cerr << "[NativeCodegen] unsupported or malformed std_sys "
                       "interop call '"
                    << func_name << "' in function '" << fn.name << "'\n";
          return false;
        }

        // Generic intrinsic calls can be emitted directly at the call site
        // when specialization removes the declaration wrapper. Keep this
        // allowlist as strict as the wrapper lowering above; everything else
        // must fail closed instead of entering the legacy foreign-call path.
        if (lib_name == "intrinsic") {
          if (func_name.rfind("nildata.", 0) == 0) {
            const std::vector<std::string> expected{"text", "text", "text", "i64"};
            if (func_name != "nildata.request" || minstr.args.size() != 4 || minstr.args[2] != "4" ||
                minstr.args[3] != "array<text>" || impl_->value_stack.size() < 4) {
              std::cerr << "[NativeCodegen] nildata.request has an invalid native ABI\n";
              return false;
            }
            std::vector<llvm::Value *> arguments(4);
            for (std::size_t i = 4; i > 0; --i) {
              auto *value = impl_->popValue();
              if (!value || normalize_nil_type(impl_->nilType(value)) != expected[i - 1] ||
                  (i == 4 ? !value->getType()->isIntegerTy() : !value->getType()->isPointerTy())) {
                std::cerr << "[NativeCodegen] nildata.request requires exact typed arguments\n";
                return false;
              }
              arguments[i - 1] = i == 4 ? impl_->coerceToI64(value) : value;
            }
            auto *pointer = llvm::PointerType::getUnqual(ctx);
            auto *type = llvm::FunctionType::get(pointer,
                {pointer, pointer, pointer, llvm::Type::getInt64Ty(ctx)}, false);
            auto runtime = impl_->module->getOrInsertFunction("nyrt_nildata_v1_request", type);
            impl_->pushValue(impl_->builder->CreateCall(type, runtime.getCallee(), arguments), "array<text>");
            break;
          }
          if (func_name.rfind("regex.", 0) == 0) {
            const bool validation = func_name == "regex.validate";
            const bool searching = func_name == "regex.search";
            const bool group = func_name == "regex.group_index";
            const std::vector<std::string> expected = validation ? std::vector<std::string>{"text"}
                : searching ? std::vector<std::string>{"text", "text", "i64", "bool"}
                            : std::vector<std::string>{"text", "text"};
            const std::string result_type = validation ? "text" : searching ? "array<i64>" : "i64";
            if ((!validation && !searching && !group) || minstr.args.size() != 4 ||
                minstr.args[2] != std::to_string(expected.size()) || minstr.args[3] != result_type ||
                impl_->value_stack.size() < expected.size()) {
              std::cerr << "[NativeCodegen] " << func_name << " has an invalid native ABI\n";
              return false;
            }
            auto *pointer = llvm::PointerType::getUnqual(ctx);
            auto *word = llvm::Type::getInt64Ty(ctx);
            std::vector<llvm::Value *> arguments(expected.size());
            std::vector<llvm::Type *> types(expected.size());
            for (std::size_t i = expected.size(); i > 0; --i) {
              auto *value = impl_->popValue();
              if (normalize_nil_type(impl_->nilType(value)) != expected[i - 1] ||
                  (expected[i - 1] == "text" ? !value->getType()->isPointerTy()
                                             : !value->getType()->isIntegerTy())) {
                std::cerr << "[NativeCodegen] " << func_name << " requires exact typed arguments\n";
                return false;
              }
              types[i - 1] = expected[i - 1] == "text" ? static_cast<llvm::Type *>(pointer) : word;
              arguments[i - 1] = expected[i - 1] == "text" ? value : impl_->coerceToI64(value);
            }
            auto *type = llvm::FunctionType::get(group ? static_cast<llvm::Type *>(word) : pointer, types, false);
            auto runtime = impl_->module->getOrInsertFunction(validation ? "nyrt_regex_validate" :
                searching ? "nyrt_regex_search" : "nyrt_regex_group_index", type);
            impl_->pushValue(impl_->builder->CreateCall(type, runtime.getCallee(), arguments), result_type);
            break;
          }
          if (func_name == "console.read_line" || func_name == "console.flush") {
            const bool reading = func_name == "console.read_line";
            if (minstr.args.size() != 4 || minstr.args[2] != "0" ||
                minstr.args[3] != (reading ? "text" : "void")) {
              std::cerr << "[NativeCodegen] " << func_name
                        << " requires the exact () -> "
                        << (reading ? "text" : "void") << " ABI\n";
              return false;
            }
            auto *runtime_type = llvm::FunctionType::get(
                reading ? static_cast<llvm::Type *>(llvm::PointerType::getUnqual(ctx))
                        : llvm::Type::getVoidTy(ctx), {}, false);
            auto runtime = impl_->module->getOrInsertFunction(
                reading ? "nyrt_read_line" : "nyrt_flush_stdout", runtime_type);
            auto *value = impl_->builder->CreateCall(runtime_type, runtime.getCallee(), {});
            if (reading) impl_->pushValue(value, "text");
            break;
          }
          if (is_array_intrinsic(func_name) && minstr.args.size() == 5) {
            array_intrinsic_contract contract;
            try {
              contract = parse_array_intrinsic(minstr.args);
            } catch (const std::exception &error) {
              std::cerr << "[NativeCodegen] " << error.what() << '\n';
              return false;
            }
            if (impl_->value_stack.size() < contract.argument_count) {
              std::cerr << "[NativeCodegen] array intrinsic operand stack underflow\n";
              return false;
            }
            std::vector<llvm::Value *> arguments(contract.argument_count);
            for (std::size_t index = contract.argument_count; index > 0; --index)
              arguments[index - 1] = impl_->popValue();
            const auto kind = native_array_kind_for_element(contract.element);
            if (!kind) return false;
            const auto integer_argument = [&](std::size_t index) -> llvm::Value * {
              auto *value = arguments[index];
              const auto nil_type = normalize_nil_type(impl_->nilType(value));
              if (!native_value_is_numeric_scalar(value) ||
                  !value->getType()->isIntegerTy() ||
                  (nil_type != "i32" && nil_type != "i64")) {
                std::cerr << "[NativeCodegen] array intrinsic requires an integer size/index\n";
                return nullptr;
              }
              return impl_->coerceToI64(value);
            };
            const auto checked_range = [&](llvm::Value *value, std::int64_t minimum,
                                           const char *diagnostic) {
              auto *valid = impl_->builder->CreateAnd(
                  impl_->builder->CreateICmpSGE(value, llvm::ConstantInt::get(
                      ctx, llvm::APInt(64, minimum, true))),
                  impl_->builder->CreateICmpSLE(value, llvm::ConstantInt::get(
                      ctx, llvm::APInt(64, std::numeric_limits<std::int32_t>::max()))));
              auto *success = llvm::BasicBlock::Create(ctx, "array.range.valid",
                                                        impl_->current_function);
              auto *failure = llvm::BasicBlock::Create(ctx, "array.range.invalid",
                                                        impl_->current_function);
              impl_->builder->CreateCondBr(valid, success, failure);
              impl_->builder->SetInsertPoint(failure);
              auto *message = impl_->builder->CreateGlobalString(
                  std::string("NIL native runtime error: ") + diagnostic + "\n");
              impl_->builder->CreateCall(get_runtime_failure_function(), {message});
              impl_->builder->CreateUnreachable();
              impl_->builder->SetInsertPoint(success);
            };
            auto *kind_value = llvm::ConstantInt::get(ctx, llvm::APInt(32, *kind));
            auto *word_type = llvm::Type::getInt64Ty(ctx);
            auto *pointer_type = llvm::PointerType::getUnqual(ctx);
            auto *kind_type = llvm::Type::getInt32Ty(ctx);
            if (contract.operation == array_intrinsic_operation::create) {
              auto *size = integer_argument(0);
              require_initialized_struct(arguments[1], func_name);
              auto *initial = native_storage_word(arguments[1], contract.element, func_name);
              if (!size || !initial)
                return false;
              checked_range(size, 0, "array size must be between 0 and 2147483647");
              auto *type = llvm::FunctionType::get(pointer_type,
                  {kind_type, word_type, word_type}, false);
              auto runtime = impl_->module->getOrInsertFunction("nyrt_array_new_with_value", type);
              auto *array = impl_->builder->CreateCall(type, runtime.getCallee(),
                                                       {kind_value, size, initial});
              emit_allocation_null_check(array, func_name);
              impl_->pushValue(array, contract.result);
              break;
            }
            auto *array = arguments[0];
            if (!array->getType()->isPointerTy() ||
                !native_handle_types_compatible(impl_->nilType(array),
                                                "array<" + contract.element + ">")) {
              std::cerr << "[NativeCodegen] array intrinsic requires a matching typed array handle\n";
              return false;
            }
            if (contract.operation == array_intrinsic_operation::length) {
              auto *length = array_length_value(array);
              checked_range(length, 0, "array length exceeds the i32 range");
              impl_->pushValue(length, "i32");
              break;
            }
            auto *index = integer_argument(1);
            if (!index)
              return false;
            checked_range(index, std::numeric_limits<std::int32_t>::min(),
                          "array index is outside the i32 range");
            if (contract.operation == array_intrinsic_operation::get) {
              auto *type = llvm::FunctionType::get(word_type,
                  {pointer_type, kind_type, word_type}, false);
              auto runtime = impl_->module->getOrInsertFunction("nyrt_array_get", type);
              auto *word = impl_->builder->CreateCall(type, runtime.getCallee(),
                                                      {array, kind_value, index});
              impl_->pushValue(native_array_loaded_value(word, contract.element), contract.element);
            } else {
              require_initialized_struct(arguments[2], func_name);
              auto *value = native_storage_word(arguments[2], contract.element, func_name);
              if (!value)
                return false;
              auto *type = llvm::FunctionType::get(kind_type,
                  {pointer_type, kind_type, word_type, word_type}, false);
              auto runtime = impl_->module->getOrInsertFunction("nyrt_array_set", type);
              impl_->builder->CreateCall(type, runtime.getCallee(),
                                         {array, kind_value, index, value});
            }
            break;
          }
          if (func_name == "array.length" && minstr.args.size() == 4 &&
              minstr.args[2] == "1" && minstr.args[3] == "i64") {
            auto *array = impl_->popValue();
            const std::string array_type =
                array == nullptr ? std::string{} : impl_->nilType(array);
            if (!array || !array->getType()->isPointerTy() ||
                native_handle_kind_for_nil_type(array_type) !=
                    native_handle_kind::scalar_array) {
              std::cerr << "[NativeCodegen] intrinsic array.length requires "
                           "one exact native array operand in function '"
                        << fn.name << "'\n";
              return false;
            }
            auto *length = array_length_value(array);
            if (!length) {
              std::cerr << "[NativeCodegen] intrinsic array.length has no "
                           "supported element-kind metadata in function '"
                        << fn.name << "'\n";
              return false;
            }
            auto *valid = impl_->builder->CreateAnd(
                impl_->builder->CreateICmpSGE(
                    length, llvm::ConstantInt::get(ctx, llvm::APInt(64, 0))),
                impl_->builder->CreateICmpSLE(
                    length,
                    llvm::ConstantInt::get(
                        ctx,
                        llvm::APInt(
                            64, std::numeric_limits<std::int32_t>::max()))));
            auto *failure = llvm::BasicBlock::Create(
                ctx, "array.length.failure", impl_->current_function);
            auto *success = llvm::BasicBlock::Create(
                ctx, "array.length.success", impl_->current_function);
            impl_->builder->CreateCondBr(valid, success, failure);
            impl_->builder->SetInsertPoint(failure);
            auto *message = impl_->builder->CreateGlobalString(
                "NIL native runtime error: array.length received an invalid "
                "array or a length outside the i32 range\n");
            impl_->builder->CreateCall(get_runtime_failure_function(),
                                       {message});
            impl_->builder->CreateUnreachable();
            impl_->builder->SetInsertPoint(success);
            impl_->pushValue(length, "i32");
            break;
          }

          std::cerr << "[NativeCodegen] unsupported direct native intrinsic '"
                    << func_name << "' in function '" << fn.name << "'\n";
          return false;
        }

        // External interop still lacks a declared, versioned native ABI and
        // the former generic path guessed argument layouts while ignoring the
        // encoded library. That could silently bind an unrelated host symbol.
        // Reject it until an explicit FFI contract and library allowlist are
        // carried into native codegen. This also quarantines the obsolete
        // size-prefixed vm__get_address/vm__write_bytes assumptions, which do
        // not match the current C-string and opaque-array runtime layouts.
        std::cerr << "[NativeCodegen] external interop library '" << lib_name
                  << "' for function '" << func_name
                  << "' has no supported versioned native ABI in function '"
                  << fn.name << "'\n";
        return false;
      }

      case Opcode::RET: {
        const bool generated_nonvoid_fallthrough =
            minstr.args.size() == 1 &&
            minstr.args.front() == nil_nonvoid_fallthrough_operand;
        if (!minstr.args.empty() && !generated_nonvoid_fallthrough) {
          std::cerr << "[NativeCodegen] invalid RET operand in function '"
                    << fn.name << "'\n";
          return false;
        }
        const bool typed_nonvoid =
            !fn.return_type.empty() && fn.return_type != "void";
        const std::size_t expected_returns =
            typed_nonvoid && !generated_nonvoid_fallthrough ? 1 : 0;
        const bool legacy_untyped_main =
            fn.name == "main" && fn.return_type.empty() &&
            impl_->value_stack.size() <= 1;
        if (!legacy_untyped_main &&
            impl_->value_stack.size() != expected_returns) {
          std::cerr << "[NativeCodegen] invalid RET stack in function '"
                    << fn.name << "': expected " << expected_returns
                    << " value(s), found " << impl_->value_stack.size()
                    << '\n';
          return false;
        }
        if (generated_nonvoid_fallthrough) {
          auto *message = impl_->builder->CreateGlobalString(
              "NIL native runtime error: non-void function reached "
              "fallthrough\n");
          impl_->builder->CreateCall(get_runtime_failure_function(), {message});
          impl_->builder->CreateUnreachable();
          skip_until_label = true;
          break;
        }

        // Get current function return type
        llvm::Function *current_fn =
            impl_->builder->GetInsertBlock()->getParent();
        llvm::Type *ret_type = current_fn->getReturnType();

        if (fn.name == "main") {
          // Main always returns i32
          llvm::Value *ret_val = impl_->popValue();
          if (ret_val) {
            if (find_nil_enum_definition(mod, impl_->nilType(ret_val)) !=
                nullptr) {
              std::cerr << "[NativeCodegen] main cannot return enum '"
                        << impl_->nilType(ret_val)
                        << "' through the platform integer exit ABI\n";
              return false;
            }
            if (ret_val->getType() != llvm::Type::getInt32Ty(ctx)) {
              if (ret_val->getType()->isFloatingPointTy()) {
                // Raw NIL must request the checked CAST operation explicitly.
                // An implicit FPToSI here would be poison for NaN, infinity,
                // and finite values outside the platform exit-code width.
                std::cerr << "[NativeCodegen] main return value must be an "
                             "integer scalar; use an explicit CAST\n";
                return false;
              } else if (ret_val->getType()->isIntegerTy()) {
                ret_val =
                    impl_->castInteger(ret_val, llvm::Type::getInt32Ty(ctx));
              } else {
                std::cerr
                    << "[NativeCodegen] main return value is not numeric\n";
                return false;
              }
            }
            impl_->builder->CreateRet(ret_val);
          } else {
            impl_->builder->CreateRet(
                llvm::ConstantInt::get(ctx, llvm::APInt(32, 0)));
          }
        } else if (ret_type->isVoidTy()) {
          // Void function - just return
          impl_->builder->CreateRetVoid();
        } else {
          // Non-void function - return value from stack or default 0
          llvm::Value *ret_val = impl_->popValue();
          const bool returns_aggregate =
              is_native_aggregate_nil_type(mod, fn.return_type);
          const auto *return_enum =
              find_nil_enum_definition(mod, fn.return_type);
          const auto return_handle_kind =
              native_handle_kind_for_nil_type(fn.return_type);
          const bool returns_handle =
              return_handle_kind == native_handle_kind::text ||
              return_handle_kind == native_handle_kind::anything ||
              return_handle_kind == native_handle_kind::scalar_array ||
              return_handle_kind == native_handle_kind::integer_pointer;
          if (ret_val) {
            const std::string actual_nil_type = impl_->nilType(ret_val);
            if (normalize_nil_type(fn.return_type) == "f64") {
              if (normalize_nil_type(actual_nil_type) != "f64" ||
                  (!ret_val->getType()->isDoubleTy() &&
                   !ret_val->getType()->isIntegerTy(64))) {
                std::cerr << "[NativeCodegen] function '" << fn.name
                          << "' requires an exact f64 return value\n";
                return false;
              }
              if (ret_val->getType()->isDoubleTy())
                ret_val = impl_->builder->CreateBitCast(
                    ret_val, llvm::Type::getInt64Ty(ctx), "ny.return.f64.bits");
            } else if (normalize_nil_type(actual_nil_type) == "f64") {
              std::cerr << "[NativeCodegen] function '" << fn.name
                        << "' cannot return f64 through its '" << fn.return_type
                        << "' signature\n";
              return false;
            } else if (returns_aggregate) {
              if (!ret_val->getType()->isPointerTy() ||
                  !native_aggregate_types_compatible(mod, actual_nil_type,
                                                     fn.return_type)) {
                std::cerr << "[NativeCodegen] function '" << fn.name
                          << "' returns '"
                          << (actual_nil_type.empty()
                                  ? std::string("untyped value")
                                  : actual_nil_type)
                          << "' where its NIL signature requires aggregate "
                             "type '"
                          << fn.return_type << "'\n";
                return false;
              }
              require_initialized_struct(ret_val, "RET");
            } else if (returns_handle) {
              if (!ret_val->getType()->isPointerTy() ||
                  !native_handle_types_compatible(actual_nil_type,
                                                  fn.return_type)) {
                std::cerr << "[NativeCodegen] function '" << fn.name
                          << "' returns '"
                          << (actual_nil_type.empty()
                                  ? std::string("untyped value")
                                  : actual_nil_type)
                          << "' where its NIL signature requires handle type '"
                          << fn.return_type << "'\n";
                return false;
              }
            } else if (return_enum != nullptr) {
              const auto *actual_enum =
                  find_nil_enum_definition(mod, actual_nil_type);
              if (!ret_val->getType()->isIntegerTy() ||
                  actual_enum != return_enum) {
                std::cerr << "[NativeCodegen] function '" << fn.name
                          << "' returns '"
                          << (actual_nil_type.empty()
                                  ? std::string("untyped scalar")
                                  : actual_nil_type)
                          << "' where its NIL signature requires enum type '"
                          << fn.return_type << "'\n";
                return false;
              }
            } else if (find_nil_enum_definition(mod, actual_nil_type) !=
                       nullptr) {
              std::cerr << "[NativeCodegen] function '" << fn.name
                        << "' returns enum '" << actual_nil_type
                        << "' where its NIL signature requires scalar type '"
                        << fn.return_type << "'\n";
              return false;
            } else if (ret_val->getType()->isPointerTy()) {
              std::cerr << "[NativeCodegen] function '" << fn.name
                        << "' returns opaque handle '"
                        << impl_->nilType(ret_val)
                        << "' where its NIL signature requires scalar type '"
                        << fn.return_type << "'\n";
              return false;
            }
            if (normalize_nil_type(fn.return_type) == "bool") {
              ret_val = native_storage_word(ret_val, "bool", "RET");
              if (!ret_val) return false;
            }
            // Cast to expected return type if needed
            if (ret_val->getType() != ret_type) {
              if (ret_val->getType()->isIntegerTy() &&
                  ret_type->isIntegerTy()) {
                ret_val = impl_->castInteger(ret_val, ret_type);
              } else if (ret_val->getType()->isFloatingPointTy()) {
                std::cerr
                    << "[NativeCodegen] floating-point return values require "
                       "typed NIL function ABI metadata in function '"
                    << fn.name << "'\n";
                return false;
              }
            }
            impl_->builder->CreateRet(ret_val);
          } else {
            if (returns_aggregate || returns_handle || return_enum != nullptr) {
              std::cerr << "[NativeCodegen] function '" << fn.name
                        << "' returns no value where its NIL signature "
                           "requires typed "
                        << (returns_aggregate
                                ? "aggregate"
                                : (returns_handle ? "handle" : "enum"))
                        << " type '" << fn.return_type << "'\n";
              return false;
            } else {
              // Preserve the historical scalar default only for scalar ABI
              // signatures; typed handles, structs, and enums must never be
              // manufactured from an untyped zero.
              impl_->builder->CreateRet(llvm::Constant::getNullValue(ret_type));
            }
          }
        }

        // Create a dead block for any unreachable code that follows
        // This prevents "instructions after terminator" errors
        auto dead_bb = llvm::BasicBlock::Create(ctx, "after_ret", current_fn);
        impl_->builder->SetInsertPoint(dead_bb);
        // Mark that we should skip instructions until next LABEL
        skip_until_label = true;
        // Clear value stack to prevent stale values from being used
        impl_->value_stack.clear();
        break;
      }

      case Opcode::STORE_LOCAL: {
        if (!minstr.args.empty()) {
          std::string var_name = minstr.args[0];
          // std::cerr << "[NativeCodegen] STORE_LOCAL '" << var_name << "'
          // stack_size=" << impl_->value_stack.size() << "\n";
          auto val = impl_->popValue();
          if (val) {
            require_initialized_struct(val, "STORE_LOCAL " + var_name);
            const std::string value_nil_type = impl_->nilType(val);
            // Ensure we have an alloca for this variable
            llvm::AllocaInst *alloca_inst = nullptr;
            auto it = impl_->locals_map.find(var_name);
            if (it != impl_->locals_map.end()) {
              alloca_inst = it->second;
            } else {
              // Insert alloca in entry block
              llvm::IRBuilder<> tmp_builder(entry);
              if (entry->empty()) {
                tmp_builder.SetInsertPoint(entry);
              } else {
                tmp_builder.SetInsertPoint(&entry->front());
              }
              alloca_inst =
                  tmp_builder.CreateAlloca(val->getType(), nullptr, var_name);
              impl_->locals_map[var_name] = alloca_inst;
              if (!value_nil_type.empty())
                impl_->local_nil_types[var_name] = value_nil_type;
              // std::cerr << "[NativeCodegen] Created alloca for '" << var_name
              // << "'\n";
            }
            llvm::Type *storage_type = alloca_inst->getAllocatedType();
            if (val->getType()->isPointerTy() != storage_type->isPointerTy()) {
              std::cerr << "[NativeCodegen] STORE_LOCAL for '" << var_name
                        << "' in function '" << fn.name
                        << "' cannot reinterpret an opaque handle as a scalar "
                           "or a scalar as an opaque handle\n";
              return false;
            }
            const auto expected_nil_type =
                impl_->local_nil_types.find(var_name);
            if (val->getType()->isPointerTy() &&
                expected_nil_type != impl_->local_nil_types.end() &&
                !expected_nil_type->second.empty() &&
                !native_pointer_types_compatible(mod, value_nil_type,
                                                 expected_nil_type->second)) {
              std::cerr << "[NativeCodegen] STORE_LOCAL for '" << var_name
                        << "' in function '" << fn.name
                        << "' assigns pointer value '" << value_nil_type
                        << "' to incompatible local type '"
                        << expected_nil_type->second << "'\n";
              return false;
            }
            if (val->getType()->isIntegerTy() &&
                expected_nil_type != impl_->local_nil_types.end() &&
                !expected_nil_type->second.empty()) {
              const auto *expected_enum =
                  find_nil_enum_definition(mod, expected_nil_type->second);
              const auto *actual_enum =
                  find_nil_enum_definition(mod, value_nil_type);
              if ((expected_enum != nullptr || actual_enum != nullptr) &&
                  expected_enum != actual_enum) {
                std::cerr << "[NativeCodegen] STORE_LOCAL for '" << var_name
                          << "' in function '" << fn.name << "' assigns '"
                          << (value_nil_type.empty()
                                  ? std::string("untyped scalar")
                                  : value_nil_type)
                          << "' to incompatible enum/scalar local type '"
                          << expected_nil_type->second << "'\n";
                return false;
              }
            }
            if (expected_nil_type != impl_->local_nil_types.end() &&
                expected_nil_type->second == "bool") {
              val = native_storage_word(val, "bool", "STORE_LOCAL");
              if (!val) return false;
            }
            if (val->getType() != storage_type) {
              if (val->getType()->isIntegerTy() &&
                  storage_type->isIntegerTy()) {
                val = impl_->castInteger(val, storage_type);
              } else if ((val->getType()->isIntegerTy() &&
                          storage_type->isFloatingPointTy()) ||
                         (val->getType()->isFloatingPointTy() &&
                          storage_type->isIntegerTy())) {
                // A local's established scalar representation is not an
                // implicit numeric conversion boundary. In particular,
                // unchecked FPToSI can manufacture poison from raw NIL.
                // Source lowering emits explicit, range-checked CASTs.
                std::cerr << "[NativeCodegen] STORE_LOCAL for '" << var_name
                          << "' in function '" << fn.name
                          << "' cannot implicitly convert between integer "
                             "and floating-point storage; use an explicit CAST\n";
                return false;
              } else if (val->getType()->isFloatingPointTy() &&
                         storage_type->isFloatingPointTy()) {
                val = storage_type->isDoubleTy()
                          ? impl_->builder->CreateFPExt(val, storage_type)
                          : impl_->builder->CreateFPTrunc(val, storage_type);
              } else if (val->getType()->isPointerTy() &&
                         storage_type->isPointerTy()) {
                val = impl_->builder->CreatePointerCast(val, storage_type);
              }
            }
            if (val->getType()->isPointerTy()) {
              const std::string stored_type =
                  expected_nil_type == impl_->local_nil_types.end()
                      ? value_nil_type
                      : expected_nil_type->second;
              // Storage owns this spelling. Do not retag a shared source SSA
              // value which may also be pending on another operand-stack edge.
              impl_->local_nil_types[var_name] = stored_type;
            }
            impl_->builder->CreateStore(val, alloca_inst);
          } else {
            std::cerr << "[NativeCodegen] ERROR: STORE_LOCAL '" << var_name
                      << "' - no value on stack!\n";
            return false;
          }
        }
        break;
      }

      case Opcode::REF_LOCAL:
      case Opcode::LOAD_LOCAL: {
        if (!minstr.args.empty()) {
          std::string var_name = minstr.args[0];
          auto it = impl_->locals_map.find(var_name);
          if (it != impl_->locals_map.end()) {
            const auto local_type = impl_->local_nil_types.find(var_name);
            const std::string nil_type =
                local_type == impl_->local_nil_types.end()
                    ? std::string{}
                    : local_type->second;
            const auto *aggregate =
                find_nil_struct_definition(mod, nil_type);
            if (op == Opcode::REF_LOCAL &&
                (!it->second->getAllocatedType()->isPointerTy() || !aggregate)) {
              std::cerr << "[NativeCodegen] REF_LOCAL for '" << var_name
                        << "' in function '" << fn.name
                        << "' requires a concrete struct local; primitive and "
                           "nested references have no native loan semantics\n";
              return false;
            }
            auto load = impl_->builder->CreateLoad(
                it->second->getAllocatedType(), it->second, var_name);
            impl_->pushValue(load, op == Opcode::REF_LOCAL
                                       ? "ref<" + aggregate->name + ">"
                                       : nil_type);
          } else {
            std::cerr << "[NativeCodegen] Error: Undefined local " << var_name
                      << "\n";
            return false;
          }
        }
        break;
      }

      case Opcode::STORE_GLOBAL: {
        if (minstr.args.empty())
          break;
        const auto global = impl_->globals_map.find(minstr.args[0]);
        auto *value = impl_->popValue();
        require_initialized_struct(value, "STORE_GLOBAL " + minstr.args[0]);
        const auto &type = impl_->global_nil_types.at(minstr.args[0]);
        const bool typed_handle = type == "text" || type == "anything";
        const bool valid = typed_handle
            ? value && value->getType()->isPointerTy() &&
                  normalize_nil_type(impl_->nilType(value)) == type
            : type == "f64"
                ? native_value_is_numeric_scalar(value) &&
                      value->getType()->isDoubleTy()
                : !type.empty()
                    ? native_value_is_numeric_scalar(value) &&
                          value->getType()->isIntegerTy()
                    : !native_value_has_nominal_or_handle_semantics(value);
        if (!valid && !type.empty()) {
          std::cerr << "[NativeCodegen] global '" << minstr.args[0]
                    << "' value does not match storage type '" << type << "'\n";
          return false;
        }
        if (global == impl_->globals_map.end()) {
          // The global has no reader in the native reachable graph. Its
          // initializer was still evaluated; discard only the dead result.
          break;
        }
        if (!valid) {
          std::cerr << "[NativeCodegen] pointer/text global '" << minstr.args[0]
                    << "' requires typed NIL global metadata\n";
          return false;
        }
        auto *stored_value = typed_handle || type == "f64"
                                 ? value : impl_->coerceToI64(value);
        if (!stored_value) {
          std::cerr << "[NativeCodegen] Error: Cannot store global '"
                    << minstr.args[0] << "'\n";
          return false;
        }
        if (type == "i32" || type == "u8" || type == "bool") {
          const std::int64_t minimum = type == "i32" ? INT32_MIN : 0;
          const std::int64_t maximum = type == "i32" ? INT32_MAX
                                      : type == "u8" ? 255 : 1;
          auto *in_range = impl_->builder->CreateAnd(
              impl_->builder->CreateICmpSGE(stored_value,
                  llvm::ConstantInt::getSigned(llvm::Type::getInt64Ty(ctx), minimum)),
              impl_->builder->CreateICmpSLE(stored_value,
                  llvm::ConstantInt::getSigned(llvm::Type::getInt64Ty(ctx), maximum)));
          auto *failure = llvm::BasicBlock::Create(ctx, "global.range.failure",
                                                   impl_->current_function);
          auto *success = llvm::BasicBlock::Create(ctx, "global.range.success",
                                                   impl_->current_function);
          impl_->builder->CreateCondBr(in_range, success, failure);
          impl_->builder->SetInsertPoint(failure);
          auto *message = impl_->builder->CreateGlobalString(
              "NIL native runtime error: global value does not match storage type\n");
          impl_->builder->CreateCall(get_runtime_failure_function(), {message});
          impl_->builder->CreateUnreachable();
          impl_->builder->SetInsertPoint(success);
        }
        impl_->builder->CreateStore(stored_value, global->second);
        if (!type.empty())
          impl_->builder->CreateStore(llvm::ConstantInt::getTrue(ctx),
                                      impl_->global_initialized.at(minstr.args[0]));
        break;
      }

      case Opcode::LOAD_GLOBAL: {
        if (minstr.args.empty())
          break;
        const auto global = impl_->globals_map.find(minstr.args[0]);
        if (global == impl_->globals_map.end()) {
          std::cerr << "[NativeCodegen] Error: Undefined global '"
                    << minstr.args[0] << "'\n";
          return false;
        }
        const auto &type = impl_->global_nil_types.at(minstr.args[0]);
        if (!type.empty()) {
          auto *initialized = impl_->builder->CreateLoad(
              llvm::Type::getInt1Ty(ctx),
              impl_->global_initialized.at(minstr.args[0]));
          auto *failure = llvm::BasicBlock::Create(ctx, "global.init.failure",
                                                   impl_->current_function);
          auto *success = llvm::BasicBlock::Create(ctx, "global.init.success",
                                                   impl_->current_function);
          impl_->builder->CreateCondBr(initialized, success, failure);
          impl_->builder->SetInsertPoint(failure);
          auto *message = impl_->builder->CreateGlobalString(
              "NIL native runtime error: global '" + minstr.args[0] +
              "' read before initialization\n");
          impl_->builder->CreateCall(get_runtime_failure_function(), {message});
          impl_->builder->CreateUnreachable();
          impl_->builder->SetInsertPoint(success);
        }
        impl_->pushValue(impl_->builder->CreateLoad(
            global->second->getValueType(), global->second, minstr.args[0]), type);
        break;
      }

      case Opcode::LABEL: {
        if (!minstr.args.empty()) {
          std::string label_name = minstr.args[0];
          auto bb = impl_->label_map[label_name];
          if (!bb) {
            std::cerr << "[NativeCodegen] Error: Label '" << label_name
                      << "' not found in map!\n";
          } else {
            // A synthetic block after RET is not a real predecessor. Giving
            // it a fallthrough edge would fabricate an empty incoming stack.
            if (!impl_->builder->GetInsertBlock()->getTerminator()) {
              if (preceding_unreachable) {
                impl_->builder->CreateUnreachable();
              } else {
                if (!record_stack_edge(bb, impl_->builder->GetInsertBlock()))
                  return false;
                impl_->builder->CreateBr(bb);
              }
            }
            impl_->builder->SetInsertPoint(bb);
            auto &incoming = incoming_stacks[bb];
            // A backwards-layout block with no known predecessor may only
            // establish an empty stack. A later nonempty edge rejects rather
            // than changing the meaning of instructions already lowered.
            incoming.established = true;
            impl_->value_stack.assign(incoming.values.begin(),
                                       incoming.values.end());
          }
        }
        break;
      }

      case Opcode::JMP: {
        if (!minstr.args.empty()) {
          std::string label_name = minstr.args[0];
          auto bb = impl_->label_map[label_name];
          if (bb) {
            if (!record_stack_edge(bb, impl_->builder->GetInsertBlock()))
              return false;
            impl_->builder->CreateBr(bb);
            skip_until_label = true;
            impl_->value_stack.clear();
          } else {
            std::cerr << "[NativeCodegen] Error: JMP target '" << label_name
                      << "' not found!\n";
            return false;
          }
        }
        break;
      }

      case Opcode::JMP_IF_NOT: {
        if (!minstr.args.empty()) {
          std::string label_name = minstr.args[0];
          auto target_bb = impl_->label_map[label_name];
          auto cond = impl_->popValue();

          if (!target_bb) {
            std::cerr << "[NativeCodegen] Error: JMP_IF_NOT target '"
                      << label_name << "' not found!\n";
            return false;
          }
          if (!cond) {
            std::cerr
                << "[NativeCodegen] Error: Stack underflow at JMP_IF_NOT\n";
            return false;
          }

          // Fallthrough block
          auto fallthrough_bb = llvm::BasicBlock::Create(
              ctx, "fallthrough", impl_->current_function);

          cond = impl_->truthy(cond);
          if (!cond) {
            std::cerr << "[NativeCodegen] condition is not truth-testable in "
                         "function '"
                      << fn.name << "'\n";
            return false;
          }

          if (!record_stack_edge(target_bb, impl_->builder->GetInsertBlock()))
            return false;
          impl_->builder->CreateCondBr(cond, fallthrough_bb, target_bb);
          impl_->builder->SetInsertPoint(fallthrough_bb);
        }
        break;
      }

      case Opcode::JMP_IF: {
        if (minstr.args.empty())
          break;
        const std::string &label_name = minstr.args[0];
        auto target_bb = impl_->label_map[label_name];
        auto cond = impl_->popValue();
        if (!target_bb || !cond) {
          std::cerr << "[NativeCodegen] invalid JMP_IF to '" << label_name
                    << "' in function '" << fn.name << "'\n";
          return false;
        }
        cond = impl_->truthy(cond);
        if (!cond) {
          std::cerr << "[NativeCodegen] condition is not truth-testable in "
                       "function '"
                    << fn.name << "'\n";
          return false;
        }
        auto fallthrough_bb = llvm::BasicBlock::Create(ctx, "fallthrough",
                                                       impl_->current_function);
        if (!record_stack_edge(target_bb, impl_->builder->GetInsertBlock()))
          return false;
        impl_->builder->CreateCondBr(cond, target_bb, fallthrough_bb);
        impl_->builder->SetInsertPoint(fallthrough_bb);
        break;
      }

      case Opcode::NOT: {
        auto *value = impl_->popValue();
        auto *condition = impl_->truthy(value);
        if (!condition) {
          std::cerr << "[NativeCodegen] NOT operand is not truth-testable in "
                       "function '"
                    << fn.name << "'\n";
          return false;
        }
        impl_->pushValue(impl_->builder->CreateNot(condition), "bool");
        break;
      }
      case Opcode::CMP_EQ:
      case Opcode::CMP_NE:
      case Opcode::CMP_LT:
      case Opcode::CMP_LE:
      case Opcode::CMP_GT:
      case Opcode::CMP_GE: {
        auto *rhs = impl_->popValue();
        auto *lhs = impl_->popValue();
        if (!lhs || !rhs)
          break;

        const bool lhs_text =
            native_handle_kind_for_nil_type(impl_->nilType(lhs)) ==
            native_handle_kind::text;
        const bool rhs_text =
            native_handle_kind_for_nil_type(impl_->nilType(rhs)) ==
            native_handle_kind::text;
        bool raw_pointer_comparison = false;
        if (lhs_text || rhs_text) {
          if (!lhs_text || !rhs_text || !lhs->getType()->isPointerTy() ||
              !rhs->getType()->isPointerTy()) {
            std::cerr << "[NativeCodegen] " << minstr.opname
                      << " cannot compare text with a non-text value in "
                         "function '"
                      << fn.name << "'\n";
            return false;
          }
          // Native text is NUL-free. strcmp therefore matches the VM's
          // unsigned-byte lexicographic string order. Only its sign matters;
          // neither pointer addresses nor a presumed -1/+1 result define it.
          auto *strcmp_type = llvm::FunctionType::get(
              llvm::Type::getInt32Ty(ctx),
              {llvm::PointerType::getUnqual(ctx),
               llvm::PointerType::getUnqual(ctx)}, false);
          auto strcmp_fn =
              impl_->module->getOrInsertFunction("strcmp", strcmp_type);
          lhs = impl_->builder->CreateCall(
              strcmp_type, strcmp_fn.getCallee(), {lhs, rhs});
          rhs = llvm::ConstantInt::get(ctx, llvm::APInt(32, 0));
        } else {
          raw_pointer_comparison =
              native_values_are_matching_raw_pointers(lhs, rhs);
          if (reject_native_comparison_operands(lhs, rhs, minstr.opname,
                                                fn.name, minstr.debug_line)) {
            return false;
          }
          impl_->ensureSymmetricTypes(lhs, rhs);
        }

        llvm::CmpInst::Predicate integer_predicate = llvm::CmpInst::ICMP_EQ;
        llvm::CmpInst::Predicate floating_predicate = llvm::CmpInst::FCMP_OEQ;
        switch (op) {
        case Opcode::CMP_EQ:
          break;
        case Opcode::CMP_NE:
          integer_predicate = llvm::CmpInst::ICMP_NE;
          floating_predicate = llvm::CmpInst::FCMP_UNE;
          break;
        case Opcode::CMP_LT:
          integer_predicate = raw_pointer_comparison ? llvm::CmpInst::ICMP_ULT
                                                     : llvm::CmpInst::ICMP_SLT;
          floating_predicate = llvm::CmpInst::FCMP_OLT;
          break;
        case Opcode::CMP_LE:
          integer_predicate = raw_pointer_comparison ? llvm::CmpInst::ICMP_ULE
                                                     : llvm::CmpInst::ICMP_SLE;
          floating_predicate = llvm::CmpInst::FCMP_OLE;
          break;
        case Opcode::CMP_GT:
          integer_predicate = raw_pointer_comparison ? llvm::CmpInst::ICMP_UGT
                                                     : llvm::CmpInst::ICMP_SGT;
          floating_predicate = llvm::CmpInst::FCMP_OGT;
          break;
        case Opcode::CMP_GE:
          integer_predicate = raw_pointer_comparison ? llvm::CmpInst::ICMP_UGE
                                                     : llvm::CmpInst::ICMP_SGE;
          floating_predicate = llvm::CmpInst::FCMP_OGE;
          break;
        default:
          break;
        }
        impl_->pushValue(lhs->getType()->isFloatingPointTy()
            ? impl_->builder->CreateFCmp(floating_predicate, lhs, rhs)
            : impl_->builder->CreateICmp(integer_predicate, lhs, rhs), "bool");
        break;
      }

      case Opcode::NEW_ARRAY: {
        if (minstr.args.size() != 1 || minstr.args[0].empty()) {
          std::cerr << "[NativeCodegen] NEW_ARRAY in function '" << fn.name
                    << "' requires one exact non-negative size operand\n";
          return false;
        }
        std::uint64_t size = 0;
        try {
          std::size_t consumed = 0;
          size = std::stoull(minstr.args[0], &consumed, 10);
          if (consumed != minstr.args[0].size() ||
              (!minstr.args[0].empty() && minstr.args[0].front() == '-')) {
            throw std::invalid_argument("array size");
          }
        } catch (const std::exception &) {
          std::cerr << "[NativeCodegen] NEW_ARRAY size '" << minstr.args[0]
                    << "' is not a valid non-negative integer in function '"
                    << fn.name << "'\n";
          return false;
        }
        const std::uint64_t maximum_count = native_size_max / 8U - 1U;
        if (size > maximum_count ||
            size > static_cast<std::uint64_t>(
                       std::numeric_limits<std::int64_t>::max())) {
          std::cerr << "[NativeCodegen] NEW_ARRAY size does not fit the "
                       "target allocation ABI in function '"
                    << fn.name << "'\n";
          return false;
        }
        auto *size_val =
            llvm::ConstantInt::get(ctx, llvm::APInt(64, size, false));

        auto runtime_type = llvm::FunctionType::get(
            llvm::PointerType::getUnqual(ctx),
            {llvm::Type::getInt32Ty(ctx), llvm::Type::getInt64Ty(ctx)}, false);
        auto runtime =
            impl_->module->getOrInsertFunction("nyrt_array_new", runtime_type);
        auto *raw_ptr = impl_->builder->CreateCall(
            runtime_type, runtime.getCallee(),
            {llvm::ConstantInt::get(ctx,
                                    llvm::APInt(32, NYRT_ARRAY_I64, false)),
             size_val});
        emit_allocation_null_check(raw_ptr, "NEW_ARRAY");
        impl_->pushValue(raw_ptr, "array<i64>");
        break;
      }

      case Opcode::NEW_ARRAY_LITERAL: {
        if (minstr.args.size() < 2 || minstr.args[1].empty() ||
            normalize_nil_type(minstr.args[1]) == "any" ||
            normalize_nil_type(minstr.args[1]) == "$integer") {
          std::cerr << "[NativeCodegen] NEW_ARRAY_LITERAL in function '"
                    << fn.name
                    << "' requires exact contextual element metadata "
                       "(i32, i64, bool, u8, f64, text, or tuple); got '"
                    << (minstr.args.size() < 2 ? std::string("<missing>")
                                               : minstr.args[1])
                    << "'\n";
          return false;
        }
        const std::string element_type = canonical_array_type(minstr.args[1]);
        if (!native_array_kind_for_element(element_type)) {
          if (element_type == "f32" || element_type == "f64") {
            std::cerr << "[NativeCodegen] floating-point array elements are "
                         "not implemented for NEW_ARRAY_LITERAL in function '"
                      << fn.name << "'\n";
          } else if (element_type == "text" || element_type == "string" ||
                     element_type == "str" ||
                     is_pointer_like_nil_type(element_type)) {
            std::cerr << "[NativeCodegen] pointer-valued array elements are "
                         "not implemented for NEW_ARRAY_LITERAL in function '"
                      << fn.name << "'\n";
          } else {
            std::cerr << "[NativeCodegen] array element type '" << element_type
                      << "' has no supported native scalar layout in function '"
                      << fn.name << "'\n";
          }
          return false;
        }
        std::int64_t count = 0;
        try {
          std::size_t consumed = 0;
          count = std::stoll(minstr.args[0], &consumed, 10);
          if (consumed != minstr.args[0].size() || count < 0)
            throw std::invalid_argument("array literal count");
        } catch (const std::exception &) {
          std::cerr << "[NativeCodegen] NEW_ARRAY_LITERAL size does not fit "
                       "the target allocation ABI in function '"
                    << fn.name << "'\n";
          return false;
        }

        const auto kind = native_array_kind_for_element(element_type);
        auto *size_val = llvm::ConstantInt::get(
            ctx, llvm::APInt(64, static_cast<std::uint64_t>(count), false));
        auto runtime_new_type = llvm::FunctionType::get(
            llvm::PointerType::getUnqual(ctx),
            {llvm::Type::getInt32Ty(ctx), llvm::Type::getInt64Ty(ctx)}, false);
        auto runtime_new = impl_->module->getOrInsertFunction("nyrt_array_new",
                                                              runtime_new_type);
        llvm::Value *raw_ptr = nullptr;
        if (*kind == NYRT_ARRAY_ANYTHING && count > 0) {
          if (impl_->value_stack.size() < static_cast<std::size_t>(count)) {
            std::cerr << "[NativeCodegen] NEW_ARRAY_LITERAL requires all initialized elements\n";
            return false;
          }
          auto *initial = native_storage_word(impl_->value_stack.back(), element_type,
                                               "NEW_ARRAY_LITERAL");
          if (!initial) return false;
          auto constructor = impl_->module->getOrInsertFunction("nyrt_array_new_with_value",
              llvm::FunctionType::get(llvm::PointerType::getUnqual(ctx),
                  {llvm::Type::getInt32Ty(ctx), llvm::Type::getInt64Ty(ctx),
                   llvm::Type::getInt64Ty(ctx)}, false));
          raw_ptr = impl_->builder->CreateCall(constructor,
              {llvm::ConstantInt::get(ctx, llvm::APInt(32, *kind, false)), size_val, initial});
        } else {
          raw_ptr = impl_->builder->CreateCall(
              runtime_new_type, runtime_new.getCallee(),
              {llvm::ConstantInt::get(ctx, llvm::APInt(32, *kind, false)), size_val});
        }
        emit_allocation_null_check(raw_ptr, "NEW_ARRAY_LITERAL");

        auto runtime_set_type = llvm::FunctionType::get(
            llvm::Type::getInt32Ty(ctx),
            {llvm::PointerType::getUnqual(ctx), llvm::Type::getInt32Ty(ctx),
             llvm::Type::getInt64Ty(ctx), llvm::Type::getInt64Ty(ctx)},
            false);
        auto runtime_set = impl_->module->getOrInsertFunction("nyrt_array_set",
                                                              runtime_set_type);

        // Pop elements in reverse order and store
        for (std::int64_t i = count - 1; i >= 0; --i) {
          auto *val = impl_->popValue();
          if (!val) {
            std::cerr << "[NativeCodegen] NEW_ARRAY_LITERAL in function '"
                      << fn.name << "' encodes " << count
                      << " element(s), but the NIL operand stack underflowed "
                         "while collecting them";
            if (minstr.debug_line > 0)
              std::cerr << " at Ny source line " << minstr.debug_line;
            std::cerr << '\n';
            return false;
          }

          require_initialized_struct(val, "NEW_ARRAY_LITERAL");
          llvm::Value *val_to_store =
              native_storage_word(val, element_type, "NEW_ARRAY_LITERAL");
          if (!val_to_store)
            return false;

          impl_->builder->CreateCall(
              runtime_set_type, runtime_set.getCallee(),
              {raw_ptr,
               llvm::ConstantInt::get(ctx, llvm::APInt(32, *kind, false)),
               llvm::ConstantInt::get(
                   ctx, llvm::APInt(64, static_cast<std::uint64_t>(i), false)),
               val_to_store});
        }

        impl_->pushValue(raw_ptr, "array<" + element_type + ">");
        break;
      }

      case Opcode::GET_INDEX: {
        auto index_val = impl_->popValue();
        auto arr_ptr = impl_->popValue();
        if (!arr_ptr || !native_value_is_numeric_scalar(index_val) ||
            !arr_ptr->getType()->isPointerTy() ||
            !index_val->getType()->isIntegerTy()) {
          std::cerr << "[NativeCodegen] GET_INDEX requires a typed handle and "
                       "integer index in function '"
                    << fn.name << "'\n";
          return false;
        }
        const std::string handle_type = impl_->nilType(arr_ptr);
        const auto handle_kind = native_handle_kind_for_nil_type(handle_type);
        if ((scalar_array_method && handle_kind != native_handle_kind::scalar_array) ||
            (handle_kind != native_handle_kind::scalar_array &&
             handle_kind != native_handle_kind::integer_pointer)) {
          std::cerr << "[NativeCodegen] GET_INDEX cannot index native handle '"
                    << handle_type << "' in function '" << fn.name << "'\n";
          return false;
        }
        index_val = impl_->coerceToI64(index_val);

        const std::optional<std::string> element_type = native_generic_argument(
            handle_type,
            handle_kind == native_handle_kind::scalar_array ? "array" : "ptr");
        const auto kind = element_type
                              ? native_array_kind_for_element(*element_type)
                              : std::nullopt;
        if (!kind) {
          std::cerr << "[NativeCodegen] GET_INDEX has no exact element kind "
                       "in function '"
                    << fn.name << "'\n";
          return false;
        }

        llvm::Value *val = nullptr;
        if (handle_kind == native_handle_kind::scalar_array) {
          auto runtime_type = llvm::FunctionType::get(
              llvm::Type::getInt64Ty(ctx),
              {llvm::PointerType::getUnqual(ctx), llvm::Type::getInt32Ty(ctx),
               llvm::Type::getInt64Ty(ctx)},
              false);
          auto runtime = impl_->module->getOrInsertFunction("nyrt_array_get",
                                                            runtime_type);
          val = impl_->builder->CreateCall(
              runtime_type, runtime.getCallee(),
              {arr_ptr,
               llvm::ConstantInt::get(ctx, llvm::APInt(32, *kind, false)),
               index_val});
        } else if (impl_->memory_mode == MemoryMode::SAFE_GC) {
          auto *runtime_type = llvm::FunctionType::get(
              llvm::Type::getInt64Ty(ctx),
              {llvm::PointerType::getUnqual(ctx), llvm::Type::getInt32Ty(ctx),
               llvm::Type::getInt64Ty(ctx)},
              false);
          auto runtime = impl_->module->getOrInsertFunction("nyrt_buffer_get",
                                                            runtime_type);
          val = impl_->builder->CreateCall(
              runtime_type, runtime.getCallee(),
              {arr_ptr,
               llvm::ConstantInt::get(ctx, llvm::APInt(32, *kind, false)),
               index_val});
        } else {
          auto *storage_type = native_pointer_storage_type(*element_type);
          auto *element_pointer = impl_->builder->CreateGEP(
              storage_type, arr_ptr, {index_val}, "ny.pointer.element");
          auto *loaded = impl_->builder->CreateLoad(
              storage_type, element_pointer, "ny.pointer.value");
          if (storage_type->isIntegerTy(64)) {
            val = loaded;
          } else if (normalize_nil_type(*element_type) == "i32") {
            val = impl_->builder->CreateSExt(
                loaded, llvm::Type::getInt64Ty(ctx), "ny.pointer.i32.word");
          } else {
            val = impl_->builder->CreateZExt(
                loaded, llvm::Type::getInt64Ty(ctx), "ny.pointer.byte.word");
          }
        }
        if (handle_kind == native_handle_kind::scalar_array)
          val = native_array_loaded_value(val, *element_type);
        if (*element_type == "bool") {
          val = native_storage_word(val, "bool", "GET_INDEX");
          if (!val) return false;
        }
        impl_->pushValue(val, element_type.value_or("i64"));
        break;
      }

      case Opcode::SET_INDEX: {
        // NIL codegen pushes operands as array, index, value.

        auto val = impl_->popValue();
        auto index_val = impl_->popValue();
        auto arr_ptr = impl_->popValue();

        if (!arr_ptr || !native_value_is_numeric_scalar(index_val) ||
            !val ||
            !arr_ptr->getType()->isPointerTy() ||
            !index_val->getType()->isIntegerTy()) {
          std::cerr << "[NativeCodegen] SET_INDEX requires a typed handle, "
                       "integer index, and value in function '"
                    << fn.name << "'\n";
          return false;
        }

        const std::string handle_type = impl_->nilType(arr_ptr);
        const auto handle_kind = native_handle_kind_for_nil_type(handle_type);
        if ((scalar_array_method && handle_kind != native_handle_kind::scalar_array) ||
            (handle_kind != native_handle_kind::scalar_array &&
             handle_kind != native_handle_kind::integer_pointer)) {
          std::cerr << "[NativeCodegen] SET_INDEX cannot index native handle '"
                    << handle_type << "' in function '" << fn.name << "'\n";
          return false;
        }

        index_val = impl_->coerceToI64(index_val);
        const std::optional<std::string> element_type = native_generic_argument(
            handle_type,
            handle_kind == native_handle_kind::scalar_array ? "array" : "ptr");
        const auto kind = element_type
                              ? native_array_kind_for_element(*element_type)
                              : std::nullopt;
        if (!kind) {
          std::cerr << "[NativeCodegen] SET_INDEX has no exact element kind "
                       "in function '"
                    << fn.name << "'\n";
          return false;
        }
        require_initialized_struct(val, "SET_INDEX");
        llvm::Value *val_to_store =
            native_storage_word(val, *element_type, "SET_INDEX");
        if (!val_to_store) {
          std::cerr << "[NativeCodegen] SET_INDEX cannot lower its value in "
                       "function '"
                    << fn.name << "'\n";
          return false;
        }
        if (handle_kind == native_handle_kind::scalar_array) {
          auto runtime_type = llvm::FunctionType::get(
              llvm::Type::getInt32Ty(ctx),
              {llvm::PointerType::getUnqual(ctx), llvm::Type::getInt32Ty(ctx),
               llvm::Type::getInt64Ty(ctx), llvm::Type::getInt64Ty(ctx)},
              false);
          auto runtime = impl_->module->getOrInsertFunction("nyrt_array_set",
                                                            runtime_type);
          impl_->builder->CreateCall(
              runtime_type, runtime.getCallee(),
              {arr_ptr,
               llvm::ConstantInt::get(ctx, llvm::APInt(32, *kind, false)),
               index_val, val_to_store});
        } else if (impl_->memory_mode == MemoryMode::SAFE_GC) {
          auto *runtime_type = llvm::FunctionType::get(
              llvm::Type::getInt32Ty(ctx),
              {llvm::PointerType::getUnqual(ctx), llvm::Type::getInt32Ty(ctx),
               llvm::Type::getInt64Ty(ctx), llvm::Type::getInt64Ty(ctx)},
              false);
          auto runtime = impl_->module->getOrInsertFunction("nyrt_buffer_set",
                                                            runtime_type);
          impl_->builder->CreateCall(
              runtime_type, runtime.getCallee(),
              {arr_ptr,
               llvm::ConstantInt::get(ctx, llvm::APInt(32, *kind, false)),
               index_val, val_to_store});
        } else {
          auto *storage_type = native_pointer_storage_type(*element_type);
          auto *element_pointer = impl_->builder->CreateGEP(
              storage_type, arr_ptr, {index_val}, "ny.pointer.element");
          llvm::Value *stored = val_to_store;
          if (storage_type != llvm::Type::getInt64Ty(ctx)) {
            stored = impl_->builder->CreateTrunc(val_to_store, storage_type,
                                                 "ny.pointer.stored");
          }
          impl_->builder->CreateStore(stored, element_pointer);
        }
        break;
      }

      case Opcode::LEN: {
        auto arr_ptr = impl_->popValue();
        if (!arr_ptr || !arr_ptr->getType()->isPointerTy() ||
            native_handle_kind_for_nil_type(impl_->nilType(arr_ptr)) !=
                native_handle_kind::scalar_array) {
          std::cerr << "[NativeCodegen] LEN requires a typed array handle in "
                       "function '"
                    << fn.name << "'\n";
          return false;
        }

        auto *len = array_length_value(arr_ptr);
        if (!len) {
          std::cerr << "[NativeCodegen] LEN has no exact array element kind in "
                       "function '"
                    << fn.name << "'\n";
          return false;
        }
        impl_->pushValue(len, "i64");
        break;
      }

      case Opcode::TO_STRING: {
        auto *value = impl_->popValue();
        const auto actual = value ? scalar_text_type(impl_->nilType(value)) : "";
        // Legacy dynamic conversion sees booleans as integer VM values. Only
        // an explicit bool operand opts into the new true/false spelling.
        const auto type = minstr.args.empty() ? (actual == "bool" ? "i64" : actual)
                                             : scalar_text_type(minstr.args[0]);
        const auto integer_type = [](const std::string &name) {
          return name == "i32" || name == "i64" || name == "u8" || name == "bool";
        };
        if (!value || minstr.args.size() > 1 || !supports_scalar_text(type) ||
            !supports_scalar_text(actual) ||
            !(actual == type || (integer_type(actual) && integer_type(type)))) {
          std::cerr << "[NativeCodegen] TO_STRING requires matching scalar source metadata in function '"
                    << fn.name << "'\n";
          return false;
        }
        if (type == "text") {
          if (!value->getType()->isPointerTy()) return false;
          impl_->pushValue(value, "text");
          break;
        }
        llvm::Type *argument_type = nullptr;
        const char *symbol = nullptr;
        if (type == "f64" && value->getType()->isDoubleTy()) {
          argument_type = llvm::Type::getDoubleTy(ctx);
          symbol = "nyrt_text_f64";
        } else if (integer_type(type) && value->getType()->isIntegerTy()) {
          argument_type = llvm::Type::getInt64Ty(ctx);
          value = impl_->coerceToI64(value);
          symbol = type == "bool" ? "nyrt_text_bool" : "nyrt_text_i64";
        } else {
          std::cerr << "[NativeCodegen] TO_STRING scalar storage disagrees with metadata\n";
          return false;
        }
        auto *signature = llvm::FunctionType::get(llvm::PointerType::getUnqual(ctx),
                                                  {argument_type}, false);
        auto runtime = impl_->module->getOrInsertFunction(symbol, signature);
        impl_->pushValue(impl_->builder->CreateCall(signature, runtime.getCallee(), {value}), "text");
        break;
      }

      case Opcode::STR_CONCAT: {
        auto *rhs = impl_->popValue();
        auto *lhs = impl_->popValue();
        if (!lhs || !rhs || !lhs->getType()->isPointerTy() ||
            !rhs->getType()->isPointerTy() ||
            native_handle_kind_for_nil_type(impl_->nilType(lhs)) !=
                native_handle_kind::text ||
            native_handle_kind_for_nil_type(impl_->nilType(rhs)) !=
                native_handle_kind::text) {
          std::cerr << "[NativeCodegen] STR_CONCAT requires two text operands "
                       "in function '"
                    << fn.name << "'\n";
          return false;
        }
        auto *concat = impl_->module->getFunction("__ny_string_concat");
        if (!concat) {
          std::cerr << "[NativeCodegen] internal string concat helper is "
                       "missing in function '"
                    << fn.name << "'\n";
          return false;
        }
        impl_->pushValue(impl_->builder->CreateCall(concat, {lhs, rhs}),
                         "text");
        break;
      }

      case Opcode::STRLEN: {
        auto str_ptr = impl_->popValue();
        if (!str_ptr || !str_ptr->getType()->isPointerTy() ||
            native_handle_kind_for_nil_type(impl_->nilType(str_ptr)) !=
                native_handle_kind::text) {
          std::cerr << "[NativeCodegen] STRLEN requires a typed text handle in "
                       "function '"
                    << fn.name << "'\n";
          return false;
        }

        auto *strlen_fn = impl_->module->getFunction("strlen");
        if (!strlen_fn) {
          auto *ft = llvm::FunctionType::get(
              native_size_type, {llvm::PointerType::getUnqual(ctx)}, false);
          strlen_fn = llvm::Function::Create(
              ft, llvm::Function::ExternalLinkage, "strlen", *impl_->module);
        }

        llvm::Value *len = impl_->builder->CreateCall(strlen_fn, {str_ptr});
        if (len->getType() != llvm::Type::getInt64Ty(ctx))
          len = impl_->builder->CreateZExtOrTrunc(
              len, llvm::Type::getInt64Ty(ctx), "ny.strlen");
        impl_->pushValue(len, "i64");
        break;
      }

      default:
        std::cerr << "[NativeCodegen] internal lowering gap for NIL opcode '"
                  << minstr.opname << "' in function '" << fn.name << "'\n";
        return false;
      }
    }

    // A missing terminator in reachable NIL is malformed, not an implicit
    // scalar zero return. Only synthetic/dead blocks may receive unreachable.
    std::unordered_set<llvm::BasicBlock *> reachable_blocks;
    std::vector<llvm::BasicBlock *> pending_blocks = {entry};
    while (!pending_blocks.empty()) {
      auto *block = pending_blocks.back();
      pending_blocks.pop_back();
      if (!reachable_blocks.insert(block).second || !block->getTerminator())
        continue;
      for (auto *successor : llvm::successors(block))
        pending_blocks.push_back(successor);
    }
    for (auto &bb : *impl_->current_function) {
      if (!bb.getTerminator()) {
        if (reachable_blocks.count(&bb) != 0) {
          std::cerr << "[NativeCodegen] reachable NIL falls through without "
                       "RET in function '"
                    << fn.name << "'\n";
          return false;
        }
        impl_->builder->SetInsertPoint(&bb);
        impl_->builder->CreateUnreachable();
      }
    }

    // Resolve construction identity after all PHI incoming edges exist. A
    // dominated single allocation can remain under construction across an
    // expression diamond. Other merges require complete incoming objects;
    // previous loop iterations must not alias the current construction proof.
    if (!native_struct_initializations.empty()) {
      struct ConstructionOrigins {
        std::unordered_set<const llvm::Value *> allocations;
        bool external = false;
      };
      std::unordered_map<const llvm::Value *, ConstructionOrigins> origins;
      std::vector<const llvm::Instruction *> identity_values;
      for (auto &block : *impl_->current_function) {
        for (auto &instruction : block) {
          if (instruction.getType()->isPointerTy() &&
              (llvm::isa<llvm::PHINode>(instruction) ||
               llvm::isa<llvm::FreezeInst>(instruction) ||
               llvm::isa<llvm::BitCastInst>(instruction))) {
            identity_values.push_back(&instruction);
            origins.emplace(&instruction, ConstructionOrigins{});
          }
        }
      }
      const auto value_origins = [&](const llvm::Value *value) {
        if (native_struct_initializations.count(value))
          return ConstructionOrigins{{value}, false};
        const auto found = origins.find(value);
        return found == origins.end() ? ConstructionOrigins{{}, true}
                                      : found->second;
      };
      bool origins_changed = true;
      while (origins_changed) {
        origins_changed = false;
        for (const auto *value : identity_values) {
          auto &result = origins.at(value);
          const std::size_t previous_size = result.allocations.size();
          const bool previous_external = result.external;
          for (const auto &operand : value->operands()) {
            const auto incoming = value_origins(operand.get());
            result.allocations.insert(incoming.allocations.begin(), incoming.allocations.end());
            result.external = result.external || incoming.external;
          }
          origins_changed = origins_changed || previous_size != result.allocations.size() ||
                            previous_external != result.external;
        }
      }
      llvm::DominatorTree dominators(*impl_->current_function);
      const auto construction_identity = [&](const llvm::Value *value)
          -> const llvm::Value * {
        if (native_struct_initializations.count(value))
          return value;
        const auto found = origins.find(value);
        if (found == origins.end() || found->second.external ||
            found->second.allocations.size() != 1)
          return nullptr;
        const auto *allocation = *found->second.allocations.begin();
        const auto *instruction = llvm::dyn_cast<llvm::Instruction>(value);
        return instruction && dominators.dominates(
                                  llvm::cast<llvm::Instruction>(allocation), instruction)
                   ? allocation : nullptr;
      };
      for (const auto *value : identity_values) {
        const auto *phi = llvm::dyn_cast<llvm::PHINode>(value);
        if (!phi || construction_identity(phi))
          continue;
        for (unsigned index = 0; index < phi->getNumIncomingValues(); ++index) {
          struct_events[phi->getIncomingBlock(index)].push_back(
              {StructInitializationAction::use, phi->getIncomingValue(index),
               {}, "aggregate PHI merge"});
        }
      }

      std::unordered_map<const llvm::Value *, std::size_t> field_offsets;
      std::size_t field_count = 0;
      for (const auto &[allocation, initialization] : native_struct_initializations) {
        field_offsets.emplace(allocation, field_count);
        field_count += initialization.definition->fields.size();
      }
      using initialized_fields = std::vector<bool>;
      const auto apply_struct_event = [&](initialized_fields &state,
                                           const StructInitializationEvent &event) {
        const auto *allocation = construction_identity(event.value);
        if (!allocation || event.action == StructInitializationAction::use)
          return;
        const auto *definition = native_struct_initializations.at(allocation).definition;
        const auto &layout = native_struct_layouts.at(definition);
        const auto offset = field_offsets.at(allocation);
        if (event.action == StructInitializationAction::allocate) {
          for (std::size_t field = 0; field < definition->fields.size(); ++field)
            state[offset + field] = false;
        } else {
          state[offset + layout.field_indices.at(event.field) - layout.hidden_fields] = true;
        }
      };
      std::unordered_map<llvm::BasicBlock *, initialized_fields> fields_in, fields_out;
      for (auto *block : reachable_blocks) {
        fields_in[block] = initialized_fields(field_count, block != entry);
        fields_out[block] = initialized_fields(field_count, block != entry);
      }
      bool fields_changed = true;
      while (fields_changed) {
        fields_changed = false;
        for (auto &block_ref : *impl_->current_function) {
          auto *block = &block_ref;
          if (!reachable_blocks.count(block))
            continue;
          initialized_fields incoming(field_count, block != entry);
          if (block != entry) {
            for (auto *predecessor : llvm::predecessors(block)) {
              if (!reachable_blocks.count(predecessor))
                continue;
              for (std::size_t field = 0; field < field_count; ++field)
                incoming[field] = incoming[field] && fields_out.at(predecessor)[field];
            }
          }
          auto outgoing = incoming;
          for (const auto &event : struct_events[block])
            apply_struct_event(outgoing, event);
          if (fields_in.at(block) != incoming || fields_out.at(block) != outgoing) {
            fields_in[block] = std::move(incoming);
            fields_out[block] = std::move(outgoing);
            fields_changed = true;
          }
        }
      }
      for (auto *block : reachable_blocks) {
        auto initialized = fields_in.at(block);
        for (const auto &event : struct_events[block]) {
          const auto *allocation = construction_identity(event.value);
          if (event.action == StructInitializationAction::use && allocation) {
            const auto *definition = native_struct_initializations.at(allocation).definition;
            const auto offset = field_offsets.at(allocation);
            for (std::size_t field = 0; field < definition->fields.size(); ++field) {
              if (!initialized[offset + field]) {
                std::cerr << "[NativeCodegen] NEW_STRUCT '" << definition->name
                          << "' in function '" << fn.name << "' leaves required field '"
                          << definition->fields[field].first
                          << "' uninitialized on an incoming path before "
                          << event.operation << '\n';
                return false;
              }
            }
          }
          apply_struct_event(initialized, event);
        }
      }
    }

    // An alloca's existence is not proof that a local was assigned along the
    // path reaching a load. Intersect initialized locals across CFG edges;
    // this rejects conditional first assignments and first-iteration loop
    // reads instead of emitting loads from uninitialized native stack memory.
    using initialized_locals = std::unordered_set<llvm::AllocaInst *>;
    initialized_locals all_locals;
    for (const auto &local : impl_->locals_map)
      all_locals.insert(local.second);
    std::unordered_map<llvm::BasicBlock *, initialized_locals> locals_in;
    std::unordered_map<llvm::BasicBlock *, initialized_locals> locals_out;
    std::deque<llvm::BasicBlock *> local_worklist;
    std::unordered_set<llvm::BasicBlock *> queued_blocks;
    for (auto *block : reachable_blocks) {
      locals_out[block] = all_locals;
      local_worklist.push_back(block);
      queued_blocks.insert(block);
    }
    while (!local_worklist.empty()) {
      auto *block = local_worklist.front();
      local_worklist.pop_front();
      queued_blocks.erase(block);
      initialized_locals incoming = block == entry ? initialized_locals{}
                                                  : all_locals;
      if (block != entry) {
        for (auto *predecessor : llvm::predecessors(block)) {
          if (reachable_blocks.count(predecessor) == 0)
            continue;
          const auto &previous = locals_out.at(predecessor);
          for (auto local = incoming.begin(); local != incoming.end();) {
            if (previous.count(*local) == 0)
              local = incoming.erase(local);
            else
              ++local;
          }
        }
      }
      locals_in[block] = incoming;
      auto outgoing = incoming;
      for (auto &instruction : *block) {
        auto *store = llvm::dyn_cast<llvm::StoreInst>(&instruction);
        auto *local = store ? llvm::dyn_cast<llvm::AllocaInst>(
                                  store->getPointerOperand())
                            : nullptr;
        if (local && all_locals.count(local) != 0)
          outgoing.insert(local);
      }
      if (outgoing == locals_out.at(block))
        continue;
      locals_out[block] = std::move(outgoing);
      for (auto *successor : llvm::successors(block)) {
        if (queued_blocks.insert(successor).second)
          local_worklist.push_back(successor);
      }
    }
    for (auto *block : reachable_blocks) {
      auto initialized = locals_in.at(block);
      for (auto &instruction : *block) {
        if (auto *store = llvm::dyn_cast<llvm::StoreInst>(&instruction)) {
          auto *local =
              llvm::dyn_cast<llvm::AllocaInst>(store->getPointerOperand());
          if (local && all_locals.count(local) != 0)
            initialized.insert(local);
        } else if (auto *load = llvm::dyn_cast<llvm::LoadInst>(&instruction)) {
          auto *local =
              llvm::dyn_cast<llvm::AllocaInst>(load->getPointerOperand());
          if (local && all_locals.count(local) != 0 &&
              initialized.count(local) == 0) {
            std::cerr << "[NativeCodegen] local '" << local->getName().str()
                      << "' is not initialized on every incoming path in "
                         "function '" << fn.name << "'\n";
            return false;
          }
        }
      }
    }

    // Never pass malformed IR into optimization or execution. LLVM's later
    // stages may abort the process when handed an invalid function, whereas a
    // verifier diagnostic lets nilcompiler fail normally.
    std::string function_error;
    llvm::raw_string_ostream function_error_stream(function_error);
    if (llvm::verifyFunction(*impl_->current_function,
                             &function_error_stream)) {
      function_error_stream.flush();
      std::cerr << "[NativeCodegen] function verify failure in '" << fn.name
                << "': " << function_error << std::endl;
      return false;
    }

    // Optimizations
    impl_->fpm->run(*impl_->current_function);
  }

  // Keep the Ny-facing entry signature independent of the platform ABI. Ny
  // main remains a zero-parameter function; this generated C entrypoint owns
  // the process/JIT argc+argv bridge and the native runtime context lifetime.
  // Worker implementations are private specializations. A worker's arithmetic
  // failure must not call process-exit from a host thread: record it, return a
  // scalar sentinel, and propagate that status through every direct helper.
  // Ordinary calls to the same Ny function retain their usual failure path.
  std::unordered_map<llvm::Function *, llvm::Function *> worker_clones;
  for (const auto &name : parallel_workers) {
    const auto original = native_functions.find(name);
    if (original == native_functions.end()) continue;
    llvm::ValueToValueMapTy mapping;
    auto *clone = llvm::CloneFunction(original->second, mapping);
    clone->setName("__ny_parallel_worker." + std::to_string(worker_clones.size()));
    clone->setLinkage(llvm::Function::InternalLinkage);
    worker_clones.emplace(original->second, clone);
  }
  if (!worker_clones.empty()) {
    std::unordered_set<llvm::Function *> cloned_workers;
    for (const auto &pair : worker_clones) cloned_workers.insert(pair.second);
    auto record_failure = impl_->module->getOrInsertFunction("nyrt_parallel_fail_v1",
        llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), {llvm::PointerType::getUnqual(ctx)}, false));
    auto failed = impl_->module->getOrInsertFunction("nyrt_parallel_failed_v1",
        llvm::FunctionType::get(llvm::Type::getInt32Ty(ctx), {}, false));
    for (const auto &pair : worker_clones) {
      auto *clone = pair.second;
      std::vector<llvm::CallInst *> calls;
      for (auto &block : *clone)
        for (auto &instruction : block)
          if (auto *call = llvm::dyn_cast<llvm::CallInst>(&instruction)) calls.push_back(call);
      const auto emit_zero_return = [&](llvm::IRBuilder<> &builder) {
        if (clone->getReturnType()->isVoidTy()) builder.CreateRetVoid();
        else builder.CreateRet(llvm::Constant::getNullValue(clone->getReturnType()));
      };
      for (auto *call : calls) {
        auto *callee = call->getCalledFunction();
        if (!callee) {
          std::cerr << "[NativeCodegen] isolated worker contains an indirect native call\n";
          return false;
        }
        if (callee->getName() == "nyrt_profile_site") {
          // Worker clones are certified scalar-only and outside the parent
          // capture. Keep their copies free of runtime context access while
          // retaining attribution in the ordinary callable function.
          call->eraseFromParent();
          continue;
        }
        if (callee->getName() == "nyrt_bool_i64" ||
            callee->getName() == "nyrt_bool_f64") {
          // The ordinary helpers terminate on malformed values. Workers must
          // report the error through the joined scalar-worker protocol instead
          // of exiting the process from a host thread.
          auto *input = call->getArgOperand(0);
          auto *block = call->getParent();
          auto *continuation = block->splitBasicBlock(call, "ny.parallel.bool.valid");
          block->getTerminator()->eraseFromParent();
          llvm::IRBuilder<> check(block);
          llvm::Value *zero = nullptr;
          llvm::Value *one = nullptr;
          if (callee->getName() == "nyrt_bool_f64") {
            zero = check.CreateFCmpOEQ(input, llvm::ConstantFP::get(input->getType(), 0.0));
            one = check.CreateFCmpOEQ(input, llvm::ConstantFP::get(input->getType(), 1.0));
          } else {
            zero = check.CreateICmpEQ(input, llvm::ConstantInt::get(input->getType(), 0));
            one = check.CreateICmpEQ(input, llvm::ConstantInt::get(input->getType(), 1));
          }
          auto *converted = check.CreateZExt(one, call->getType(), "ny.parallel.bool.value");
          auto *failure = llvm::BasicBlock::Create(ctx, "ny.parallel.bool.invalid", clone);
          check.CreateCondBr(check.CreateOr(zero, one), continuation, failure);
          llvm::IRBuilder<> exit(failure);
          exit.CreateCall(record_failure, {exit.CreateGlobalString("boolean requires 0 or 1")});
          emit_zero_return(exit);
          call->replaceAllUsesWith(converted);
          call->eraseFromParent();
          continue;
        }
        if (callee->getName() == "nyrt_fail") {
          auto *terminator = call->getParent()->getTerminator();
          if (!llvm::isa<llvm::UnreachableInst>(terminator) || call->getNextNode() != terminator) {
            std::cerr << "[NativeCodegen] parallel worker failure edge is not closed\n";
            return false;
          }
          llvm::IRBuilder<> at(call);
          at.CreateCall(record_failure, {call->getArgOperand(0)});
          emit_zero_return(at);
          terminator->eraseFromParent();
          call->eraseFromParent();
          continue;
        }
        const auto target = worker_clones.find(callee);
        if (target != worker_clones.end()) {
          call->setCalledFunction(target->second);
        } else if (!cloned_workers.count(callee)) {
          static const std::unordered_set<std::string> isolated_runtime_calls = {
              "tan", "asin", "acos", "atan", "atan2", "cbrt", "nyrt_fault_v1_pending"};
          if (!callee->isIntrinsic() && !isolated_runtime_calls.count(callee->getName().str())) {
            std::cerr << "[NativeCodegen] isolated worker contains unexpected native call '"
                      << callee->getName().str() << "'\n";
            return false;
          }
          continue;
        }
        auto *block = call->getParent();
        auto *continuation = block->splitBasicBlock(call->getNextNode(), "ny.parallel.continue");
        block->getTerminator()->eraseFromParent();
        auto *failure = llvm::BasicBlock::Create(ctx, "ny.parallel.failure", clone);
        llvm::IRBuilder<> exit(failure);
        emit_zero_return(exit);
        llvm::IRBuilder<> check(block);
        auto *status = check.CreateCall(failed);
        check.CreateCondBr(check.CreateICmpNE(status,
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 0)), failure, continuation);
      }
    }
    for (const auto &pair : parallel_callbacks) {
      auto *callback = pair.second;
      auto *worker = worker_clones.at(native_functions.at(pair.first));
      auto *entry = llvm::BasicBlock::Create(ctx, "entry", callback);
      llvm::IRBuilder<> at(entry);
      llvm::Value *input = callback->getArg(0);
      auto *parameter = worker->getFunctionType()->getParamType(0);
      if (parameter->isDoubleTy()) input = at.CreateBitCast(input, parameter);
      else if (parameter != input->getType()) input = at.CreateTrunc(input, parameter);
      llvm::Value *result = at.CreateCall(worker, {input});
      if (result->getType()->isDoubleTy()) result = at.CreateBitCast(result, llvm::Type::getInt64Ty(ctx));
      else if (result->getType() != llvm::Type::getInt64Ty(ctx))
        result = at.CreateSExt(result, llvm::Type::getInt64Ty(ctx));
      at.CreateRet(result);
    }
  }

  const auto ny_main_function = native_functions.find("main");
  auto *ny_main = ny_main_function == native_functions.end()
                      ? nullptr
                      : ny_main_function->second;
  const nil_function *nil_main = find_nil_function(mod, "main");
  if (!ny_main) {
    std::cerr << "[NativeCodegen] module does not define entry function "
                 "'main'\n";
    return false;
  }
  if (impl_->module->getFunction("__ny_user_main") != nullptr) {
    std::cerr << "[NativeCodegen] function name '__ny_user_main' is reserved "
                 "by the native entry ABI\n";
    return false;
  }
  if (!ny_main->arg_empty() ||
      ny_main->getReturnType() != llvm::Type::getInt32Ty(ctx)) {
    std::cerr << "[NativeCodegen] internal main signature does not match the "
                 "native entry ABI\n";
    return false;
  }
  ny_main->setName("__ny_user_main");
  ny_main->setLinkage(llvm::Function::InternalLinkage);

  const auto global_initializer_function =
      native_functions.find("__globals_init__");
  auto *global_initializer =
      global_initializer_function == native_functions.end()
          ? nullptr
          : global_initializer_function->second;
  bool user_main_calls_global_initializer = false;
  if (nil_main != nullptr && !nil_main->body.empty()) {
    const auto &first = nil_main->body.front();
    user_main_calls_global_initializer =
        stringToOpcode(first.opname) == Opcode::CALL &&
        first.args.size() == 2 && first.args[0] == "__globals_init__" &&
        first.args[1] == "0";
  }
  if (global_initializer != nullptr &&
      (!global_initializer->arg_empty() ||
       !global_initializer->getReturnType()->isVoidTy())) {
    std::cerr << "[NativeCodegen] global initializer must have the exact () -> "
                 "void ABI\n";
    return false;
  }

  auto *i32_type = llvm::Type::getInt32Ty(ctx);
  auto *opaque_pointer_type = llvm::PointerType::getUnqual(ctx);
  auto *platform_main_type =
      llvm::FunctionType::get(i32_type, {i32_type, opaque_pointer_type}, false);
  auto *platform_main = llvm::Function::Create(platform_main_type,
                                               llvm::Function::ExternalLinkage,
                                               "main", *impl_->module);
  auto argument = platform_main->arg_begin();
  llvm::Value *argc_value = &*argument++;
  llvm::Value *argv_value = &*argument;
  argc_value->setName("argc");
  argv_value->setName("argv");

  auto *entry = llvm::BasicBlock::Create(ctx, "entry", platform_main);
  impl_->builder->SetInsertPoint(entry);
  auto *zero = llvm::ConstantInt::get(i32_type, 0);
  auto *one = llvm::ConstantInt::get(i32_type, 1);
  auto *null_argv = llvm::ConstantPointerNull::get(opaque_pointer_type);
  auto *has_program_name = impl_->builder->CreateAnd(
      impl_->builder->CreateICmpSGT(argc_value, zero),
      impl_->builder->CreateICmpNE(argv_value, null_argv));
  auto *program_argc = impl_->builder->CreateSelect(
      has_program_name, impl_->builder->CreateSub(argc_value, one), zero,
      "ny.argc");
  auto *argv_after_program_name = impl_->builder->CreateGEP(
      opaque_pointer_type, argv_value, one, "ny.argv.after_program_name");
  auto *program_argv = impl_->builder->CreateSelect(
      has_program_name, argv_after_program_name, null_argv, "ny.argv");

  auto begin_context = impl_->module->getOrInsertFunction(
      "nyrt_context_begin",
      llvm::FunctionType::get(llvm::Type::getVoidTy(ctx),
                              {i32_type, opaque_pointer_type}, false));
  auto end_context = impl_->module->getOrInsertFunction(
      "nyrt_context_end",
      llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), {}, false));
  auto runtime_abi_version = impl_->module->getOrInsertFunction(
      "nyrt_abi_version", llvm::FunctionType::get(i32_type, {}, false));
  auto *abi_version =
      impl_->builder->CreateCall(runtime_abi_version, {}, "ny.runtime.abi");
  auto *abi_failure =
      llvm::BasicBlock::Create(ctx, "runtime.abi.failure", platform_main);
  auto *abi_success =
      llvm::BasicBlock::Create(ctx, "runtime.abi.success", platform_main);
  impl_->builder->CreateCondBr(
      impl_->builder->CreateICmpEQ(abi_version,
                                   llvm::ConstantInt::get(i32_type, 1)),
      abi_success, abi_failure);
  impl_->builder->SetInsertPoint(abi_failure);
  auto *abi_message = impl_->builder->CreateGlobalString(
      "NIL native runtime error: incompatible nilruntime ABI (expected 1)\n");
  impl_->builder->CreateCall(get_runtime_failure_function(), {abi_message});
  impl_->builder->CreateUnreachable();
  impl_->builder->SetInsertPoint(abi_success);
  impl_->builder->CreateCall(begin_context, {program_argc, program_argv});
  llvm::Value *global_roots = nullptr;
  if (impl_->memory_mode == MemoryMode::SAFE_GC) {
    std::vector<llvm::Value *> slots;
    for (const auto &global : impl_->globals_map) slots.push_back(global.second);
    global_roots = emit_root_frame(*impl_->module, *impl_->builder, slots);
  }
  llvm::BasicBlock *uncaught_fault = nullptr;
  const auto entry_fault_check = [&]() {
    if (!fault_protocol_enabled) return;
    if (!uncaught_fault)
      uncaught_fault = llvm::BasicBlock::Create(ctx, "ny.fault.uncaught", platform_main);
    auto pending = impl_->module->getOrInsertFunction("nyrt_fault_v1_pending",
        llvm::FunctionType::get(i32_type, {}, false));
    auto *status = impl_->builder->CreateCall(pending, {}, "ny.fault.pending");
    auto *success = llvm::BasicBlock::Create(ctx, "ny.fault.entry.success", platform_main);
    impl_->builder->CreateCondBr(impl_->builder->CreateICmpNE(status, zero),
                                 uncaught_fault, success);
    impl_->builder->SetInsertPoint(success);
  };
  // Current source-generated NIL calls its initializer at the start of Ny
  // main so interpreter routes share the same semantics. Legacy or hand-made
  // NIL may omit that synthetic call; native entry still runs a rooted global
  // initializer exactly once in that case.
  if (global_initializer != nullptr && !user_main_calls_global_initializer) {
    impl_->builder->CreateCall(global_initializer);
    entry_fault_check();
  }
  auto *exit_code = impl_->builder->CreateCall(ny_main);
  entry_fault_check();
  if (global_roots) {
    auto leave = impl_->module->getOrInsertFunction("nyrt_roots_leave",
        llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), {opaque_pointer_type}, false));
    impl_->builder->CreateCall(leave, {global_roots});
  }
  impl_->builder->CreateCall(end_context);
  impl_->builder->CreateRet(exit_code);
  if (uncaught_fault) {
    impl_->builder->SetInsertPoint(uncaught_fault);
    auto report = impl_->module->getOrInsertFunction("nyrt_fault_v1_report_uncaught",
        llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), {}, false));
    impl_->builder->CreateCall(report);
    if (global_roots) {
      auto leave = impl_->module->getOrInsertFunction("nyrt_roots_leave",
          llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), {opaque_pointer_type}, false));
      impl_->builder->CreateCall(leave, {global_roots});
    }
    impl_->builder->CreateCall(end_context);
    impl_->builder->CreateRet(one);
  }

  std::string wrapper_error;
  llvm::raw_string_ostream wrapper_error_stream(wrapper_error);
  if (llvm::verifyFunction(*platform_main, &wrapper_error_stream)) {
    wrapper_error_stream.flush();
    std::cerr << "[NativeCodegen] platform entry verify failure: "
              << wrapper_error << std::endl;
    return false;
  }
  impl_->fpm->run(*platform_main);

  if (fault_protocol_enabled)
    for (const auto &function : native_functions)
      instrument_native_fault_frame(*function.second, *impl_->module);

  const bool uses_owned_scopes = std::any_of(reachable_functions.begin(), reachable_functions.end(),
      [](const nil_function *function) {
        return std::any_of(function->body.begin(), function->body.end(), [](const nil_instruction &instruction) {
          return instruction.opname.rfind("OWN_", 0) == 0;
        });
      });
  if (uses_owned_scopes)
    for (const auto &function : native_functions)
      instrument_native_owned_frame(*function.second, *impl_->module);

  if (impl_->memory_mode == MemoryMode::SAFE_GC) {
    if (impl_->module->getDataLayout().getPointerSize() != 8) {
      std::cerr << "[NativeCodegen] managed native roots require a 64-bit target\n";
      return false;
    }
    std::unordered_set<llvm::Function *> isolated_scalar_functions;
    for (const auto &pair : worker_clones) isolated_scalar_functions.insert(pair.second);
    for (const auto &pair : parallel_callbacks) isolated_scalar_functions.insert(pair.second);
    // The validated worker graph cannot create or access a Ny heap value.
    // Publishing GC roots there adds per-item allocations with no reachable
    // object to trace and would introduce a process-fatal allocator boundary
    // inside the joined scalar-error protocol. Ordinary source implementations
    // of the same functions retain normal root instrumentation.
    for (auto &function : *impl_->module)
      if (!function.isDeclaration() && &function != platform_main &&
          !isolated_scalar_functions.count(&function))
        instrument_native_roots(function, *impl_->module);
  }

  if (impl_->embed_context) {
    // Retain the original NIL module's explicitly annotated declarations,
    // including unused functions, without making their native bodies live.
    // This is a constant record, not executable code or runtime state.
    auto *bytes = llvm::ConstantDataArray::get(ctx, context_manifest);
    auto *manifest = new llvm::GlobalVariable(
        *impl_->module, bytes->getType(), true,
        llvm::GlobalValue::PrivateLinkage, bytes, "__ny_context_v1");
    manifest->setSection(".ny.context");
    manifest->setAlignment(llvm::Align(1));
    llvm::appendToUsed(*impl_->module, {manifest});
  }

  std::string err;
  llvm::raw_string_ostream err_s(err);
  if (llvm::verifyModule(*impl_->module, &err_s)) {
    err_s.flush();
    std::cerr << "[NativeCodegen] module verify failures: " << err << std::endl;
    return false;
  }
  return true;
}

int NativeCodegen::executeJIT(const std::vector<std::string> &program_args) {
  if (!impl_->module)
    return 1;

  // ORC is intentionally lazy: AOT compilation never needs to create a JIT
  // session or executable-memory manager. The independently created host
  // TargetMachine above supplies the module layout for both routes.
  if (!impl_->jit) {
    auto jit_expected = llvm::orc::LLJITBuilder().create();
    if (!jit_expected) {
      std::cerr << "[NativeCodegen] Failed to create JIT: "
                << toString(jit_expected.takeError()) << std::endl;
      return 1;
    }
    impl_->jit = std::move(*jit_expected);

    if (impl_->jit->getDataLayout().getStringRepresentation() !=
        impl_->module->getDataLayout().getStringRepresentation()) {
      std::cerr << "[NativeCodegen] JIT target data layout does not match the "
                   "generated native module\n";
      impl_->jit.reset();
      return 1;
    }

    auto generator =
        llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
            impl_->jit->getDataLayout().getGlobalPrefix());
    if (!generator) {
      std::cerr << "[NativeCodegen] Failed to add process symbol generator: "
                << toString(generator.takeError()) << std::endl;
      impl_->jit.reset();
      return 1;
    }
    impl_->jit->getMainJITDylib().addGenerator(std::move(*generator));
  }

  // Static-library symbols are not guaranteed to be exported from the
  // nilcompiler process for dynamic lookup. Define the runtime C ABI at its
  // exact in-process addresses so JIT and AOT resolve the same symbol names.
  llvm::orc::MangleAndInterner mangle(impl_->jit->getExecutionSession(),
                                      impl_->jit->getDataLayout());
  llvm::orc::SymbolMap runtime_symbols;
  auto expose_runtime_symbol = [&](const char *name, auto *address) {
    runtime_symbols[mangle(name)] =
        llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(address),
                                     llvm::JITSymbolFlags::Exported);
  };
  expose_runtime_symbol("nyrt_ui_v1_available", &nyrt_ui_v1_available);
  expose_runtime_symbol("nyrt_ui_v1_window", &nyrt_ui_v1_window);
  expose_runtime_symbol("nyrt_ui_v1_widget", &nyrt_ui_v1_widget);
  expose_runtime_symbol("nyrt_ui_v1_grid_attach", &nyrt_ui_v1_grid_attach);
  expose_runtime_symbol("nyrt_ui_v1_set_text", &nyrt_ui_v1_set_text);
  expose_runtime_symbol("nyrt_ui_v1_read_text", &nyrt_ui_v1_read_text);
  expose_runtime_symbol("nyrt_ui_v1_set_number", &nyrt_ui_v1_set_number);
  expose_runtime_symbol("nyrt_ui_v1_read_number", &nyrt_ui_v1_read_number);
  expose_runtime_symbol("nyrt_ui_v1_activate", &nyrt_ui_v1_activate);
  expose_runtime_symbol("nyrt_ui_v1_poll", &nyrt_ui_v1_poll);
  expose_runtime_symbol("nyrt_ui_v1_event_number", &nyrt_ui_v1_event_number);
  expose_runtime_symbol("nyrt_ui_v1_event_text", &nyrt_ui_v1_event_text);
  expose_runtime_symbol("nyrt_ui_v1_release_event", &nyrt_ui_v1_release_event);
  expose_runtime_symbol("nyrt_ui_v1_close", &nyrt_ui_v1_close);
  expose_runtime_symbol("nyrt_db2_available", &nyrt_db2_available);
  expose_runtime_symbol("nyrt_db2_open", &nyrt_db2_open);
  expose_runtime_symbol("nyrt_db2_execute", &nyrt_db2_execute);
  expose_runtime_symbol("nyrt_db2_read_i64", &nyrt_db2_read_i64);
  expose_runtime_symbol("nyrt_db2_read_text", &nyrt_db2_read_text);
  expose_runtime_symbol("nyrt_abi_version", &nyrt_abi_version);
  expose_runtime_symbol("nyrt_time_now_ms", &nyrt_time_now_ms);
  expose_runtime_symbol("nyrt_sleep_ms", &nyrt_sleep_ms);
  expose_runtime_symbol("nyrt_parallel_map_v1", &nyrt_parallel_map_v1);
  expose_runtime_symbol("nyrt_parallel_fail_v1", &nyrt_parallel_fail_v1);
  expose_runtime_symbol("nyrt_parallel_failed_v1", &nyrt_parallel_failed_v1);
  expose_runtime_symbol("nyrt_owned_v1_depth", &nyrt_owned_v1_depth);
  expose_runtime_symbol("nyrt_owned_v1_enter", &nyrt_owned_v1_enter);
  expose_runtime_symbol("nyrt_owned_v1_leave", &nyrt_owned_v1_leave);
  expose_runtime_symbol("nyrt_owned_v1_bind", &nyrt_owned_v1_bind);
  expose_runtime_symbol("nyrt_owned_v1_take", &nyrt_owned_v1_take);
  expose_runtime_symbol("nyrt_owned_v1_unwind", &nyrt_owned_v1_unwind);
  expose_runtime_symbol("nyrt_context_begin", &nyrt_context_begin);
  expose_runtime_symbol("nyrt_context_end", &nyrt_context_end);
  expose_runtime_symbol("nyrt_profile_site", &nyrt_profile_site);
  expose_runtime_symbol("nyrt_roots_enter", &nyrt_roots_enter);
  expose_runtime_symbol("nyrt_roots_leave", &nyrt_roots_leave);
  expose_runtime_symbol("nyrt_safepoint", &nyrt_safepoint);
  expose_runtime_symbol("nyrt_fault_v1_frame_enter", &nyrt_fault_v1_frame_enter);
  expose_runtime_symbol("nyrt_fault_v1_frame_leave", &nyrt_fault_v1_frame_leave);
  expose_runtime_symbol("nyrt_fault_v1_saved_depth", &nyrt_fault_v1_saved_depth);
  expose_runtime_symbol("nyrt_fault_v1_unwind", &nyrt_fault_v1_unwind);
  expose_runtime_symbol("nyrt_fault_v1_pending", &nyrt_fault_v1_pending);
  expose_runtime_symbol("nyrt_fault_v1_raise", &nyrt_fault_v1_raise);
  expose_runtime_symbol("nyrt_fault_v1_save", &nyrt_fault_v1_save);
  expose_runtime_symbol("nyrt_fault_v1_match", &nyrt_fault_v1_match);
  expose_runtime_symbol("nyrt_fault_v1_bind", &nyrt_fault_v1_bind);
  expose_runtime_symbol("nyrt_fault_v1_drop", &nyrt_fault_v1_drop);
  expose_runtime_symbol("nyrt_fault_v1_resume", &nyrt_fault_v1_resume);
  expose_runtime_symbol("nyrt_fault_v1_report_uncaught", &nyrt_fault_v1_report_uncaught);
  expose_runtime_symbol("nyrt_fail", &nyrt_fail);
  expose_runtime_symbol("nyrt_panic", &nyrt_panic);
  expose_runtime_symbol("nyrt_text_substr", &nyrt_text_substr);
  expose_runtime_symbol("nyrt_text_i64", &nyrt_text_i64);
  expose_runtime_symbol("nyrt_text_f64", &nyrt_text_f64);
  expose_runtime_symbol("nyrt_text_bool", &nyrt_text_bool);
  expose_runtime_symbol("nyrt_bool_i64", &nyrt_bool_i64);
  expose_runtime_symbol("nyrt_bool_f64", &nyrt_bool_f64);
  expose_runtime_symbol("nyrt_bool_text", &nyrt_bool_text);
  expose_runtime_symbol("nyrt_any_v1_box", &nyrt_any_v1_box);
  expose_runtime_symbol("nyrt_any_v1_unbox", &nyrt_any_v1_unbox);
  expose_runtime_symbol("nyrt_any_v1_is_type", &nyrt_any_v1_is_type);
  expose_runtime_symbol("nyrt_alloc", &nyrt_alloc);
  expose_runtime_symbol("nyrt_free", &nyrt_free);
  expose_runtime_symbol("nyrt_arg_count", &nyrt_arg_count);
  expose_runtime_symbol("nyrt_arg", &nyrt_arg);
  expose_runtime_symbol("nyrt_exit", &nyrt_exit);
  expose_runtime_symbol("nyrt_write_stdout", &nyrt_write_stdout);
  expose_runtime_symbol("nyrt_write_stderr", &nyrt_write_stderr);
  expose_runtime_symbol("nyrt_read_line", &nyrt_read_line);
  expose_runtime_symbol("nyrt_flush_stdout", &nyrt_flush_stdout);
  expose_runtime_symbol("nyrt_regex_validate", &nyrt_regex_validate);
  expose_runtime_symbol("nyrt_nildata_v1_request", &nyrt_nildata_v1_request);
  expose_runtime_symbol("nyrt_regex_search", &nyrt_regex_search);
  expose_runtime_symbol("nyrt_regex_group_index", &nyrt_regex_group_index);
  expose_runtime_symbol("nyrt_array_new", &nyrt_array_new);
  expose_runtime_symbol("nyrt_array_new_with_value",
                        &nyrt_array_new_with_value);
  expose_runtime_symbol("nyrt_array_length", &nyrt_array_length);
  expose_runtime_symbol("nyrt_array_get", &nyrt_array_get);
  expose_runtime_symbol("nyrt_array_set", &nyrt_array_set);
  expose_runtime_symbol("nyrt_array_push", &nyrt_array_push);
  expose_runtime_symbol("nyrt_buffer_new", &nyrt_buffer_new);
  expose_runtime_symbol("nyrt_buffer_free", &nyrt_buffer_free);
  expose_runtime_symbol("nyrt_buffer_offset", &nyrt_buffer_offset);
  expose_runtime_symbol("nyrt_buffer_get", &nyrt_buffer_get);
  expose_runtime_symbol("nyrt_buffer_set", &nyrt_buffer_set);
  expose_runtime_symbol("nyrt_read_regular_bytes", &nyrt_read_regular_bytes);
  expose_runtime_symbol("nyrt_write_bytes_atomic", &nyrt_write_bytes_atomic);
  expose_runtime_symbol("nyrt_try_i32", &nyrt_try_i32);
  expose_runtime_symbol("nyrt_try_i64", &nyrt_try_i64);
  expose_runtime_symbol("nyrt_i32_to_text", &nyrt_i32_to_text);
  expose_runtime_symbol("nyrt_i64_to_text", &nyrt_i64_to_text);
  if (auto define_error = impl_->jit->getMainJITDylib().define(
          llvm::orc::absoluteSymbols(std::move(runtime_symbols)))) {
    std::cerr << "[NativeCodegen] JIT runtime symbol registration failed: "
              << toString(std::move(define_error)) << std::endl;
    return 1;
  }

  auto err = impl_->jit->addIRModule(llvm::orc::ThreadSafeModule(
      std::move(impl_->module), std::move(impl_->context)));
  if (err) {
    std::cerr << "[NativeCodegen] JIT add failed: " << toString(std::move(err))
              << std::endl;
    return 1;
  }

  auto main_sym = impl_->jit->lookup("main");
  if (!main_sym) {
    std::cerr << "[NativeCodegen] 'main' not found in JIT" << std::endl;
    return 1;
  }

  if (program_args.size() >=
      static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    std::cerr << "[NativeCodegen] too many JIT program arguments\n";
    return 1;
  }

  std::vector<std::string> argv_storage;
  argv_storage.reserve(program_args.size() + 1);
  argv_storage.emplace_back("ny-jit");
  argv_storage.insert(argv_storage.end(), program_args.begin(),
                      program_args.end());
  std::vector<char *> argv_values;
  argv_values.reserve(argv_storage.size() + 1);
  for (auto &value : argv_storage)
    argv_values.push_back(value.data());
  argv_values.push_back(nullptr);

  auto main_fn = main_sym->toPtr<int (*)(int, char **)>();
  diag_info("[llvm] Running JIT");
  const int exit_code =
      main_fn(static_cast<int>(argv_storage.size()), argv_values.data());
  if (nyrt_arg_count() != 0) {
    std::cerr << "[NativeCodegen] JIT entry returned without cleaning its "
                 "runtime context\n";
    nyrt_context_end();
    return 1;
  }
  return exit_code;
}

bool NativeCodegen::emitObjectFile(const std::string &path) {
  if (!impl_->module || !impl_->target_machine) {
    std::cerr << "[NativeCodegen] Native target is not initialized\n";
    return false;
  }

  std::error_code ec;
  llvm::raw_fd_ostream dest(path, ec, llvm::sys::fs::OF_None);
  if (ec) {
    std::cerr << "Could not open file: " << ec.message() << "\n";
    return false;
  }

  llvm::legacy::PassManager pass;
  auto fileType = llvm::CodeGenFileType::ObjectFile;

  if (impl_->target_machine->addPassesToEmitFile(pass, dest, nullptr,
                                                 fileType)) {
    std::cerr << "TargetMachine can't emit a file of this type\n";
    return false;
  }

  pass.run(*impl_->module);
  // close() flushes buffered bytes and records both write and close failures in
  // the stream's error_code. Handle and clear it before destruction; otherwise
  // raw_fd_ostream deliberately terminates on an unobserved output error.
  dest.close();
  if (dest.has_error()) {
    const std::error_code output_error = dest.error();
    dest.clear_error();
    std::cerr << "Could not finish object file: " << output_error.message()
              << "\n";
    return false;
  }
  diag_info("[llvm] Wrote object file to " + path);
  return true;
}

bool NativeCodegen::loadLibrary(const std::string &path) {
  std::string errMsg;
  if (llvm::sys::DynamicLibrary::LoadLibraryPermanently(path.c_str(),
                                                        &errMsg)) {
    std::cerr << "[NativeCodegen] Failed to load library '" << path
              << "': " << errMsg << "\n";
    return false;
  }
  return true;
}

} // namespace ny

#endif // NY_HAS_LLVM
