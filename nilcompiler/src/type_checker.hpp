#ifndef NY_COMPILER_TYPE_CHECKER_HPP
#define NY_COMPILER_TYPE_CHECKER_HPP

#include "ast.hpp"
#include "semantic_index.hpp"
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace ny {

// Type information structure
struct type_info {
  std::string name;
  bool is_primitive;
  // For future: generic args, struct fields, etc.

  bool operator==(const type_info &other) const { return name == other.name; }
  bool operator!=(const type_info &other) const { return !(*this == other); }
};

class type_checker : public ast_node::ast_visitor {
public:
  explicit type_checker(bool collect_semantics = false,
                        std::optional<completion_request> completion = std::nullopt,
                        std::optional<signature_request> signature = std::nullopt);
  const semantic_index &get_semantic_index() const { return semantics_; }
  const std::optional<completion_result> &get_completion() const { return completion_; }

  const std::optional<signature_result> &get_signature() const { return signature_; }

  // Entry point
  bool check(ast_program &program);

  // Diagnostics
  struct diagnostic {
    int line;
    std::string message;
    std::string source_file;
  };
  const std::vector<diagnostic> &get_diagnostics() const {
    return diagnostics_;
  }

  // Generic type helpers (public for testing)
  struct generic_type_parts {
    std::string base_name;
    std::vector<std::string> type_args;
  };
  generic_type_parts parse_generic_type_string(const std::string &type_name);

  // Instantiate a generic type with concrete type arguments.
  // e.g., "vec<T>" + {"T": "i32"} -> "vec<i32>"
  std::string instantiate_generic_type(
      const std::string &generic_type_name,
      const std::map<std::string, std::string> &substitutions);

  // Visitor methods
  void visit(ast_program &p) override;
  void visit(ast_function_decl &p) override;
  void visit(ast_variable_decl &p) override;
  void visit(ast_module_decl &p) override;
  void visit(ast_import_decl &p) override;
  void visit(ast_type_alias_decl &p) override;
  void visit(ast_struct_decl &p) override;
  void visit(ast_class_decl &p) override;
  void visit(ast_enum_decl &p) override;
  void visit(ast_trait_decl &p) override;
  void visit(ast_impl_decl &p) override;
  void visit(ast_test_block &p) override;

  void visit(ast_block_stmt &s) override;
  void visit(ast_expr_stmt &s) override;
  void visit(ast_println_stmt &s) override;
  void visit(ast_asm_stmt &s) override;
  void visit(ast_decl_stmt &s) override;
  void visit(ast_return_stmt &s) override;
  void visit(ast_if_stmt &s) override;
  void visit(ast_while_stmt &s) override;
  void visit(ast_for_in_stmt &s) override;
  void visit(ast_loop_stmt &s) override;
  void visit(ast_break_stmt &s) override;
  void visit(ast_continue_stmt &s) override;
  void visit(ast_match_stmt &s) override;
  void visit(ast_deallocate_stmt &s) override;
  void visit(ast_attempt_stmt &s) override;
  void visit(ast_fault_handler &h) override;
  void visit(ast_raise_stmt &s) override;
  void visit(ast_assert_expr &e) override;
  void visit(ast_hotreload_expr &e) override;

  void visit(ast_call_expr &e) override;
  void visit(ast_binary_expr &e) override;
  void visit(ast_unary_expr &e) override;
  void visit(ast_assignment_expr &e) override;
  void visit(ast_identifier_expr &e) override;
  void visit(ast_string_literal &e) override;
  void visit(ast_number_literal &e) override;
  void visit(ast_char_literal &e) override;
  void visit(ast_boolean_literal &e) override;
  void visit(ast_match_expr &e) override;
  void visit(ast_try_expr &e) override;
  void visit(ast_member_access_expr &e) override;
  void visit(ast_struct_literal_expr &e) override;
  void visit(ast_array_literal_expr &e) override;

  void visit(ast_index_expr &e) override;
  void visit(ast_allocate_expr &e) override;
  void visit(ast_ref_expr &e) override;
  void visit(ast_lambda_expr &e) override;
  void visit(ast_interpolated_string_expr &e) override;
  void visit(ast_cast_expr &e) override;
  void visit(ast_if_expr &e) override;

  // Helper to check if a type satisfies a trait bound
  bool check_trait_bound(const std::string &type_name,
                         const std::string &trait_name) const;

  // Helper to resolve imported symbols
  const symbol *lookup_imported(const std::string &name) const;

  // Module system helpers
  bool is_symbol_visible(const symbol *sym,
                         const std::string &from_module) const;
  const symbol *resolve_qualified_name(const std::string &qualified_path) const;
  std::optional<std::string>
  resolve_imported_symbol(const std::string &name) const;

  // Resolve a type-name in the context of the current module's imports.
  // This returns a candidate canonical/fully-qualified type name when an
  // unqualified type identifier (e.g., `user_id` or `mytext`) is present in
  // the current import map. The returned value is suitable for subsequent
  // canonicalization (via `symbol_table_.canonicalize_type_name`) and
  // type checking. Returns `std::nullopt` if no import-based resolution
  // could be performed for the supplied type name.
  //
  // Example:
  //  - Module `lib` includes `pub type mytext = text;` (registered as alias)
  //  - Module `client` has `import lib::{mytext}`
  //  - When `client` references `mytext` the symbol table and imports are
  //    consulted to resolve `mytext` to the canonical `String` underlying
  //    the alias.
  std::optional<std::string>
  resolve_type_name_with_imports(const std::string &type_name) const;

private:
  symbol_table symbols_;
  std::vector<diagnostic> diagnostics_;
  bool collect_semantics_ = false;
  std::optional<completion_request> completion_request_;
  std::optional<completion_result> completion_;
  void capture_completion(const ast_identifier_expr &expression);
  void capture_member_completion(const ast_member_access_expr &expression,
                                 const type_info &receiver);
  std::optional<signature_request> signature_request_;
  std::optional<signature_result> signature_;
  std::set<std::pair<std::size_t, std::size_t>> signature_visits_;
  bool select_signature_call(const ast_call_expr &call);
  void capture_signature(const ast_call_expr &call, const symbol &binding,
                         std::size_t parameter_offset, bool eligible);
  semantic_index semantics_;
  std::size_t semantic_bytes_ = 0;
  std::map<const ast_node *, std::size_t> semantic_nodes_;
  std::map<std::tuple<std::string, std::size_t, std::size_t>, std::size_t> semantic_declarations_;
  std::set<std::tuple<std::string, std::size_t, std::size_t, std::size_t>> semantic_occurrences_;
  void disable_semantics();
  std::optional<std::size_t> semantic_declaration(
      const std::string &name, const std::string &kind, const std::string &type,
      const std::string &path, const std::optional<source_span> &span);
  std::optional<std::size_t> semantic_node(const ast_node &node,
      const std::string &name, const std::string &kind, const std::string &type,
      const std::string &fallback_path = {});
  void semantic_reference_to(const ast_node &node, const std::string &name,
                             const symbol &binding);

  struct trait_implementation {
    std::string trait_name;
    std::string target_type;
    std::map<std::string, std::vector<std::string>> generic_bounds;
    std::vector<std::pair<std::string, std::string>> required_bounds;
    std::optional<std::string> module;
    int line = 0;
  };
  std::vector<trait_implementation> trait_impls_;
  // Required methods implement the trait's visible contract even when their
  // implementation omits `pub`; extra helpers retain declared visibility.
  std::map<const ast_function_decl *, visibility_kind> trait_method_visibility_;
  std::set<const ast_impl_decl *> visited_impls_;
  std::set<const ast_trait_decl *> visited_traits_;
  mutable std::set<std::pair<std::string, std::string>> checking_trait_bounds_;
  std::set<const ast_type_alias_decl *> expanding_type_aliases_;

  struct import_entry {
    std::string target_path; // fully-qualified path or '*' for glob
    visibility_kind visibility;
  };

  // Module system state
  std::optional<std::string> current_module_; // Current module being checked
  std::map<std::string, std::map<std::string, import_entry>>
      imports_; // importer -> {local_name -> entry}
  std::map<std::string, const ast_variable_decl *> global_declarations_;
  std::map<std::string, std::vector<const ast_import_decl *>> global_imports_;
  std::map<std::string, std::map<std::string, std::string>> global_namespace_aliases_;
  std::optional<std::string> resolve_source_global(const std::string &name,
                                                  int line);
  type_info resolve_global_type(const symbol &variable);

  struct class_member {
    const symbol *owner = nullptr;
    const ast_struct_field *field = nullptr;
    const ast_function_decl *method = nullptr;
  };
  const symbol *resolve_class_symbol(const std::string &name);
  class_member resolve_class_member(const symbol &receiver,
                                    const std::string &member, int line);
  std::vector<ast_struct_field> class_fields(const symbol &receiver, int line);
  bool class_assignable(const std::string &expected, const std::string &actual);
  bool type_contains_class(const std::string &type);
  bool type_contains_tuple(const std::string &type);
  bool check_assignment_type(const type_info &expected, const type_info &actual,
                             ast_expr *value);
  bool class_method_is_virtual(const symbol &receiver, const std::string &name);
  std::vector<ast_expr *> checked_class_conversions_;
  std::vector<ast_call_expr *> checked_class_calls_;
  bool check_reference_handle(ast_expr &reference, ast_expr &operand,
                              const type_info &operand_type);
  std::vector<ast_expr *> checked_reference_handles_;
  std::vector<ast_call_expr *> checked_parallel_calls_;
  bool check_runtime_primitive(ast_call_expr &call);

  // A template that formats T requires scalar/text at every instantiation.
  // Retain resolved call edges until forward declarations and wrappers have
  // all been checked; never revisit expression nodes to infer this contract.
  struct scalar_text_call {
    const ast_function_decl *caller = nullptr;
    const ast_function_decl *callee = nullptr;
    std::map<std::string, std::string> substitutions;
    std::set<std::string> open_types;
    int line = 0;
    std::string source_file;
  };
  const ast_function_decl *scalar_text_function_ = nullptr;
  std::map<const ast_function_decl *, std::set<std::string>> scalar_text_requirements_;
  std::vector<scalar_text_call> scalar_text_calls_;
  void record_scalar_text_call(const ast_call_expr &call, const symbol &callee,
                              const std::map<std::string, std::string> &substitutions);
  void validate_scalar_text_calls();

  // Current state
  std::optional<type_info> current_function_return_type_;
  std::optional<std::string> current_class_name_;
  std::optional<type_info> last_expr_type_; // Result of last expression visit
  // Trait bounds belonging to the impl currently being checked. Impl generic
  // parameters live outside each method's lexical scope in the AST, so keep
  // their contracts available while resolving calls such as `key.hash()`.
  std::map<std::string, std::vector<std::string>>
      active_impl_generic_bounds_;
  bool in_loop_ = false;
  bool checking_class_call_ = false;
  std::vector<ast_attempt_stmt *> checked_attempts_;
  std::vector<ast_fault_handler *> checked_fault_handlers_;
  std::vector<ast_raise_stmt *> checked_raises_;

  // Helpers
  std::string diagnostic_source_file_;
  void add_error(int line, const std::string &msg);
  type_info resolve_type(const std::string &name);
  std::optional<std::string> resolve_fault_struct(const std::string &name,
                                                 int line);
  bool are_types_compatible(const type_info &t1, const type_info &t2);
  std::string normalize_trait_type(
      const std::string &name,
      const std::map<std::string, std::string> &substitutions = {},
      unsigned depth = 0);
  const symbol *find_trait(const std::string &name);
  void register_trait_implementation(ast_impl_decl &impl);

  // Generic type helpers (member resolution is private)
  std::optional<std::string>
  resolve_member_type(const type_info &struct_type,
                      const std::string &member_name, int line = 0);

  // Instantiates a generic struct/enum definition if it doesn't already exist.
  // Creates a new declaration with substituted types and adds it to new_declarations_.
  // Returns true if a new instantiation was created, false if it already existed.
  bool instantiate_generic_node(const std::string &base_node_name,
                                  const std::string &instantiated_name,
                                  const std::vector<std::string> &type_args);

  // Validate that concrete type arguments satisfy generic parameter bounds.
  // Returns true if all bounds are satisfied, false otherwise (emits errors).
  bool validate_generic_args(const std::vector<ast_generic_param> &params,
                             const std::vector<std::string> &type_args,
                             int line);

  // Pattern helpers: infer a pattern's structural type (if any) and bind
  // variables found in a pattern into the current symbol table scope.
  // These helpers are used by the match-statement checking logic to create
  // variable symbols for pattern-bound identifiers and to infer/validate
  // element types for compound patterns.
  //
  // Infer the type represented by a pattern when possible; returns nullopt if
  // the pattern does not convey type information (e.g., variable binding).
  std::optional<type_info> infer_type_from_pattern(const ast_pattern &pattern);

  // Infer the type of a literal token string (returns i32/f64/String or
  // unknown)
  type_info infer_literal_type(const std::string &literal) const;

  // Bind pattern variables into the current symbol table scope with the
  // provided `inferred_type` as the canonical type for each bound variable.
  // This function is a no-op for purely literal or wildcard patterns.
  const symbol *resolve_pattern_type_symbol(std::string name) const;
  void bind_pattern_to_type(ast_pattern &pattern,
                            const type_info &inferred_type, int line = 0);

  // Built-in types
  type_info type_void{"void", true};
  type_info type_i32{"i32", true};
  type_info type_i64{"i64", true};
  type_info type_f64{"f64", true};
  type_info type_bool{"bool", true};
  type_info type_string{"String", true};
  type_info type_any{"any", true};
  type_info type_unknown{"<unknown>", true};

  // Store for newly instantiated generic nodes during checking
  std::vector<ast_decl_ptr> new_declarations_;
};

} // namespace ny

#endif // NY_COMPILER_TYPE_CHECKER_HPP
