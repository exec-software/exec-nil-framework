#ifndef NY_COMPILER_AST_HPP
#define NY_COMPILER_AST_HPP

// ast.hpp
// Abstract syntax tree definitions for nilcompiler.
//
// Conventions:
// - K&R underscore naming: all identifiers use lower_case + underscores.
// - Types are inside the ny:: namespace.
// - Basic visitor interface provided for AST processing.
// - Symbol table is included for early semantic analysis.

#include "lexer.hpp"
#include "annotation_arguments.hpp"
#include <cstddef>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ny {

// Half-open UTF-8 byte range in the original source, never preprocessed text.
struct source_span {
  std::size_t start = 0;
  std::size_t length = 0;
};

// Forward declarations
struct ast_node;
struct ast_expr;
struct ast_stmt;
struct ast_decl;
struct ast_import_decl;
struct ast_namespace_alias_decl;
struct ast_module_decl;

// Structured annotation unit stored in the AST. This represents a parsed
// annotation and carries:
//  - name: the annotation identifier (without leading '@')
//  - args: optional raw args between parentheses (e.g., `(key="v")`)
//  - line: optional origin line number (useful for diagnostics)
struct ast_annotation {
  std::string name;
  std::optional<std::string> args;
  int line = 0;
  // Optional because older metadata forms may contain opaque composite values.
  // Keep args unchanged for legacy consumers; these values preserve literal
  // boundaries and decoded text independently of that legacy spelling.
  std::optional<std::vector<annotation_argument>> typed_arguments;

  bool has_name(const std::string &identity) const {
    return name == identity || name == "@" + identity;
  }

  ast_annotation() = default;
  explicit ast_annotation(std::string n,
                          std::optional<std::string> a = std::nullopt,
                          int l = 0)
      : name(std::move(n)), args(std::move(a)), line(l) {}
};

// Helper: parse a raw annotation lexeme (like "@inline" or
// "@deprecated(message)" or "@ns::flag(key=value)") into a structured
// ast_annotation form. This is a conservative, minimal parser that extracts
// the name and the raw argument contents.
inline ast_annotation parse_annotation_lexeme(const std::string &lex,
                                              int line = 0) {
  std::string s = lex;
  if (!s.empty() && s.front() == '@')
    s.erase(0, 1);
  // Cut off trailing/leading whitespace
  auto trim_inplace = [](std::string &t) {
    const char *ws = " \t\r\n";
    size_t b = t.find_first_not_of(ws);
    size_t e = t.find_last_not_of(ws);
    if (b == std::string::npos) {
      t.clear();
      return;
    }
    t = t.substr(b, e - b + 1);
  };
  trim_inplace(s);

  std::optional<std::string> args = std::nullopt;
  // find '(' optionally; treat anything inside outermost parentheses as raw
  // args
  size_t p = s.find('(');
  if (p != std::string::npos) {
    std::string name = s.substr(0, p);
    trim_inplace(name);
    size_t close = s.rfind(')');
    if (close != std::string::npos && close > p) {
      std::string inner = s.substr(p + 1, close - p - 1);
      trim_inplace(inner);
      args = inner;
    } else {
      // malformed but capture everything after '('
      std::string inner = s.substr(p + 1);
      trim_inplace(inner);
      args = inner;
    }
    trim_inplace(name);
    return ast_annotation(std::move(name), args, line);
  } else {
    // No args; name might include a namespace delimiter '::'.
    std::string name = s;
    trim_inplace(name);
    return ast_annotation(std::move(name), std::nullopt, line);
  }
}

// Forward declare new statement node types (for/loop/break/continue).
struct ast_for_in_stmt;
struct ast_loop_stmt;
struct ast_break_stmt;
struct ast_continue_stmt;
struct ast_type_alias_decl;
struct ast_struct_decl;
struct ast_enum_decl;
struct ast_trait_decl;
struct ast_impl_decl;
struct ast_member_access_expr;

// Shared pointer aliases
using ast_node_ptr = std::shared_ptr<ast_node>;
using ast_expr_ptr = std::shared_ptr<ast_expr>;
using ast_stmt_ptr = std::shared_ptr<ast_stmt>;
using ast_decl_ptr = std::shared_ptr<ast_decl>;

// AST node kinds (coarse)
enum class ast_node_kind {
  program_node,

  // Declarations
  function_decl_node,
  variable_decl_node,
  module_decl_node,
  import_decl_node,
  namespace_alias_decl_node, // namespace alias: `namespace alias = path`
  type_alias_decl_node,
  struct_decl_node,
  class_decl_node,
  enum_decl_node,
  trait_decl_node,
  impl_decl_node,

  // Statements
  block_stmt_node,
  expr_stmt_node,
  println_stmt_node,
  asm_stmt_node,  // Added asm statement node
  decl_stmt_node, // Added declaration statement node
  return_stmt_node,
  if_stmt_node,
  while_stmt_node,
  for_in_stmt_node, // for <var> in <iterable>
  loop_stmt_node,   // infinite loop: loop ... end loop
  break_stmt_node,
  continue_stmt_node,
  match_stmt_node,
  switch_stmt_node,
  attempt_stmt_node,  // attempt ... fault ... recover ... end attempt
  fault_handler_node, // fault FaultType as e when condition
  raise_stmt_node,    // raise FaultInstance

  // Expressions
  call_expr_node,
  binary_expr_node,
  unary_expr_node,
  assignment_expr_node,
  identifier_expr_node,
  string_literal_node,
  number_literal_node,
  char_literal_node,
  boolean_literal_node,
  match_expr_node,
  switch_expr_node,
  try_expr_node, // expr? error propagation operator
  member_access_expr_node,
  struct_literal_expr_node, // Struct instantiation: Type { field: val, ... }
  array_literal_expr_node,  // [1, 2, 3]
  index_expr_node,          // arr[index]
  allocate_expr_node,       // allocate<T>(size)
  generic_param_node,       // <T: Trait>
  ref_expr_node,            // ref x
  lambda_expr_node,         // fn(params) -> T ... end fn
  interpolated_string_expr_node, // "Hello {name}"
  cast_expr_node,                // expr as Type
  deallocate_stmt_node,          // deallocate(ptr)
  assert_expr_node,              // assert.method(args) - intrinsic assertion
  hotreload_expr_node, // hotreload.method(args) - intrinsic hot reload
  if_expr_node, // if cond then expr else expr end if - conditional expression
  query_expr_node, // eager collect query over a checked array

  // Ny testing nodes
  test_block_node,
  inline_test_stmt_node,
};

// Base AST node
struct ast_node {
  ast_node_kind kind;
  int line;
  std::string source_file;
  std::optional<source_span> name_span;
  // Annotations collected for the node. This is public so callers (such as the
  // parser) can set annotations directly after creating AST nodes. The type is
  // a vector of structured annotation records allowing semantic analysis to
  // apply semantics based on annotation name/args.
  std::vector<ast_annotation> annotations;

  // Inferred type for this node, set by the type checker after type resolution.
  // Used by codegen for generic instantiation and other type-dependent
  // operations.
  std::optional<std::string> inferred_type;
  // A failed tuple shape/immutability check must not become executable when
  // --no-typecheck suppresses semantic diagnostics.
  std::optional<std::string> rejected_tuple_operation;

  explicit ast_node(ast_node_kind kind_in, int line_in = 0,
                    std::vector<ast_annotation> ann = {})
      : kind(kind_in), line(line_in), annotations(std::move(ann)) {}

  virtual ~ast_node() = default;

  // Visitor accept - implemented in derived types
  struct ast_visitor;
  virtual void accept(ast_visitor &v) = 0;

  // --- Annotation helper convenience methods -----------------------------
  // Append a structured annotation
  void add_annotation(const ast_annotation &a) { annotations.push_back(a); }

  // Convenience overload: accept raw annotation lexeme (e.g., \"@test\" or
  // \"@deprecated(msg)\") and parse it into a structured `ast_annotation`.
  void add_annotation(const std::string &lexeme, int line = 0) {
    annotations.push_back(parse_annotation_lexeme(lexeme, line));
  }

  // Returns true if the node has any annotations attached.
  bool has_annotations() const { return !annotations.empty(); }

  // Access read-only view of structured annotations.
  const std::vector<ast_annotation> &get_annotations() const {
    return annotations;
  }

  // Convenience accessor to retain compatibility with code that previously
  // consumed raw annotation lexemes: returns vector of annotation names.
  std::vector<std::string> get_annotation_names() const {
    std::vector<std::string> out;
    out.reserve(annotations.size());
    for (const auto &a : annotations)
      out.push_back(a.name);
    return out;
  }

  // Clear any annotations on this node.
  void clear_annotations() { annotations.clear(); }
};

// Visitor interface for AST nodes
struct ast_node::ast_visitor {
  virtual ~ast_visitor() = default;

  // Declarations
  virtual void visit(struct ast_program &p) {}
  virtual void visit(struct ast_function_decl &p) {}
  virtual void visit(struct ast_variable_decl &p) {}
  virtual void visit(struct ast_module_decl &p) {}
  virtual void visit(struct ast_import_decl &p) {}
  virtual void visit(struct ast_namespace_alias_decl &p) {}
  virtual void visit(struct ast_type_alias_decl &p) {}
  virtual void visit(ast_struct_decl &) {}
  virtual void visit(struct ast_class_decl &d) {}
  virtual void visit(ast_enum_decl &) {}
  virtual void visit(ast_trait_decl &) {}
  virtual void visit(ast_impl_decl &) {}

  // Statements
  virtual void visit(struct ast_block_stmt &s) {}
  virtual void visit(struct ast_expr_stmt &s) {}
  virtual void visit(struct ast_println_stmt &s) {}
  virtual void visit(struct ast_asm_stmt &s) {}
  virtual void visit(struct ast_decl_stmt &s) {
  } // Added visit method for decl_stmt
  virtual void visit(struct ast_return_stmt &s) {}
  virtual void visit(struct ast_if_stmt &s) {}
  virtual void visit(struct ast_while_stmt &s) {}
  virtual void visit(struct ast_for_in_stmt &s) {}
  virtual void visit(struct ast_loop_stmt &s) {}
  virtual void visit(struct ast_break_stmt &s) {}
  virtual void visit(struct ast_continue_stmt &s) {}
  virtual void visit(struct ast_match_stmt &s) {}
  virtual void visit(struct ast_switch_stmt &s) {}
  virtual void visit(struct ast_deallocate_stmt &s) {}
  virtual void visit(struct ast_fault_handler &s) {}
  virtual void visit(struct ast_attempt_stmt &s) {}
  virtual void visit(struct ast_raise_stmt &s) {}

  // Ny testing
  virtual void visit(struct ast_test_block &s) {}
  virtual void visit(struct ast_inline_test_stmt &s) {}

  // Expressions
  virtual void visit(struct ast_call_expr &e) {}
  virtual void visit(struct ast_binary_expr &e) {}
  virtual void visit(struct ast_unary_expr &e) {}
  virtual void visit(struct ast_assignment_expr &e) {}
  virtual void visit(struct ast_identifier_expr &e) {}
  virtual void visit(struct ast_string_literal &e) {}
  virtual void visit(struct ast_number_literal &e) {}
  virtual void visit(struct ast_char_literal &e) {}
  virtual void visit(struct ast_boolean_literal &e) {}
  virtual void visit(struct ast_match_expr &e) {}
  virtual void visit(struct ast_switch_expr &e) {}
  virtual void visit(struct ast_try_expr &e) {}
  virtual void visit(struct ast_member_access_expr &e) {}
  virtual void visit(struct ast_struct_literal_expr &e) {}
  virtual void visit(struct ast_array_literal_expr &e) {}

  virtual void visit(struct ast_index_expr &e) {}
  virtual void visit(struct ast_allocate_expr &e) {}
  virtual void visit(struct ast_ref_expr &e) {}
  virtual void visit(struct ast_lambda_expr &e) {}
  virtual void visit(struct ast_interpolated_string_expr &e) {}
  virtual void visit(struct ast_cast_expr &e) {}
  virtual void visit(struct ast_assert_expr &e) {}
  virtual void visit(struct ast_hotreload_expr &e) {}
  virtual void visit(struct ast_if_expr &e) {}
  virtual void visit(struct ast_query_expr &e) {}
};

enum class visibility_kind {
  visibility_private,
  visibility_public,
  visibility_crate, // pub(crate)
  visibility_super  // pub(super)
};

// Base classes for expression / statement / declaration
// Parameter labels are not part of structural fn(...) type identity. Retain
// them only when checking proves the callable value's immutable provenance.
struct callable_parameter_contract {
  std::vector<std::string> names;
  bool variadic = false;
};

struct ast_expr : public ast_node {
  // Allow expressions to optionally carry annotations though typically these
  // are attached to declarations/statements; this provides a flexible surface
  // for future features where expression-level annotations might be desired.

  // Type inferred by the type checker, used by codegen for method resolution.
  std::optional<std::string> inferred_type;
  // Proven directional class conversion at an explicitly typed boundary.
  // It must never be inferred from a codegen contextual type alone.
  std::optional<std::string> checked_class_upcast;
  // A typed boundary may box a concrete value without changing the type of
  // its expression. Only the checker may establish this source descriptor.
  std::optional<std::string> checked_any_box_type;
  std::optional<std::string> rejected_dynamic_operation;
  // REF_LOCAL aliases an existing nominal aggregate handle; it is not a
  // primitive stack reference. Only successful semantic checking grants this
  // proof, and any semantic error invalidates it before diagnostic continuation.
  bool checked_reference_handle = false;
  // A direct argument borrowing a mutable caller binding. This proof is
  // separate from an ordinary ref<T> aggregate handle and cannot escape a call.
  bool checked_binding_reference = false;
  // Affine ownership proof. This is erased when any semantic check fails.
  bool checked_ownership = false;
  std::optional<callable_parameter_contract> checked_callable_parameters;

  explicit ast_expr(ast_node_kind k, int l = 0,
                    std::vector<ast_annotation> ann = {})
      : ast_node(k, l, std::move(ann)) {}
};

struct ast_stmt : public ast_node {
  // Many statements can be annotated (e.g., tests, benchmarks, inline hints).
  // Provide an optional annotation vector to initialize the node at creation
  // time, and allow direct mutation via the public `annotations` member.
  explicit ast_stmt(ast_node_kind k, int l = 0,
                    std::vector<ast_annotation> ann = {})
      : ast_node(k, l, std::move(ann)) {}
};

struct ast_decl : public ast_node {
  // Visibility: expressive enum (pub, pub(crate), pub(super), private).
  // Mirrors the symbol table visibility enum and enables richer visibility
  // semantics in AST and tooling. We keep the legacy `is_pub` boolean for
  // backwards compatibility with existing code paths.
  visibility_kind visibility = visibility_kind::visibility_private;
  bool is_pub = false; // legacy compatibility flag

  // Primary constructor: explicit visibility-kind based.
  explicit ast_decl(ast_node_kind k, int l = 0,
                    std::vector<ast_annotation> ann = {},
                    visibility_kind vis = visibility_kind::visibility_private)
      : ast_node(k, l, std::move(ann)), visibility(vis),
        is_pub(vis == visibility_kind::visibility_public) {}

  // Legacy boolean constructor for compatibility with current call sites that
  // pass `bool is_pub`.
  explicit ast_decl(ast_node_kind k, int l, std::vector<ast_annotation> ann,
                    bool isp)
      : ast_decl(k, l, std::move(ann),
                 isp ? visibility_kind::visibility_public
                     : visibility_kind::visibility_private) {}

  // Helper accessor preserving prior API
  bool is_public() const {
    return is_pub || visibility == visibility_kind::visibility_public;
  }

  // New accessor returning the full visibility kind
  visibility_kind get_visibility() const { return visibility; }
};

// Program root
struct ast_program : public ast_node {
  std::vector<ast_decl_ptr> declarations;
  std::string filename; // Source filename from which this AST was parsed

  explicit ast_program(int line = 0)
      : ast_node(ast_node_kind::program_node, line) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Helper: generic parameter with optional bounds
struct ast_generic_param {
  std::string name;
  std::vector<std::string> bounds; // Trait names
};

// Helper: parameter tuple
struct ast_param {
  std::string name;
  std::optional<source_span> name_span;
  std::optional<std::string> type_name; // textual typing for MVP
  bool is_mut =
      false; // optional mut marker for variable params (e.g., for mut x)
  bool is_variadic = false; // true for rest/varargs params (e.g., ..args: T)
  bool is_ref = false; // aliases the caller's binding for this synchronous call
  bool checked_binding_reference = false;
  int line = 0;
  std::optional<std::shared_ptr<ast_expr>> default_value; // default parameter expression

  // Constructor to support initialization
  ast_param(const std::string& n,
            const std::optional<std::string>& t,
            bool m,
            int l,
            const std::optional<std::shared_ptr<ast_expr>>& d,
            bool va = false)
      : name(n), type_name(t), is_mut(m), is_variadic(va), line(l), default_value(d) {}

  // Default constructor
  ast_param() : name(""), type_name(std::nullopt), is_mut(false), is_variadic(false), line(0), default_value(std::nullopt) {}
};

// Declarations

// Function declaration
struct ast_function_decl : public ast_decl {
  bool checked_ownership = false;
  std::string name;
  std::vector<ast_param> params;
  std::vector<ast_generic_param> generic_params;
  std::optional<std::string> return_type;
  std::shared_ptr<ast_stmt> body; // typically a block

  // Ny testing integration
  std::vector<std::shared_ptr<ast_test_block>> test_blocks;
  std::vector<std::shared_ptr<ast_test_block>> example_blocks;
  std::optional<std::string> ai_context;

  bool checked_test_function = false;

  // Modifiers
  bool is_virtual = false;
  bool is_abstract = false;
  bool is_override = false;

  ast_function_decl(std::string n, std::vector<ast_param> p = {},
                    std::optional<std::string> ret = std::nullopt,
                    std::shared_ptr<ast_stmt> b = nullptr,
                    std::vector<ast_generic_param> gp = {}, bool isp = false,
                    int l = 0, std::vector<ast_annotation> ann = {},
                    bool is_virt = false, bool is_abs = false,
                    bool is_ovr = false)
      : ast_decl(ast_node_kind::function_decl_node, l, std::move(ann), isp),
        name(std::move(n)), params(std::move(p)), return_type(std::move(ret)),
        body(std::move(b)), generic_params(std::move(gp)), is_virtual(is_virt),
        is_abstract(is_abs), is_override(is_ovr) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

//
// Type alias declaration (e.g., `type alias = existing_type`)
//
struct ast_type_alias_decl : public ast_decl {
  std::string name;
  std::string target_type;
  std::vector<ast_generic_param> generic_params;

  ast_type_alias_decl(const std::string &n = "", const std::string &t = "",
                      std::vector<ast_generic_param> gp = {}, bool isp = false,
                      int l = 0)
      : ast_decl(ast_node_kind::type_alias_decl_node, l, {}, isp), name(n),
        target_type(t), generic_params(std::move(gp)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

struct ast_import_decl : public ast_decl {
  std::string module_path;        // e.g., 'std::io::read_file' or 'std::io'
  std::vector<std::string> names; // specific names imported (optional)
  bool is_glob = false;           // e.g., import std::io::*

  ast_import_decl(std::string mod_path, std::vector<std::string> nm = {},
                  bool glob = false, int l = 0,
                  visibility_kind vis = visibility_kind::visibility_private)
      : ast_decl(ast_node_kind::import_decl_node, l, {}, vis),
        module_path(std::move(mod_path)), names(std::move(nm)), is_glob(glob) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

struct ast_namespace_alias_decl : public ast_decl {
  std::string alias;         // alias name, e.g., 'alias'
  std::string target_module; // target module path, e.g., 'long::nested::name'

  ast_namespace_alias_decl(std::string a, std::string t, bool isp = false,
                           int l = 0)
      : ast_decl(ast_node_kind::namespace_alias_decl_node, l, {}, isp),
        alias(std::move(a)), target_module(std::move(t)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

//
// Module declaration (e.g., `module mylib::collections`)
//
struct ast_module_decl : public ast_decl {
  std::string name; // qualified module name

  // Accept a visibility flag `isp` (is public) and forward it to `ast_decl`
  // so that `is_pub` is available on the base declaration. We keep the
  // `l` (line) parameter as the trailing optional parameter for backward
  // compatibility with existing usage.
  explicit ast_module_decl(std::string n, bool isp = false, int l = 0)
      : ast_decl(ast_node_kind::module_decl_node, l, {}, isp),
        name(std::move(n)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Variable declaration (top-level or inside block)
struct ast_variable_decl : public ast_decl {
  std::string name;
  std::optional<std::string> type_name;
  ast_expr_ptr initializer; // optional
  bool is_mut = false;      // variable declared as `mut`
  bool is_const = false;    // variable declared as `const` (NY-050)
  bool rejected_dynamic_initializer = false;
  // Tuple destructuring keeps one initializer and distinct source bindings.
  std::vector<std::shared_ptr<ast_variable_decl>> tuple_bindings;

  ast_variable_decl(const std::string &n, ast_expr_ptr init = nullptr,
                    std::optional<std::string> type_in = std::nullopt,
                    bool mut = false, bool isp = false, int l = 0)
      : ast_decl(ast_node_kind::variable_decl_node, l, {}, isp), name(n),
        type_name(type_in), initializer(std::move(init)), is_mut(mut) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Struct field
struct ast_struct_field {
  std::string name;
  std::string type_name;
  int line;
  std::optional<source_span> name_span;
};

// Struct declaration
struct ast_struct_decl : public ast_decl {
  std::string name;
  std::vector<ast_struct_field> fields;
  std::vector<ast_generic_param> generic_params;
  // Type-checker-generated specialization metadata must not masquerade as a
  // source declaration with its lossy legacy identifier (e.g. Box_i64_).
  std::optional<std::string> concrete_generic_type;

  ast_struct_decl(std::string n, std::vector<ast_struct_field> f,
                  std::vector<ast_generic_param> gp = {}, bool isp = false,
                  int l = 0)
      : ast_decl(ast_node_kind::struct_decl_node, l, {}, isp),
        name(std::move(n)), fields(std::move(f)),
        generic_params(std::move(gp)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Enum variant
struct ast_enum_variant {
  std::string name;
  std::optional<std::string> associated_type; // For tagged unions (optional)
  int line;
};

// Enum declaration
struct ast_enum_decl : public ast_decl {
  std::string name;
  std::vector<ast_enum_variant> variants;
  std::vector<ast_generic_param> generic_params;

  ast_enum_decl(std::string n, std::vector<ast_enum_variant> v,
                std::vector<ast_generic_param> gp = {}, bool isp = false,
                int l = 0)
      : ast_decl(ast_node_kind::enum_decl_node, l, {}, isp), name(std::move(n)),
        variants(std::move(v)), generic_params(std::move(gp)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Trait declaration
struct ast_trait_decl : public ast_decl {
  std::string name;
  std::vector<ast_generic_param> generic_params;
  std::vector<std::shared_ptr<ast_function_decl>> methods; // Signatures only

  ast_trait_decl(std::string n, std::vector<ast_generic_param> gp = {},
                 std::vector<std::shared_ptr<ast_function_decl>> m = {},
                 bool isp = false, int l = 0)
      : ast_decl(ast_node_kind::trait_decl_node, l, {}, isp),
        name(std::move(n)), generic_params(std::move(gp)),
        methods(std::move(m)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Impl declaration
struct ast_impl_decl : public ast_decl {
  std::optional<std::string> trait_name; // Optional: if implementing a trait
  std::string target_type;
  std::vector<ast_generic_param> generic_params;
  std::vector<std::shared_ptr<ast_function_decl>> methods;

  ast_impl_decl(std::string target,
                std::optional<std::string> trait = std::nullopt,
                std::vector<ast_generic_param> gp = {},
                std::vector<std::shared_ptr<ast_function_decl>> m = {},
                int l = 0)
      : ast_decl(ast_node_kind::impl_decl_node, l, {}),
        trait_name(std::move(trait)), target_type(std::move(target)),
        generic_params(std::move(gp)), methods(std::move(m)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Statements

// Block statement
struct ast_block_stmt : public ast_stmt {
  std::vector<ast_stmt_ptr> statements;

  explicit ast_block_stmt(int l = 0)
      : ast_stmt(ast_node_kind::block_stmt_node, l) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Expression statement
struct ast_expr_stmt : public ast_stmt {
  ast_expr_ptr expression;

  ast_expr_stmt(ast_expr_ptr expr, int l = 0)
      : ast_stmt(ast_node_kind::expr_stmt_node, l),
        expression(std::move(expr)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// println statement (concrete)
struct ast_println_stmt : public ast_stmt {
  std::vector<ast_expr_ptr> args;

  ast_println_stmt(std::vector<ast_expr_ptr> a = {}, int l = 0)
      : ast_stmt(ast_node_kind::println_stmt_node, l), args(std::move(a)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Native assembly: asm "x86_64", "template" (inputs...) -> output end asm.
// The historical targetless form is parsed for a useful migration diagnostic.
struct ast_asm_stmt : public ast_stmt {
  std::string assembly_code;
  std::string target;
  std::vector<ast_expr_ptr> inputs;
  std::optional<std::string> output;

  ast_asm_stmt(std::string code, int l = 0)
      : ast_stmt(ast_node_kind::asm_stmt_node, l),
        assembly_code(std::move(code)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Class declaration: `class Name inherits Base1, Base2 { ... }`
struct ast_class_decl : public ast_decl {
  std::string name;
  std::vector<std::string> base_classes;
  std::vector<ast_struct_field> fields;
  std::vector<std::shared_ptr<ast_function_decl>> methods;
  std::vector<ast_generic_param> generic_params;
  bool is_abstract;

  // Exact checker-proven identity/layout. Codegen must not rediscover module
  // ownership or flatten unchecked inheritance by suffix matching.
  std::optional<std::string> checked_name;
  std::vector<ast_struct_field> checked_fields;
  std::set<std::string> checked_virtual_methods;

  ast_class_decl(std::string n, std::vector<std::string> bases,
                 std::vector<ast_struct_field> f,
                 std::vector<std::shared_ptr<ast_function_decl>> m,
                 std::vector<ast_generic_param> gp = {}, bool abstract = false,
                 int l = 0, std::vector<ast_annotation> ann = {})
      : ast_decl(ast_node_kind::class_decl_node, l, std::move(ann)),
        name(std::move(n)), base_classes(std::move(bases)),
        fields(std::move(f)), methods(std::move(m)),
        generic_params(std::move(gp)), is_abstract(abstract) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Declaration statement (wrapper for decls inside blocks)
struct ast_decl_stmt : public ast_stmt {
  ast_decl_ptr declaration;

  explicit ast_decl_stmt(ast_decl_ptr d, int l = 0)
      : ast_stmt(ast_node_kind::decl_stmt_node, l), declaration(std::move(d)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// return statement
struct ast_return_stmt : public ast_stmt {
  ast_expr_ptr value; // optional

  explicit ast_return_stmt(ast_expr_ptr val = nullptr, int l = 0)
      : ast_stmt(ast_node_kind::return_stmt_node, l), value(std::move(val)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// if statement
struct ast_if_stmt : public ast_stmt {
  ast_expr_ptr condition;
  std::shared_ptr<ast_stmt> then_branch;
  std::shared_ptr<ast_stmt> else_branch; // optional

  ast_if_stmt(ast_expr_ptr cond, std::shared_ptr<ast_stmt> then_b,
              std::shared_ptr<ast_stmt> else_b = nullptr, int l = 0)
      : ast_stmt(ast_node_kind::if_stmt_node, l), condition(std::move(cond)),
        then_branch(std::move(then_b)), else_branch(std::move(else_b)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// while statement
struct ast_while_stmt : public ast_stmt {
  ast_expr_ptr condition;
  std::shared_ptr<ast_stmt> body;

  ast_while_stmt(ast_expr_ptr cond, std::shared_ptr<ast_stmt> b, int l = 0)
      : ast_stmt(ast_node_kind::while_stmt_node, l), condition(std::move(cond)),
        body(std::move(b)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// For-in loop: `for <var> in <iterable> ... end for`
struct ast_for_in_stmt : public ast_stmt {
  // Single-variable loop (original form)
  ast_param variable;

  // Tuple-unpacking support: if `variables` is non-empty, the loop
  // is a tuple unpack: `for (a, b) in zip(...)` etc.
  std::vector<ast_param> variables;
  bool tuple_pattern = false; // Distinguishes `(x,)` from a single loop binding.

  ast_expr_ptr iterable;
  std::shared_ptr<ast_stmt> body;

  // Constructor for single-variable for loop
  ast_for_in_stmt(ast_param var, ast_expr_ptr it, std::shared_ptr<ast_stmt> b,
                  int l = 0)
      : ast_stmt(ast_node_kind::for_in_stmt_node, l), variable(std::move(var)),
        variables(), iterable(std::move(it)), body(std::move(b)) {}

  // Constructor for tuple-unpacking for loop
  explicit ast_for_in_stmt(std::vector<ast_param> vars, ast_expr_ptr it,
                           std::shared_ptr<ast_stmt> b, int l = 0)
      : ast_stmt(ast_node_kind::for_in_stmt_node, l),
        variable(vars.size() ? vars[0] : ast_param()),
        variables(std::move(vars)), iterable(std::move(it)),
        body(std::move(b)) {}

  // Convenience: true when destructuring a tuple of variables
  bool is_tuple_unpack() const { return !variables.empty(); }

  // Return the list of loop variables (either a single-element list for
  // legacy single-variable loops, or the tuple unpack list).
  std::vector<ast_param> get_variables() const {
    if (!variables.empty())
      return variables;
    return std::vector<ast_param>{variable};
  }

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Infinite loop: `loop ... end loop`
struct ast_loop_stmt : public ast_stmt {
  std::shared_ptr<ast_stmt> body;

  explicit ast_loop_stmt(std::shared_ptr<ast_stmt> b, int l = 0)
      : ast_stmt(ast_node_kind::loop_stmt_node, l), body(std::move(b)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

enum class break_target_kind { nearest, loop, switch_ };

// Break statement; a value is allowed only when targeting a value switch.
struct ast_break_stmt : public ast_stmt {
  break_target_kind target = break_target_kind::nearest;
  ast_expr_ptr value;
  const ast_node *checked_target = nullptr;
  explicit ast_break_stmt(int l = 0)
      : ast_stmt(ast_node_kind::break_stmt_node, l) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Continue statement: `continue`
struct ast_continue_stmt : public ast_stmt {
  explicit ast_continue_stmt(int l = 0)
      : ast_stmt(ast_node_kind::continue_stmt_node, l) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Deallocate statement: `deallocate(ptr)`
struct ast_deallocate_stmt : public ast_stmt {
  ast_expr_ptr ptr;

  explicit ast_deallocate_stmt(ast_expr_ptr p, int l = 0)
      : ast_stmt(ast_node_kind::deallocate_stmt_node, l), ptr(std::move(p)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Fault handler: represents a single fault handler clause
// Syntax: fault FaultType as variable [when condition]
struct ast_fault_handler : public ast_node {
  std::optional<std::string> fault_type; // optional fault type filter
  std::string variable;                  // variable name to bind fault to
  ast_expr_ptr guard;                    // optional when condition
  std::shared_ptr<ast_block_stmt> body;  // handler body
  // Exact checker-selected nominal identity; catch-all has no payload type.
  std::optional<std::string> checked_fault_type;
  bool fault_checked = false;

  ast_fault_handler(std::optional<std::string> type, std::string var,
                    ast_expr_ptr when_guard,
                    std::shared_ptr<ast_block_stmt> handler_body, int l = 0)
      : ast_node(ast_node_kind::fault_handler_node, l),
        fault_type(std::move(type)), variable(std::move(var)),
        guard(std::move(when_guard)), body(std::move(handler_body)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Attempt statement: structured fault handling
// Syntax:
//   attempt
//     <body>
//   fault FaultType as e [when condition]
//     <handler>
//   [recover
//     <cleanup>]
//   end attempt
struct ast_attempt_stmt : public ast_stmt {
  std::shared_ptr<ast_block_stmt> body;                     // attempt block
  std::vector<std::shared_ptr<ast_fault_handler>> handlers; // fault handlers
  std::shared_ptr<ast_block_stmt> recover; // optional recover block
  bool fault_checked = false;

  ast_attempt_stmt(
      std::shared_ptr<ast_block_stmt> attempt_body,
      std::vector<std::shared_ptr<ast_fault_handler>> fault_handlers,
      std::shared_ptr<ast_block_stmt> recover_block, int l = 0)
      : ast_stmt(ast_node_kind::attempt_stmt_node, l),
        body(std::move(attempt_body)), handlers(std::move(fault_handlers)),
        recover(std::move(recover_block)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Ny testing: test block
enum class test_type { unit, example, property, integration };

struct ast_test_block : public ast_decl {
  bool checked_test = false;
  std::string description;
  std::shared_ptr<ast_block_stmt> body;
  test_type type;

  // Property testing metadata
  // Vector of generators is stored if this is a property test
  // For now we store generic params, later we will parse specific generator
  // syntax
  std::vector<ast_annotation> generators;

  ast_test_block(std::string desc, std::shared_ptr<ast_block_stmt> b,
                 test_type t = test_type::unit, int l = 0)
      : ast_decl(ast_node_kind::test_block_node, l),
        description(std::move(desc)), body(std::move(b)), type(t) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};
// Inline test regions retain the enclosing frame, but have their own scope.
struct ast_inline_test_stmt : public ast_stmt {
  std::shared_ptr<ast_test_block> test;
  bool checked_test = false;
  explicit ast_inline_test_stmt(std::shared_ptr<ast_test_block> value, int line = 0)
      : ast_stmt(ast_node_kind::inline_test_stmt_node, line), test(std::move(value)) {}
  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Syntax: raise FaultExpression
struct ast_raise_stmt : public ast_stmt {
  ast_expr_ptr fault_expr; // expression evaluating to fault
  std::optional<std::string> checked_fault_type;
  const ast_fault_handler *checked_rethrow_handler = nullptr;
  bool fault_checked = false;

  explicit ast_raise_stmt(ast_expr_ptr expr, int l = 0)
      : ast_stmt(ast_node_kind::raise_stmt_node, l),
        fault_expr(std::move(expr)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Expressions

// Identifier expression
struct ast_identifier_expr : public ast_expr {
  std::string name;
  // Checker-selected module-level storage identity. Unlike source spelling,
  // this remains exact through imports, aliases and generic specialization.
  // An empty value records ambiguous global resolution; it must not fall back
  // to a function or another spelling when checker errors are ignored.
  std::optional<std::string> resolved_global_name;

  ast_identifier_expr(std::string n, int l = 0)
      : ast_expr(ast_node_kind::identifier_expr_node, l), name(std::move(n)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// String literal
struct ast_string_literal : public ast_expr {
  std::string value;

  ast_string_literal(std::string v, int l = 0)
      : ast_expr(ast_node_kind::string_literal_node, l), value(std::move(v)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Number literal
struct ast_number_literal : public ast_expr {
  std::string value; // Keep textual representation for precise parsing later
                     // (bigint, decimal, etc.)

  ast_number_literal(std::string v, int l = 0)
      : ast_expr(ast_node_kind::number_literal_node, l), value(std::move(v)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Binary expression (e.g., a + b)
// Boolean literal
struct ast_boolean_literal : public ast_expr {
  bool value;

  ast_boolean_literal(bool v, int l = 0)
      : ast_expr(ast_node_kind::boolean_literal_node, l), value(v) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

struct ast_binary_expr : public ast_expr {
  ast_expr_ptr left;
  token_kind op; // operator token kind (lexed)
  ast_expr_ptr right;

  ast_binary_expr(ast_expr_ptr l, token_kind o, ast_expr_ptr r, int ln = 0)
      : ast_expr(ast_node_kind::binary_expr_node, ln), left(std::move(l)),
        op(o), right(std::move(r)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Unary expression (e.g., -a, !b)
struct ast_unary_expr : public ast_expr {
  token_kind op;
  ast_expr_ptr operand;

  ast_unary_expr(token_kind o, ast_expr_ptr opd, int ln = 0)
      : ast_expr(ast_node_kind::unary_expr_node, ln), op(o),
        operand(std::move(opd)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Call expression: callee(args...) or callee<type_args>(args...)
// Cast expression: expr as Type
struct ast_cast_expr : public ast_expr {
  ast_expr_ptr operand;
  std::string target_type;
  bool checked_identity = false; // Only the type checker may prove erasure safe.
  bool checked_any_cast = false;

  ast_cast_expr(ast_expr_ptr op, std::string type, int ln = 0)
      : ast_expr(ast_node_kind::cast_expr_node, ln), operand(std::move(op)),
        target_type(std::move(type)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// =============================================================================
// Assert Expression (Intrinsic)
// =============================================================================

// Assert intrinsic: assert.method(args)
// Examples: assert.true(x > 0), assert.equal(a, b), assert.contains(vec, item)
struct ast_assert_expr : public ast_expr {
  std::string method; // Assertion method: "true", "equal", "contains", etc.
  std::vector<ast_expr_ptr> args; // Arguments to the assertion
  bool checked_assertion = false;
  std::string inferred_type;      // Type inference result (usually void)

  ast_assert_expr(std::string m, std::vector<ast_expr_ptr> a, int ln = 0)
      : ast_expr(ast_node_kind::assert_expr_node, ln), method(std::move(m)),
        args(std::move(a)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

struct ast_hotreload_expr : public ast_expr {
  std::string method; // Hot reload method: "enable", "disable", "version", etc.
  std::vector<ast_expr_ptr> args; // Arguments to the hot reload method
  std::string inferred_type;      // Type inference result

  ast_hotreload_expr(std::string m, std::vector<ast_expr_ptr> a, int ln = 0)
      : ast_expr(ast_node_kind::hotreload_expr_node, ln), method(std::move(m)),
        args(std::move(a)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// If expression (ternary-style): if cond then expr else expr end if
struct ast_if_expr : public ast_expr {
  ast_expr_ptr condition; // The condition expression
  ast_expr_ptr then_expr; // Expression if condition is true
  ast_expr_ptr else_expr; // Expression if condition is false

  ast_if_expr(ast_expr_ptr cond, ast_expr_ptr then_e, ast_expr_ptr else_e,
              int ln = 0)
      : ast_expr(ast_node_kind::if_expr_node, ln), condition(std::move(cond)),
        then_expr(std::move(then_e)), else_expr(std::move(else_e)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

struct ast_call_expr : public ast_expr {
  // Compiler-owned operations; ordinary user calls never acquire this proof.
  std::optional<std::string> checked_any_type_test;
  std::string ownership_operation;
  std::optional<std::string> parallel_worker;
  std::string parallel_input_type;
  std::string parallel_output_type;
  ast_expr_ptr callee;
  std::vector<ast_expr_ptr> args;
  // Matrix named arguments retain source labels until the checker resolves
  // the actual callable. Empty entries represent a positional variadic tail.
  // An empty vector means an ordinary positional call.
  std::vector<std::string> argument_names;
  // Native source argument region and source-order slots for signature help.
  // They remain in written order when checking reorders Matrix named args.
  std::optional<source_span> argument_span;
  std::vector<std::size_t> argument_starts;
  std::vector<std::string> source_argument_names;
  std::vector<std::string> type_args; // Explicit type arguments for generics
  // Checked associated call on a bounded type parameter (for example
  // T.default()). The receiver is a type, never a runtime argument.
  std::optional<std::string> static_receiver_type;
  // Checked static view and selected declaring owner for class dispatch.
  std::optional<std::string> concrete_class_receiver;
  std::optional<std::string> class_method_owner;
  bool class_virtual_call = false;
  bool class_instance_call = false;
  // Canonical non-receiver parameter types retained for exact call-boundary
  // conversion; argument expression types must not stand in for the signature.
  std::vector<std::string> class_argument_types;

  ast_call_expr(ast_expr_ptr c, std::vector<ast_expr_ptr> a = {},
                std::vector<std::string> ta = {}, int ln = 0)
      : ast_expr(ast_node_kind::call_expr_node, ln), callee(std::move(c)),
        args(std::move(a)), type_args(std::move(ta)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Assignment expression (x = y)
struct ast_assignment_expr : public ast_expr {
  ast_expr_ptr target; // should be identifier_expr or index_expr later
  ast_expr_ptr value;

  ast_assignment_expr(ast_expr_ptr t, ast_expr_ptr v, int ln = 0)
      : ast_expr(ast_node_kind::assignment_expr_node, ln), target(std::move(t)),
        value(std::move(v)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Member access expression (object.member)
struct ast_member_access_expr : public ast_expr {
  ast_expr_ptr object;
  std::string member_name;

  ast_member_access_expr(ast_expr_ptr obj, std::string mem, int ln = 0)
      : ast_expr(ast_node_kind::member_access_expr_node, ln),
        object(std::move(obj)), member_name(std::move(mem)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Struct literal expression: Type { field: val, ... }
struct ast_struct_literal_expr : public ast_expr {
  std::string type_name;
  std::vector<std::pair<std::string, ast_expr_ptr>> fields;
  std::vector<std::string> type_args; // Generic type arguments: Type<T> { ... }
  bool tuple_literal = false; // Structural immutable tuple, fields ordered 0..N-1.

  ast_struct_literal_expr(std::string name,
                          std::vector<std::pair<std::string, ast_expr_ptr>> f,
                          std::vector<std::string> ta = {}, int ln = 0)
      : ast_expr(ast_node_kind::struct_literal_expr_node, ln),
        type_name(std::move(name)), fields(std::move(f)),
        type_args(std::move(ta)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Array literal expression: [e1, e2, ...]
struct ast_array_literal_expr : public ast_expr {
  std::vector<ast_expr_ptr> elements;

  ast_array_literal_expr(std::vector<ast_expr_ptr> el, int ln = 0)
      : ast_expr(ast_node_kind::array_literal_expr_node, ln),
        elements(std::move(el)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Both frontends share query binding, typing and eager evaluation semantics.
// Bounds are evaluated outside the binder's scope; row expressions are not
// closures and never outlive this expression. Checked types are required even
// when callers request diagnostic continuation with --no-typecheck.
struct ast_query_expr : public ast_expr {
  ast_param binding;
  ast_expr_ptr source, predicate, order_key, skip, take, projection;
  bool descending = false;
  bool checked_query = false;
  std::string element_type, result_element_type, key_type;

  explicit ast_query_expr(int line = 0)
      : ast_expr(ast_node_kind::query_expr_node, line) {}
  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Index expression: array[index]
struct ast_index_expr : public ast_expr {
  ast_expr_ptr object;
  ast_expr_ptr index;

  ast_index_expr(ast_expr_ptr obj, ast_expr_ptr idx, int ln = 0)
      : ast_expr(ast_node_kind::index_expr_node, ln), object(std::move(obj)),
        index(std::move(idx)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Pattern matching: structural pattern kinds & AST node for structured patterns
enum class ast_pattern_kind {
  wildcard_pattern,
  literal_pattern,
  variable_pattern,
  constructor_pattern
};

struct ast_pattern {
  ast_pattern_kind kind = ast_pattern_kind::wildcard_pattern;
  std::string name; // variable/constructor name; '_' indicates wildcard
  std::string literal_value; // if literal pattern
  // Preserve lexical identity: quoted "12" and "true" are text, not scalars.
  ast_node_kind literal_kind = ast_node_kind::number_literal_node;
  std::vector<ast_pattern>
      subpatterns; // nested pattern elements (for constructors / tuples)

  ast_pattern() = default;
};

// Match arm used by statement-style match branches: (pattern -> body)
// optionally with guard.
struct ast_match_arm {
  ast_pattern pattern;
  std::shared_ptr<ast_stmt> body; // block or single statement
  ast_expr_ptr guard;             // optional guard condition (null if none)

  ast_match_arm(ast_pattern p, std::shared_ptr<ast_stmt> b,
                ast_expr_ptr g = nullptr)
      : pattern(std::move(p)), body(std::move(b)), guard(std::move(g)) {}
};

// Match statement (structured) - statement-level match: `match <expr> { ... }`
// or `match ... end match`
struct ast_match_stmt : public ast_stmt {
  ast_expr_ptr target;
  std::vector<ast_match_arm> arms;

  ast_match_stmt(ast_expr_ptr t, std::vector<ast_match_arm> a, int ln = 0)
      : ast_stmt(ast_node_kind::match_stmt_node, ln), target(std::move(t)),
        arms(std::move(a)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// A null label is the default; arms execute in source order after dispatch.
struct ast_switch_arm {
  ast_expr_ptr label;
  ast_stmt_ptr body;
  int line = 0;
  // A checked, payload-free enum label uses its canonical variant identity.
  std::string enum_variant;
};

struct ast_switch_stmt : public ast_stmt {
  ast_expr_ptr target;
  std::vector<ast_switch_arm> arms;
  bool checked_switch = false;
  ast_switch_stmt(ast_expr_ptr t, std::vector<ast_switch_arm> a, int ln = 0)
      : ast_stmt(ast_node_kind::switch_stmt_node, ln), target(std::move(t)), arms(std::move(a)) {}
  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

struct ast_switch_expr : public ast_expr {
  ast_expr_ptr target;
  std::vector<ast_switch_arm> arms;
  bool checked_switch = false;
  ast_switch_expr(ast_expr_ptr t, std::vector<ast_switch_arm> a, int ln = 0)
      : ast_expr(ast_node_kind::switch_expr_node, ln), target(std::move(t)), arms(std::move(a)) {}
  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Match case (helper struct, not a full node for now, but could be)
struct ast_match_case {
  ast_expr_ptr pattern;
  ast_expr_ptr guard;             // optional guard condition
  std::shared_ptr<ast_stmt> body; // block or single statement

  ast_match_case(ast_expr_ptr p, ast_expr_ptr g, std::shared_ptr<ast_stmt> b)
      : pattern(std::move(p)), guard(std::move(g)), body(std::move(b)) {}
};

// Match expression: match target { case ... }
struct ast_match_expr : public ast_expr {
  ast_expr_ptr target;
  std::vector<ast_match_case> cases;

  ast_match_expr(ast_expr_ptr t, std::vector<ast_match_case> c, int ln = 0)
      : ast_expr(ast_node_kind::match_expr_node, ln), target(std::move(t)),
        cases(std::move(c)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Try expression for error propagation (expr?)
// Represents the ? operator used with Result<T,E> and Option<T>
struct ast_try_expr : public ast_expr {
  ast_expr_ptr inner; // Expression being tried

  explicit ast_try_expr(ast_expr_ptr e, int l = 0)
      : ast_expr(ast_node_kind::try_expr_node, l), inner(std::move(e)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// ------------------------
// Symbol Table
// ------------------------

enum class symbol_kind { variable_symbol, function_symbol, type_symbol };

/* visibility_kind already defined earlier in this file to support AST and
   symbol types. */

struct symbol {
  std::string name;
  symbol_kind kind;
  ast_node *defining_node; // The AST node that declared this symbol
  std::optional<std::string> type_name; // textual type if available
  // Analysis-only retained declaration identity, including parameters which
  // have no ast_node. Copies retain identity after their lexical scope exits.
  std::optional<std::size_t> semantic_id;
  std::optional<std::string>
      module; // Optional module path owning this symbol (e.g., "std::io")
  bool is_mut = false; // Indicates when a variable symbol is mutable
  bool is_pub = false; // Backwards-compatible boolean visibility flag
  bool is_generic_parameter = false;
  visibility_kind visibility =
      visibility_kind::visibility_private; // New enum-based visibility

  std::vector<ast_generic_param> generic_params; // Generic parameters if any
  std::vector<ast_param> params;                 // Function parameters if any
  std::optional<callable_parameter_contract> checked_callable_parameters;
  std::vector<std::string> bounds; // Trait bounds if any (for generic params)
  std::unordered_map<std::string, symbol>
      members; // Members of the symbol (e.g. methods of a struct/enum)

  symbol() = default;

  // Preferred constructor taking explicit visibility enumeration so callers can
  // specify precise visibility semantics beyond a boolean `pub` flag.
  symbol(std::string n, symbol_kind k, ast_node *node = nullptr,
         std::optional<std::string> t = std::nullopt, bool mut = false,
         visibility_kind vis = visibility_kind::visibility_private,
         std::optional<std::string> mod = std::nullopt,
         std::vector<ast_generic_param> gp = {}, std::vector<ast_param> p = {},
         std::vector<std::string> b = {})
      : name(std::move(n)), kind(k), defining_node(std::move(node)),
        type_name(t), module(mod), is_mut(mut),
        is_pub(vis == visibility_kind::visibility_public), visibility(vis),
        generic_params(std::move(gp)), params(std::move(p)),
        bounds(std::move(b)) {}

  // Backwards-compatible boolean constructor delegating to enum-based
  // visibility constructor. This resolves ambiguity for legacy sites that used
  // a boolean `pub` argument while steering the data model towards enum-based
  // visibility.
  symbol(std::string n, symbol_kind k, ast_node *node = nullptr,
         std::optional<std::string> t = std::nullopt, bool mut = false,
         bool pub = false, std::optional<std::string> mod = std::nullopt)
      : symbol(std::move(n), k, node, t, mut,
               pub ? visibility_kind::visibility_public
                   : visibility_kind::visibility_private,
               mod) {}

  // Convenience 2-arg constructor to avoid ambiguous overloads when callers
  // create symbols with only `name` and `kind` (e.g. `symbol("i8",
  // symbol_kind::type_symbol)`). Delegates to the primary constructor with
  // default visibility_private.
  symbol(const std::string &n, symbol_kind k)
      : symbol(std::string(n), k, nullptr, std::nullopt, false,
               visibility_kind::visibility_private, std::nullopt, {}, {}, {}) {}

  // Backwards-compatible constructor accepting a boolean `pub` flag.
  // This delegates to the legacy semantics and sets the visibility enum
  // accordingly for new tooling that expects it.
  // Backwards-compatible boolean constructor removed.
  // Use the explicit visibility-kind signature:
  // symbol(std::string n, symbol_kind k, ast_node_ptr node = nullptr,
  //        std::optional<std::string> t = std::nullopt, bool mut = false,
  //        visibility_kind vis = visibility_kind::visibility_private,
  //        std::optional<std::string> mod = std::nullopt)
  // The convenience 2-arg constructor and the full-arg constructor remain.
};

class symbol_table {
public:
  explicit symbol_table() { enter_scope(); }

  // Enter a new lexical scope
  void enter_scope() { scopes.emplace_back(); }

  // Leave the current lexical scope
  void leave_scope() {
    if (!scopes.empty())
      scopes.pop_back();
  }

  // Insert a new symbol into the current scope; returns false if name already
  // present
  bool insert_symbol(const symbol &s) {
    if (scopes.empty())
      enter_scope();
    auto &current = scopes.back();
    if (current.find(s.name) != current.end())
      return false;
    current.emplace(s.name, s);
    return true;
  }

  // Convenience wrapper to insert a type alias directly into the current scope.
  // This helps callers avoid creating a symbol by-hand when they want to
  // register an alias such as `text -> String`.
  bool insert_type_alias(const std::string &alias,
                         const std::string &target_type) {
    // Use the module-aware alias insertion helper for consistent symbol
    // creation (defaults to private visibility in the current module).
    return insert_type_alias_in_module(
        alias, target_type, visibility_kind::visibility_private, std::nullopt);
  }

  // Helper to insert a symbol with context (module, visibility, etc.)
  bool insert_symbol_with_context(
      const std::string &name, symbol_kind kind, ast_node *node = nullptr,
      std::optional<std::string> type_in = std::nullopt, bool is_mut = false,
      visibility_kind visibility = visibility_kind::visibility_private,
      std::optional<std::string> module = std::nullopt,
      std::vector<ast_generic_param> gp = {}, std::vector<ast_param> p = {},
      std::vector<std::string> bounds = {}) {
    // Construct the symbol using the enum-based visibility constructor to
    // avoid ambiguity between boolean and enum overloads. This ensures the
    // `symbol` instance sees the correct typing for visibility.
    symbol s(name, kind, node, type_in, is_mut, visibility, module, gp, p,
             bounds);
    return insert_symbol(s);
  }

  // Overload for inserting with explicit visibility enum
  bool
  insert_symbol_with_context(const std::string &name, symbol_kind kind,
                             ast_node *node, std::optional<std::string> type_in,
                             bool is_mut, bool is_pub,
                             std::optional<std::string> module = std::nullopt) {
    visibility_kind vis = is_pub ? visibility_kind::visibility_public
                                 : visibility_kind::visibility_private;
    return insert_symbol_with_context(name, kind, node, type_in, is_mut, vis,
                                      module);
  }

  // Convenience wrapper to insert a type alias with an explicit module context.
  // Accept `visibility_kind` for explicit visibility control and also provide
  // a backward-compatible bool overload.
  bool insert_type_alias_in_module(
      const std::string &alias, const std::string &target_type,
      visibility_kind visibility = visibility_kind::visibility_private,
      std::optional<std::string> module = std::nullopt) {
    // Construct alias symbol using enum visibility; keep module context and
    // register the alias in the symbol table.
    symbol s(alias, symbol_kind::type_symbol, nullptr,
             std::optional<std::string>(target_type), false, visibility,
             module);
    return insert_symbol(s);
  }

  // Bool wrapper overload -> maps to visibility enum
  bool insert_type_alias_in_module(
      const std::string &alias, const std::string &target_type, bool is_pub,
      std::optional<std::string> module = std::nullopt) {
    visibility_kind vis = is_pub ? visibility_kind::visibility_public
                                 : visibility_kind::visibility_private;
    return insert_type_alias_in_module(alias, target_type, vis, module);
  }

  // Lookup symbol in nested scopes (closest to farthest). Returns nullptr if
  // not found.
  const symbol *lookup(const std::string &name) const {
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
      auto found = it->find(name);
      if (found != it->end()) {
        return &found->second;
      }
    }
    return nullptr;
  }

  // Type identity is independent of value bindings with the same spelling.
  // Stop at the nearest type declaration, including a generic type parameter;
  // an unproven parameter must not expose an outer nominal declaration.
  const symbol *lookup_type(const std::string &name) const {
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
      const auto found = it->find(name);
      if (found != it->end() && found->second.kind == symbol_kind::type_symbol)
        return &found->second;
    }
    return nullptr;
  }

  // Declaration checks must distinguish this lexical scope from a visible
  // outer symbol; looking up and updating the latter changes its contract.
  const symbol *lookup_current_scope(const std::string &name) const {
    if (scopes.empty())
      return nullptr;
    const auto found = scopes.back().find(name);
    return found == scopes.back().end() ? nullptr : &found->second;
  }

  // Snapshot the nearest binding of each spelling without extending the
  // lifetime of a lexical scope. Used only by bounded editor queries.
  std::vector<const symbol *> visible_symbols() const {
    std::set<std::string> seen;
    std::vector<const symbol *> result;
    for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope)
      for (const auto &[name, value] : *scope)
        if (seen.insert(name).second) result.push_back(&value);
    return result;
  }

  // Function parameters, pattern bindings and lexical locals live outside
  // the first/global scope. Global enum-variant aliases are deliberately not
  // included: unlike variables, those aliases do not own runtime storage.
  const symbol *lookup_non_global(const std::string &name) const {
    for (std::size_t index = scopes.size(); index > 1; --index) {
      const auto found = scopes[index - 1].find(name);
      if (found != scopes[index - 1].end())
        return &found->second;
    }
    return nullptr;
  }

  // Mutable lookup for internal compiler passes (e.g. type checker populating
  // members)
  symbol *lookup_mut(const std::string &name) {
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
      auto found = it->find(name);
      if (found != it->end()) {
        return &found->second;
      }
    }
    return nullptr;
  }

  // Find a TYPE symbol by its short name wherever it was declared.
  //
  // A pass that attaches implementation methods to a type must reach the
  // type's own symbol, and an extension names its target the way the author
  // wrote it -- unqualified -- while the declaration lives under its own
  // module's prefix. Exact lookup therefore misses whenever a book extends a
  // type from another book. The deepest match wins, matching the equivalent
  // resolution in codegen.
  symbol *lookup_type_suffix_mut(const std::string &short_name) {
    if (short_name.find("::") != std::string::npos)
      return lookup_mut(short_name);
    const std::string suffix = "::" + short_name;
    symbol *best = nullptr;
    std::size_t best_length = 0;
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
      for (auto &entry : *it) {
        if (entry.second.kind != symbol_kind::type_symbol)
          continue;
        const std::string &key = entry.first;
        if (key.size() > suffix.size() &&
            key.compare(key.size() - suffix.size(), suffix.size(), suffix) == 0 &&
            key.size() > best_length) {
          best = &entry.second;
          best_length = key.size();
        }
      }
    }
    return best;
  }

  // Lookup a symbol by name and check whether it is visible to a consumer in
  // `current_module` (if provided). A symbol is considered visible if it is
  // public (`is_pub == true`) or if it resides in the same module as the
  // `current_module` argument.
  const symbol *lookup_visible(
      const std::string &name,
      const std::optional<std::string> &current_module = std::nullopt) const {
    const symbol *s = lookup(name);
    if (!s)
      return nullptr;

    // Public symbols are always visible.
    if (s->is_pub)
      return s;

    // Symbols declared within the same module are visible to each other.
    if (current_module.has_value() && s->module.has_value() &&
        *current_module == *s->module)
      return s;

    // Otherwise, not visible from the requested module.
    return nullptr;
  }

  // Return the direct alias target if the symbol is a type alias and has a
  // recorded target type. Example: if `text` is an alias to `String` this
  // returns "String" for `lookup_type_alias("text")`.
  //
  // This does not perform recursive alias resolution; for canonicalization use
  // `canonicalize_type_name`.
  std::optional<std::string> lookup_type_alias(const std::string &name) const {
    const symbol *s = lookup(name);
    if (!s)
      return std::nullopt;
    if (s->kind != symbol_kind::type_symbol)
      return std::nullopt;
    return s->type_name;
  }

  // Canonicalize a textual type name by following alias chains (if any) and
  // return the final resolved type name. Example:
  //   "mytext" -> "text" -> "String"   yields "String".
  //
  // This method is safe, uses a small cycle-detection set, and stops at a
  // configurable depth limit to avoid infinite recursion on malicious input.
  std::string canonicalize_type_name(const std::string &name,
                                     size_t depth_limit = 32) const {
    if (name.empty())
      return name;

    std::string cur = name;
    std::unordered_set<std::string> seen;
    size_t depth = 0;

    while (true) {
      if (!seen.insert(cur).second) {
        // cycle detected; break out and return what we've resolved so far.
        break;
      }
      if (++depth > depth_limit) {
        // reached the depth limit; avoid infinite loop.
        break;
      }

      auto alias_target = lookup_type_alias(cur);
      if (!alias_target.has_value())
        break;

      // If the alias target is the same as the current name, break.
      const std::string &next = *alias_target;
      if (next == cur)
        break;

      cur = next;
    }

    return cur;
  }

  // Resolve the alias chain into a vector for debugging or tools. The first
  // element is the original name; subsequent entries are alias targets as the
  // resolution proceeds. The returned chain stops once a non-alias type is
  // reached or a cycle/limit is encountered.
  std::vector<std::string> resolve_alias_chain(const std::string &name,
                                               size_t depth_limit = 32) const {
    std::vector<std::string> chain;
    if (name.empty())
      return chain;

    std::string cur = name;
    std::unordered_set<std::string> seen;
    chain.push_back(cur);
    size_t depth = 0;

    while (true) {
      if (!seen.insert(cur).second)
        break; // cycle detected

      if (++depth > depth_limit)
        break;

      auto alias_target = lookup_type_alias(cur);
      if (!alias_target.has_value())
        break;

      cur = *alias_target;
      chain.push_back(cur);
    }

    return chain;
  }

  // Debugging dump (improved to include alias targets for type symbols)
  void dump(std::ostream &os = std::cout) const {
    os << "Symbol Table Dump:\n";
    int depth = static_cast<int>(scopes.size());
    for (int i = 0; i < depth; ++i) {
      os << "  Scope[" << i << "]:\n";
      for (auto &p : scopes[i]) {
        os << "    " << p.first << " (" << kind_to_string(p.second.kind);

        // Show the module that declared this symbol (if present)
        if (p.second.module.has_value()) {
          os << " module=" << *p.second.module;
        }

        // Type alias or type reference arrows
        if (p.second.kind == symbol_kind::type_symbol &&
            p.second.type_name.has_value()) {
          os << " -> " << *p.second.type_name;
        }

        // Print variable symbol type and mutability flags (if available)
        if (p.second.kind == symbol_kind::variable_symbol) {
          if (p.second.type_name.has_value()) {
            os << " : " << *p.second.type_name;
          }
          if (p.second.is_mut) {
            os << " mut";
          }
        }

        // Visibility indicator
        // Prefer enum visibility flag but tolerate the legacy boolean flag.
        if (p.second.visibility == visibility_kind::visibility_public ||
            p.second.is_pub) {
          os << " pub";
        }

        os << ")\n";
      }
    }
  }

private:
  std::vector<std::unordered_map<std::string, symbol>> scopes;

  static std::string kind_to_string(symbol_kind k) {
    switch (k) {
    case symbol_kind::variable_symbol:
      return "variable";
    case symbol_kind::function_symbol:
      return "function";
    case symbol_kind::type_symbol:
      return "type";
    default:
      return "unknown";
    }
  }
};

// ------------------------
// AST Utility / Factory Helpers
// ------------------------

inline ast_program *make_program() { return new ast_program(); }

inline ast_stmt_ptr make_block_stmt() {
  return std::make_shared<ast_block_stmt>();
}

inline ast_decl_ptr
make_function_decl(const std::string &name, std::vector<ast_param> params = {},
                   std::optional<std::string> ret_type = std::nullopt,
                   std::vector<ast_generic_param> gen_params = {},
                   bool is_pub = false, int line = 0) {
  return std::make_shared<ast_function_decl>(
      name, std::move(params), ret_type, std::shared_ptr<ast_stmt>(nullptr),
      std::move(gen_params), is_pub, line);
}

inline ast_expr_ptr make_string_literal(const std::string &value,
                                        int line = 0) {
  return std::make_shared<ast_string_literal>(value, line);
}

inline ast_expr_ptr make_number_literal(const std::string &value,
                                        int line = 0) {
  return std::make_shared<ast_number_literal>(value, line);
}

inline ast_expr_ptr make_identifier_expr(const std::string &name,
                                         int line = 0) {
  return std::make_shared<ast_identifier_expr>(name, line);
}

inline ast_expr_ptr make_binary_expr(ast_expr_ptr left, token_kind op,
                                     ast_expr_ptr right, int line = 0) {
  return std::make_shared<ast_binary_expr>(std::move(left), op,
                                           std::move(right), line);
}

inline ast_expr_ptr make_call_expr(ast_expr_ptr callee,
                                   std::vector<ast_expr_ptr> args = {},
                                   std::vector<std::string> type_args = {},
                                   int line = 0) {
  return std::make_shared<ast_call_expr>(std::move(callee), std::move(args),
                                         std::move(type_args), line);
}

inline ast_decl_ptr
make_variable_decl(const std::string &name, ast_expr_ptr init = nullptr,
                   std::optional<std::string> type_in = std::nullopt,
                   bool is_mut = false, bool is_pub = false, int line = 0) {
  return std::make_shared<ast_variable_decl>(name, init, type_in, is_mut,
                                             is_pub, line);
}

inline ast_stmt_ptr make_for_in_stmt(ast_param var, ast_expr_ptr iterable,
                                     ast_stmt_ptr body, int line = 0) {
  return std::make_shared<ast_for_in_stmt>(std::move(var), std::move(iterable),
                                           std::move(body), line);
}

// Overload for tuple-unpacking `for` loops. The vector should contain
// the variable elements in the order they appear in the tuple.
inline ast_stmt_ptr make_for_in_stmt(const std::vector<ast_param> &vars,
                                     ast_expr_ptr iterable, ast_stmt_ptr body,
                                     int line = 0) {
  if (vars.empty()) {
    // Fallback to a single variable loop with a default param; parser should
    // avoid this.
    ast_param dummy;
    return std::make_shared<ast_for_in_stmt>(
        std::move(dummy), std::move(iterable), std::move(body), line);
  }
  return std::make_shared<ast_for_in_stmt>(
      std::vector<ast_param>(vars), std::move(iterable), std::move(body), line);
}

inline ast_stmt_ptr make_loop_stmt(ast_stmt_ptr body, int line = 0) {
  return std::make_shared<ast_loop_stmt>(std::move(body), line);
}

inline ast_stmt_ptr make_break_stmt(int line = 0) {
  return std::make_shared<ast_break_stmt>(line);
}

inline ast_stmt_ptr make_continue_stmt(int line = 0) {
  return std::make_shared<ast_continue_stmt>(line);
}

inline ast_decl_ptr make_import_decl(const std::string &module_path,
                                     std::vector<std::string> names = {},
                                     bool is_glob = false, int line = 0) {
  return std::make_shared<ast_import_decl>(module_path, std::move(names),
                                           is_glob, line);
}

inline ast_decl_ptr make_module_decl(const std::string &name,
                                     bool is_pub = false, int line = 0) {
  return std::make_shared<ast_module_decl>(name, is_pub, line);
}

/* duplicate make_type_alias_decl removed */

inline ast_decl_ptr
make_type_alias_decl(const std::string &name, const std::string &target_type,
                     std::vector<ast_generic_param> gen_params = {},
                     bool is_pub = false, int line = 0) {
  // Return a generic ast_decl_ptr (base-class pointer) while allocating
  // a concrete ast_type_alias_decl. We use a static_pointer_cast to maintain
  // the shared_ptr<ast_decl> type returned by the factory.
  return std::static_pointer_cast<ast_decl>(
      std::make_shared<ast_type_alias_decl>(
          name, target_type, std::move(gen_params), is_pub, line));
}

inline ast_expr_ptr make_match_expr(ast_expr_ptr target,
                                    std::vector<ast_match_case> cases,
                                    int line = 0) {
  return std::make_shared<ast_match_expr>(std::move(target), std::move(cases),
                                          line);
}

inline ast_stmt_ptr make_match_stmt(ast_expr_ptr target,
                                    std::vector<ast_match_arm> arms,
                                    int line = 0) {
  return std::make_shared<ast_match_stmt>(std::move(target), std::move(arms),
                                          line);
}

inline ast_expr_ptr make_struct_literal_expr(
    std::string type_name,
    std::vector<std::pair<std::string, ast_expr_ptr>> fields,
    std::vector<std::string> type_args = {}, int line = 0) {
  return std::make_shared<ast_struct_literal_expr>(
      std::move(type_name), std::move(fields), std::move(type_args), line);
}

// Allocate expression: `allocate<T>(size)`
struct ast_allocate_expr : public ast_expr {
  std::string type_name; // T
  ast_expr_ptr size;     // size (number of elements)

  ast_allocate_expr(std::string t, ast_expr_ptr s, int l = 0)
      : ast_expr(ast_node_kind::allocate_expr_node, l), type_name(std::move(t)),
        size(std::move(s)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Reference expression: `ref x`
struct ast_ref_expr : public ast_expr {
  ast_expr_ptr operand; // The lvalue being referenced

  ast_ref_expr(ast_expr_ptr op, int l = 0)
      : ast_expr(ast_node_kind::ref_expr_node, l), operand(std::move(op)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Lambda expression: `fn(params) -> RetType ... end fn`
struct ast_lambda_expr : public ast_expr {
  std::vector<ast_param> params;
  std::optional<std::string> return_type;
  std::shared_ptr<ast_stmt> body;
  std::string generated_name; // Filled during codegen

  ast_lambda_expr(std::vector<ast_param> p, std::optional<std::string> ret,
                  std::shared_ptr<ast_stmt> b, int l = 0)
      : ast_expr(ast_node_kind::lambda_expr_node, l), params(std::move(p)),
        return_type(std::move(ret)), body(std::move(b)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Interpolated string part - either a literal string or an expression
struct interpolated_part {
  bool is_literal;
  std::string literal_value; // When is_literal == true
  ast_expr_ptr expression;   // When is_literal == false
};

// Interpolated string: "Hello {name}, you have {count} messages"
struct ast_interpolated_string_expr : public ast_expr {
  std::vector<interpolated_part> parts;
  // Explicit formatted strings carry ordinary expression tokens with exact
  // original spans; legacy Ny identifier interpolation remains compatible.
  bool explicit_syntax = false;

  ast_interpolated_string_expr(std::vector<interpolated_part> p, int l = 0)
      : ast_expr(ast_node_kind::interpolated_string_expr_node, l),
        parts(std::move(p)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

struct ast_char_literal : public ast_expr {
  std::string value;

  explicit ast_char_literal(std::string v, int l = 0)
      : ast_expr(ast_node_kind::char_literal_node, l), value(std::move(v)) {}

  void accept(ast_node::ast_visitor &v) override { v.visit(*this); }
};

// Remove test-only declarations and inline regions before a production check.
// Analysis retains the original tree; test compilation passes include_tests=true.
void prepare_test_program(ast_program &program, bool include_tests);

} // namespace ny

#endif // NY_COMPILER_AST_HPP
