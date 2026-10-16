#ifndef NY_COMPILER_PARSER_HPP
#define NY_COMPILER_PARSER_HPP

// parser.hpp
// Parser interface and declarations for nilcompiler.
//
// Style / naming:
//  - K&R underscore style: function and variable names use lower_case with
//  underscores.
//  - File-scoped implementation will live in parser.cpp
//  - The parser consumes lexer tokens and produces nodes from ast.hpp.
//
// Responsibilities:
//  - Transform a source string into an AST (ast_program).
//  - Produce structured diagnostics on parse errors while attempting to
//  recover.
//  - Provide a small public API that the CLI / tools will use to compile input
//  files.
//  - Enable incremental feature parsing and later AST semantic checks.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ast.hpp"
#include "lexer.hpp"
#include "preprocessor.hpp"

namespace ny {

//
// Parse diagnostic
//
struct diagnostic {
  int line = 0;                    // 1-based line number
  std::string message;             // Human-readable message
  std::optional<std::string> hint; // Optional suggestion for fix
};

//
// Parser options – for now, basic flags used for enabling experimental
// behavior.
//
struct parser_options {
  bool allow_recovery =
      true; // Attempt simple error recovery and continue parsing.
  bool record_tokens =
      false; // Keep the full token stream in memory for diagnostics.

  // Enable running the preprocessor prior to tokenization/parsing (default
  // true). This allows the parser to accept preprocessed Ny source where
  // @include,
  // @pragma, and other textual directives may have modified the input.
  bool enable_preprocessor = true;

  // Preprocessor options to configure @include search paths, macro expansion,
  // and other preprocessor behaviors; these options are used by the parser
  // when `enable_preprocessor` is set.
  preprocessor_options preproc_options = {};

  std::string filename; // Optional filename for error messages.
};

//
// Parser public API
//
class parser {
public:
  // Create a parser instance from a source string with optional parser options
  explicit parser(const std::string &source,
                  const parser_options &opts = parser_options());

  // Parse the entire source and return the program AST root.
  // If parse fails, returns a non-null AST with partial content when
  // allow_recovery is true. Diagnostics are emitted to diagnostics().
  std::shared_ptr<ast_program> parse_program();

  // Access the parser's symbol table for diagnostic or semantic inspection.
  const symbol_table &symbols() const { return symbol_table_; }

  // Access diagnostics recorded during parsing.
  const std::vector<diagnostic> &diagnostics() const { return diagnostics_; }

  // Return true if any errors were recorded.
  bool has_errors() const { return !diagnostics_.empty(); }

private:
  // Internal helpers and grammar entry points
  std::vector<std::string> parse_leading_annotations();
  std::shared_ptr<ast_decl> parse_declaration();
  std::shared_ptr<ast_decl> parse_function_declaration(
      visibility_kind visibility = visibility_kind::visibility_private,
      bool is_abstract = false,
      const std::vector<std::string> &annotations = {});
  std::shared_ptr<ast_decl> parse_variable_declaration(
      visibility_kind visibility = visibility_kind::visibility_private);
  std::shared_ptr<ast_decl>
  parse_import_declaration(visibility_kind visibility =
                               visibility_kind::visibility_private);
  std::shared_ptr<ast_decl> parse_namespace_alias_declaration(
      visibility_kind visibility = visibility_kind::visibility_private);
  std::shared_ptr<ast_decl> parse_module_declaration(
      visibility_kind visibility = visibility_kind::visibility_private);
  std::shared_ptr<ast_decl> parse_type_declaration(
      visibility_kind visibility = visibility_kind::visibility_private);
  std::shared_ptr<ast_decl> parse_struct_declaration(
      visibility_kind visibility = visibility_kind::visibility_private);
  std::shared_ptr<ast_decl> parse_enum_declaration(
      visibility_kind visibility = visibility_kind::visibility_private);
  std::shared_ptr<ast_decl> parse_trait_declaration(
      visibility_kind visibility = visibility_kind::visibility_private);
  std::shared_ptr<ast_decl> parse_impl_declaration();
  std::shared_ptr<ast_decl> parse_class_declaration(
      visibility_kind visibility = visibility_kind::visibility_private,
      bool is_abstract = false);

  std::shared_ptr<ast_stmt> parse_statement();
  // Parse a block and optionally register predeclared variables in the block's
  // scope. `predeclared` defaults to an empty vector for backward
  // compatibility.
  std::shared_ptr<ast_stmt> parse_block_statement(
      const std::vector<ast_param> &predeclared = std::vector<ast_param>());
  std::shared_ptr<ast_stmt> parse_println_statement();
  std::shared_ptr<ast_stmt> parse_return_statement();
  std::shared_ptr<ast_stmt> parse_if_statement();
  std::shared_ptr<ast_stmt> parse_while_statement();
  std::shared_ptr<ast_stmt> parse_for_statement();
  std::shared_ptr<ast_stmt> parse_loop_statement();
  std::shared_ptr<ast_stmt> parse_break_statement();
  std::shared_ptr<ast_stmt> parse_continue_statement();
  std::shared_ptr<ast_stmt> parse_expr_statement();
  // Parse `match` used as a statement (e.g., `match <expr> { ... }` or
  // `match <expr> ... end match`). This distinct entry point allows the
  // parser to produce a statement-style match (ast_match_stmt) rather than an
  // expression-style match which returns an ast_expr.
  std::shared_ptr<ast_stmt> parse_match_statement();

  // Fault handling: attempt/fault/recover/raise
  std::shared_ptr<ast_stmt> parse_attempt_statement();
  std::shared_ptr<ast_fault_handler> parse_fault_handler();
  std::shared_ptr<ast_stmt> parse_raise_statement();

  // Intrinsic assertions: assert.method(args)
  std::shared_ptr<ast_expr> parse_assert_expr();

  // Intrinsic hot reload: hotreload.method(args)
  std::shared_ptr<ast_expr> parse_hotreload_expr();

  // Parse a Ny test block.
  std::shared_ptr<ast_test_block> parse_test_block();

  // Parse a pattern for use in match arms. Patterns are parsed as the AST
  // `ast_pattern` structure and may include wildcard, literal, variable, and
  // constructor/tuple-style nested patterns.
  ast_pattern parse_pattern();

  std::shared_ptr<ast_expr> parse_expression();
  std::shared_ptr<ast_expr> parse_assignment();
  std::shared_ptr<ast_expr> parse_logic_or();
  std::shared_ptr<ast_expr> parse_logic_and();
  std::shared_ptr<ast_expr> parse_bitwise_or();
  std::shared_ptr<ast_expr> parse_bitwise_xor();
  std::shared_ptr<ast_expr> parse_bitwise_and();
  std::shared_ptr<ast_expr> parse_equality();
  std::shared_ptr<ast_expr> parse_comparison();
  std::shared_ptr<ast_expr> parse_shift();
  std::shared_ptr<ast_expr> parse_term();
  std::shared_ptr<ast_expr> parse_factor();
  std::shared_ptr<ast_expr> parse_unary();
  std::shared_ptr<ast_expr> parse_cast();
  std::shared_ptr<ast_expr> parse_call(std::shared_ptr<ast_expr> callee);
  std::shared_ptr<ast_expr> parse_primary();
  std::shared_ptr<ast_expr> parse_match_expression();
  std::shared_ptr<ast_expr> parse_if_expression();  // if cond then expr else expr end if

  // Helper to parse generic parameters: <T, U>
  std::vector<ast_generic_param> parse_generic_params();

  // Helper to parse where clauses: where T: Trait, U: Other
  // Returns a map of generic param name -> list of bounds
  std::unordered_map<std::string, std::vector<std::string>>
  parse_where_clause();
  void validate_where_parameters(
      const std::vector<ast_generic_param> &parameters,
      const std::unordered_map<std::string, std::vector<std::string>> &constraints);

  // Helper to parse a type name (including generics): Box<T>, std::vector<i32>
  std::string parse_type_name();

  // Utility parsing helpers
  bool match(const token_kind &expected);
  bool check(const token_kind &expected) const;
  bool check_ahead(const token_kind &expected) const;
  bool is_at_end() const;
  bool is_match_arm_start() const; // Lookahead: does current position begin a new match arm?
  const token &peek() const;
  token &peek();
  token &previous();
  token advance();
  token consume(const token_kind &expected, const std::string &msg);
  void synchronize(); // Error recovery: skip tokens to next statement boundary

  // Helper to parse optional `pub` visibility modifiers: `pub`, `pub(crate)`,
  // `pub(super)`. Returns the resolved visibility kind (defaults to private).
  visibility_kind parse_visibility_modifier();

  // Diagnostic helpers
  void add_error(const token &t, const std::string &msg,
                 const std::optional<std::string> &hint = std::nullopt);
  void add_error_at_line(int line, const std::string &msg,
                         const std::optional<std::string> &hint = std::nullopt);

  // Token stream / source tracking
  std::vector<token> tokens_;
  size_t pos_ = 0; // current token index (points at the next token)
  parser_options options_;

  // Local copy of preprocessor options (for convenience in the parser).
  // This mirrors `options_.preproc_options` and is intended for the parser to
  // configure and run the preprocessor when `options_.enable_preprocessor`
  // is true.
  preprocessor_options preproc_options_;

  // Whether to run the preprocessor before tokenization; mirrors
  // `options_.enable_preprocessor` and is used by the parser implementation.
  bool enable_preprocessing_ = false;

  // Diagnostics
  std::vector<diagnostic> diagnostics_;

  // Symbol table used for early name resolution (populated during parsing)
  symbol_table symbol_table_;

  // Raw source / filename
  std::string source_;
  std::string filename_;
};

} // namespace ny

#endif // NY_COMPILER_PARSER_HPP
