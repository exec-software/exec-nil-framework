#ifndef NY_COMPILER_NIL_CONTRACT_HPP
#define NY_COMPILER_NIL_CONTRACT_HPP

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ny {

// Executable v3 preserves schemas and function metadata omitted by v1.
// Library v2 retains its distinct embedded-source bootstrap contract.
inline constexpr std::uint32_t legacy_executable_binary_nil_version = 1;
inline constexpr std::uint32_t library_binary_nil_version = 2;
inline constexpr std::uint32_t executable_binary_nil_version = 3;
// v4 adds explicit class metadata (section 7), retaining the executable v3
// schemas and function records. Flags distinguish executables from libraries.
inline constexpr std::uint32_t class_binary_nil_version = 4;
// v5 executable: v4 records (including an empty class section when needed),
// plus section 8 containing exact instruction source/lexical bindings.
inline constexpr std::uint32_t source_debug_binary_nil_version = 5;

// Header flags. The library bit is historical; the type-annotation bit says
// the encoded type records in sections 1 and 7 each carry their retained
// '@ai_context' annotations.
//
// This is a flag rather than a sixth version because the capability is
// orthogonal to the cumulative v1..v5 layers: a module can state intent on a
// record with or without class metadata, and with or without source debug
// records, and a linear version cannot express a feature matrix. A reader
// that does not know a flag bit refuses the artifact rather than ignoring it,
// so one that states more than the reader understands fails loudly instead of
// being read as one that states less.
inline constexpr std::uint32_t binary_nil_library_flag = 1;
inline constexpr std::uint32_t binary_nil_type_annotation_flag = 2;
inline constexpr std::uint32_t binary_nil_known_flags =
    binary_nil_library_flag | binary_nil_type_annotation_flag;

// Does this flag word say the type records carry their annotations?
inline bool binary_nil_annotates_types(std::uint32_t flags) {
  return (flags & binary_nil_type_annotation_flag) != 0U;
}

// LOAD_GLOBAL/STORE_GLOBAL accept a trailing scalar, text, or anything storage type. The historical
// one-operand form remains readable, but a module may not mix typed/untyped
// accesses to one name. Operands already have extensible text/binary encoding;
// this contract does not change the binary section layout or version.
inline bool is_typed_global_storage_type(std::string_view type) {
  return type == "text" || type == "bool" || type == "u8" ||
         type == "i32" || type == "i64" || type == "f64" || type == "anything";
}

// Compiler-generated non-void fallthrough is represented explicitly in NIL.
// A consumer must fail at runtime if this path is reached. For typed functions,
// ordinary RET requires exactly zero frame-local operands for void or one for
// a value return. Historical NIL without a return signature remains untyped.
inline constexpr const char *nil_nonvoid_fallthrough_operand =
    "$ny.generated.nonvoid-fallthrough";

// Fault instructions append names/ordinals without changing the extensible
// operand encoding or executable section layout. Old consumers reject them.
// Saved slots are opaque frame-owned metadata, never operand-stack values.
struct Instruction;
struct nil_function;
struct nil_module;
struct nil_class_def;
struct nil_class_method;
struct nil_class_method_resolution {
  std::string owner;
  const nil_class_method *method = nullptr;
};
// Dynamic metadata is exact and never admits borrowed/affine storage. Nested
// aggregate fields and enum payloads are checked too, including hidden cells.
void validate_nil_dynamic_type(const nil_module &module, const std::string &type,
                               bool allow_anything = false);
bool is_reserved_nil_class_name(const std::string &name);
const nil_class_def *find_nil_class(const nil_module &module,
                                  const std::string &name);
bool nil_class_assignable(const nil_module &module, const std::string &expected,
                          const std::string &actual);
nil_class_method_resolution resolve_nil_class_method(
    const nil_module &module, const std::string &static_class,
    const std::string &method_name);
void validate_nil_class_contract(const nil_module &module);
void validate_nil_class_contract(const std::vector<Instruction> &program);
// Shared instruction provenance pass, used after structural class validation.
void validate_nil_class_provenance(const nil_module &module);
struct nil_fault_region {
  std::string id;
  std::size_t enter_pc = 0;
  std::size_t dispatch_pc = 0;
  std::size_t stack_height = 0;
  std::vector<std::string> saved_slots;
};
struct nil_fault_instruction_state {
  bool reachable = false;
  bool pending = false;
  std::size_t stack_height = 0;
  std::vector<nil_fault_region> regions;
  std::vector<std::string> saved_slots;
};
struct nil_fault_analysis {
  // States immediately before each instruction; indexes match function.body.
  std::vector<nil_fault_instruction_state> instructions;
};
nil_fault_analysis analyze_nil_fault_function(const nil_function &function,
                                              const nil_module &module);
// Compiler-only marker and finalization. Source returns nested in a value
// switch may abandon suspended expression operands; executable NIL still
// requires an exact 0/1-value RET stack and never accepts this marker.
inline constexpr const char *nil_switch_return_marker = "$nil.codegen.switch-return";
void finalize_nil_switch_returns(nil_module &module);
void validate_nil_fault_contract(const nil_module &module);
// Loader-bypassing embedders still receive region/stack/state validation.
void validate_nil_fault_contract(const std::vector<Instruction> &program);

} // namespace ny

#endif // NY_COMPILER_NIL_CONTRACT_HPP
