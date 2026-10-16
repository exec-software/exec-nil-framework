#ifndef NY_COMPILER_MATRIX_LEXER_HPP
#define NY_COMPILER_MATRIX_LEXER_HPP

// matrix_lexer.hpp
// Lexer for the Matrix surface language.
//
// Matrix is a second surface over Ny's semantic core: this lexer feeds
// matrix_parser.cpp, which produces the same ast.hpp nodes the Ny parser
// produces. See matrix/docs/grammar.ebnf for the grammar this implements and
// matrix/README.md for why the surface looks the way it does.
//
// Three properties distinguish it from lexer.hpp:
//
//  1. Case folds, ASCII only.  'BEGIN CHOICE' and 'begin choice' are the same
//     token pair.  A non-ASCII byte in an identifier is never folded, because
//     locale-dependent folding makes distinct-looking names compare equal,
//     which is a confusable-identifier hazard rather than a convenience.
//
//  2. A hyphen joins the words of one name: 'frame-kind' is a single
//     identifier.  '-' is never an operator, because subtraction and negation
//     are both spelled 'minus'.  A hyphen must sit between a letter-or-digit
//     and a letter, so '-kind', 'kind-', 'frame--kind' and 'total-1' are
//     diagnosed at the hyphen.
//
//  3. Text literals have NO escape sequences.  A quoted literal is raw up to
//     its closing quote, so 'C:\dir\file' needs no doubling.  A literal
//     quote, newline or tab is the predefined text constant 'quote',
//     'newline' or 'tab', concatenated with 'plus' -- which is already text
//     concatenation.  Awkward or multi-line content uses a raw block:
//
//         value banner is begin text
//             He said "hi" and the path is C:\dir
//         end text
//
//     terminated by a line whose only content is 'end text'.  Nothing inside
//     needs escaping because the terminator is a line-anchored keyword rather
//     than a character.  Character literals do not exist: there is no 'char'
//     in NIL, and removing them removes an escaping surface.
//
//  4. A statement ends at end of line.  ';' or '.' ends one explicitly, so
//     several statements can share a line.  '.' is a terminator only when it
//     is not followed by a name character; 'frame.header' is still member
//     access.  English uses a period to end a sentence and a semicolon to
//     join independent clauses on one line, and both readings are accepted
//     because both are what a writer of prose would reach for.
//
//  5. Multi-word operators ('is at least', 'shifted left by', 'does not
//     equal') are NOT lexed as single tokens.  The lexer emits one token per
//     word; the parser matches phrases positionally.  This keeps words like
//     'least', 'than' and 'by' out of the reserved set, which matters because
//     a word-based surface already taxes naming heavily.

#include <cstddef>
#include <string>
#include <vector>

namespace matrix {

// Hard keywords: words that are structurally decisive -- they begin a
// declaration, statement or type, or they terminate a clause. Everything else
// a phrase needs ('least', 'most', 'than', 'by', 'each', 'length',
// 'milliseconds', 'receiver', ...) stays an ordinary identifier and is matched
// by the parser on folded text, so it remains usable as a name.
enum class kw {
  // compilation unit, named on the book metaphor: a library holds books, a
  // book is one namespace, a page is a source file, a paragraph is a function
  // and a sentence is a statement. 'read' imports, because reading is what an
  // import does. 'chapter' is RESERVED and unimplemented: NIL has exactly two
  // namespace levels and inventing a third would out-promise it.
  library, book, chapter, read, cite, everything, from, as, requires_,
  // declaration frame
  begin, end, paragraph, record, choice, class_, contract, implementation,
  extension, test,
  field, means, inherits, provides, meeting, of, type,
  // modifiers and receivers
  public_, abstract, virtual_, replaces, on, mutable_, without, reference,
  // 'forever' is POLYSEMOUS BY POSITION: after 'repeat' it is an unbounded
  // loop, and as a declaration modifier it is unbounded lifetime (static).
  // One relation -- without end -- applied to iteration or to storage. The
  // lexer always classifies it as a keyword, so this needs no lexer feedback;
  // only the parser assigns the meaning, from position.
  my,
  // function clauses
  receives, returns, more, defaulting, anonymous, the,
  // bindings and assignment
  value, variable, constant, is, set, to,
  // statements
  say, give, evaluate, append, raise, again, nothing,
  // control flow
  when, then, otherwise, provided, consider, holding, repeat, while_, for_, in_,
  forever, stop, continue_, repeating,
  // faults
  attempt, caught, anything, always,
  // memory
  own, borrow, exclusively, move, into, allocate, release, sized,
  // construction and calls
  build, with, being,
  // concurrency, time, assembly, annotations
  apply, across, now, sleep, assembly, using_, giving, note, context,
  // operators that must be reserved to stay unambiguous
  and_, or_, not_, plus, minus, times, over, remainder, at, bitwise, shifted,
  equals, does, becomes, xor_,
  // literals and named characters (no backslash escapes exist)
  true_, false_, quote, newline, tab,
  // type constructors
  pointer, array, owned, shared, exclusive, view,
  // ergonomic types: what a program says unless it has a reason not to
  number, floating, boolean, text,
  // the machine-representation escape hatch
  override,
  // machine types, legal only after 'override'
  integer, long_, byte, double_,
  // machine types reserved but REJECTED: NIL has no cross-route
  // implementation for them (see docs/language-reference/types.md). Reserved
  // so that adding them later is additive and misuse is diagnosed precisely
  // rather than parsed as an identifier.
  short_, unsigned_, decimal, single, letter,
  not_a_keyword
};

enum class token_kind {
  identifier,     // includes backtick-escaped names
  keyword,
  number_literal,
  string_literal,
  left_paren, right_paren,
  left_bracket, right_bracket,
  comma,
  dot,
  double_colon,
  // An explicit statement terminator. Both ';' and '.' produce this: a model
  // writing prose-shaped code ends sentences with a period, and rejecting the
  // most likely natural-language artifact would be a pointless failure. The
  // lexeme is kept so diagnostics can quote what was actually written.
  statement_end,
  end_of_file,
  invalid
};

struct token {
  token_kind kind = token_kind::invalid;
  kw word = kw::not_a_keyword;  // set when kind == keyword
  std::string lexeme;           // exactly as written, for diagnostics
  std::string key;              // lookup key: ASCII-folded, '-' and '_' removed
  int line = 1;
  int column = 1;

  bool is(kw k) const { return kind == token_kind::keyword && word == k; }
  bool is_name() const { return kind == token_kind::identifier; }
  // Soft-keyword test: a word matched on folded text rather than reserved.
  bool reads(const char *folded) const { return key == folded; }
};

struct lex_error {
  int line = 0;
  int column = 0;
  std::string message;
  std::string hint;
};

// Fold one identifier to its lookup key: ASCII-lowercase, with every '-' and
// '_' removed. 'frame-kind', 'FRAME-KIND', 'frame_kind' and 'framekind'
// therefore share a key. Underscores fold with hyphens so Ny's snake_case
// library names can be written in kebab-case.
std::string fold_key(const std::string &spelling);

// Rewrite a Matrix identifier to the symbol emitted into NIL: the exact
// spelling with '-' replaced by '_'. Case is preserved, because NIL identity
// is case-significant and Ny identifiers admit no hyphen.
std::string nil_symbol(const std::string &spelling);

// Look up a folded key in the hard-keyword table.
kw keyword_for(const std::string &folded_key);

// Human-readable keyword spelling, for diagnostics.
const char *keyword_name(kw k);

class lexer {
public:
  explicit lexer(const std::string &source) : src_(source) {}

  std::vector<token> scan();
  const std::vector<lex_error> &errors() const { return errors_; }
  bool has_errors() const { return !errors_.empty(); }

private:
  const std::string &src_;
  std::vector<lex_error> errors_;
  size_t pos_ = 0;
  int line_ = 1;
  int col_ = 1;

  bool at_end() const { return pos_ >= src_.size(); }
  char peek(size_t ahead = 0) const {
    return pos_ + ahead < src_.size() ? src_[pos_ + ahead] : '\0';
  }
  char advance();
  void skip_trivia();
  token make(token_kind kind, const std::string &lexeme, int line, int col) const;
  token scan_identifier();
  token scan_number();
  token scan_string();
  // Raw block literal: consumes lines after 'begin text' until a line whose
  // only content is 'end text', then dedents by the common indentation.
  token scan_text_block(int line, int col);
  // True when 'begin' is immediately followed by 'text' and end of line.
  bool text_block_follows() const;
  token scan_backtick_identifier();
  void error(int line, int col, std::string message, std::string hint = {});
};

} // namespace matrix

#endif // NY_COMPILER_MATRIX_LEXER_HPP
