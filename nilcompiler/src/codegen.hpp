#ifndef NY_COMPILER_CODEGEN_HPP
#define NY_COMPILER_CODEGEN_HPP

// codegen.hpp
//
// Code generator public API for nilcompiler. This header defines the
// interfaces used to convert the AST (ast.hpp) into NIL (textual or
// binary) and to emit NIL artifacts to disk.
//
// Conventions and style:
//  - K&R underscore naming (lowercase + underscores for function/variable
//  names).
//  - Types and functions live inside the ny:: namespace.
//  - Minimal, implementation-agnostic API; concrete behaviors live in
//  codegen.cpp.
//
// NOTE: Do not include heavy implementation headers in this public file.
//       Keep only minimal includes to build clients easily.

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <vector>

#include "ast.hpp"
#include "source_debug.hpp"

namespace ny {

// Forward declarations
struct ast_program;
struct ast_function_decl;
struct ast_stmt;
struct ast_expr;
class emitter;

// Codegen-level diagnostics (independent from the parser diagnostics)
struct codegen_diagnostic {
  int line = 0;        // 1-based line number (if known)
  std::string message; // Human-readable message
  std::string hint;    // Optional hint/suggestion
};

// NIL format options (text or binary)
enum class nil_format { nil_text, nil_binary };

// Build modes for conditional compilation and test handling
enum class build_mode {
  release_mode, // Strip tests
  debug_mode,   // Compile tests, no auto-register
  test_mode     // Compile and register tests
};

// Codegeneration options
struct codegen_options {
  nil_format target_format = nil_format::nil_text; // Default textual NIL
  bool include_debug_info = true; // include symbol / line mappings
  bool include_source_debug = false; // opt-in source paths and lexical bindings
  int opt_level = 0;              // optimization level (0..3)
  bool preserve_comments = false; // keep comments in text output
  std::string target_triple;      // e.g., "x86_64-pc-windows-msvc"
  uint16_t nil_version_major = 0;
  uint16_t nil_version_minor = 1;
  build_mode mode = build_mode::debug_mode; // Default to debug
};

// A simple NIL instruction abstraction used by codegen to assemble textual
// representations (or to serialize to binary later).
struct nil_instruction {
  std::string opname; // e.g., "call", "ret", "add.i32"
  std::vector<std::string>
      args;           // Each argument as a string (SSA names or constants)
  int debug_line = 0; // Original source line (if debug info on)
  std::string source_file;
  std::vector<DebugLocalBinding> debug_locals;
};

// Concrete generic-enum metadata is carried as trailing instruction operands,
// leaving the historical stack-machine operands intact:
//   NEW_VARIANT(tag, count, concrete_enum, payload_type)
//   IS_VARIANT(tag, concrete_enum, payload_type)
//   UNWRAP_VARIANT(count, concrete_enum, payload_type)
// Older NIL may omit both metadata operands. Unit variants use "void" as the
// instruction payload_type; their nil_enum_def associated_type remains empty.

// NIL function representation (intermediate for the writer)
struct nil_function {
  std::string name;                // Function name (mangled or direct)
  std::vector<std::string> params; // Parameter names/types (textual)
  std::vector<std::string>
      generic_params;                // Generic type parameters (e.g. "T", "U")
  std::vector<nil_instruction> body; // Sequence of instructions
  std::string return_type;           // textual return type (optional)

  // Compiler-provided hint: the function was annotated with `@inline`.
  // The codegen may set this to true when it sees `@inline` on the AST
  // function node; the writer will emit @inline above the function if set.
  bool is_inline = false;

  // Test indicator: functions annotated with `@test` will set this to true,
  // which enables test-harness registration downstream in the toolchain.
  bool is_test = false;

  // Optional textual annotations collected on the function. This allows
  // passing annotations through the NIL for textual or tooling output.
  std::vector<std::string> annotations;

  int source_line = 0; // Source line of declaration
  std::string source_file;
};

// Main NIL module representation produced by the code generator.
// Contains all sections needed for serialization or textual output.
struct nil_struct_def {
  std::string name;
  std::vector<std::pair<std::string, std::string>> fields; // name, type
  // Annotations written above the declaration, in their textual '@name(...)'
  // form. A record's '@ai_context' is how a program states what the record is
  // for; under NIL-0093 that statement is part of what the artifact carries,
  // so it has to survive emission and reloading rather than living only in the
  // source it came from.
  std::vector<std::string> annotations;
};

struct nil_class_method {
  std::string name;
  std::string implementation;
  std::string receiver_type;
  std::vector<std::string> parameter_types; // excludes self
  std::string return_type;
  bool is_virtual = false;
  bool is_abstract = false;
};

struct nil_class_def {
  std::string name;
  bool is_abstract = false;
  std::vector<std::string> bases;
  std::vector<nil_class_method> methods; // declarations owned by this class
  // See nil_struct_def::annotations. A class states what it is for on the same
  // terms as a record does.
  std::vector<std::string> annotations;
};

struct nil_enum_def {
  std::string name;
  struct variant {
    std::string name;
    std::string associated_type; // optional, empty if none
  };
  std::vector<variant> variants;
  // See nil_struct_def::annotations.
  std::vector<std::string> annotations;
};

struct nil_embedded_source {
  std::string path;
  std::string contents;
};

struct nil_module {
  std::string module_name;
  bool is_library = false;
  std::vector<std::string> dependencies;
  std::vector<nil_embedded_source> embedded_sources;
  std::vector<nil_function> functions;
  std::vector<nil_struct_def> structs;
  std::vector<nil_class_def> classes;
  std::vector<nil_enum_def> enums;
  std::vector<std::pair<std::string, std::string>>
      string_constants; // name, value

  // Collection of test function names discovered in this module. This list
  // is typically populated by codegen when it translates functions annotated
  // as `@test` into the nil_function entries and registers their names
  // for test harness generation.
  std::vector<std::string> tests;

  // (Optional) type/trait/metadata sections may be added later
};

// A lightweight NIL writer that emits NIL (textual or binary). The writer's
// design is intentionally small: it offers textual emission and a small
// binary serialization interface. The implementation encapsulates header
// layout and section ordering, so clients can call high-level codegen APIs.
class nil_binary_writer {
public:
  nil_binary_writer(std::ostream &out, const codegen_options &opts)
      : out_(out), options_(opts) {}

  void emit_binary_module(const nil_module &mod);

private:
  std::ostream &out_;
  codegen_options options_;

  void write_u8(uint8_t v);
  void write_u16(uint16_t v);
  void write_u32(uint32_t v);
  void write_u64(uint64_t v);
  void write_string(const std::string &s);
};

class nil_writer {
public:
  explicit nil_writer(std::ostream &out,
                      const codegen_options &opts = codegen_options());

  // Emits the file header and all sections in a textual manner.
  // NOTE: This method is safe to call many times; it writes directly to 'out_'.
  //
  // The textual emission will also include function-level annotations such
  // as `@inline` (if `nil_function::is_inline` is set) directly above the
  // function declaration in the emitted NIL text. The writer may also print
  // any additional function annotations stored in `nil_function::annotations`.
  void emit_text_module(const nil_module &mod);

  // Emits binary NIL representation of the module to the configured stream.
  // The binary format is versioned by codegen_options.
  void emit_binary_module(const nil_module &mod);

  // Utility function that writes a single instruction in textual form.
  // Provided as a helper for lower-level codegen implementations.
  static void emit_text_instruction(std::ostream &os,
                                    const nil_instruction &instr);

  // Emit function-level annotations (lines like `@inline` or `@test`) for the
  // provided function into the textual output stream. The method prints any
  // annotations in `fn.annotations` (raw textual form) and also outputs a
  // `@inline` annotation when `fn.is_inline` is true. This helper is used by
  // `emit_text_module` when outputting function declarations.
  static void emit_declaration_annotations(std::ostream &os,
                                           const std::vector<std::string> &annotations);
  static void emit_function_annotations(std::ostream &os,
                                        const nil_function &fn);

private:
  std::ostream &out_;
  codegen_options options_;

  // Internal helpers (implementation details are intentionally omitted here)
  void emit_header(uint16_t major, uint16_t minor);
  void emit_type_section(const nil_module &mod);
  void emit_constant_section(const nil_module &mod);
  void emit_function_section(const nil_module &mod);
  void emit_symbol_section(const nil_module &mod);
};

// The core code generator which translates the AST to a NIL module.
class codegen {
public:
  explicit codegen(const codegen_options &opts = codegen_options());
  ~codegen();

  // Generate a nil_module from a full AST. The generator will walk the AST,
  // build instructions, and populate sections (string pool, functions).
  // If `diags` is provided, code generation diagnostics are appended.
  std::unique_ptr<nil_module>
  generate_module(const ast_program &program,
                  std::vector<codegen_diagnostic> *diags = nullptr);

  // Produce textual NIL and return as string (for quick inspection or tests).
  std::string generate_text(const ast_program &program,
                            std::vector<codegen_diagnostic> *diags = nullptr);

  // Emit NIL directly to a file. Returns true on success.
  bool emit_nil_to_file(const ast_program &program,
                        const std::filesystem::path &out_path,
                        std::vector<codegen_diagnostic> *diags = nullptr);

  // Emit NIL binary to file (compact, versioned binary format). This is
  // separate because the runtime and future backends read the binary encoded
  // NIL.
  bool
  emit_nil_binary_to_file(const ast_program &program,
                          const std::filesystem::path &out_path,
                          std::vector<codegen_diagnostic> *diags = nullptr);

  // Helper to find a function declaration by name (for generics)
  const ast_function_decl *lookup_function_decl(const std::string &name);
  // Like lookup_function_decl but also returns the canonical key stored in the map
  const ast_function_decl *lookup_function_decl_key(const std::string &name,
                                                     std::string &canonical_key);

  // Helper to find an enum declaration by name
  // Helper to find an enum declaration by name
  // Helper to find an enum declaration by name
  const ast_enum_decl *lookup_enum_decl(const std::string &name) {
      auto it = enum_decls_.find(name);
      if (it != enum_decls_.end()) return it->second;
      return nullptr;
  }
  std::string lookup_enum_fqn(const std::string &short_name) const {
    if (short_name.find("::") != std::string::npos)
      return short_name;
    const std::string suffix = "::" + short_name;
    std::string fallback;
    for (const auto &kv : enum_decls_) {
      if (kv.first.size() > suffix.size() &&
          kv.first.substr(kv.first.size() - suffix.size()) == suffix &&
          (fallback.empty() || kv.first.size() < fallback.size())) {
        fallback = kv.first;
      }
    }
    return fallback.empty() ? short_name : fallback;
  }
  // The module that DECLARES a type, which is where an implementation's
  // symbol must be keyed. A call site mangles a method name from the
  // receiver's INFERRED type, so keying on the extending module instead made
  // the two disagree whenever a book extended a type from another book: the
  // program compiled and the method was simply unreachable at run time.
  std::string qualify_impl_target(const std::string &name,
                                  const std::string &current_module) const;

  const ast_class_decl *lookup_class_decl(const std::string &name);
  const ast_struct_decl *lookup_struct_decl(const std::string &name) {
      auto it = struct_decls_.find(name);
      if (it != struct_decls_.end()) return it->second;
      return nullptr;
  }

  // Given an unqualified struct name, find its fully qualified name
  // by searching struct_decls_ for a key ending with "::short_name".
  // Returns the FQN if found, or short_name unchanged if not found.
  std::string lookup_struct_fqn(const std::string &short_name) const {
    if (short_name.find("::") != std::string::npos)
      return short_name; // already qualified
    const std::string suffix = "::" + short_name;
    std::string fallback;
    for (const auto &kv : struct_decls_) {
      if (kv.first.size() > suffix.size() &&
          kv.first.substr(kv.first.size() - suffix.size()) == suffix) {
        // Prefer key with deeper qualification (longer key = more specific)
        if (fallback.empty() || kv.first.size() > fallback.size())
          fallback = kv.first;
      }
    }
    return fallback.empty() ? short_name : fallback;
  }

  // Given an unqualified class name, find its fully qualified name.
  std::string lookup_class_fqn(const std::string &short_name) const {
    if (short_name.find("::") != std::string::npos)
      return short_name;
    const std::string suffix = "::" + short_name;
    std::string fallback;
    for (const auto &kv : class_decls_) {
      if (kv.first.size() > suffix.size() &&
          kv.first.substr(kv.first.size() - suffix.size()) == suffix) {
        if (fallback.empty() || kv.first.size() > fallback.size())
          fallback = kv.first;
      }
    }
    return fallback.empty() ? short_name : fallback;
  }

  // Given an unqualified function name, find its fully qualified name.
  // Priority: current_module::name (local scope) > any qualified key ending
  // with ::name (imported/stdlib) > fallback to current_module::name.
  std::string lookup_function_fqn(const std::string &fname,
                                   const std::string &current_module) const {
    // Compiler/runtime builtins have no AST declaration to qualify. Their
    // process-wide names must remain stable when called from any module.
    if (fname == "panic")
      return fname;
    // 1. Try current_module + "::" + fname first (highest priority).
    //    This handles both unqualified names ("print" → "core::print" when
    //    module doesn't have it, but catches same-module functions first) and
    //    partially-qualified names ("server_config::default" inside
    //    "nyhttp::server" → "nyhttp::server::server_config::default").
    if (!current_module.empty()) {
      const std::string qualified = current_module + "::" + fname;
      if (function_decls_.count(qualified))
        return qualified;
    }
    // 2. If fname already contains "::" and is an exact match (fully qualified)
    if (fname.find("::") != std::string::npos && function_decls_.count(fname))
      return fname;
    // 3. Search for any qualified entry ending with "::fname"
    //    Prefer entries with more segments (more specific module).
    const std::string suffix = "::" + fname;
    std::string best;
    for (const auto &kv : function_decls_) {
      if (kv.first.size() > suffix.size() &&
          kv.first.substr(kv.first.size() - suffix.size()) == suffix) {
        if (best.empty() || kv.first.size() < best.size())
          best = kv.first; // prefer shortest (least ambiguous) qualified name
      }
    }
    if (!best.empty())
      return best;
    // 4. Fall back to current module qualification
    if (!current_module.empty())
      return current_module + "::" + fname;
    return fname;
  }

  // Search all registered enums for a unit variant (no associated type) with
  // the given bare name. Prefers qualified enum keys (e.g. "core::option")
  // so the emitted FQN ("core::option::none") is stable and consistent with
  // what IS_VARIANT expects. Falls back to bare keys if no qualified key found.
  // Returns true and sets out_fqn on success.
  bool lookup_unit_variant(const std::string &variant_name,
                           std::string &out_fqn) const {
    std::string fallback;
    for (const auto &kv : enum_decls_) {
      for (const auto &v : kv.second->variants) {
        if (v.name == variant_name && !v.associated_type.has_value()) {
          if (kv.first.find("::") != std::string::npos) {
            out_fqn = kv.first + "::" + variant_name;
            return true;
          } else if (fallback.empty()) {
            fallback = kv.first + "::" + variant_name;
          }
        }
      }
    }
    if (!fallback.empty()) {
      out_fqn = fallback;
      return true;
    }
    return false;
  }

  // Search all registered enums for a tuple variant (has associated type) with
  // the given bare name. Returns true and sets out_fqn on success.
  bool lookup_tuple_variant(const std::string &variant_name,
                            std::string &out_fqn) const {
    std::string fallback;
    for (const auto &kv : enum_decls_) {
      for (const auto &v : kv.second->variants) {
        if (v.name == variant_name && v.associated_type.has_value()) {
          if (kv.first.find("::") != std::string::npos) {
            out_fqn = kv.first + "::" + variant_name;
            return true;
          } else if (fallback.empty()) {
            fallback = kv.first + "::" + variant_name;
          }
        }
      }
    }
    if (!fallback.empty()) {
      out_fqn = fallback;
      return true;
    }
    return false;
  }

  // String constant pool management/mangling helpers
  std::string make_const_string_name(const std::string &value);
  std::string mangle_function_name(const std::string &fqn);
  std::string sanitize_identifier(const std::string &name);

private:
  friend class emitter;

  void reject_codegen_construct(int line, std::string message,
                                std::string hint);
  std::string materialize_struct_schema(const std::string &type,
                                        nil_module &module, int line,
                                        unsigned depth = 0,
                                        bool require_concrete = true);

  codegen_options options_;
  std::vector<codegen_diagnostic> rejected_codegen_constructs_;

  // Internal helpers:
public:
  std::unique_ptr<nil_module>
  assemble_module(const ast_program &program,
                  std::vector<codegen_diagnostic> *diags);

private:
  // Helper utilities to convert AST nodes to NIL instructions.
  void emit_function(const ast_function_decl &fn, nil_function &out_fn,
                     std::vector<codegen_diagnostic> *diags);

  // Small, local cache for constants and their assigned names
  std::unordered_map<std::string, std::string> string_const_map_;

  // Map of function declarations for lookup during specialization
  std::map<std::string, const ast_function_decl *> function_decls_;

  // Map of enum declarations for lookup during variant construction
  std::map<std::string, const ast_enum_decl *> enum_decls_;
  std::map<std::string, const ast_class_decl *> class_decls_;
  std::map<std::string, const ast_struct_decl *> struct_decls_;
  std::map<const ast_struct_decl *, std::string> struct_qualified_names_;

  // Map of global variable declarations (module-level let)
  std::map<std::string, const ast_variable_decl *> global_var_decls_;
  std::map<std::string, std::string> global_var_modules_;
  std::map<const ast_variable_decl *, std::string> global_storage_names_;
  std::map<const ast_node *, std::string> declaration_modules_;

  std::optional<std::string> lookup_global_name(const std::string &name,
                                               const std::string &module) const {
    if (!module.empty() && global_var_decls_.count(module + "::" + name))
      return module + "::" + name;
    if (global_var_decls_.count(name)) return name;
    return std::nullopt;
  }

  // Set of known global variable names (for emitter to check)
public:
  std::set<std::string> global_var_names_;
private:


  // Generic types may have separate inherent and trait impl blocks. Retain
  // every declaration so method specialization can select the impl that owns
  // the requested method instead of silently overwriting earlier traits.
  std::map<std::string, std::vector<const ast_impl_decl *>>
      generic_impl_decls_;

  // Generics support
  struct specialization_request {
    std::string generic_fn_name;
    std::vector<std::string> type_args;
    const ast_function_decl *decl_node;
  };

  // Impl method specialization request
  struct impl_method_specialization_request {
    std::string target_type;        // e.g., "vec"
    std::string method_name;        // e.g., "new"
    std::vector<std::string> type_args;  // e.g., ["i32"]
    const ast_impl_decl *impl_decl;
    const ast_function_decl *method_decl;
  };

  std::vector<specialization_request> pending_specializations_;
  std::vector<impl_method_specialization_request> pending_impl_specializations_;
  std::set<std::string> generated_specializations_; // mangled names

  // Generate a specialized version of a generic function
  void generate_specialization(const specialization_request &req,
                               nil_module &mod,
                               std::vector<codegen_diagnostic> *diags);

  // Generate a specialized version of a generic impl method
  void generate_impl_method_specialization(const impl_method_specialization_request &req,
                                           nil_module &mod,
                                           std::vector<codegen_diagnostic> *diags);

public:
  // Public for emitter access
  void queue_specialization(const std::string &generic_fn_name,
                            const std::vector<std::string> &type_args,
                            const ast_function_decl *decl_node) {
    // Basic mangling check to avoid duplicates
    // Note: This is a simple check; real check happens in
    // generate_specialization
    pending_specializations_.push_back({generic_fn_name, type_args, decl_node});
  }

  // Queue an impl method specialization
  void queue_impl_method_specialization(const std::string &target_type,
                                        const std::string &method_name,
                                        const std::vector<std::string> &type_args,
                                        const ast_impl_decl *impl_decl,
                                        const ast_function_decl *method_decl) {
    pending_impl_specializations_.push_back({target_type, method_name, type_args, impl_decl, method_decl});
  }

  // Look up the generic impl that owns a method on a target type.
  const ast_impl_decl *lookup_generic_impl(const std::string &target_type,
                                           const std::string &method_name);

  // Look up a method in a generic impl
  const ast_function_decl* lookup_impl_method(const ast_impl_decl *impl, const std::string &method_name);
};

// High-level convenience: compile an AST program and write NIL text or binary
// depending on options. Returns true on success; diags contains any emitted
// diagnostics.
bool codegen_compile_and_emit(const ast_program &program,
                              const std::filesystem::path &out_path,
                              const codegen_options &opts,
                              std::vector<codegen_diagnostic> *diags = nullptr);

} // namespace ny

#endif // NY_COMPILER_CODEGEN_HPP
