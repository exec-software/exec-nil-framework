#ifndef NY_COMPILER_LEXER_HPP
#define NY_COMPILER_LEXER_HPP

#include <iosfwd>
#include <cctype>
#include <string>
#include <vector>

namespace ny {

// Annotation prefix used by both lexer and parser to detect annotations
// (e.g., @inline, @deprecated). Some preprocessor directives also begin with
// '@' (e.g., @include), but the preprocessor runs before lexing when enabled.
// This constant is a convenient place to document the character and makes the
// behavior explicit for future lexer improvements.
constexpr char annotation_prefix = '@';

// Token kinds used by the lexer.
enum class token_kind {
  // Single character tokens
  left_paren,
  right_paren,
  left_brace,
  right_brace,
  left_bracket,
  right_bracket,
  comma,
  colon,
  double_colon, // ::
  semicolon,
  dot,
  double_dot, // ..  range operator
  at_sign, // '@' annotation / preprocessor marker

  // Operators
  plus,
  minus,
  star,
  slash,
  percent, // %
  equal,
  double_equal,
  not_equal,
  less,
  greater,
  less_equal,
  greater_equal,
  less_less,      // <<
  greater_greater, // >>
  amp,       // &
  amp_amp,   // &&
  pipe,      // |
  pipe_pipe, // ||
  arrow,     // ->
  question,  // ? (error propagation operator)

  // Literals
  identifier,
  number_literal,
  string_literal,
  char_literal,
  annotation, // '@' followed by identifier (e.g., @inline, parsed by
              // lexer/parser)

  // Keywords
  function_kw,
  let_kw,
  const_kw,
  if_kw,
  else_kw,
  return_kw,
  println_kw,
  then_kw,   // VB-style: if condition then
  elseif_kw, // VB-style: elseif condition then
  while_kw,
  where_kw,
  for_kw,
  in_kw,
  break_kw,
  continue_kw,
  loop_kw,
  do_kw,
  mut_kw,
  import_kw,
  package_kw,
  module_kw,
  namespace_kw,
  using_kw,
  pub_kw,
  match_kw,
  when_kw,
  type_kw,
  as_kw,
  end_kw,
  true_kw,
  false_kw,
  struct_kw,
  enum_kw,
  trait_kw,
  impl_kw,
  allocate_kw,
  deallocate_kw,
  ref_kw,
  fn_kw,
  mod_kw, // mod operator
  caret,  // ^ bitwise XOR
  tilde,  // ~ bitwise complement
  not_kw, // not keyword for logical negation

  // Ny testing keywords
  test_kw,
  example_kw,
  property_kw,
  generate_kw,

  // Fault handling keywords
  attempt_kw,  // attempt ... end attempt (replaces try)
  fault_kw,    // fault FaultType as e (replaces catch)
  recover_kw,  // recover ... (replaces finally)
  raise_kw,    // raise FaultInstance (replaces throw)

  // Intrinsic keywords
  assert_kw,     // assert.method(args) - intrinsic assertion class
  hotreload_kw,  // hotreload.method(args) - intrinsic hot reload class

  // Class / OOP keywords
  class_kw,
  abstract_kw,
  virtual_kw,
  override_kw,
  inherits_kw,
  self_kw,

  // End of file / error
  eof,
  invalid
};

// Representation of a single token.
struct token {
  token_kind kind;
  std::string lexeme; // The raw text
  int line;           // 1-based line number
  // Byte range in the lexer's UTF-8 input. Zero length also marks synthesized
  // recovery tokens, which must not acquire editable source locations.
  size_t offset = 0;
  size_t length = 0;
};

class lexer {
public:
  explicit lexer(const std::string &source)
      : src(source), start(0), current(0), line(1) {}

  // Produce all tokens until EOF.
  std::vector<token> scan_tokens();

private:
  const std::string &src;
  size_t start, current;
  int line;

  bool is_at_end() const { return current >= src.size(); }
  char advance() { return src[current++]; }
  char peek() const {
    if (is_at_end())
      return '\0';
    return src[current];
  }
  char peek_next() const {
    if (current + 1 >= src.size())
      return '\0';
    return src[current + 1];
  }

  void skip_whitespace();
  token identifier();
  token number();
  token string();
  token character(); // Lex a character literal 'c'
  token
  annotation(); // Lex an annotation token starting with '@' (e.g., @inline)

  // Helpers for recognizing keywords.
  bool match(char expected) {
    if (is_at_end() || src[current] != expected)
      return false;
    ++current;
    return true;
  }

  // Helper: match the double-colon operator (::).
  // This checks if the next two characters in the stream are "::" and consumes
  // them.
  bool match_double_colon() {
    if (is_at_end())
      return false;
    // Ensure there is at least one more character to peek at.
    if (current + 1 >= src.size())
      return false;
    if (src[current] == ':' && src[current + 1] == ':') {
      current += 2;
      return true;
    }
    return false;
  }

  // Helper: match the arrow operator (->).
  // This checks if the next two characters in the stream are "->" and consumes
  // them.
  bool match_arrow() {
    if (is_at_end())
      return false;
    // Ensure there is at least one more character to peek at.
    if (current + 1 >= src.size())
      return false;
    if (src[current] == '-' && src[current + 1] == '>') {
      current += 2;
      return true;
    }
    return false;
  }
};

} // namespace ny

#endif // NY_COMPILER_LEXER_HPP
