#ifndef NY_COMPILER_MATRIX_PARSER_HPP
#define NY_COMPILER_MATRIX_PARSER_HPP

// matrix_parser.hpp
// Parser for the Matrix surface language.
//
// Matrix is a second surface over Ny's semantic core. This parser produces the
// SAME ast.hpp nodes the Ny parser produces, so type_checker.cpp, codegen.cpp
// and every contract checker run unchanged. That is the invariant that keeps
// the two languages from drifting: there is no Matrix-only AST node, and
// therefore no place to put a Matrix-only semantic.
//
// All Matrix-specific meaning is desugaring performed here:
//
//   book / library / read / cite  ->  module / package / import / pub use
//   <name> ... begin paragraph    ->  fn <name>(...) ... end fn
//   value/variable/constant x is  ->  let / let mut / const
//   set <place> to <expr>         ->  assignment
//   say / give / evaluate         ->  println / return / expression statement
//   when/otherwise when/otherwise ->  if / elseif / else
//   repeat while|for|forever      ->  while / for-in / loop
//   stop|continue repeating       ->  break / continue
//   <recv> at <index>             ->  index expression
//   <f> with a being 1            ->  positional call in declaration order
//   <e> becomes <type>            ->  cast
//   the length of <e>             ->  <e>.len()
//   append <e> to <place>         ->  <place>.push(<e>)
//   number/floating/boolean/text  ->  i64 / f64 / bool / text
//   override integer|long|byte|double -> i32 / i64 / u8 / f64
//   attempt/caught T as b/always  -> attempt/fault/recover
//   raise T with f being v        -> raise T { f: v }; raise t again -> raise t
//   own b as array of T sized n   -> let [mut] b = owned<T>(n)
//   borrow v from b [exclusively] -> let v = owned_borrow[_mut](b)
//   move b into c                 -> let c = owned_move(b)
//   allocate p as pointer to T    -> let p = allocate<T>(n); release p
//   apply w across xs             -> parallel_map(w, xs)
//   now in milliseconds           -> the time.now intrinsic
//   assembly for x86_64 is "..."  -> asm "x86_64", "..." (i) -> o end asm
//
// Scope note: stages 1-6 of matrix/docs/front-end-plan.md are lowered --
// books, reads, paragraphs, bindings, word operators, control flow, calls,
// casts, records, choices, generics, classes, contracts, extensions, faults,
// ownership, structured parallelism, time and inline assembly. Constructs the
// surface defines but this parser does not lower are diagnosed as
// unimplemented rather than silently skipped, so a passing parse never
// overstates coverage. Cross-language binary linking (stage 7) is not here.

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ast.hpp"
#include "matrix_diagnostics.hpp"
#include "matrix_lexer.hpp"
#include "parser.hpp" // for ny::diagnostic

namespace matrix {

struct parser_options {
  std::string filename;
  std::optional<std::size_t> signature_cursor;
};

class parser {
public:
  explicit parser(const std::string &source,
                  const parser_options &opts = parser_options());

  std::shared_ptr<ny::ast_program> parse_program();

  const std::vector<ny::diagnostic> &diagnostics() const { return diagnostics_; }
  // The same diagnostics with spans, codes and hints, for the renderer in
  // matrix_diagnostics.hpp. Kept alongside the plain form so existing
  // consumers are unaffected.
  const std::vector<matrix::diagnostic> &rich_diagnostics() const {
    return rich_;
  }
  bool has_errors() const { return !diagnostics_.empty(); }
  bool recovered_signature_call() const { return recovered_signature_call_; }

  // Editor suggestions must pass the same page-wide declaration folding as
  // a newly inserted identifier; expose resolution without parsing a new AST.
  std::string resolve_completion_name(const std::string &written) const {
    return resolve_name(written);
  }

private:
  std::vector<token> tokens_;
  std::unordered_map<size_t, std::shared_ptr<ny::ast_number_literal>>
      integer_boundaries_;
  size_t pos_ = 0;
  std::vector<ny::diagnostic> diagnostics_;
  std::vector<matrix::diagnostic> rich_;
  std::string filename_;
  std::optional<std::size_t> signature_cursor_, signature_blank_start_;
  bool recovered_signature_call_ = false;

  // Folded lookup key -> the NIL symbol fixed by the DECLARATION site.
  // References fold against this, which is what makes 'READ-SENSOR',
  // 'read_sensor' and 'read-sensor' one name while NIL still carries the
  // readable spelling the author declared. Ny's checker is case-sensitive, so
  // this canonicalisation has to happen before the AST is handed over.
  //
  // Populated by a pre-scan so a reference may precede its declaration.
  std::unordered_map<std::string, std::string> declared_;

  // Type names only (records, choices, classes), keyed the same way. Case
  // folding means 'Tagged' and 'tagged' are one name, so a value named after
  // its own type in different case is a collision, not a convention. Without
  // this check the reference silently resolves to the type and the failure
  // surfaces much later as a method call with no receiver.
  std::unordered_map<std::string, std::string> declared_types_;

  // Diagnose a value name that collides with a declared type name.
  void reject_type_name_collision(const token &at, const std::string &written);

  // Collect declaration spellings from the token stream before parsing.
  void prescan_declarations();
  // Resolve a written reference to its declared NIL symbol, falling back to
  // the written spelling for names declared elsewhere (a read book, the
  // standard library) whose declaration this page never sees.
  std::string resolve_name(const std::string &written) const;

  // ---- token helpers ----
  const token &peek(size_t ahead = 0) const;
  const token &previous() const;
  bool at_end() const;
  // matrix::token_kind here is the LEXER's enum. ny::token_kind, which the
  // AST stores as a binary/unary operator, is always written qualified.
  bool check(matrix::token_kind kind) const;
  bool check(kw word) const;
  bool check_word(const char *folded, size_t ahead = 0) const;
  bool match(matrix::token_kind kind);
  bool match(kw word);
  bool match_word(const char *folded);
  const token &advance();
  bool expect(kw word, const char *context);
  bool expect(matrix::token_kind kind, const char *what, const char *context);
  // Consume a statement terminator if present; Matrix statements end at end of
  // line, so ';' and '.' are optional rather than required.
  void skip_terminators();

  void error(const token &at, std::string message, std::string hint = {},
             std::string code = {});

  // Emit the right diagnostic when a name was expected. A keyword in a name
  // position is the commonest way to go wrong in this surface, because it
  // reserves ordinary English words -- 'value', 'text', 'shifted', 'move'.
  // Saying "expected a name" there teaches nothing; naming the reserved word
  // and the hyphenation remedy teaches the rule at the point of the mistake.
  void expected_name(const char *context);

  // An agent has no training on this language and will reach for the keyword
  // from a language it does know. 'fn', 'return', 'if', 'struct', 'import',
  // 'self', 'try' and friends are NOT Matrix keywords, so they arrive as
  // ordinary names and would otherwise produce only "expected a statement".
  // Naming the equivalent turns each into a lesson at the point of the
  // mistake. Returns true when it recognised and reported one.
  bool report_foreign_keyword(const token &at);
  void recover_to_declaration();

  // ---- declarations ----
  std::shared_ptr<ny::ast_decl> parse_book_or_library();
  std::shared_ptr<ny::ast_decl> parse_read();
  std::shared_ptr<ny::ast_decl> parse_cite();
  std::shared_ptr<ny::ast_decl> parse_test(int line, const std::string &name);
  // A bodyless declaration backed by a runtime provider named in a
  // 'note intrinsic' annotation.
  std::shared_ptr<ny::ast_decl>
  finish_intrinsic(bool is_public, int line, const std::string &name,
                   std::vector<ny::ast_annotation> annotations);
  // Declare 'time.now' / 'time.sleep' on first use, so 'now in milliseconds'
  // and 'sleep for n milliseconds' work without reading a library book.
  void ensure_time_intrinsic(const std::string &provider,
                             const std::string &name,
                             std::optional<std::string> return_type,
                             std::optional<std::string> parameter_type);
  std::vector<std::shared_ptr<ny::ast_decl>> synthesized_;
  std::unordered_map<std::string, bool> synthesized_names_;
  std::shared_ptr<ny::ast_decl> parse_declaration();
  // Where a paragraph sits decides whether it takes a receiver and whether a
  // body is required. A class, implementation or extension member gets a
  // synthesised first 'self' parameter; a contract member gets one too but has
  // no body; a free paragraph has neither.
  enum class paragraph_context { free, member, contract_member };

  std::shared_ptr<ny::ast_function_decl>
  parse_paragraph(bool is_public, int line, const std::string &name,
                  paragraph_context context = paragraph_context::free,
                  bool is_abstract = false, bool is_virtual = false,
                  bool is_override = false);

  // Read modifiers, a name and clauses for one member of a class, contract,
  // implementation or extension.
  std::shared_ptr<ny::ast_function_decl> parse_member(paragraph_context context);

  std::shared_ptr<ny::ast_decl> parse_class(bool is_public, bool is_abstract,
                                            int line, const std::string &name,
                                            std::vector<ny::ast_generic_param> generics);
  std::shared_ptr<ny::ast_decl> parse_contract(bool is_public, int line,
                                               const std::string &name,
                                               std::vector<ny::ast_generic_param> generics);
  // Covers 'begin implementation' and 'begin extension': both lower to an
  // impl block, and the only difference is whether this book declares the
  // target type.
  std::shared_ptr<ny::ast_decl> parse_implementation(int line,
                                                     const std::string &target,
                                                     std::vector<ny::ast_generic_param> generics,
                                                     std::optional<std::string> contract,
                                                     kw closing_kind);
  std::shared_ptr<ny::ast_decl> parse_book_binding(bool is_public);
  std::shared_ptr<ny::ast_decl> parse_text_pattern(const token &name, bool is_public);
  std::shared_ptr<ny::ast_decl> parse_record(bool is_public, int line,
                                             const std::string &name,
                                             std::vector<ny::ast_generic_param> generics);
  std::shared_ptr<ny::ast_decl> parse_choice(bool is_public, int line,
                                             const std::string &name,
                                             std::vector<ny::ast_generic_param> generics);
  // 'of type T [meeting B and C], type U' on a record, choice or paragraph.
  std::vector<ny::ast_generic_param> parse_generic_clause();

  // ---- statements ----
  std::shared_ptr<ny::ast_stmt> parse_statement();
  std::shared_ptr<ny::ast_stmt> parse_block(kw closing_kind);
  std::shared_ptr<ny::ast_stmt> parse_binding_statement();
  std::shared_ptr<ny::ast_stmt> parse_set();
  std::shared_ptr<ny::ast_stmt> parse_say();
  std::shared_ptr<ny::ast_stmt> parse_give();
  std::shared_ptr<ny::ast_stmt> parse_evaluate();
  std::shared_ptr<ny::ast_stmt> parse_append();
  std::shared_ptr<ny::ast_stmt> parse_when_statement();
  std::shared_ptr<ny::ast_stmt> parse_repeat();
  std::shared_ptr<ny::ast_stmt> parse_consider_statement();
  std::shared_ptr<ny::ast_stmt> parse_allocate();
  std::shared_ptr<ny::ast_stmt> parse_release();
  // A 'when' arm pattern: a literal, a bare binding, '_' or
  // 'Type.Variant [holding binding]'.
  ny::ast_pattern parse_pattern();
  std::shared_ptr<ny::ast_stmt> parse_attempt();
  std::shared_ptr<ny::ast_stmt> parse_raise();
  std::shared_ptr<ny::ast_stmt> parse_own();
  std::shared_ptr<ny::ast_stmt> parse_borrow();
  std::shared_ptr<ny::ast_stmt> parse_move();
  std::shared_ptr<ny::ast_stmt> parse_sleep();
  std::shared_ptr<ny::ast_stmt> parse_assembly();
  // Build 'let <name> = <op>(<args>)' for an ownership operation, which Ny
  // requires to be a direct local declaration initialiser.
  std::shared_ptr<ny::ast_stmt>
  make_ownership_binding(const std::string &name, const std::string &operation,
                         std::vector<ny::ast_expr_ptr> args,
                         std::vector<std::string> type_args, bool is_mut,
                         std::optional<std::string> declared_type, int line,
                         const token &name_token);

  // ---- expressions, lowest precedence first ----
  ny::ast_expr_ptr parse_expression();
  ny::ast_expr_ptr parse_or();
  ny::ast_expr_ptr parse_and();
  ny::ast_expr_ptr parse_not();
  ny::ast_expr_ptr parse_comparison();
  ny::ast_expr_ptr parse_bitwise();
  ny::ast_expr_ptr parse_shift();
  ny::ast_expr_ptr parse_additive();
  ny::ast_expr_ptr parse_multiplicative();
  ny::ast_expr_ptr parse_unary();
  ny::ast_expr_ptr parse_postfix();
  ny::ast_expr_ptr parse_primary();
  ny::ast_expr_ptr parse_query_expression();
  ny::ast_expr_ptr parse_when_expression();
  ny::ast_expr_ptr parse_consider_expression();
  ny::ast_expr_ptr parse_build();

  // ---- shared pieces ----
  std::optional<std::string> parse_type();
  std::string parse_dotted_name();
  std::vector<ny::ast_param> parse_parameters(std::vector<std::string> &names);
  // Retain named labels for checked callable resolution; parsing does not
  // know which lexical/imported declaration a callee will select.
  std::vector<ny::ast_expr_ptr>
  parse_with_arguments(std::vector<std::string> &argument_names,
                       std::vector<std::size_t> &argument_starts);
};

} // namespace matrix

#endif // NY_COMPILER_MATRIX_PARSER_HPP
