#include "matrix_lexer.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace matrix {
namespace {

// ASCII-only classification. A byte >= 0x80 is never a letter and never folds:
// it may appear inside an identifier but compares byte-exact.
bool ascii_alpha(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
bool ascii_digit(unsigned char c) { return c >= '0' && c <= '9'; }
bool high_byte(unsigned char c) { return c >= 0x80; }

// A letter for identifier purposes: ASCII letter or a non-ASCII byte.
bool name_letter(unsigned char c) { return ascii_alpha(c) || high_byte(c); }
bool name_start(unsigned char c) { return name_letter(c) || c == '_'; }
bool name_part(unsigned char c) { return name_letter(c) || ascii_digit(c) || c == '_'; }

char ascii_lower(unsigned char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a')
                                : static_cast<char>(c);
}

struct keyword_entry { const char *key; kw word; const char *spelling; };

// Normalise one source line for terminator matching: trim the ends, collapse
// internal whitespace runs to a single space, ASCII-lowercase. A line is the
// 'end text' terminator when this yields exactly "end text", so indentation
// and extra spacing between the two words do not matter.
std::string normalise_line(const std::string &raw) {
  std::string collapsed;
  bool pending_space = false;
  for (unsigned char c : raw) {
    if (c == ' ' || c == '\t' || c == '\r') {
      if (!collapsed.empty()) pending_space = true;
      continue;
    }
    if (pending_space) { collapsed.push_back(' '); pending_space = false; }
    collapsed.push_back(ascii_lower(c));
  }
  return collapsed;
}

// Table is keyed by folded spelling. Entries are the hard keywords only; see
// matrix_lexer.hpp for why 'least', 'than', 'by' and friends are absent.
const keyword_entry keyword_table[] = {
  {"library", kw::library, "library"}, {"book", kw::book, "book"},
  {"chapter", kw::chapter, "chapter"},
  {"read", kw::read, "read"}, {"cite", kw::cite, "cite"},
  {"everything", kw::everything, "everything"}, {"from", kw::from, "from"},
  {"as", kw::as, "as"},
  {"requires", kw::requires_, "requires"},
  {"begin", kw::begin, "begin"}, {"end", kw::end, "end"},
  {"paragraph", kw::paragraph, "paragraph"}, {"record", kw::record, "record"},
  {"choice", kw::choice, "choice"}, {"class", kw::class_, "class"},
  {"contract", kw::contract, "contract"},
  {"implementation", kw::implementation, "implementation"},
  {"extension", kw::extension, "extension"},
  {"test", kw::test, "test"}, {"field", kw::field, "field"},
  {"means", kw::means, "means"}, {"inherits", kw::inherits, "inherits"},
  {"provides", kw::provides, "provides"}, {"meeting", kw::meeting, "meeting"},
  {"of", kw::of, "of"}, {"type", kw::type, "type"},
  {"public", kw::public_, "public"}, {"abstract", kw::abstract, "abstract"},
  {"virtual", kw::virtual_, "virtual"},
  {"replaces", kw::replaces, "replaces"},
  // 'a' is deliberately NOT reserved: it is an English article and the single
  // most common variable name, and unlike most collisions the hyphen rule
  // cannot rescue it ('a-b' is a different name). The receiver clauses are
  // spelled without it.
  {"on", kw::on, "on"},
  {"mutable", kw::mutable_, "mutable"}, {"without", kw::without, "without"},
  {"reference", kw::reference, "reference"}, {"my", kw::my, "my"},
  {"receives", kw::receives, "receives"}, {"returns", kw::returns, "returns"},
  {"more", kw::more, "more"}, {"defaulting", kw::defaulting, "defaulting"},
  {"anonymous", kw::anonymous, "anonymous"}, {"the", kw::the, "the"},
  {"value", kw::value, "value"}, {"variable", kw::variable, "variable"},
  {"constant", kw::constant, "constant"}, {"is", kw::is, "is"},
  {"set", kw::set, "set"}, {"to", kw::to, "to"},
  {"say", kw::say, "say"}, {"give", kw::give, "give"},
  {"evaluate", kw::evaluate, "evaluate"}, {"append", kw::append, "append"},
  {"raise", kw::raise, "raise"}, {"again", kw::again, "again"},
  {"nothing", kw::nothing, "nothing"},
  {"when", kw::when, "when"}, {"then", kw::then, "then"},
  {"otherwise", kw::otherwise, "otherwise"},
  {"provided", kw::provided, "provided"}, {"consider", kw::consider, "consider"},
  {"holding", kw::holding, "holding"}, {"repeat", kw::repeat, "repeat"},
  {"while", kw::while_, "while"}, {"for", kw::for_, "for"},
  {"in", kw::in_, "in"}, {"forever", kw::forever, "forever"},
  {"stop", kw::stop, "stop"}, {"continue", kw::continue_, "continue"},
  {"repeating", kw::repeating, "repeating"},
  {"attempt", kw::attempt, "attempt"}, {"caught", kw::caught, "caught"},
  {"anything", kw::anything, "anything"}, {"always", kw::always, "always"},
  {"own", kw::own, "own"}, {"borrow", kw::borrow, "borrow"},
  {"exclusively", kw::exclusively, "exclusively"}, {"move", kw::move, "move"},
  {"into", kw::into, "into"}, {"allocate", kw::allocate, "allocate"},
  {"release", kw::release, "release"}, {"sized", kw::sized, "sized"},
  {"build", kw::build, "build"}, {"with", kw::with, "with"},
  {"being", kw::being, "being"},
  {"apply", kw::apply, "apply"}, {"across", kw::across, "across"},
  {"now", kw::now, "now"}, {"sleep", kw::sleep, "sleep"},
  {"assembly", kw::assembly, "assembly"}, {"using", kw::using_, "using"},
  {"giving", kw::giving, "giving"}, {"note", kw::note, "note"},
  {"context", kw::context, "context"},
  {"and", kw::and_, "and"}, {"or", kw::or_, "or"}, {"not", kw::not_, "not"},
  {"plus", kw::plus, "plus"}, {"minus", kw::minus, "minus"},
  {"times", kw::times, "times"}, {"over", kw::over, "over"},
  {"remainder", kw::remainder, "remainder"}, {"at", kw::at, "at"},
  {"bitwise", kw::bitwise, "bitwise"}, {"shifted", kw::shifted, "shifted"},
  {"equals", kw::equals, "equals"}, {"does", kw::does, "does"},
  {"xor", kw::xor_, "xor"},
  {"becomes", kw::becomes, "becomes"},
  {"true", kw::true_, "true"}, {"false", kw::false_, "false"},
  // Named characters replace escape sequences entirely. They are ordinary
  // text values, concatenated with 'plus', which is already text
  // concatenation -- so no escaping syntax exists to get wrong.
  {"quote", kw::quote, "quote"}, {"newline", kw::newline, "newline"},
  {"tab", kw::tab, "tab"},
  {"pointer", kw::pointer, "pointer"}, {"array", kw::array, "array"},
  {"owned", kw::owned, "owned"}, {"shared", kw::shared, "shared"},
  {"exclusive", kw::exclusive, "exclusive"}, {"view", kw::view, "view"},
  // Ergonomic types.
  {"number", kw::number, "number"}, {"floating", kw::floating, "floating"},
  {"boolean", kw::boolean, "boolean"}, {"text", kw::text, "text"},
  // The machine-representation escape hatch.
  {"override", kw::override, "override"},
  // Machine types, legal only after 'override'.
  {"integer", kw::integer, "integer"}, {"long", kw::long_, "long"},
  {"byte", kw::byte, "byte"}, {"double", kw::double_, "double"},
  // Reserved and rejected: no cross-route NIL implementation exists.
  {"short", kw::short_, "short"}, {"unsigned", kw::unsigned_, "unsigned"},
  {"decimal", kw::decimal, "decimal"}, {"single", kw::single, "single"},
  {"letter", kw::letter, "letter"},
};

const std::unordered_map<std::string, const keyword_entry *> &keyword_index() {
  static const std::unordered_map<std::string, const keyword_entry *> index = [] {
    std::unordered_map<std::string, const keyword_entry *> built;
    for (const auto &entry : keyword_table) built.emplace(entry.key, &entry);
    return built;
  }();
  return index;
}

} // namespace

std::string fold_key(const std::string &spelling) {
  std::string folded;
  folded.reserve(spelling.size());
  for (unsigned char c : spelling) {
    if (c == '-' || c == '_') continue;
    folded.push_back(ascii_lower(c));
  }
  return folded;
}

std::string nil_symbol(const std::string &spelling) {
  std::string symbol = spelling;
  for (char &c : symbol)
    if (c == '-') c = '_';
  return symbol;
}

kw keyword_for(const std::string &folded_key) {
  const auto &index = keyword_index();
  const auto it = index.find(folded_key);
  return it == index.end() ? kw::not_a_keyword : it->second->word;
}

const char *keyword_name(kw k) {
  for (const auto &entry : keyword_table)
    if (entry.word == k) return entry.spelling;
  return "<identifier>";
}

char lexer::advance() {
  const char c = src_[pos_++];
  if (c == '\n') { ++line_; col_ = 1; } else { ++col_; }
  return c;
}

void lexer::error(int line, int col, std::string message, std::string hint) {
  errors_.push_back({line, col, std::move(message), std::move(hint)});
}

token lexer::make(token_kind kind, const std::string &lexeme, int line,
                  int col) const {
  token t;
  t.kind = kind;
  t.lexeme = lexeme;
  t.line = line;
  t.column = col;
  return t;
}

// Whitespace and comments. '!!!' runs to end of line; there is no block form,
// because a bare '!!!' used as a blank comment line would be indistinguishable
// from a block delimiter.
void lexer::skip_trivia() {
  for (;;) {
    const char c = peek();
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { advance(); continue; }
    if (c == '!' && peek(1) == '!' && peek(2) == '!') {
      while (!at_end() && peek() != '\n') advance();
      continue;
    }
    return;
  }
}

token lexer::scan_identifier() {
  const int line = line_, col = col_;
  std::string spelling;
  spelling.push_back(advance());
  for (;;) {
    const unsigned char c = static_cast<unsigned char>(peek());
    if (name_part(c)) { spelling.push_back(advance()); continue; }
    if (c != '-') break;
    // A hyphen joins words: it must be followed by a letter. Rejecting
    // 'total-1' here is deliberate -- the habit of writing 'total - 1' then
    // fails at the hyphen with a message naming the rule, rather than later as
    // an undefined name.
    const unsigned char next = static_cast<unsigned char>(peek(1));
    if (!name_letter(next)) {
      const int hline = line_, hcol = col_;
      advance(); // consume '-' so scanning continues past it
      if (next == '-') {
        // 'frame--kind': consume the run so the stray-character branch does
        // not report the same mistake a second time.
        while (peek() == '-') advance();
        error(hline, hcol, "a hyphen must join two words of one name",
              "'" + spelling + "--' repeats the hyphen; use a single '-' "
              "between words");
      } else if (ascii_digit(next)) {
        error(hline, hcol,
              "a hyphen joins the words of one name, so it cannot be followed "
              "by a digit",
              "arithmetic uses words: write 'minus' for subtraction, as in '" +
                  spelling + " minus " + std::string(1, static_cast<char>(next)) + "'");
      } else {
        error(hline, hcol,
              "a hyphen must join two words of one name",
              "'" + spelling + "-' ends with a hyphen; remove it or complete "
              "the name");
      }
      break;
    }
    spelling.push_back(advance()); // '-'
    spelling.push_back(advance()); // the letter
  }
  token t = make(token_kind::identifier, spelling, line, col);
  t.key = fold_key(spelling);

  // A keyword never contains a separator, so a spelling that contains one is
  // unconditionally a name. This is what lets 'read-file' be a paragraph name
  // while 'read' stays the import keyword -- the reserved set costs its single
  // word, not the whole family of names built from it.
  //
  // The check is on the SPELLING, not the folded key, and that distinction is
  // the whole point: the key has its separators removed, so folding first
  // would turn 're-ad' into the keyword 'read'. Hyphen and underscore both
  // exempt, because they already fold together for identity ('read-file' and
  // 'read_file' are one name), so exempting only one would leave 'a_s'
  // collapsing onto 'as'.
  const bool has_separator =
      spelling.find('-') != std::string::npos ||
      spelling.find('_') != std::string::npos;
  if (!has_separator) {
    const kw word = keyword_for(t.key);
    if (word != kw::not_a_keyword) {
      t.kind = token_kind::keyword;
      t.word = word;
    }
  }
  return t;
}

// A backtick-escaped identifier is never a keyword, which is how a name that
// collides with a reserved word is written. Case folding still applies: `Value`
// and `value` escape to the same name.
token lexer::scan_backtick_identifier() {
  const int line = line_, col = col_;
  advance(); // opening backtick
  std::string spelling;
  while (!at_end() && peek() != '`') {
    if (peek() == '\n') break;
    spelling.push_back(advance());
  }
  if (at_end() || peek() != '`') {
    error(line, col, "unterminated escaped identifier",
          "an escaped identifier is written `like-this` on one line");
    token t = make(token_kind::invalid, spelling, line, col);
    t.key = fold_key(spelling);
    return t;
  }
  advance(); // closing backtick
  if (spelling.empty()) {
    error(line, col, "an escaped identifier cannot be empty");
    return make(token_kind::invalid, spelling, line, col);
  }
  token t = make(token_kind::identifier, spelling, line, col);
  t.key = fold_key(spelling);
  return t;
}

token lexer::scan_number() {
  const int line = line_, col = col_;
  std::string spelling;
  if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
    spelling.push_back(advance());
    spelling.push_back(advance());
    if (!std::isxdigit(static_cast<unsigned char>(peek()))) {
      error(line, col, "hexadecimal literal has no digits");
    }
    while (std::isxdigit(static_cast<unsigned char>(peek())))
      spelling.push_back(advance());
    token t = make(token_kind::number_literal, spelling, line, col);
    t.key = spelling;
    return t;
  }
  while (ascii_digit(static_cast<unsigned char>(peek())))
    spelling.push_back(advance());
  if (peek() == '.' && ascii_digit(static_cast<unsigned char>(peek(1)))) {
    spelling.push_back(advance());
    while (ascii_digit(static_cast<unsigned char>(peek())))
      spelling.push_back(advance());
  }
  token t = make(token_kind::number_literal, spelling, line, col);
  t.key = spelling;
  return t;
}

// A quoted literal is RAW: there are no escape sequences, so a backslash is
// an ordinary character and 'C:\dir\file' means what it says. A literal
// quote is the 'quote' text constant, concatenated with 'plus'; multi-line or
// quote-heavy content uses a 'begin text' block.
token lexer::scan_string() {
  const int line = line_, col = col_;
  advance(); // opening quote
  std::string contents;
  while (!at_end() && peek() != '"') {
    if (peek() == '\n') break; // a quoted literal never spans a line
    contents.push_back(advance());
  }
  if (at_end() || peek() != '"') {
    error(line, col, "unterminated text literal",
          "a quoted literal stays on one line and has no escape sequences; "
          "use 'plus quote plus' for a literal quote, or a 'begin text' "
          "block for multi-line content");
    return make(token_kind::invalid, contents, line, col);
  }
  advance(); // closing quote
  token t = make(token_kind::string_literal, contents, line, col);
  t.key = contents;
  return t;
}

// 'begin text' at end of line opens a raw block literal.
bool lexer::text_block_follows() const {
  size_t probe = pos_;
  while (probe < src_.size() && (src_[probe] == ' ' || src_[probe] == '\t'))
    ++probe;
  const char *word = "text";
  for (size_t i = 0; i < 4; ++i) {
    if (probe + i >= src_.size()) return false;
    if (ascii_lower(static_cast<unsigned char>(src_[probe + i])) != word[i])
      return false;
  }
  probe += 4;
  // The word must not run on into a longer identifier.
  if (probe < src_.size() &&
      name_part(static_cast<unsigned char>(src_[probe])))
    return false;
  while (probe < src_.size() && (src_[probe] == ' ' || src_[probe] == '\t' ||
                                 src_[probe] == '\r'))
    ++probe;
  return probe >= src_.size() || src_[probe] == '\n';
}

// Consume a raw block: every line after 'begin text' up to a line whose only
// content is 'end text'. The common leading indentation is removed, and
// leading and trailing blank lines are dropped. Nothing inside is escaped,
// which is the point: the terminator is a line-anchored keyword, not a
// character, so no content can accidentally end the literal.
token lexer::scan_text_block(int line, int col) {
  while (!at_end() && peek() != '\n') advance(); // 'text' and the rest of the line
  if (!at_end()) advance();                       // the newline itself

  std::vector<std::string> lines;
  bool terminated = false;
  while (!at_end()) {
    std::string raw;
    while (!at_end() && peek() != '\n') raw.push_back(advance());
    if (!at_end()) advance();
    if (!raw.empty() && raw.back() == '\r') raw.pop_back();
    if (normalise_line(raw) == "end text") { terminated = true; break; }
    lines.push_back(raw);
  }
  if (!terminated) {
    error(line, col, "unterminated 'begin text' block",
          "close it with a line whose only content is 'end text'");
    return make(token_kind::invalid, "", line, col);
  }

  while (!lines.empty() && normalise_line(lines.front()).empty())
    lines.erase(lines.begin());
  while (!lines.empty() && normalise_line(lines.back()).empty()) lines.pop_back();

  size_t indent = std::string::npos;
  for (const auto &text : lines) {
    const size_t first = text.find_first_not_of(" \t");
    if (first == std::string::npos) continue; // blank lines do not set indent
    indent = std::min(indent, first);
  }
  if (indent == std::string::npos) indent = 0;

  std::string contents;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i != 0) contents.push_back('\n');
    contents += lines[i].size() >= indent ? lines[i].substr(indent) : std::string();
  }
  token t = make(token_kind::string_literal, contents, line, col);
  t.key = contents;
  return t;
}

std::vector<token> lexer::scan() {
  std::vector<token> tokens;
  for (;;) {
    skip_trivia();
    if (at_end()) break;
    const int line = line_, col = col_;
    const unsigned char c = static_cast<unsigned char>(peek());

    if (name_start(c)) {
      token word = scan_identifier();
      // 'begin text' at end of line opens a raw block literal rather than a
      // block of declarations.
      if (word.is(kw::begin) && text_block_follows()) {
        tokens.push_back(scan_text_block(word.line, word.column));
        continue;
      }
      tokens.push_back(word);
      continue;
    }
    if (ascii_digit(c)) { tokens.push_back(scan_number()); continue; }
    if (c == '"') { tokens.push_back(scan_string()); continue; }
    if (c == '`') { tokens.push_back(scan_backtick_identifier()); continue; }

    switch (c) {
    case '(': advance(); tokens.push_back(make(token_kind::left_paren, "(", line, col)); continue;
    case ')': advance(); tokens.push_back(make(token_kind::right_paren, ")", line, col)); continue;
    case '[': advance(); tokens.push_back(make(token_kind::left_bracket, "[", line, col)); continue;
    case ']': advance(); tokens.push_back(make(token_kind::right_bracket, "]", line, col)); continue;
    case ',': advance(); tokens.push_back(make(token_kind::comma, ",", line, col)); continue;
    // A statement normally ends at end of line. ';' and '.' end one
    // explicitly, so several statements can share a line -- the cheaper
    // spelling when token count matters, and the one a prose writer reaches
    // for.
    case ';': advance(); tokens.push_back(make(token_kind::statement_end, ";", line, col)); continue;
    case '.': {
      advance();
      // A period between names is member access; a period that ends a
      // sentence is a statement terminator.
      if (name_start(static_cast<unsigned char>(peek()))) {
        tokens.push_back(make(token_kind::dot, ".", line, col));
      } else {
        tokens.push_back(make(token_kind::statement_end, ".", line, col));
      }
      continue;
    }
    default: break;
    }

    if (c == ':' && peek(1) == ':') {
      advance(); advance();
      tokens.push_back(make(token_kind::double_colon, "::", line, col));
      continue;
    }

    // Everything else is an error with a rule-naming message. '-' reaching
    // here means it was not inside an identifier.
    //
    // Two-character operators are consumed whole so that '==' reports once
    // rather than twice. These are the symbols a model trained on C-family
    // code is most likely to emit, so each gets the word that replaces it.
    advance();
    struct symbol_operator { const char *spelling; const char *replacement; };
    static const symbol_operator symbol_operators[] = {
        {"==", "equals"},            {"!=", "does not equal"},
        {"<=", "is at most"},        {">=", "is at least"},
        {"&&", "and"},              {"||", "or"},
        {"<<", "shifted left by"},  {">>", "shifted right by"},
        {"->", "returns"},
    };
    const char pair[3] = {static_cast<char>(c), peek(), '\0'};
    const symbol_operator *matched = nullptr;
    for (const auto &candidate : symbol_operators) {
      if (pair[1] != '\0' && candidate.spelling[0] == pair[0] &&
          candidate.spelling[1] == pair[1]) {
        matched = &candidate;
        break;
      }
    }
    if (matched != nullptr) {
      advance(); // consume the second character of the pair
      error(line, col,
            std::string("'") + matched->spelling +
                "' is not a Matrix operator",
            std::string("write '") + matched->replacement + "' instead");
      tokens.push_back(make(token_kind::invalid, matched->spelling, line, col));
      continue;
    }
    if (c == '-') {
      error(line, col,
            "'-' is not an operator in Matrix; it joins the words of one name",
            "write 'minus' for subtraction and negation, and 'frame-kind' for "
            "a hyphenated name");
    } else if (c == '=') {
      error(line, col, "'=' is not used in Matrix",
            "a binding is written 'value x as integer is 1'; an assignment is "
            "written 'set x to 1'");
    } else if (c == '<' || c == '>' || c == '+' || c == '*' ||
               c == '/' || c == '%' || c == '&' || c == '|' || c == '^' ||
               c == '~' || c == '!') {
      error(line, col,
            std::string("'") + static_cast<char>(c) +
                "' is not a Matrix operator",
            "operators are words: plus, minus, times, over, remainder, "
            "equals, 'is less than', 'bitwise and', 'shifted left by'");
    } else if (c == '{' || c == '}') {
      error(line, col,
            std::string("'") + static_cast<char>(c) + "' is not used in Matrix",
            "blocks are written 'begin <kind>' ... 'end <kind>'");
    } else if (c == '\'') {
      error(line, col, "Matrix has no character literals",
            "text is written in double quotes, and there is no 'letter' "
            "scalar type: \"a\" is a one-letter text value");
    } else if (c == ':') {
      error(line, col, "a single ':' is not used in Matrix",
            "a type is introduced by 'as', and '::' qualifies an area path");
    } else {
      error(line, col,
            "unexpected character in Matrix source",
            "the only punctuation Matrix uses is ( ) [ ] , . and ::");
    }
    tokens.push_back(make(token_kind::invalid, std::string(1, static_cast<char>(c)), line, col));
  }
  token eof = make(token_kind::end_of_file, "", line_, col_);
  tokens.push_back(eof);
  return tokens;
}

} // namespace matrix
