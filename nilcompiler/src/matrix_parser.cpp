#include "matrix_parser.hpp"
#include "numeric_literal.hpp"
#include "pattern_syntax.hpp"
#include "pattern_engine.hpp"
#include "standard_namespace.hpp"

#include <algorithm>

#include <utility>

namespace matrix {
namespace {

struct submission_abort { submission_state state; };

bool exact_phrase_word(const token &value, const char *word) {
  if ((value.kind != matrix::token_kind::identifier && value.kind != matrix::token_kind::keyword) ||
      value.escaped_identifier) return false;
  auto text = value.lexeme;
  for (auto &c : text) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  return text == word;
}

ny::source_span name_span(const token &name) {
  // Keep delimiters around escaped names out of the editable name range.
  return name.escaped_identifier
             ? ny::source_span{name.start + 1, name.length - 2}
             : ny::source_span{name.start, name.length};
}

template <typename Node>
std::shared_ptr<Node> named(std::shared_ptr<Node> node, const token &name) {
  if (node) node->name_span = name_span(name);
  return node;
}

// NOTE ON NAMES: this file sees two unrelated 'token_kind' enums --
// matrix::token_kind from the lexer, and ny::token_kind which is what
// ast_binary_expr/ast_unary_expr store as their operator. They are always
// written qualified below; a 'using' here would make the two indistinguishable
// at a glance, which is exactly the kind of ambiguity this language exists to
// avoid.

// Matrix type names -> the NIL/Ny spelling. Tier one is what a program says
// unless it has a reason not to; tier two is reached only through 'override',
// so every exact width is marked. See matrix/docs/types-and-text.md.
const char *ergonomic_nil_type(kw word) {
  switch (word) {
  case kw::number:   return "i64";
  case kw::floating: return "f64";
  case kw::boolean:  return "bool";
  case kw::text:     return "text";
  case kw::nothing:  return "void";
  default:           return nullptr;
  }
}

const char *machine_nil_type(kw word) {
  switch (word) {
  case kw::integer: return "i32";
  case kw::long_:   return "i64";
  case kw::byte:    return "u8";
  case kw::double_: return "f64";
  default:          return nullptr;
  }
}

// Reserved machine types NIL cannot carry. Diagnosed with the real reason
// rather than parsed as an identifier.
const char *rejected_machine_reason(kw word) {
  switch (word) {
  case kw::unsigned_:
    return "NIL's only unsigned type is 'byte'; u16, u32 and u64 have no "
           "cross-route implementation";
  case kw::decimal:
    return "NIL has no fixed-point type";
  case kw::short_:
    return "i16 has no cross-route implementation";
  case kw::single:
    return "f32 has no cross-route implementation";
  case kw::letter:
    return "NIL has no character type; a one-letter text value is just text, "
           "and letter handling belongs to the standard library";
  default:
    return nullptr;
  }
}

// When a binding declares an array type and its initialiser is an array
// literal, give each element the declared element type. Integer literals infer
// i32 while 'number' is i64, so 'variable xs as array of number is [1, 2, 3]'
// would otherwise be an array<i32> assigned to an array<i64>.
//
// This reads the declared annotation rather than inferring anything, which is
// why it belongs in the parser. Elements that already carry a cast are left
// alone.
static void apply_declared_element_type(const std::optional<std::string> &declared,
                                        const ny::ast_expr_ptr &initializer) {
  if (!declared || !initializer) return;
  if (initializer->kind != ny::ast_node_kind::array_literal_expr_node) return;
  if (declared->rfind("array<", 0) != 0 || declared->back() != '>') return;
  const std::string element =
      declared->substr(6, declared->size() - 7);
  // Only scalar elements: an aggregate element is not a cast target.
  if (element != "i32" && element != "i64" && element != "u8" &&
      element != "f64" && element != "bool")
    return;
  auto &literal = static_cast<ny::ast_array_literal_expr &>(*initializer);
  for (auto &item : literal.elements) {
    if (!item) continue;
    if (item->kind == ny::ast_node_kind::cast_expr_node) continue;
    if (item->kind != ny::ast_node_kind::number_literal_node &&
        item->kind != ny::ast_node_kind::boolean_literal_node)
      continue;
    item = std::make_shared<ny::ast_cast_expr>(item, element, item->line);
  }
}

// Book paths use '::' in NIL. Ny's module declaration normalises BOTH '.' and
// '::' to '::' (parser.cpp, parse_module_declaration), so Matrix must do the
// same: otherwise a Matrix book and an Ny module with the same written name
// produce different schema names -- 'stage.four::point' against
// 'stage::four::point' -- and could never interoperate. This is the cross
// language identity the whole exercise depends on, and it is invisible from
// comparing program output.
//
// Hyphens still become underscores, since Ny identifiers admit no hyphen.
// Member access and match patterns are NOT paths and keep their dots.
std::string nil_path(const std::string &written) {
  std::string path;
  size_t index = 0;
  while (index < written.size()) {
    if (written.compare(index, 2, "::") == 0) {
      path += "::";
      index += 2;
      continue;
    }
    if (written[index] == '.') {
      path += "::";
      ++index;
      continue;
    }
    path.push_back(written[index] == '-' ? '_' : written[index]);
    ++index;
  }
  return ny::canonical_std_namespace(std::move(path));
}

} // namespace

parser::parser(const std::string &source, const parser_options &opts)
    : filename_(opts.filename), signature_cursor_(opts.signature_cursor),
      source_size_(source.size()), submission_requested_(opts.submission) {
  if (opts.submission && source.size() > submission_source_limit) {
    token eof;
    eof.kind = matrix::token_kind::end_of_file;
    tokens_.push_back(eof);
    submission_limit_error(eof, "submission exceeds the 65536-byte source limit");
    return;
  }
  if (signature_cursor_ && *signature_cursor_ <= source.size()) {
    auto start = *signature_cursor_;
    while (start && (source[start - 1] == ' ' || source[start - 1] == '\t')) --start;
    signature_blank_start_ = start;
  }
  lexer lex(source, 0, opts.submission);
  tokens_ = lex.scan();
  if (opts.submission && tokens_.size() > submission_token_limit + 1) {
    submission_limit_error(tokens_[submission_token_limit],
                           "submission exceeds the 4096-token limit");
    return;
  }
  prescan_declarations(opts.submission);
  for (const auto &e : lex.errors()) {
    lexical_incomplete_ |= e.incomplete;
    lexical_invalid_ |= !e.incomplete;
    if (e.incomplete) incomplete_tokens_.push_back(e.start);
    matrix::diagnostic rich;
    rich.level = matrix::severity::error;
    rich.code = "MX0001";
    rich.message = e.message;
    rich.primary.line = e.line;
    rich.primary.column = e.column;
    rich.primary.length = 1;
    rich.hint = e.hint;
    rich_.push_back(std::move(rich));

    ny::diagnostic d;
    d.line = e.line;
    d.message = e.message;
    if (!e.hint.empty()) d.hint = e.hint;
    diagnostics_.push_back(std::move(d));
  }
}

// A declaration's spelling is the canonical NIL symbol for its name. This
// pre-scan collects those spellings so that a reference written in any case or
// with either separator resolves to it -- including a reference that appears
// before the declaration.
//
// Only declaring positions are recorded. A name this page never declares falls
// back to its written spelling in resolve_name, which is the right answer for
// the standard library: 'read-regular-bytes' becomes 'read_regular_bytes',
// matching Ny's snake_case declaration.
void parser::prescan_declarations(bool submission) {
  const auto record = [&](const token &t) {
    if (!t.is_name()) return;
    declared_.emplace(t.key, nil_symbol(t.lexeme));
  };
  const auto record_type = [&](const token &t) {
    if (!t.is_name()) return;
    declared_types_.emplace(t.key, t.lexeme);
  };
  for (size_t i = 0; i + 1 < tokens_.size(); ++i) {
    const token &here = tokens_[i];
    const token &next = tokens_[i + 1];

    // A top-level declaration: '<name> receives|returns|begin|means ...'
    if (here.is_name() &&
        (next.is(kw::receives) || next.is(kw::returns) || next.is(kw::begin) ||
         next.is(kw::means)))
      record(here);

    // Any TOP-LEVEL declaration, for collision detection. Two declarations in
    // one book whose folded keys match but whose spellings differ are one
    // name, which the checker reports only as "already declared in this
    // scope" without saying that case folding is the reason.
    if (!submission && here.is_name() && here.column == 1 &&
        (next.is(kw::receives) || next.is(kw::returns) || next.is(kw::begin) ||
         next.is(kw::means) || next.is(kw::inherits) || next.is(kw::provides)))
      record_type(here);
    if (!submission && (here.is(kw::constant) || here.is(kw::value) ||
         here.is(kw::variable)) && here.column <= 8 && next.is_name())
      record_type(next);

    // A type declaration specifically: '<name> begin record|choice|class' or
    // '<name> inherits ...'.
    if (here.is_name() &&
        (here.is_name() && next.is(kw::begin) && i + 2 < tokens_.size() &&
         (tokens_[i + 2].is(kw::record) || tokens_[i + 2].is(kw::choice) ||
          tokens_[i + 2].is(kw::class_))))
      record_type(here);
    if (here.is_name() && next.is(kw::inherits))
      record_type(here);

    // '<name> of type T begin ...' -- a generic record, choice or paragraph.
    if (here.is_name() && next.is(kw::of) && i + 2 < tokens_.size() &&
        tokens_[i + 2].is(kw::type))
      record(here);

    // 'field <name> as', 'value|variable|constant <name>', 'allocate <name> as'
    if ((here.is(kw::field) || here.is(kw::value) || here.is(kw::variable) ||
         here.is(kw::constant) || here.is(kw::allocate) || here.is(kw::own)) &&
        next.is_name())
      record(next);

    // A submission's tuple destructuring names are locals just like scalar
    // bindings. Keep their declared spelling for folded references as well.
    if (submission) {
      std::size_t opening = tokens_.size();
      if ((here.is(kw::value) || here.is(kw::variable) || here.is(kw::constant)) &&
          next.kind == token_kind::left_paren)
        opening = i + 1;
      if (here.is(kw::for_) && next.reads("each") && i + 2 < tokens_.size() &&
          tokens_[i + 2].kind == token_kind::left_paren)
        opening = i + 2;
      for (auto binding = opening + (opening < tokens_.size());
           binding < tokens_.size() && tokens_[binding].is_name(); binding += 2) {
        record(tokens_[binding]);
        if (binding + 1 >= tokens_.size() || tokens_[binding + 1].kind != token_kind::comma)
          break;
      }
    }

    // A parameter binding: '<name> as <type>' anywhere in a header.
    if (here.is_name() && next.is(kw::as))
      record(here);

    // Loop variables, bound payloads, moves and borrows.
    if ((here.is(kw::for_) || here.is(kw::holding) || here.is(kw::into) ||
         here.is(kw::borrow)) && next.is_name())
      record(next);
    if (here.is(kw::for_) && next.reads("each") && i + 2 < tokens_.size())
      record(tokens_[i + 2]);
    if (here.is(kw::from) && next.reads("each") && i + 2 < tokens_.size())
      record(tokens_[i + 2]);
  }
}

// A value may not be named after a declared type in different case. Folding
// makes them one name, so 'value tagged is build Tagged ...' would make every
// later 'tagged' resolve to the type.
void parser::reject_type_name_collision(const token &at,
                                        const std::string &written) {
  const auto it = declared_types_.find(fold_key(written));
  if (it == declared_types_.end()) return;
  if (it->second == written) return; // the declaration itself
  error(at,
        "'" + written + "' is already the name of a type, declared as '" +
            it->second + "'",
        "names fold case and separators, so these are one name; rename one of "
        "them (for example '" + written + "-value')");
}

std::string parser::resolve_name(const std::string &written) const {
  // A QUALIFIED name is a path: its dots normalise to '::', because Ny's
  // module declaration does the same and NIL identity uses '::'. A book-local
  // name is looked up against this page's declarations so that any case or
  // separator spelling reaches the one declared symbol.
  //
  // A qualified name is never looked up: it names a declaration on another
  // page, whose spelling this page cannot see. Its written spelling is
  // therefore authoritative, which is exactly why a Matrix client must write
  // an Ny name the way Ny declared it.
  if (written.find('.') != std::string::npos ||
      written.find("::") != std::string::npos)
    return nil_path(written);
  const auto it = declared_.find(fold_key(written));
  return it == declared_.end() ? nil_symbol(written) : it->second;
}

// ---------------------------------------------------------------------------
// Token helpers
// ---------------------------------------------------------------------------

const token &parser::peek(size_t ahead) const {
  const size_t index = pos_ + ahead;
  return index < tokens_.size() ? tokens_[index] : tokens_.back();
}
const token &parser::previous() const {
  return pos_ > 0 ? tokens_[pos_ - 1] : tokens_.front();
}
bool parser::at_end() const {
  return peek().kind == matrix::token_kind::end_of_file;
}
bool parser::check(matrix::token_kind kind) const { return peek().kind == kind; }
bool parser::check(kw word) const { return peek().is(word); }
bool parser::check_word(const char *folded, size_t ahead) const {
  return peek(ahead).key == folded;
}
bool parser::match(matrix::token_kind kind) {
  if (!check(kind)) return false;
  advance();
  return true;
}
bool parser::match(kw word) {
  if (!check(word)) return false;
  advance();
  return true;
}
bool parser::match_word(const char *folded) {
  if (!check_word(folded)) return false;
  advance();
  return true;
}
const token &parser::advance() {
  if (!at_end()) ++pos_;
  return previous();
}

void parser::error(const token &at, std::string message, std::string hint,
                   std::string code) {
  // A span, not just a line: the lexer records a column on every token and
  // the lexeme gives its width, so the caret can sit exactly under the text
  // that is wrong.
  matrix::diagnostic rich;
  rich.level = matrix::severity::error;
  rich.code = std::move(code);
  rich.message = message;
  rich.primary.line = at.line;
  rich.primary.column = at.column;
  rich.primary.length = static_cast<int>(at.lexeme.size());
  rich.hint = hint;
  rich_.push_back(std::move(rich));

  ny::diagnostic d;
  d.line = at.line;
  d.message = std::move(message);
  if (!hint.empty()) d.hint = std::move(hint);
  diagnostics_.push_back(std::move(d));
  if (parsing_submission_) {
    const bool lexical_end = at.kind == matrix::token_kind::invalid &&
        std::find(incomplete_tokens_.begin(), incomplete_tokens_.end(), at.start) !=
            incomplete_tokens_.end();
    throw submission_abort{at.kind == matrix::token_kind::end_of_file || lexical_end
                               ? submission_state::incomplete
                               : submission_state::invalid};
  }
}

void parser::submission_limit_error(const token &at, const char *message) {
  lexical_invalid_ = true;
  // Resource limits cannot be repaired by appending more source, even at EOF.
  const bool was_parsing = parsing_submission_;
  parsing_submission_ = false;
  error(at, message, {}, "MX1001");
  parsing_submission_ = was_parsing;
  if (was_parsing) throw submission_abort{submission_state::invalid};
}

parser::submission_depth_guard::submission_depth_guard(parser &value)
    : owner(value), active(value.parsing_submission_) {
  if (active) {
    if (owner.submission_depth_ >= submission_depth_limit)
      owner.submission_limit_error(owner.peek(),
                                  "submission exceeds the 64-level parser nesting limit");
    ++owner.submission_depth_;
  }
}
parser::submission_depth_guard::~submission_depth_guard() {
  if (active) --owner.submission_depth_;
}

// Keywords an agent is likely to bring from another language, with the Matrix
// spelling. Every entry here is a word Matrix does NOT reserve, so it arrives
// as a name; without this it produces only "expected a statement, found 'fn'".
bool parser::report_foreign_keyword(const token &at) {
  struct foreign { const char *key; const char *advice; };
  static const foreign table[] = {
      // Declaring a function
      {"fn", "a paragraph: '<name> receives <a> as <type> returns <type> begin paragraph'"},
      {"func", "a paragraph: '<name> ... begin paragraph' ... 'end paragraph'"},
      {"function", "a paragraph: '<name> ... begin paragraph' ... 'end paragraph'"},
      {"def", "a paragraph: '<name> ... begin paragraph' ... 'end paragraph'"},
      {"sub", "a paragraph: '<name> ... begin paragraph' ... 'end paragraph'"},
      {"method", "a paragraph inside a class, implementation or extension"},
      {"procedure", "a paragraph: '<name> ... begin paragraph'"},
      // Statements
      {"return", "'give <value>', or 'give nothing'"},
      {"print", "'say <value>' -- exactly one value"},
      {"println", "'say <value>' -- exactly one value"},
      {"printf", "'say <value>'; build the text with 'plus'"},
      {"echo", "'say <value>'"},
      {"let", "'value <name> is ...' for immutable, 'variable' for mutable"},
      {"var", "'variable <name> is ...'"},
      {"dim", "'variable <name> as <type> is ...'"},
      {"new", "'build <record> with <field> being <value>'"},
      {"delete", "'release <pointer>' for raw memory"},
      {"free", "'release <pointer>'"},
      {"assert", "a 'when ... then raise ...' check, or ny.lang.core's assert paragraphs"},
      // Control flow
      {"if", "'when <condition> then' ... 'end when'"},
      {"elif", "'otherwise when <condition> then'"},
      {"elsif", "'otherwise when <condition> then'"},
      {"else", "'otherwise', or 'otherwise when <condition> then'"},
      {"endif", "'end when'"},
      {"fi", "'end when'"},
      {"switch", "'consider <value>' ... 'end consider'"},
      {"match", "'consider <value>' ... 'end consider'"},
      {"case", "'when <pattern> then' inside a 'consider'"},
      {"default", "'otherwise' inside a 'consider'"},
      {"do", "'repeat while <condition>' ... 'end repeat'"},
      {"foreach", "'repeat for each <name> in <collection>'"},
      {"break", "'stop repeating'"},
      {"goto", "nothing -- Matrix has no jumps"},
      // Declarations
      {"import", "'read <book>', or 'read <names> from <book>'"},
      {"include", "'read <book>'"},
      {"require", "'read <book>'"},
      {"export", "'public' on the declaration, or 'cite <names> from <book>'"},
      {"module", "'book <name>'"},
      {"namespace", "'book <name>'"},
      {"package", "'library <name>' for a package, 'book' for a namespace"},
      {"struct", "'<name> begin record' ... 'end record'"},
      {"enum", "'<name> begin choice' ... 'end choice'"},
      {"union", "'<name> begin choice' ... 'end choice'"},
      {"interface", "'<name> begin contract' ... 'end contract'"},
      {"trait", "'<name> begin contract' ... 'end contract'"},
      {"protocol", "'<name> begin contract' ... 'end contract'"},
      {"impl", "'<type> begin implementation', or 'provides <contract>'"},
      {"extends", "'inherits <base>'"},
      {"implements", "'provides <contract>'"},
      {"typedef", "'<name> means <type>'"},
      {"static", "'forever' is reserved for this and is not implemented"},
      // Receivers and faults
      {"self", "'my <member>', or bare 'my' for the receiver"},
      {"this", "'my <member>', or bare 'my' for the receiver"},
      {"try", "'attempt' ... 'end attempt'"},
      {"catch", "'caught <FaultRecord> as <name>'"},
      {"except", "'caught <FaultRecord> as <name>'"},
      {"rescue", "'caught <FaultRecord> as <name>'"},
      {"finally", "'always'"},
      {"ensure", "'always'"},
      {"throw", "'raise <record> with <field> being <value>'"},
      {"raise_", "'raise <record> with <field> being <value>'"},
      // Values and types
      {"null", "nothing -- Matrix has no null; use a choice with an absent case"},
      {"nil", "nothing -- Matrix has no null; use a choice with an absent case"},
      {"none", "nothing -- Matrix has no null; use a choice with an absent case"},
      {"undefined", "nothing -- every binding is initialised"},
      {"void", "'nothing'"},
      {"int", "'number', or 'override integer' for exactly 32 bits"},
      {"i32", "'override integer' -- 'i32' is the Ny spelling"},
      {"i64", "'number', or 'override long' -- 'i64' is the Ny spelling"},
      {"u8", "'override byte' -- 'u8' is the Ny spelling"},
      {"f64", "'floating', or 'override double' -- 'f64' is the Ny spelling"},
      {"uint", "unsigned widths beyond 'override byte' are not available"},
      {"float", "'floating'"},
      {"string", "'text'"},
      {"str", "'text'"},
      {"char", "'text' -- there is no character type; \"a\" is one-letter text"},
      {"bool", "'boolean'"},
      {"vec", "'array of <type>'"},
      {"list", "'array of <type>'"},
      {"map", "a dictionary from ny.lang.collections"},
      {"dict", "a dictionary from ny.lang.collections"},
  };
  for (const auto &entry : table) {
    if (at.key != entry.key) continue;
    error(at,
          "'" + at.lexeme + "' is not how Matrix spells this",
          std::string("use ") + entry.advice,
          "MX0103");
    return true;
  }
  return false;
}

void parser::expected_name(const char *context) {
  const token &at = peek();
  if (at.kind == matrix::token_kind::keyword) {
    // Hyphenating is the usual fix and is always available, because a keyword
    // never contains a separator: 'shifted' is reserved, 'shift-result' is
    // not. Suggest it concretely rather than describing the rule.
    error(at,
          "'" + at.lexeme + "' is a reserved word and cannot be used as a name",
          "a keyword never contains a separator, so hyphenating works: write '" +
              at.lexeme + "-value' or 'the-" + at.lexeme +
              "'. A backtick escape (`" + at.lexeme +
              "`) is a last resort.",
          "MX0101");
    return;
  }
  error(at,
        std::string("expected a name ") + context + ", found '" + at.lexeme +
            "'",
        "a name begins with a letter or underscore and may join words with "
        "'-'",
        "MX0102");
}

bool parser::expect(kw word, const char *context) {
  if (match(word)) return true;
  error(peek(), std::string("expected '") + keyword_name(word) + "' " + context +
                    ", found '" + peek().lexeme + "'");
  return false;
}

bool parser::expect(matrix::token_kind kind, const char *what,
                    const char *context) {
  if (match(kind)) return true;
  error(peek(), std::string("expected ") + what + " " + context + ", found '" +
                    peek().lexeme + "'");
  return false;
}

// A statement ends at end of line; ';' and '.' end one explicitly. Both are
// optional, so consuming them is all that is needed.
void parser::skip_terminators() {
  while (check(matrix::token_kind::statement_end)) advance();
}

void parser::recover_to_declaration() {
  // Resynchronise at the next token that can begin a top-level declaration.
  while (!at_end()) {
    const token &t = peek();
    if (t.is(kw::read) || t.is(kw::cite) || t.is(kw::public_) ||
        t.is(kw::value) || t.is(kw::variable) || t.is(kw::constant) ||
        t.is(kw::forever) || t.is(kw::note) || t.is(kw::context))
      return;
    // A declaration also begins with a bare name followed by a clause word.
    if (t.is_name() &&
        (peek(1).is(kw::receives) || peek(1).is(kw::returns) ||
         peek(1).is(kw::begin) || peek(1).is(kw::means)))
      return;
    advance();
  }
}

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

// A type is tier one by name, or tier two behind 'override'. A bare machine
// type is an error: 'as byte' is rejected and 'as override byte' is the
// spelling, so every exact width is marked and greppable.
std::optional<std::string> parser::parse_type() {
  submission_depth_guard depth(*this);
  const token &start = peek();

  if (match(kw::tuple)) {
    if (!expect(kw::of, "after 'tuple'") ||
        !expect(matrix::token_kind::left_paren, "'('", "before tuple element types"))
      return std::nullopt;
    std::string type = "tuple<";
    size_t count = 0;
    while (!check(matrix::token_kind::right_paren) && !at_end()) {
      auto element = parse_type();
      if (!element) return std::nullopt;
      if (count++) type += ", ";
      type += *element;
      if (!match(matrix::token_kind::comma)) break;
    }
    expect(matrix::token_kind::right_paren, "')'", "after tuple element types");
    if (!count) {
      error(start, "a tuple requires at least one element type");
      return std::nullopt;
    }
    return type + ">";
  }

  if (match(kw::override)) {
    const token &machine = peek();
    if (const char *reason = rejected_machine_reason(machine.word)) {
      error(machine,
            std::string("'") + machine.lexeme + "' is reserved but not "
            "available: " + reason,
            "use 'number', 'floating', or an available override type: "
            "integer, long, byte, double");
      advance();
      return std::nullopt;
    }
    if (const char *nil = machine_nil_type(machine.word)) {
      advance();
      return std::string(nil);
    }
    error(machine, "'override' must be followed by a machine type",
          "the available machine types are integer, long, byte and double");
    return std::nullopt;
  }

  if (const char *nil = ergonomic_nil_type(start.word)) {
    advance();
    return std::string(nil);
  }

  if (const char *nil = machine_nil_type(start.word)) {
    error(start,
          std::string("'") + start.lexeme + "' is a machine type and needs "
          "'override'",
          std::string("write 'override ") + start.lexeme + "', or use the "
          "ergonomic type: " +
              (std::string(nil) == "f64" ? "floating" : "number"));
    advance();
    return std::string(nil); // continue with the intended type
  }

  if (const char *reason = rejected_machine_reason(start.word)) {
    error(start,
          std::string("'") + start.lexeme + "' is reserved but not available: " +
              reason);
    advance();
    return std::nullopt;
  }

  // array of T
  if (match(kw::array)) {
    if (!expect(kw::of, "after 'array'")) return std::nullopt;
    auto element = parse_type();
    if (!element) return std::nullopt;
    return "array<" + *element + ">";
  }

  // owned array of T
  if (match(kw::owned)) {
    if (!expect(kw::array, "after 'owned'")) return std::nullopt;
    if (!expect(kw::of, "after 'owned array'")) return std::nullopt;
    auto element = parse_type();
    if (!element) return std::nullopt;
    return "owned<" + *element + ">";
  }

  // pointer to T
  if (match(kw::pointer)) {
    if (!expect(kw::to, "after 'pointer'")) return std::nullopt;
    auto element = parse_type();
    if (!element) return std::nullopt;
    return "ptr<" + *element + ">";
  }

  // reference to T
  if (match(kw::reference)) {
    if (!expect(kw::to, "after 'reference'")) return std::nullopt;
    auto element = parse_type();
    if (!element) return std::nullopt;
    return "ref<" + *element + ">";
  }

  // shared view of T / exclusive view of T
  if (check(kw::shared) || check(kw::exclusive)) {
    const bool exclusive = peek().is(kw::exclusive);
    advance();
    if (!expect(kw::view, "after 'shared'/'exclusive'")) return std::nullopt;
    if (!expect(kw::of, "after 'view'")) return std::nullopt;
    auto element = parse_type();
    if (!element) return std::nullopt;
    return (exclusive ? "borrow_mut<" : "borrow<") + *element + ">";
  }

  // A nominal type, optionally applied to type arguments: 'envelope of number'.
  if (start.is_name()) {
    // A qualified type name is a path; an unqualified one is a plain symbol.
    const std::string written = parse_dotted_name();
    std::string name = (written.find('.') != std::string::npos ||
                        written.find("::") != std::string::npos)
                           ? nil_path(written)
                           : nil_symbol(written);
    if (match(kw::of)) {
      std::vector<std::string> args;
      do {
        auto arg = parse_type();
        if (!arg) return std::nullopt;
        args.push_back(*arg);
      } while (match(matrix::token_kind::comma));
      name += "<";
      for (size_t i = 0; i < args.size(); ++i) {
        if (i) name += ", ";
        name += args[i];
      }
      name += ">";
    }
    return name;
  }

  error(start, "expected a type, found '" + start.lexeme + "'",
        "tier one is text, number, floating, boolean, nothing; a machine type "
        "needs 'override'");
  return std::nullopt;
}

// A path segment after a separator may be a KEYWORD: 'ny.lang.text',
// 'ny.lang.io.file' and 'demo.text' all contain one, and refusing them would
// make large parts of the standard library unnameable.
//
// This is safe because the lexer only emits a 'dot' token when a name
// character follows it -- a period at end of line or before a non-name is
// already a statement terminator. So if a dot token is present, the next token
// is a name or a keyword, and either is a legitimate segment.
//
// The FIRST segment stays name-only: a book called 'text' would shadow the
// type, and nothing needs it.
std::string parser::parse_dotted_name() {
  std::string name;
  if (!peek().is_name()) {
    expected_name("here");
    return name;
  }
  name = advance().lexeme;
  const auto segment_follows = [&]() {
    const token &next = peek(1);
    return next.is_name() || next.kind == matrix::token_kind::keyword;
  };
  for (;;) {
    if (check(matrix::token_kind::dot) && segment_follows()) {
      advance();
      name += "." + advance().lexeme;
      continue;
    }
    if (check(matrix::token_kind::double_colon) && segment_follows()) {
      advance();
      name += "::" + advance().lexeme;
      continue;
    }
    break;
  }
  return name;
}

// ---------------------------------------------------------------------------
// Declarations
// ---------------------------------------------------------------------------

// 'book a.b' -> module a.b ; 'library a.b' -> package a.b.
std::shared_ptr<ny::ast_decl> parser::parse_book_or_library() {
  const int line = peek().line;
  const bool is_library = peek().is(kw::library);
  advance();
  const std::string name = nil_path(parse_dotted_name());

  // 'requires <authority>, ...' is reserved; see
  // matrix/docs/authority-and-sandboxing.md. It parses and is not yet enforced,
  // so it is rejected rather than silently accepted as a promise.
  if (check(kw::requires_)) {
    error(peek(),
          "'requires' is reserved and not implemented yet",
          "authority declarations need a NIL manifest section and a decision "
          "record first; see matrix/docs/authority-and-sandboxing.md");
    advance();
    while (!at_end() && (peek().is_name() || check(matrix::token_kind::comma)))
      advance();
  }
  skip_terminators();
  // Ny has one namespace construct; a library lowers to a module declaration
  // whose contents are re-exports, which is what 'package' already means.
  return std::make_shared<ny::ast_module_decl>(name, is_library, line);
}

// 'read <book> [as alias]' | 'read a, b from <book>' | 'read everything from <book>'
std::shared_ptr<ny::ast_decl> parser::parse_read() {
  const int line = peek().line;
  advance(); // 'read'

  if (match(kw::everything)) {
    if (!expect(kw::from, "after 'read everything'")) return nullptr;
    const std::string path = nil_path(parse_dotted_name());
    skip_terminators();
    return std::make_shared<ny::ast_import_decl>(path, std::vector<std::string>{},
                                                 true, line);
  }

  // Distinguish 'read a, b from book' from 'read book [as alias]' by looking
  // for a ',' or a 'from' before any dot.
  size_t probe = pos_;
  bool selective = false;
  while (probe < tokens_.size()) {
    const token &t = tokens_[probe];
    if (t.kind == matrix::token_kind::comma) { selective = true; break; }
    if (t.is(kw::from)) { selective = true; break; }
    if (t.kind == matrix::token_kind::dot ||
        t.kind == matrix::token_kind::statement_end ||
        t.is(kw::as) || t.kind == matrix::token_kind::end_of_file)
      break;
    ++probe;
  }

  if (selective) {
    std::vector<std::string> names;
    do {
      if (!peek().is_name()) {
        expected_name("to read");
        break;
      }
      names.push_back(nil_symbol(advance().lexeme));
    } while (match(matrix::token_kind::comma));
    if (!expect(kw::from, "after the names being read")) return nullptr;
    const std::string path = nil_path(parse_dotted_name());
    skip_terminators();
    return std::make_shared<ny::ast_import_decl>(path, names, false, line);
  }

  const std::string path = nil_path(parse_dotted_name());
  if (match(kw::as)) {
    if (!peek().is_name()) {
      expected_name("as the alias after 'as'");
      return nullptr;
    }
    const token &alias_token = peek();
    const std::string alias = nil_symbol(advance().lexeme);
    skip_terminators();
    return named(std::make_shared<ny::ast_namespace_alias_decl>(alias, path, false,
                                                             line), alias_token);
  }
  skip_terminators();
  return std::make_shared<ny::ast_import_decl>(path, std::vector<std::string>{},
                                               false, line);
}

// 'cite a, b from <book>' | 'cite everything from <book>' -> a public import,
// which is how Ny spells a re-export.
std::shared_ptr<ny::ast_decl> parser::parse_cite() {
  const int line = peek().line;
  advance(); // 'cite'
  bool glob = false;
  std::vector<std::string> names;
  if (match(kw::everything)) {
    glob = true;
  } else {
    do {
      if (!peek().is_name()) {
        expected_name("to cite");
        break;
      }
      names.push_back(nil_symbol(advance().lexeme));
    } while (match(matrix::token_kind::comma));
  }
  if (!expect(kw::from, "after the names being cited")) return nullptr;
  const std::string path = nil_path(parse_dotted_name());
  skip_terminators();
  return std::make_shared<ny::ast_import_decl>(
      path, names, glob, line, ny::visibility_kind::visibility_public);
}

// Parameters: 'receives a as T, b as T' with 'and' accepted in place of ',',
// plus a trailing 'more name as T' variadic.
std::vector<ny::ast_param>
parser::parse_parameters(std::vector<std::string> &names) {
  std::vector<ny::ast_param> params;
  for (;;) {
    const int line = peek().line;
    bool variadic = false;
    if (match(kw::more)) variadic = true;
    // 'variable x as T' makes the callee's own binding mutable.
    const bool mutable_binding = match(kw::variable);

    if (!peek().is_name()) {
      error(peek(), "expected a parameter name, found '" + peek().lexeme + "'",
            "a parameter is written '<name> as <type>'");
      break;
    }
    const token &name_token = peek();
    const std::string name = nil_symbol(advance().lexeme);
    reject_type_name_collision(name_token, name_token.lexeme);
    if (!expect(kw::as, "after a parameter name")) break;
    auto type = parse_type();
    // A variadic tail collects its arguments into an array, so the declared
    // element type becomes array<T> -- the same thing Ny's parser does for
    // '..name: T'. Without this the body sees a scalar and 'the length of' it
    // fails.
    if (variadic && type) type = "array<" + *type + ">";

    std::optional<ny::ast_expr_ptr> default_value;
    if (match(kw::defaulting)) {
      if (!expect(kw::to, "after 'defaulting'")) break;
      // Deliberately restricted to a literal atom: an arbitrary expression
      // here would make 'and' ambiguous between another parameter and a
      // boolean operand. Ny rejects omitted arguments anyway.
      default_value = (check(kw::negative) || check(kw::minus) ||
                       check(matrix::token_kind::numeric_sign))
          ? parse_unary() : parse_primary();
    }

    params.emplace_back(name, type, mutable_binding, line, default_value,
                        variadic);
    params.back().name_span = name_span(name_token);
    names.push_back(name);

    if (variadic) break; // a variadic tail is final
    if (match(matrix::token_kind::comma)) continue;
    // 'and' separates parameter declarations only, where '<name> as <type>'
    // keeps it unambiguous.
    if (check(kw::and_) && peek(2).is(kw::as)) { advance(); continue; }
    break;
  }
  return params;
}

// '<name> [receives ...] [receiver clause] [returns T] begin paragraph ... end'
std::shared_ptr<ny::ast_function_decl>
parser::parse_paragraph(bool is_public, int line, const std::string &name,
                        paragraph_context context, bool is_abstract,
                        bool is_virtual, bool is_override) {
  std::vector<std::string> param_names;
  std::vector<ny::ast_param> params;
  if (match(kw::receives)) params = parse_parameters(param_names);

  // A receiver clause changes the shape of the synthesised 'self', or removes
  // it: 'without receiver' is an associated function, callable through a
  // bounded type parameter and never on an instance.
  bool wants_receiver = context != paragraph_context::free;
  bool reference_receiver = false;
  bool mutable_receiver = false;
  if (match(kw::without)) {
    if (!match_word("receiver"))
      error(peek(), "expected 'receiver' after 'without'");
    wants_receiver = false;
  } else if (match(kw::on)) {
    if (match(kw::reference)) {
      reference_receiver = true;
    } else {
      mutable_receiver = match(kw::mutable_);
      if (!match_word("receiver"))
        error(peek(), "expected 'receiver' or 'reference' after 'on'",
              "the receiver clauses are 'on receiver', 'on mutable receiver', "
              "'on reference' and 'without receiver'");
    }
    if (context == paragraph_context::free)
      error(peek(), "a receiver clause needs an enclosing class, contract, "
                    "implementation or extension");
  }

  std::optional<std::string> return_type;
  if (match(kw::returns)) {
    return_type = parse_type();
  } else {
    return_type = std::string("void");
  }

  // 'main' is the one paragraph whose result lowers to i32, because that is
  // the NIL entry contract. Documented in matrix/docs/types-and-text.md.
  if (context == paragraph_context::free && fold_key(name) == "main" &&
      return_type && *return_type == "i64")
    return_type = std::string("i32");

  // Synthesise the explicit first 'self' parameter Ny requires, so the NIL
  // CALL_METHOD arity matches a hand-written Ny method exactly. 'my' in the
  // body resolves against this parameter.
  if (wants_receiver) {
    ny::ast_param receiver("self",
                           reference_receiver
                               ? std::optional<std::string>("ref<Self>")
                               : std::nullopt,
                           mutable_receiver, line, std::nullopt);
    params.insert(params.begin(), receiver);
  }

  const bool body_forbidden = context == paragraph_context::contract_member;
  const bool body_optional = body_forbidden || is_abstract;

  std::shared_ptr<ny::ast_stmt> body;
  if (check(kw::begin)) {
    if (body_forbidden)
      error(peek(), "a contract member is a signature and has no body",
            "default method bodies are rejected in Ny, so Matrix has no "
            "spelling for one");
    advance(); // 'begin'
    if (!expect(kw::paragraph, "after 'begin'")) return nullptr;
    body = parse_block(kw::paragraph);
  } else if (!body_optional) {
    error(peek(), "expected 'begin paragraph' to open the body",
          context == paragraph_context::free
              ? "a bodyless paragraph is an intrinsic-backed declaration, "
                "which is not lowered yet"
              : "only an abstract member may omit its body");
    return nullptr;
  } else if (is_abstract) {
    // An abstract member declares no executable body.
    skip_terminators();
  }

  // A member's name is scoped to its type, not to the page, so it keeps its
  // WRITTEN spelling and is not folded against the page-wide map -- the same
  // rule member access follows, and for the same reason. Folding it aliases
  // unrelated names: a contract 'Fingerprint' and its method 'fingerprint'
  // are one key, and the method would silently take the contract's spelling.
  // Only a free paragraph is a top-level name.
  const std::string emitted = context == paragraph_context::free
                                  ? resolve_name(name)
                                  : nil_symbol(name);
  auto decl = std::make_shared<ny::ast_function_decl>(
      emitted, params, return_type, body,
      std::vector<ny::ast_generic_param>{}, is_public, line,
      std::vector<ny::ast_annotation>{}, is_virtual, is_abstract, is_override);
  decl->source_file = filename_;
  return decl;
}

// A book-level binding becomes a module global.
std::shared_ptr<ny::ast_decl> parser::parse_book_binding(bool is_public) {
  const int line = peek().line;
  const bool mutable_binding = peek().is(kw::variable);
  const bool constant_binding = peek().is(kw::constant);
  advance();

  if (!peek().is_name()) {
    expected_name("after the binding keyword");
    return nullptr;
  }
  const token &name_token = peek();
  const std::string name = nil_symbol(advance().lexeme);
  std::optional<std::string> type;
  if (match(kw::as)) type = parse_type();
  if (!expect(kw::is, "before the initial value")) return nullptr;
  auto init = parse_expression();
  skip_terminators();
  apply_declared_element_type(type, init);

  auto decl = std::make_shared<ny::ast_variable_decl>(
      name, init, type, mutable_binding, is_public, line);
  decl->name_span = name_span(name_token);
  decl->is_const = constant_binding;
  return decl;
}

std::shared_ptr<ny::ast_decl> parser::parse_text_pattern(const token &name, bool is_public) {
  advance(); advance(); // 'begin pattern'
  const auto begin = pos_;
  std::vector<ny::patterns::prose_token> body;
  int nesting = 1;
  while (!at_end() && body.size() <= 16384) {
    const auto current = advance();
    auto spelling = current.lexeme;
    if (current.kind != matrix::token_kind::string_literal) {
      for (auto &c : spelling) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    }
    body.push_back({spelling, current.kind == matrix::token_kind::string_literal,
                    current.kind == matrix::token_kind::statement_end,
                    current.escaped_identifier});
    if (exact_phrase_word(peek(), "pattern")) {
      if (current.is(kw::begin)) ++nesting;
      if (current.is(kw::end) && --nesting == 0) {
        advance(); body.push_back({"pattern", false, false}); break;
      }
    }
  }
  auto parsed = ny::patterns::parse_prose(body);
  if (!parsed.error.empty()) {
    const auto index = std::min(begin + parsed.error_token, tokens_.size() - 1);
    error(tokens_[index], parsed.error);
  } else {
    const auto problem = ny::patterns::validate(parsed.source);
    if (!problem.empty()) error(name, "Invalid text pattern: " + problem);
  }
  skip_terminators();
  auto decl = std::make_shared<ny::ast_variable_decl>(nil_symbol(name.lexeme),
      std::make_shared<ny::ast_string_literal>(parsed.source, name.line),
      std::string("text"), false, is_public, name.line);
  decl->is_const = true;
  decl->name_span = name_span(name);
  return decl;
}

std::shared_ptr<ny::ast_decl> parser::parse_declaration() {
  // Annotations: 'context "..."' is @ai_context; 'note <name> <args>' is a
  // general annotation. Both attach to the following declaration.
  std::vector<ny::ast_annotation> annotations;
  while (check(kw::context) || check(kw::note)) {
    const bool is_context = peek().is(kw::context);
    advance();
    ny::ast_annotation annotation;
    annotation.line = peek().line;
    annotation.name = is_context ? "ai_context" : nil_symbol(advance().lexeme);
    // ast_annotation holds one optional argument string, so several tokens
    // are joined with a space -- which matches how the Ny parser stores the
    // raw contents of '@name(...)'.
    // Arguments stay on the annotation's own line. Without this the name of
    // the following declaration is read as another argument, and the
    // declaration then starts at its 'receives' or 'returns' clause.
    const int annotation_line = previous().line;
    std::string args;
    while (peek().line == annotation_line &&
           (check(matrix::token_kind::string_literal) ||
            check(matrix::token_kind::number_literal) || peek().is_name())) {
      if (!args.empty()) args += " ";
      args += advance().lexeme;
    }
    if (!args.empty()) annotation.args = args;
    annotations.push_back(std::move(annotation));
    skip_terminators();
  }

  // A 'context' the author wrote and the artifact does not carry is worse than
  // no context at all: it reads as documented intent that silently is not
  // there, and under NIL-0093 that intent is part of what an image states
  // about itself.
  //
  // A paragraph, record, class and choice each have a declaration in the
  // emitted NIL, so each carries its own annotations. The kinds below do not,
  // and this is not a gap waiting to be filled: a contract emits nothing at
  // all, a binding is code rather than a declaration, and a read states a
  // dependency rather than declaring anything. There is no place in the
  // artifact to put an annotation for them, so one is refused rather than
  // accepted and dropped.
  const auto refuse_annotations = [&](const char *subject) {
    if (annotations.empty()) return;
    const bool has_context =
        std::any_of(annotations.begin(), annotations.end(),
                    [](const ny::ast_annotation &a) { return a.name == "ai_context"; });
    error(peek(),
          std::string(has_context ? "'context' " : "'note' ") +
              "cannot attach to " + subject,
          "only a paragraph, record, class or choice appears in the compiled "
          "artifact, so only those can carry one; write it as a '!!!' comment "
          "if it is for a human reader");
  };

  bool is_public = match(kw::public_);

  // 'abstract' marks a class whose obligations a concrete descendant must
  // satisfy. The other member modifiers only make sense inside a body.
  const bool is_abstract = match(kw::abstract);
  if (check(kw::virtual_) || check(kw::replaces) || check(kw::forever)) {
    const token &at = peek();
    error(at,
          at.is(kw::forever)
              ? "'forever' as a modifier is reserved and not implemented"
              : std::string("'") + at.lexeme +
                    "' applies to a member, not a top-level declaration",
          at.is(kw::forever)
              ? "NIL has no class-level shared storage; see "
                "matrix/docs/types-and-text.md"
              : "write it inside a class, implementation or extension body");
    recover_to_declaration();
    return nullptr;
  }

  if (check(kw::value) || check(kw::variable) || check(kw::constant)) {
    refuse_annotations("a value, variable or constant");
    return parse_book_binding(is_public);
  }

  if (check(kw::read) || check(kw::cite)) {
    if (is_public) error(previous(), "'public' does not apply to a read or cite");
    refuse_annotations("a read or cite");
    return check(kw::read) ? parse_read() : parse_cite();
  }

  // An extension may target a tier-one type, whose name is a keyword:
  // 'text begin extension' is how the standard library grows text's surface
  // while the language itself keeps knowing nothing about encoding.
  if (peek().kind == matrix::token_kind::keyword && peek(1).is(kw::begin) &&
      peek(2).is(kw::extension)) {
    const int target_line = peek().line;
    auto target = parse_type();
    if (!target) { recover_to_declaration(); return nullptr; }
    return parse_implementation(target_line, *target,
                                std::vector<ny::ast_generic_param>{},
                                std::nullopt, kw::extension);
  }

  if (!peek().is_name()) {
    if (!report_foreign_keyword(peek()))
      error(peek(), "expected a declaration, found '" + peek().lexeme + "'",
            "a declaration is '<name> <clauses> begin <kind>'", "MX0105");
    recover_to_declaration();
    return nullptr;
  }

  const token &name_token = peek();
  const int line = peek().line;
  const std::string name = advance().lexeme;

  if (check(kw::begin) && exact_phrase_word(peek(1), "pattern")) {
    refuse_annotations("a text pattern binding");
    return parse_text_pattern(name_token, is_public);
  }

  // 'of type T [meeting B]' applies to records, choices and paragraphs alike.
  std::vector<ny::ast_generic_param> generics;
  std::string target_name = name;
  if (check(kw::of) && peek(1).is(kw::type)) {
    advance();
    generics = parse_generic_clause();
  } else if (check(kw::of)) {
    // 'envelope of number begin extension' applies the target to CONCRETE
    // type arguments rather than declaring parameters. The target name carries
    // them, matching how Ny spells 'impl Envelope<i64>'.
    advance();
    std::vector<std::string> args;
    do {
      auto arg = parse_type();
      if (!arg) break;
      args.push_back(*arg);
    } while (match(matrix::token_kind::comma));
    if (!args.empty()) {
      target_name = resolve_name(name) + "<";
      for (size_t i = 0; i < args.size(); ++i) {
        if (i) target_name += ", ";
        target_name += args[i];
      }
      target_name += ">";
    }
  }

  if (check(kw::begin) && peek(1).is(kw::record)) {
    // A record carries its annotations into the artifact now, so a 'context'
    // written above one is kept rather than refused. The other declaration
    // kinds below still refuse, because textual NIL has nowhere to put theirs
    // yet; each is lifted as the format grows to hold it.
    auto record = parse_record(is_public, line, name, std::move(generics));
    if (record) record->annotations = std::move(annotations);
    return named(record, name_token);
  }
  if (check(kw::begin) && peek(1).is(kw::choice)) {
    auto choice = parse_choice(is_public, line, name, std::move(generics));
    if (choice) choice->annotations = std::move(annotations);
    return named(choice, name_token);
  }
  if (check(kw::begin) && peek(1).is(kw::contract)) {
    refuse_annotations("a contract");
    return named(parse_contract(is_public, line, name, std::move(generics)), name_token);
  }
  if (check(kw::begin) && peek(1).is(kw::test))
    return named(parse_test(line, name), name_token);
  if (check(kw::inherits) || (check(kw::begin) && peek(1).is(kw::class_))) {
    auto klass = parse_class(is_public, is_abstract, line, name, std::move(generics));
    if (klass) klass->annotations = std::move(annotations);
    return named(klass, name_token);
  }

  // '<Type> provides <Contract> begin implementation', or a bare
  // 'begin implementation' / 'begin extension'.
  if (check(kw::provides)) {
    advance();
    auto contract = parse_type();
    if (!check(kw::begin)) {
      error(peek(), "expected 'begin implementation' after the contract name");
      recover_to_declaration();
      return nullptr;
    }
    const kw closing = peek(1).is(kw::extension) ? kw::extension
                                                 : kw::implementation;
    return parse_implementation(line, target_name == name
                                          ? resolve_name(name) : target_name,
                                std::move(generics), contract, closing);
  }
  if (check(kw::begin) &&
      (peek(1).is(kw::implementation) || peek(1).is(kw::extension))) {
    const kw closing = peek(1).is(kw::extension) ? kw::extension
                                                 : kw::implementation;
    return parse_implementation(line, target_name == name
                                          ? resolve_name(name) : target_name,
                                std::move(generics), std::nullopt, closing);
  }

  if (match(kw::means)) {
    auto target = parse_type();
    skip_terminators();
    if (!target) return nullptr;
    return named(std::make_shared<ny::ast_type_alias_decl>(
        nil_symbol(name), *target, std::vector<ny::ast_generic_param>{},
        is_public, line), name_token);
  }

  // Declaration kinds beyond paragraphs are a later stage. Diagnose precisely
  // rather than silently skipping, so a clean parse never overstates coverage.
  if (check(kw::begin) && !peek(1).is(kw::paragraph)) {
    const token &kind = peek(1);
    error(kind,
          std::string("'begin ") + kind.lexeme + "' is not implemented in this "
          "stage",
          "this stage lowers books, reads, paragraphs, bindings and statements; "
          "see matrix/docs/front-end-plan.md");
    recover_to_declaration();
    return nullptr;
  }

  // A declaration carrying 'note intrinsic' has no body: the named runtime
  // provider is the implementation.
  bool intrinsic_backed = false;
  for (const auto &annotation : annotations)
    if (annotation.name == "intrinsic") intrinsic_backed = true;
  if (intrinsic_backed)
    return named(finish_intrinsic(is_public, line, name, std::move(annotations)), name_token);

  auto paragraph = parse_paragraph(is_public, line, name);
  if (paragraph) {
    paragraph->annotations = std::move(annotations);
    if (!generics.empty()) {
      auto *fn = static_cast<ny::ast_function_decl *>(paragraph.get());
      fn->generic_params = std::move(generics);
    }
  }
  return named(paragraph, name_token);
}

std::shared_ptr<ny::ast_program> parser::parse_program() {
  auto program = std::make_shared<ny::ast_program>(1);
  program->filename = filename_;

  skip_terminators();
  if (check(kw::book) || check(kw::library)) {
    if (auto header = parse_book_or_library())
      program->declarations.push_back(header);
  } else if (!at_end()) {
    error(peek(), "a page must open with 'book <name>' or 'library <name>'",
          "every page belongs to a book; see matrix/samples/01-hello.matrix");
  }

  while (!at_end()) {
    skip_terminators();
    if (at_end()) break;
    const size_t before = pos_;
    if (auto decl = parse_declaration())
      program->declarations.push_back(decl);
    if (pos_ == before) advance(); // guarantee progress
  }

  // Intrinsic declarations synthesised for 'now in milliseconds' and
  // 'sleep for ...' are appended once, after the page's own declarations.
  for (auto &decl : synthesized_)
    program->declarations.push_back(decl);
  validate_integer_literals();
  return program;
}

void parser::validate_integer_literals() {
  // Check every source integer, including unused/default expressions. Only
  // direct unary minimum literals may use the positive boundary magnitude.
  for (size_t index = 0; index < tokens_.size(); ++index) {
    const auto &number = tokens_[index];
    if (number.kind != matrix::token_kind::number_literal ||
        ny::is_floating_literal_spelling(number.lexeme)) continue;
    if (submission_requested_ &&
        std::find(incomplete_tokens_.begin(), incomplete_tokens_.end(), number.start) !=
            incomplete_tokens_.end()) continue;
    ny::integer_literal_info info;
    if (const auto *message = ny::parse_integer_literal(number.lexeme, info)) {
      const auto boundary = integer_boundaries_.find(index);
      if (boundary == integer_boundaries_.end() ||
          ny::parse_integer_literal(boundary->second->value, info))
        error(number, message);
    }
  }
}

bool parser::submission_declaration_start() const {
  switch (peek().word) {
  case kw::book: case kw::library: case kw::chapter: case kw::read: case kw::cite:
  case kw::public_: case kw::abstract: case kw::virtual_: case kw::replaces:
  case kw::context: case kw::note: case kw::field: case kw::test:
    return true;
  default: break;
  }
  if (!peek().is_name()) return false;
  if (peek(1).is(kw::begin))
    return !(exact_phrase_word(peek(), "collect") && exact_phrase_word(peek(2), "query"));
  return peek(1).is(kw::receives) || peek(1).is(kw::returns) ||
      peek(1).is(kw::means) || peek(1).is(kw::inherits) || peek(1).is(kw::provides) ||
      (peek(1).is(kw::of) && peek(2).is(kw::type));
}

bool parser::submission_statement_start() {
  if (check(kw::when)) {
    // 'when condition give ...' is unambiguously a value. The form containing
    // 'then' retains its existing statement interpretation at statement level;
    // parentheses can request value position for either spelling.
    const auto saved = pos_;
    advance();
    parse_expression();
    const bool value_position = check(kw::give);
    pos_ = saved;
    return !value_position;
  }
  switch (peek().word) {
  case kw::value: case kw::variable: case kw::constant: case kw::set:
  case kw::say: case kw::give: case kw::evaluate: case kw::append:
  case kw::when: case kw::repeat: case kw::stop: case kw::continue_:
  case kw::consider: case kw::allocate: case kw::release: case kw::attempt:
  case kw::raise: case kw::own: case kw::borrow: case kw::move:
  case kw::sleep: case kw::assembly:
    return true;
  default: return false;
  }
}

submission_parse_result parser::parse_submission() {
  submission_parse_result result;
  if (source_size_ > submission_source_limit && !lexical_invalid_)
    submission_limit_error(peek(), "submission exceeds the 65536-byte source limit");
  if (tokens_.size() > submission_token_limit + 1 && !lexical_invalid_)
    submission_limit_error(tokens_[submission_token_limit], "submission exceeds the 4096-token limit");
  if (lexical_invalid_) {
    result.diagnostics = rich_;
    return result;
  }
  // A submission has local bindings, even at column one. Page-wide collision
  // heuristics for book declarations must not turn those bindings into types.
  declared_.clear();
  declared_types_.clear();
  prescan_declarations(true);
  signature_cursor_.reset();
  signature_blank_start_.reset();
  submission_requested_ = true;
  parsing_submission_ = true;
  result.body = std::make_shared<ny::ast_block_stmt>(1);
  result.body->source_file = filename_;
  result.state = submission_state::complete;
  try {
    if (pos_ != 0)
      error(peek(), "a submission parser cannot reuse an already consumed token stream", {}, "MX1002");
    skip_terminators();
    while (!at_end()) {
      if (submission_declaration_start())
        error(peek(), "books, imports and named declarations are unavailable in a submission",
              "use local value bindings and expressions, or compile a full Matrix page", "MX1002");
      if (submission_statement_start()) {
        auto statement = parse_statement();
        if (statement) result.body->statements.push_back(std::move(statement));
      } else {
        result.value = parse_expression();
        skip_terminators();
        if (!at_end())
          error(peek(), "a bare expression must be the final item in a submission", {}, "MX1002");
        break;
      }
      skip_terminators();
    }
    if (lexical_incomplete_) result.state = submission_state::incomplete;
  } catch (const submission_abort &failure) {
    result.state = failure.state;
  }
  parsing_submission_ = false;
  // A malformed integer earlier in the input stays invalid even if a later
  // operand or block is unfinished. Use the same boundary validation as pages.
  const auto diagnostic_count = diagnostics_.size();
  validate_integer_literals();
  if (diagnostics_.size() != diagnostic_count || lexical_invalid_)
    result.state = submission_state::invalid;
  if (result.state != submission_state::complete) {
    result.body.reset();
    result.value.reset();
  }
  result.diagnostics = rich_;
  return result;
}

// ---------------------------------------------------------------------------
// Stage 5: classes, contracts, implementations and extensions.
//
// 'my' is the receiver. Matrix synthesises the explicit first 'self' parameter
// that Ny requires, so the NIL CALL_METHOD arity is identical and a class
// declared here is indistinguishable from one declared in Ny.
// ---------------------------------------------------------------------------

// One member of a class, contract, implementation or extension: modifiers,
// then a name, then the ordinary paragraph clauses.
std::shared_ptr<ny::ast_function_decl>
parser::parse_member(paragraph_context context) {
  bool is_public = false, is_abstract = false, is_virtual = false;
  bool is_override = false;
  for (;;) {
    if (match(kw::public_)) { is_public = true; continue; }
    if (match(kw::abstract)) { is_abstract = true; continue; }
    if (match(kw::virtual_)) { is_virtual = true; continue; }
    // 'replaces' is the override marker. It is not spelled 'override',
    // because 'override' is the machine-type escape hatch and two
    // near-identical words for unrelated things is the ambiguity this surface
    // exists to avoid.
    if (match(kw::replaces)) { is_override = true; is_virtual = true; continue; }
    break;
  }
  if (!peek().is_name()) {
    error(peek(), "expected a member name",
          "a member is '[public] [virtual|replaces|abstract] <name> "
          "[receives ...] [returns <type>] begin paragraph'");
    return nullptr;
  }
  const token &name_token = peek();
  const int line = peek().line;
  const std::string name = advance().lexeme;
  return named(parse_paragraph(is_public, line, name, context, is_abstract,
                               is_virtual, is_override), name_token);
}

// '<name> [inherits B, C] begin class' ... 'end class'
std::shared_ptr<ny::ast_decl>
parser::parse_class(bool is_public, bool is_abstract, int line,
                    const std::string &name,
                    std::vector<ny::ast_generic_param> generics) {
  std::vector<std::string> bases;
  if (match(kw::inherits)) {
    do {
      if (!peek().is_name()) {
        expected_name("after 'inherits'");
        break;
      }
      // Multiple inheritance uses the actual concrete field layout, not an
      // assumed first-base prefix; ambiguous members are rejected downstream.
      bases.push_back(resolve_name(parse_dotted_name()));
    } while (match(matrix::token_kind::comma));
  }

  if (!expect(kw::begin, "to open a class body")) return nullptr;
  if (!expect(kw::class_, "after 'begin'")) return nullptr;

  std::vector<ny::ast_struct_field> fields;
  std::vector<std::shared_ptr<ny::ast_function_decl>> methods;
  for (;;) {
    skip_terminators();
    if (at_end()) {
      error(peek(), "expected 'end class' before the end of the page");
      break;
    }
    if (check(kw::end) && peek(1).is(kw::class_)) {
      advance(); advance(); skip_terminators();
      break;
    }
    if (match(kw::field)) {
      if (!peek().is_name()) {
        expected_name("after 'field'");
        continue;
      }
      ny::ast_struct_field field;
      field.line = peek().line;
      field.name_span = name_span(peek());
      field.name = nil_symbol(advance().lexeme); // a member: written spelling
      if (!expect(kw::as, "after a field name")) continue;
      auto type = parse_type();
      if (type) field.type_name = *type;
      fields.push_back(std::move(field));
      skip_terminators();
      continue;
    }
    const size_t before = pos_;
    if (auto method = parse_member(paragraph_context::member))
      methods.push_back(method);
    if (pos_ == before) advance();
  }
  auto decl = std::make_shared<ny::ast_class_decl>(
      resolve_name(name), std::move(bases), std::move(fields),
      std::move(methods), std::move(generics), is_abstract, line);
  decl->visibility = is_public ? ny::visibility_kind::visibility_public
                               : ny::visibility_kind::visibility_private;
  decl->is_pub = is_public;
  return decl;
}

// '<name> begin contract' ... 'end contract'. Members are signatures only:
// default bodies are rejected in Ny and therefore have no Matrix spelling.
std::shared_ptr<ny::ast_decl>
parser::parse_contract(bool is_public, int line, const std::string &name,
                       std::vector<ny::ast_generic_param> generics) {
  advance(); // 'begin'
  advance(); // 'contract'

  std::vector<std::shared_ptr<ny::ast_function_decl>> methods;
  for (;;) {
    skip_terminators();
    if (at_end()) {
      error(peek(), "expected 'end contract' before the end of the page");
      break;
    }
    if (check(kw::end) && peek(1).is(kw::contract)) {
      advance(); advance(); skip_terminators();
      break;
    }
    const size_t before = pos_;
    if (auto method = parse_member(paragraph_context::contract_member))
      methods.push_back(method);
    if (pos_ == before) advance();
  }
  return std::make_shared<ny::ast_trait_decl>(resolve_name(name),
                                              std::move(generics),
                                              std::move(methods), is_public,
                                              line);
}

// 'begin implementation' and 'begin extension' both lower to an impl block.
// The only difference is whether this book declares the target type, and that
// is a checker question, not a lowering one -- which is why an extension needs
// no NIL change at all.
std::shared_ptr<ny::ast_decl>
parser::parse_implementation(int line, const std::string &target,
                             std::vector<ny::ast_generic_param> generics,
                             std::optional<std::string> contract,
                             kw closing_kind) {
  advance(); // 'begin'
  advance(); // 'implementation' or 'extension'

  std::vector<std::shared_ptr<ny::ast_function_decl>> methods;
  for (;;) {
    skip_terminators();
    if (at_end()) {
      error(peek(), std::string("expected 'end ") +
                        keyword_name(closing_kind) +
                        "' before the end of the page");
      break;
    }
    if (check(kw::end) && peek(1).is(closing_kind)) {
      advance(); advance(); skip_terminators();
      break;
    }
    if (check(kw::field)) {
      // An extension may not add a field: that is a storage-layout change,
      // and NIL cannot express one after the fact.
      error(peek(), "an implementation or extension cannot add a field",
            "fields belong to the record or class declaration");
      const int bad = peek().line;
      while (!at_end() && peek().line == bad) advance();
      continue;
    }
    const size_t before = pos_;
    if (auto method = parse_member(paragraph_context::member))
      methods.push_back(method);
    if (pos_ == before) advance();
  }
  return std::make_shared<ny::ast_impl_decl>(target, std::move(contract),
                                             std::move(generics),
                                             std::move(methods), line);
}

// ---------------------------------------------------------------------------
// Stage 6: faults, ownership, structured parallelism, time and assembly.
// ---------------------------------------------------------------------------

// 'attempt / caught T as b [provided g] / caught anything as t / always / end'
//
// 'always' is Ny's 'recover': cleanup with finally semantics, not an extra
// handler. Matrix names it for what it does.
std::shared_ptr<ny::ast_stmt> parser::parse_attempt() {
  const int line = peek().line;
  advance(); // 'attempt'

  auto body = std::make_shared<ny::ast_block_stmt>(peek().line);
  std::vector<std::shared_ptr<ny::ast_fault_handler>> handlers;
  std::shared_ptr<ny::ast_block_stmt> cleanup;

  const auto collect = [&](const std::shared_ptr<ny::ast_block_stmt> &into) {
    for (;;) {
      skip_terminators();
      if (at_end()) return;
      if (check(kw::caught) || check(kw::always)) return;
      if (check(kw::end) && peek(1).is(kw::attempt)) return;
      const size_t before = pos_;
      if (auto stmt = parse_statement()) into->statements.push_back(stmt);
      if (pos_ == before) advance();
    }
  };

  collect(body);
  for (;;) {
    skip_terminators();
    if (at_end()) {
      error(peek(), "expected 'end attempt' before the end of the page");
      break;
    }
    if (check(kw::end) && peek(1).is(kw::attempt)) {
      advance(); advance(); skip_terminators();
      break;
    }
    if (match(kw::always)) {
      if (cleanup)
        error(previous(), "an attempt has at most one 'always' block");
      cleanup = std::make_shared<ny::ast_block_stmt>(peek().line);
      collect(cleanup);
      continue;
    }
    if (match(kw::caught)) {
      // 'caught anything as token' is the catch-all. The token is opaque: it
      // may be ignored or re-raised, and nothing else.
      std::optional<std::string> fault_type;
      if (!match(kw::anything)) {
        auto type = parse_type();
        if (type) fault_type = *type;
      }
      std::string bound;
      std::optional<ny::source_span> bound_span;
      if (match(kw::as)) {
        if (peek().is_name()) {
          bound_span = name_span(peek());
          bound = resolve_name(advance().lexeme);
        }
        else expected_name("after 'as' in a handler");
      } else {
        error(peek(), "expected 'as <name>' to bind the fault",
              "write 'caught ReadFault as problem'");
      }
      // A guard uses 'provided', keeping it lexically distinct from a 'when'
      // conditional inside the handler body.
      ny::ast_expr_ptr guard;
      if (match(kw::provided)) guard = parse_expression();

      auto handler_body = std::make_shared<ny::ast_block_stmt>(peek().line);
      collect(handler_body);
      handlers.push_back(std::make_shared<ny::ast_fault_handler>(
          fault_type, bound, guard, handler_body, line));
      handlers.back()->name_span = bound_span;
      continue;
    }
    const size_t before = pos_;
    if (auto stmt = parse_statement()) body->statements.push_back(stmt);
    if (pos_ == before) advance();
  }
  return std::make_shared<ny::ast_attempt_stmt>(body, std::move(handlers),
                                                cleanup, line);
}

// 'raise Payload with f being v' | 'raise token again'
std::shared_ptr<ny::ast_stmt> parser::parse_raise() {
  const int line = peek().line;
  advance(); // 'raise'

  // Re-raising a retained catch-all token keeps the original raise location
  // and type.
  if (peek().is_name() && peek(1).is(kw::again)) {
    const token &name_token = peek();
    auto token_expr = named(std::make_shared<ny::ast_identifier_expr>(
                              resolve_name(advance().lexeme), line), name_token);
    advance(); // 'again'
    skip_terminators();
    return std::make_shared<ny::ast_raise_stmt>(token_expr, line);
  }

  // Otherwise a fully initialised concrete record value. 'raise' implies
  // construction, so the payload is written 'raise ReadFault with reason
  // being "..."' rather than repeating 'build'. Parsing it as an ordinary
  // expression would make the 'with' clause a CALL, and the checker would
  // then see a payload of type 'any'.
  ny::ast_expr_ptr payload;
  if (peek().is_name() &&
      (peek(1).is(kw::with) ||
       peek(1).kind == matrix::token_kind::statement_end ||
       peek(1).line != peek().line)) {
    const int payload_line = peek().line;
    const std::string type_name = resolve_name(parse_dotted_name());
    std::vector<std::pair<std::string, ny::ast_expr_ptr>> fields;
    if (match(kw::with)) {
      do {
        if (!peek().is_name()) {
          expected_name("as a field in the raise payload");
          break;
        }
        const std::string field = nil_symbol(advance().lexeme);
        if (!expect(kw::being, "after a field name")) break;
        fields.emplace_back(field, parse_expression());
      } while (match(matrix::token_kind::comma));
    }
    payload = std::make_shared<ny::ast_struct_literal_expr>(
        type_name, std::move(fields), std::vector<std::string>{}, payload_line);
  } else {
    payload = parse_expression();
  }
  skip_terminators();
  return std::make_shared<ny::ast_raise_stmt>(payload, line);
}

// Ownership operations are statements, not initialiser expressions, because Ny
// requires each to be a direct local declaration initialiser. Expressing that
// in the grammar makes the restriction a parse rule instead of a later error.
std::shared_ptr<ny::ast_stmt> parser::make_ownership_binding(
    const std::string &name, const std::string &operation,
    std::vector<ny::ast_expr_ptr> args, std::vector<std::string> type_args,
    bool is_mut, std::optional<std::string> declared_type, int line,
    const token &name_token) {
  auto callee = std::make_shared<ny::ast_identifier_expr>(operation, line);
  auto call = std::make_shared<ny::ast_call_expr>(
      callee, std::move(args), std::move(type_args), line);
  auto decl = std::make_shared<ny::ast_variable_decl>(
      name, call, std::move(declared_type), is_mut, false, line);
  decl->name_span = name_span(name_token);
  return std::make_shared<ny::ast_decl_stmt>(decl, line);
}

// 'own [variable] b as array of T sized n' -> let [mut] b = owned<T>(n)
std::shared_ptr<ny::ast_stmt> parser::parse_own() {
  const int line = peek().line;
  advance(); // 'own'
  const bool is_mut = match(kw::variable);
  if (!peek().is_name()) {
    expected_name("after 'own'");
    return nullptr;
  }
  const token &name_token = peek();
  const std::string name = resolve_name(advance().lexeme);
  reject_type_name_collision(name_token, name_token.lexeme);
  if (!expect(kw::as, "after the owner name")) return nullptr;
  auto type = parse_type();
  if (!expect(kw::sized, "before the element count")) return nullptr;
  auto count = parse_expression();
  skip_terminators();
  if (!type) return nullptr;

  // 'own b as array of T' declares owned<T>; the element type is the operand.
  std::string element = *type;
  if (element.rfind("array<", 0) == 0 && element.back() == '>')
    element = element.substr(6, element.size() - 7);
  else if (element.rfind("owned<", 0) == 0 && element.back() == '>')
    element = element.substr(6, element.size() - 7);
  else
    error(name_token, "'own' needs an array type",
          "write 'own variable samples as array of override byte sized 64'");

  return make_ownership_binding(name, "owned", {count}, {element}, is_mut,
                                "owned<" + element + ">", line, name_token);
}

// 'borrow v from b [exclusively]' -> let v = owned_borrow[_mut](b)
std::shared_ptr<ny::ast_stmt> parser::parse_borrow() {
  const int line = peek().line;
  advance(); // 'borrow'
  if (!peek().is_name()) {
    expected_name("after 'borrow'");
    return nullptr;
  }
  const token &name_token = peek();
  const std::string name = resolve_name(advance().lexeme);
  if (!expect(kw::from, "after the view name")) return nullptr;
  if (!peek().is_name()) {
    expected_name("as the owner after 'from'");
    return nullptr;
  }
  const token &owner_token = peek();
  auto owner = named(std::make_shared<ny::ast_identifier_expr>(
                         resolve_name(advance().lexeme), line), owner_token);
  // Mutability belongs to the view type, so the view binding itself is never
  // declared mutable.
  const bool exclusive = match(kw::exclusively);
  skip_terminators();
  return make_ownership_binding(
      name, exclusive ? "owned_borrow_mut" : "owned_borrow", {owner}, {}, false,
      std::nullopt, line, name_token);
}

// 'move b into c' -> let c = owned_move(b). The source binding is invalidated.
std::shared_ptr<ny::ast_stmt> parser::parse_move() {
  const int line = peek().line;
  advance(); // 'move'
  if (!peek().is_name()) {
    expected_name("as the owner after 'move'");
    return nullptr;
  }
  const token &owner_token = peek();
  auto owner = named(std::make_shared<ny::ast_identifier_expr>(
                         resolve_name(advance().lexeme), line), owner_token);
  if (!expect(kw::into, "after the owner being moved")) return nullptr;
  if (!peek().is_name()) {
    expected_name("as the destination after 'into'");
    return nullptr;
  }
  const token &destination_token = peek();
  const std::string destination = resolve_name(advance().lexeme);
  skip_terminators();
  // A move destination chooses its own binding mutability; default to mutable
  // so the moved buffer stays writable.
  return make_ownership_binding(destination, "owned_move", {owner}, {}, true,
                                std::nullopt, line, destination_token);
}

// Declare a time intrinsic on first use, so 'now in milliseconds' needs no
// library read. This is exactly how Ny reaches TIME_NOW: an annotated,
// bodyless declaration naming a runtime provider.
void parser::ensure_time_intrinsic(const std::string &provider,
                                   const std::string &name,
                                   std::optional<std::string> return_type,
                                   std::optional<std::string> parameter_type) {
  if (synthesized_names_.count(name)) return;
  synthesized_names_[name] = true;
  std::vector<ny::ast_param> params;
  if (parameter_type)
    params.emplace_back("duration", parameter_type, false, 0, std::nullopt);
  ny::ast_annotation annotation("intrinsic", provider, 0);
  auto decl = std::make_shared<ny::ast_function_decl>(
      name, params, return_type, nullptr,
      std::vector<ny::ast_generic_param>{}, false, 0,
      std::vector<ny::ast_annotation>{annotation});
  synthesized_.push_back(decl);
}

// 'sleep for n milliseconds'
std::shared_ptr<ny::ast_stmt> parser::parse_sleep() {
  const int line = peek().line;
  advance(); // 'sleep'
  if (!match_word("for"))
    error(peek(), "expected 'for' after 'sleep'",
          "write 'sleep for 10 milliseconds'");
  auto duration = parse_expression();
  if (!match_word("milliseconds"))
    error(peek(), "expected 'milliseconds' after the duration",
          "milliseconds is the only unit; a nonpositive duration is a no-op");
  skip_terminators();
  ensure_time_intrinsic("time.sleep", "matrix_sleep_milliseconds",
                        std::string("void"), std::string("i64"));
  auto callee = std::make_shared<ny::ast_identifier_expr>(
      "matrix_sleep_milliseconds", line);
  std::vector<ny::ast_expr_ptr> args{duration};
  auto call = std::make_shared<ny::ast_call_expr>(
      callee, std::move(args), std::vector<std::string>{}, line);
  return std::make_shared<ny::ast_expr_stmt>(call, line);
}

// 'assembly for x86_64 is "..." [using a, b] [giving out]'
//
// The bounded profile is Ny's and unchanged: x86_64, register-only, i64
// operands, at most eight inputs and one output. Interpreted routes reject the
// INLINE_ASM opcode rather than emulating it, so a VM-only program cannot
// execute this -- which is a route fact, not a Matrix limitation.
std::shared_ptr<ny::ast_stmt> parser::parse_assembly() {
  const int line = peek().line;
  advance(); // 'assembly'
  if (!match_word("for"))
    error(peek(), "expected 'for' after 'assembly'",
          "write 'assembly for x86_64 is \"...\" using v giving out'");
  std::string target;
  if (peek().is_name()) target = advance().lexeme;
  else error(peek(), "expected an architecture name after 'for'");
  if (!expect(kw::is, "before the assembly template")) return nullptr;
  if (!check(matrix::token_kind::string_literal)) {
    error(peek(), "expected a text template for the assembly");
    return nullptr;
  }
  auto statement = std::make_shared<ny::ast_asm_stmt>(advance().lexeme, line);
  statement->target = target;
  if (match(kw::using_)) {
    do {
      statement->inputs.push_back(parse_expression());
    } while (match(matrix::token_kind::comma));
  }
  if (match(kw::giving)) {
    if (peek().is_name()) statement->output = resolve_name(advance().lexeme);
    else expected_name("after 'giving'");
  }
  skip_terminators();
  return statement;
}

// '<name> begin test ... end test'
std::shared_ptr<ny::ast_decl> parser::parse_test(int line,
                                                 const std::string &name) {
  advance(); // 'begin'
  advance(); // 'test'
  auto body = std::make_shared<ny::ast_block_stmt>(peek().line);
  for (;;) {
    skip_terminators();
    if (at_end()) {
      error(peek(), "expected 'end test' before the end of the page");
      break;
    }
    if (check(kw::end) && peek(1).is(kw::test)) {
      advance(); advance(); skip_terminators();
      break;
    }
    const size_t before = pos_;
    if (auto stmt = parse_statement()) body->statements.push_back(stmt);
    if (pos_ == before) advance();
  }
  return std::make_shared<ny::ast_test_block>(resolve_name(name), body,
                                              ny::test_type::unit, line);
}

// A bodyless declaration backed by the provider named in 'note intrinsic'.
// The annotation is not an implementation: the provider must exist on every
// claimed route.
std::shared_ptr<ny::ast_decl>
parser::finish_intrinsic(bool is_public, int line, const std::string &name,
                         std::vector<ny::ast_annotation> annotations) {
  std::vector<std::string> param_names;
  std::vector<ny::ast_param> params;
  if (match(kw::receives)) params = parse_parameters(param_names);
  std::optional<std::string> return_type = std::string("void");
  if (match(kw::returns)) return_type = parse_type();
  skip_terminators();
  auto decl = std::make_shared<ny::ast_function_decl>(
      resolve_name(name), params, return_type, nullptr,
      std::vector<ny::ast_generic_param>{}, is_public, line,
      std::move(annotations));
  decl->source_file = filename_;
  return decl;
}

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

// Parse statements until 'end <closing_kind>', consuming the terminator.
std::shared_ptr<ny::ast_stmt> parser::parse_block(kw closing_kind) {
  auto block = std::make_shared<ny::ast_block_stmt>(peek().line);
  for (;;) {
    skip_terminators();
    if (at_end()) {
      error(peek(), std::string("expected 'end ") + keyword_name(closing_kind) +
                        "' before the end of the page");
      break;
    }
    if (check(kw::end) && peek(1).is(closing_kind)) {
      advance();
      advance();
      skip_terminators();
      break;
    }
    const size_t before = pos_;
    if (auto stmt = parse_statement()) block->statements.push_back(stmt);
    if (pos_ == before) advance();
  }
  return block;
}

// 'value x [as T] is e' | 'variable ...' | 'constant ...'
std::shared_ptr<ny::ast_stmt> parser::parse_binding_statement() {
  const int line = peek().line;
  const bool mutable_binding = peek().is(kw::variable);
  const bool constant_binding = peek().is(kw::constant);
  advance();

  if (match(matrix::token_kind::left_paren)) {
    auto declaration = std::make_shared<ny::ast_variable_decl>("", nullptr,
        std::nullopt, mutable_binding, false, line);
    declaration->is_const = constant_binding;
    do {
      if (check(matrix::token_kind::right_paren)) break;
      if (!peek().is_name()) {
        expected_name("in tuple destructuring");
        return nullptr;
      }
      const token binding = advance();
      auto child = std::make_shared<ny::ast_variable_decl>(
          parsing_submission_ ? resolve_name(binding.lexeme) : nil_symbol(binding.lexeme),
          nullptr, std::nullopt, mutable_binding, false, binding.line);
      child->name_span = name_span(binding);
      child->is_const = constant_binding;
      declaration->tuple_bindings.push_back(std::move(child));
    } while (match(matrix::token_kind::comma));
    expect(matrix::token_kind::right_paren, "')'", "after tuple bindings");
    if (declaration->tuple_bindings.empty())
      error(previous(), "tuple destructuring requires at least one binding");
    if (!expect(kw::is, "before the tuple value")) return nullptr;
    declaration->initializer = parse_expression();
    skip_terminators();
    return std::make_shared<ny::ast_decl_stmt>(declaration, line);
  }

  if (!peek().is_name()) {
    expected_name("after the binding keyword");
    return nullptr;
  }
  if (!peek().is_name()) {
    // The name diagnostic has already been emitted by the caller's check;
    // consume the rest of the line so one mistake reports once.
    const int bad = peek().line;
    while (!at_end() && peek().line == bad) advance();
    skip_terminators();
    return nullptr;
  }
  const token &name_token = peek();
  const std::string written_name = advance().lexeme;
  const std::string name = parsing_submission_ ? resolve_name(written_name) : nil_symbol(written_name);
  reject_type_name_collision(name_token, name_token.lexeme);
  std::optional<std::string> type;
  if (match(kw::as)) type = parse_type();
  if (!expect(kw::is, "before the value")) return nullptr;
  auto init = parse_expression();
  skip_terminators();
  apply_declared_element_type(type, init);

  auto decl = std::make_shared<ny::ast_variable_decl>(
      name, init, type, mutable_binding, false, line);
  decl->name_span = name_span(name_token);
  decl->is_const = constant_binding;
  return std::make_shared<ny::ast_decl_stmt>(decl, line);
}

// 'set <place> to <expr>'
std::shared_ptr<ny::ast_stmt> parser::parse_set() {
  const int line = peek().line;
  advance(); // 'set'
  auto target = parse_postfix();
  if (!expect(kw::to, "after the assignment target")) return nullptr;
  auto value = parse_expression();
  skip_terminators();
  auto assignment =
      std::make_shared<ny::ast_assignment_expr>(target, value, line);
  return std::make_shared<ny::ast_expr_stmt>(assignment, line);
}

// 'say e' -> println(e). Exactly one value, as in Ny.
std::shared_ptr<ny::ast_stmt> parser::parse_say() {
  const int line = peek().line;
  advance();
  std::vector<ny::ast_expr_ptr> args;
  args.push_back(parse_expression());
  skip_terminators();
  return std::make_shared<ny::ast_println_stmt>(std::move(args), line);
}

// 'give e' | 'give nothing' -> return
std::shared_ptr<ny::ast_stmt> parser::parse_give() {
  const int line = peek().line;
  advance();
  if (match(kw::nothing)) {
    skip_terminators();
    return std::make_shared<ny::ast_return_stmt>(nullptr, line);
  }
  auto value = parse_expression();
  skip_terminators();
  return std::make_shared<ny::ast_return_stmt>(value, line);
}

// 'evaluate e' -> expression statement; the value is discarded.
std::shared_ptr<ny::ast_stmt> parser::parse_evaluate() {
  const int line = peek().line;
  advance();
  auto expr = parse_expression();
  skip_terminators();
  return std::make_shared<ny::ast_expr_stmt>(expr, line);
}

// 'append e to <place>' -> <place>.push(e). 'push' is a compiler builtin on an
// array receiver with no declared parameter name, which is why it gets its own
// spelling instead of a 'with ... being ...' clause.
std::shared_ptr<ny::ast_stmt> parser::parse_append() {
  const int line = peek().line;
  advance();
  auto value = parse_expression();
  if (!expect(kw::to, "after the value being appended")) return nullptr;
  auto target = parse_postfix();
  skip_terminators();
  auto member = std::make_shared<ny::ast_member_access_expr>(target, "push", line);
  std::vector<ny::ast_expr_ptr> args{value};
  auto call = std::make_shared<ny::ast_call_expr>(
      member, std::move(args), std::vector<std::string>{}, line);
  return std::make_shared<ny::ast_expr_stmt>(call, line);
}

// 'when c then ... [otherwise when c then ...] [otherwise ...] end when'
std::shared_ptr<ny::ast_stmt> parser::parse_when_statement() {
  submission_depth_guard depth(*this);
  const int line = peek().line;
  advance(); // 'when'
  auto condition = parse_expression();
  if (!expect(kw::then, "after the condition")) return nullptr;

  auto then_branch = std::make_shared<ny::ast_block_stmt>(peek().line);
  std::shared_ptr<ny::ast_stmt> else_branch;

  for (;;) {
    skip_terminators();
    if (at_end()) {
      error(peek(), "expected 'end when' before the end of the page");
      break;
    }
    if (check(kw::end) && peek(1).is(kw::when)) {
      advance(); advance(); skip_terminators();
      break;
    }
    if (check(kw::otherwise)) {
      advance();
      if (check(kw::when)) {
        // 'otherwise when' is an elseif: recurse and attach as the else arm.
        else_branch = parse_when_statement();
        break; // the recursive call consumed this construct's 'end when'
      }
      auto tail = std::make_shared<ny::ast_block_stmt>(peek().line);
      for (;;) {
        skip_terminators();
        if (at_end()) {
          error(peek(), "expected 'end when' before the end of the page");
          break;
        }
        if (check(kw::end) && peek(1).is(kw::when)) {
          advance(); advance(); skip_terminators();
          break;
        }
        const size_t before = pos_;
        if (auto stmt = parse_statement()) tail->statements.push_back(stmt);
        if (pos_ == before) advance();
      }
      else_branch = tail;
      break;
    }
    const size_t before = pos_;
    if (auto stmt = parse_statement()) then_branch->statements.push_back(stmt);
    if (pos_ == before) advance();
  }
  return std::make_shared<ny::ast_if_stmt>(condition, then_branch, else_branch,
                                           line);
}

// 'repeat while c' | 'repeat for each x in e' | 'repeat for i from a to b'
// | 'repeat forever', all closed by 'end repeat'.
std::shared_ptr<ny::ast_stmt> parser::parse_repeat() {
  const int line = peek().line;
  advance(); // 'repeat'

  if (match(kw::while_)) {
    auto condition = parse_expression();
    auto body = parse_block(kw::repeat);
    return std::make_shared<ny::ast_while_stmt>(condition, body, line);
  }

  if (match(kw::forever)) {
    auto body = parse_block(kw::repeat);
    return std::make_shared<ny::ast_loop_stmt>(body, line);
  }

  if (match(kw::for_)) {
    const bool each = match_word("each");
    if (each && match(matrix::token_kind::left_paren)) {
      std::vector<ny::ast_param> bindings;
      do {
        if (check(matrix::token_kind::right_paren)) break;
        if (!peek().is_name()) { expected_name("in tuple loop bindings"); return nullptr; }
        const auto binding = advance();
        bindings.emplace_back(parsing_submission_ ? resolve_name(binding.lexeme) : nil_symbol(binding.lexeme),
                              std::nullopt, false, binding.line, std::nullopt);
        bindings.back().name_span = name_span(binding);
      } while (match(matrix::token_kind::comma));
      expect(matrix::token_kind::right_paren, "')'", "after tuple loop bindings");
      if (bindings.empty()) error(previous(), "tuple loop binding requires at least one name");
      if (!expect(kw::in_, "after tuple loop bindings")) return nullptr;
      auto iterable = parse_expression();
      auto body = parse_block(kw::repeat);
      auto loop = std::make_shared<ny::ast_for_in_stmt>(bindings, iterable, body, line);
      loop->tuple_pattern = true;
      return loop;
    }
    if (!peek().is_name()) {
      expected_name("as the loop variable");
      return nullptr;
    }
    const token &var_token = peek();
    const int var_line = peek().line;
    const std::string written_var = advance().lexeme;
    const std::string var = parsing_submission_ ? resolve_name(written_var) : nil_symbol(written_var);

    // Use the vector constructor even for one variable: codegen's range and
    // iterator paths both test `variables.size() == 1`, and the single-variable
    // constructor leaves that vector empty.
    if (each) {
      if (!expect(kw::in_, "after the loop variable")) return nullptr;
      auto iterable = parse_expression();
      auto body = parse_block(kw::repeat);
      std::vector<ny::ast_param> loop_vars{
          ny::ast_param(var, std::nullopt, false, var_line, std::nullopt)};
      loop_vars.front().name_span = name_span(var_token);
      return std::make_shared<ny::ast_for_in_stmt>(std::move(loop_vars),
                                                   iterable, body, line);
    }

    // 'from a to b' lowers to range(a, b), which is exclusive of b.
    if (!match_word("from")) {
      error(peek(), "expected 'from' or 'each' in a counted repeat",
            "write 'repeat for i from 0 to 10' or 'repeat for each x in xs'");
      return nullptr;
    }
    auto start = parse_expression();
    if (!expect(kw::to, "after the range start")) return nullptr;
    auto stop = parse_expression();
    auto body = parse_block(kw::repeat);

    auto range_callee = std::make_shared<ny::ast_identifier_expr>("range", line);
    std::vector<ny::ast_expr_ptr> range_args{start, stop};
    auto range = std::make_shared<ny::ast_call_expr>(
        range_callee, std::move(range_args), std::vector<std::string>{}, line);
    std::vector<ny::ast_param> loop_vars{
        ny::ast_param(var, std::nullopt, false, var_line, std::nullopt)};
    loop_vars.front().name_span = name_span(var_token);
    return std::make_shared<ny::ast_for_in_stmt>(std::move(loop_vars), range,
                                                 body, line);
  }

  error(peek(), "expected 'while', 'for' or 'forever' after 'repeat'");
  return nullptr;
}

std::shared_ptr<ny::ast_stmt> parser::parse_statement() {
  submission_depth_guard depth(*this);
  const token &start = peek();
  const int line = start.line;
  if (parsing_submission_ && submission_declaration_start())
    error(start, "named declarations and imports are unavailable in a submission", {}, "MX1002");
  if (parsing_submission_ && check(kw::end) && peek(1).kind == token_kind::end_of_file)
    error(peek(1), "expected a block kind after 'end'");
  if (parsing_submission_ && (check(kw::stop) || check(kw::continue_)) &&
      peek(1).kind == token_kind::end_of_file)
    error(peek(1), "expected 'repeating' after the control-flow keyword");

  if (peek().is_name() && peek(1).is(kw::begin) && exact_phrase_word(peek(2), "pattern")) {
    const auto name = advance();
    auto decl = parse_text_pattern(name, false);
    return std::make_shared<ny::ast_decl_stmt>(decl, line);
  }
  if (check(kw::value) || check(kw::variable) || check(kw::constant))
    return parse_binding_statement();
  if (check(kw::set)) return parse_set();
  if (check(kw::say)) return parse_say();
  if (check(kw::give)) return parse_give();
  if (check(kw::evaluate)) return parse_evaluate();
  if (check(kw::append)) return parse_append();
  if (check(kw::when)) return parse_when_statement();
  if (check(kw::repeat)) return parse_repeat();

  if (check(kw::stop) && peek(1).is(kw::repeating)) {
    advance(); advance(); skip_terminators();
    return std::make_shared<ny::ast_break_stmt>(line);
  }
  if (check(kw::continue_) && peek(1).is(kw::repeating)) {
    advance(); advance(); skip_terminators();
    return std::make_shared<ny::ast_continue_stmt>(line);
  }

  if (check(kw::consider)) return parse_consider_statement();
  if (check(kw::allocate)) return parse_allocate();
  if (check(kw::release)) return parse_release();
  if (check(kw::attempt)) return parse_attempt();
  if (check(kw::raise)) return parse_raise();
  if (check(kw::own)) return parse_own();
  if (check(kw::borrow)) return parse_borrow();
  if (check(kw::move)) return parse_move();
  if (check(kw::sleep)) return parse_sleep();
  if (check(kw::assembly)) return parse_assembly();

  // 'context' and 'note' annotate a declaration, so meeting one where a
  // statement belongs is a misplaced annotation rather than an unknown
  // statement. The generic help lists statement keywords and would never
  // mention the word the author actually wrote, which is the wrong lesson for
  // a reader who is annotating a local binding and has nowhere to be told
  // that only a declaration can carry one.
  if (check(kw::context) || check(kw::note)) {
    const bool is_context = peek().is(kw::context);
    error(start,
          std::string("'") + (is_context ? "context" : "note") +
              "' annotates a declaration, not a statement",
          "only a paragraph, record, class or choice appears in the compiled "
          "artifact, so only those can carry one; write it as a '!!!' comment "
          "if it is for a human reader",
          "MX0104");
  } else if (!report_foreign_keyword(start))
    error(start, "expected a statement, found '" + start.lexeme + "'",
          "a statement begins with value, variable, constant, set, say, give, "
          "evaluate, append, when or repeat",
          "MX0104");
  while (!at_end() && peek().line == line) advance();
  skip_terminators();
  return nullptr;
}

// ---------------------------------------------------------------------------
// Expressions
//
// Precedence, lowest first: or, and, not, comparison, bitwise, shift,
// additive, multiplicative, unary, postfix. Operators are words, so there is
// no symbol precedence to misremember; '(' ')' group.
// ---------------------------------------------------------------------------

ny::ast_expr_ptr parser::parse_expression() {
  submission_depth_guard depth(*this);
  return parse_or();
}

ny::ast_expr_ptr parser::parse_or() {
  auto left = parse_and();
  while (check(kw::or_)) {
    const int line = peek().line;
    advance();
    auto right = parse_and();
    left = std::make_shared<ny::ast_binary_expr>(
        left, ny::token_kind::pipe_pipe, right, line);
  }
  return left;
}

ny::ast_expr_ptr parser::parse_and() {
  auto left = parse_not();
  // 'bitwise and' is a different operator; do not consume its 'and'.
  while (check(kw::and_)) {
    const int line = peek().line;
    advance();
    auto right = parse_not();
    left = std::make_shared<ny::ast_binary_expr>(
        left, ny::token_kind::amp_amp, right, line);
  }
  return left;
}

ny::ast_expr_ptr parser::parse_not() {
  if (check(kw::not_)) {
    submission_depth_guard depth(*this);
    const int line = peek().line;
    advance();
    auto operand = parse_not();
    return std::make_shared<ny::ast_unary_expr>(ny::token_kind::not_kw, operand,
                                                line);
  }
  return parse_comparison();
}

// 'equals' | 'does not equal' | 'is less than' | 'is greater than'
// | 'is at most' | 'is at least'. The phrase words (less, greater, than,
// least, most, equal) are matched on folded text, not reserved, so they stay
// usable as names.
ny::ast_expr_ptr parser::parse_comparison() {
  auto left = parse_bitwise();
  const int line = peek().line;

  if (match(kw::equals)) {
    auto right = parse_bitwise();
    return std::make_shared<ny::ast_binary_expr>(
        left, ny::token_kind::double_equal, right, line);
  }

  if (parsing_submission_ && check(kw::does) &&
      (peek(1).kind == token_kind::end_of_file ||
       (peek(1).is(kw::not_) && peek(2).kind == token_kind::end_of_file)))
    error(tokens_.back(), "expected 'does not equal' followed by a value");
  if (check(kw::does) && peek(1).is(kw::not_) && check_word("equal", 2)) {
    advance(); advance(); advance();
    auto right = parse_bitwise();
    return std::make_shared<ny::ast_binary_expr>(
        left, ny::token_kind::not_equal, right, line);
  }

  if (check(kw::is)) {
    ny::token_kind op = ny::token_kind::invalid;
    if (check_word("less", 1) && check_word("than", 2)) {
      op = ny::token_kind::less;
    } else if (check_word("greater", 1) && check_word("than", 2)) {
      op = ny::token_kind::greater;
    } else if (peek(1).is(kw::at) && check_word("most", 2)) {
      op = ny::token_kind::less_equal;
    } else if (peek(1).is(kw::at) && check_word("least", 2)) {
      op = ny::token_kind::greater_equal;
    }
    if (op != ny::token_kind::invalid) {
      advance(); advance(); advance();
      auto right = parse_bitwise();
      return std::make_shared<ny::ast_binary_expr>(left, op, right, line);
    }
    if (parsing_submission_ && (peek(1).kind == token_kind::end_of_file ||
        ((check_word("less", 1) || check_word("greater", 1) || peek(1).is(kw::at)) &&
         peek(2).kind == token_kind::end_of_file)))
      error(tokens_.back(), "expected the rest of the comparison after 'is'");
    error(peek(), "incomplete comparison after 'is'",
          "the comparisons are 'is less than', 'is greater than', "
          "'is at most', 'is at least', 'equals' and 'does not equal'");
    advance();
  }
  return left;
}

// 'bitwise and' | 'bitwise or' | 'bitwise xor'
ny::ast_expr_ptr parser::parse_bitwise() {
  auto left = parse_shift();
  while (check(kw::bitwise)) {
    const int line = peek().line;
    ny::token_kind op = ny::token_kind::invalid;
    if (peek(1).is(kw::and_)) op = ny::token_kind::amp;
    else if (peek(1).is(kw::or_)) op = ny::token_kind::pipe;
    else if (peek(1).is(kw::xor_)) op = ny::token_kind::caret;
    if (parsing_submission_ && peek(1).kind == token_kind::end_of_file)
      error(peek(1), "expected 'and', 'or' or 'xor' after 'bitwise'");
    if (op == ny::token_kind::invalid) break; // 'bitwise not' is unary
    advance(); advance();
    auto right = parse_shift();
    left = std::make_shared<ny::ast_binary_expr>(left, op, right, line);
  }
  return left;
}

// 'shifted left by' | 'shifted right by'
ny::ast_expr_ptr parser::parse_shift() {
  auto left = parse_additive();
  while (check(kw::shifted)) {
    const int line = peek().line;
    ny::token_kind op = ny::token_kind::invalid;
    if (check_word("left", 1) && check_word("by", 2))
      op = ny::token_kind::less_less;
    else if (check_word("right", 1) && check_word("by", 2))
      op = ny::token_kind::greater_greater;
    if (op == ny::token_kind::invalid) {
      if (parsing_submission_ && (peek(1).kind == token_kind::end_of_file ||
          ((check_word("left", 1) || check_word("right", 1)) &&
           peek(2).kind == token_kind::end_of_file)))
        error(tokens_.back(), "expected 'shifted left by' or 'shifted right by' followed by a value");
      error(peek(), "incomplete shift",
            "write 'shifted left by <count>' or 'shifted right by <count>'");
      break;
    }
    advance(); advance(); advance();
    auto right = parse_additive();
    left = std::make_shared<ny::ast_binary_expr>(left, op, right, line);
  }
  return left;
}

ny::ast_expr_ptr parser::parse_additive() {
  auto left = parse_multiplicative();
  for (;;) {
    const int line = peek().line;
    if (match(kw::plus)) {
      auto right = parse_multiplicative();
      left = std::make_shared<ny::ast_binary_expr>(left, ny::token_kind::plus,
                                                   right, line);
      continue;
    }
    if (match(kw::minus)) {
      auto right = parse_multiplicative();
      left = std::make_shared<ny::ast_binary_expr>(left, ny::token_kind::minus,
                                                   right, line);
      continue;
    }
    break;
  }
  return left;
}

ny::ast_expr_ptr parser::parse_multiplicative() {
  auto left = parse_unary();
  for (;;) {
    const int line = peek().line;
    if (match(kw::times)) {
      auto right = parse_unary();
      left = std::make_shared<ny::ast_binary_expr>(left, ny::token_kind::star,
                                                   right, line);
      continue;
    }
    if (match(kw::over)) {
      auto right = parse_unary();
      left = std::make_shared<ny::ast_binary_expr>(left, ny::token_kind::slash,
                                                   right, line);
      continue;
    }
    if (match(kw::remainder)) {
      auto right = parse_unary();
      left = std::make_shared<ny::ast_binary_expr>(left, ny::token_kind::percent,
                                                   right, line);
      continue;
    }
    break;
  }
  return left;
}

ny::ast_expr_ptr parser::parse_unary() {
  if (parsing_submission_ && check(kw::bitwise) && peek(1).kind == token_kind::end_of_file)
    error(peek(1), "expected 'not' after unary 'bitwise'");
  const int line = peek().line;
  // 'negative' is unary only; 'minus' remains the compatibility spelling.
  // The lexer emits numeric_sign only when '-' touches the following digits.
  if (match(kw::negative) || match(kw::minus) ||
      match(matrix::token_kind::numeric_sign)) {
    submission_depth_guard depth(*this);
    auto operand = parse_unary();
    if (operand && operand->kind == ny::ast_node_kind::number_literal_node) {
      auto &number = static_cast<ny::ast_number_literal &>(*operand);
      if (const auto folded = ny::negated_integer_boundary(number.value)) {
        number.value = *folded;
        return operand;
      }
    }
    return std::make_shared<ny::ast_unary_expr>(ny::token_kind::minus, operand,
                                                line);
  }
  if (check(kw::bitwise) && peek(1).is(kw::not_)) {
    submission_depth_guard depth(*this);
    advance(); advance();
    auto operand = parse_unary();
    return std::make_shared<ny::ast_unary_expr>(ny::token_kind::tilde, operand,
                                                line);
  }
  return parse_postfix();
}

// Keep labels and expressions in source order until the checker has selected
// the callable. A parser name table cannot distinguish imports or shadowing.
std::vector<ny::ast_expr_ptr>
parser::parse_with_arguments(std::vector<std::string> &argument_names,
                             std::vector<std::size_t> &argument_starts) {
  const auto &introducer = tokens_[pos_ - 1];
  argument_starts.push_back(introducer.start + introducer.length);
  if (signature_blank_start_ && *signature_blank_start_ == argument_starts.front() &&
      signature_cursor_ && *signature_cursor_ <= peek().start &&
      (peek().line > introducer.line || peek().kind == token_kind::end_of_file)) {
    recovered_signature_call_ = true;
    return {};
  }
  std::vector<ny::ast_expr_ptr> args;
  if (match(kw::nothing)) return args;
  if (match(kw::more)) {
    do {
      args.push_back(parse_expression());
    } while (match(matrix::token_kind::comma) &&
               (argument_starts.push_back(tokens_[pos_ - 1].start + tokens_[pos_ - 1].length), true));
    return args;
  }
  if (peek().is_name() && peek(1).is(kw::being)) {
    for (;;) {
      if (match(kw::more)) {
        do {
          argument_names.emplace_back();
          args.push_back(parse_expression());
        } while (match(matrix::token_kind::comma) &&
               (argument_starts.push_back(tokens_[pos_ - 1].start + tokens_[pos_ - 1].length), true));
        break;
      }
      if (!peek().is_name()) {
        expected_name("before 'being'");
        break;
      }
      // Labels name the resolved callable's parameters, not local variables.
      const std::string name = advance().lexeme;
      if (!expect(kw::being, "after an argument name")) break;
      argument_names.push_back(name);
      args.push_back(parse_expression());
      if (!match(matrix::token_kind::comma)) break;
      argument_starts.push_back(tokens_[pos_ - 1].start + tokens_[pos_ - 1].length);
    }
    return args;
  }
  do {
    args.push_back(parse_expression());
  } while (match(matrix::token_kind::comma) &&
               (argument_starts.push_back(tokens_[pos_ - 1].start + tokens_[pos_ - 1].length), true));
  return args;
}

ny::ast_expr_ptr parser::parse_postfix() {
  auto expr = parse_primary();
  for (;;) {
    const int line = peek().line;

    // A member may be spelled with a keyword, for the same reason a path
    // segment may: the lexer only produced this dot because a name character
    // follows it.
    //
    // A member name keeps its WRITTEN spelling and is deliberately not run
    // through resolve_name. A member's meaning depends on its receiver's type,
    // which the parser does not know, so resolving it through the page-wide
    // declaration map is simply wrong: a choice variant 'Dropped' and a class
    // field 'dropped' fold to one key, and the variant would silently acquire
    // the field's spelling. That produced a plain CALL instead of
    // NEW_VARIANT and was rejected much later by the fault contract.
    //
    // The cost is that member access is case-sensitive while everything else
    // folds. Closing that gap needs the type checker, which knows the
    // receiver type; it is recorded in matrix/docs/front-end-plan.md rather
    // than papered over here.
    if (check(matrix::token_kind::dot) &&
        (peek(1).is_name() || peek(1).kind == matrix::token_kind::keyword ||
         peek(1).kind == matrix::token_kind::number_literal)) {
      advance();
      const token &member_token = peek();
      const std::string member = nil_symbol(advance().lexeme);
      expr = named(std::make_shared<ny::ast_member_access_expr>(expr, member, line),
                   member_token);
      continue;
    }

    // '<recv> at <index>' is indexing. The index is a unary expression so
    // that 'values at index plus 1' reads as '(values at index) plus 1'.
    if (match(kw::at)) {
      auto index = parse_unary();
      expr = std::make_shared<ny::ast_index_expr>(expr, index, line);
      continue;
    }

    // 'Type.Variant holding v' constructs a variant with a payload, which
    // lowers to the same call shape Ny writes as 'Type.Variant(v)'.
    if (check(kw::holding) &&
        (expr->kind == ny::ast_node_kind::member_access_expr_node ||
         (expr->kind == ny::ast_node_kind::identifier_expr_node &&
          static_cast<const ny::ast_identifier_expr &>(*expr).name.find("::") !=
              std::string::npos))) {
      advance();
      std::vector<ny::ast_expr_ptr> payload{parse_unary()};
      expr = std::make_shared<ny::ast_call_expr>(
          expr, std::move(payload), std::vector<std::string>{}, line);
      continue;
    }

    if (match(kw::becomes)) {
      auto target = parse_type();
      if (target)
        expr = std::make_shared<ny::ast_cast_expr>(expr, *target, line);
      continue;
    }

    // '<paragraph> of <type>' supplies an explicit type argument to a generic
    // paragraph, matching how Ny writes 'default_width<Measurement>()'. Only
    // an identifier can be applied this way; 'of' after anything else belongs
    // to a type position.
    if (check(kw::of) &&
        expr->kind == ny::ast_node_kind::identifier_expr_node) {
      advance();
      std::vector<std::string> type_args;
      do {
        auto arg = parse_type();
        if (!arg) break;
        type_args.push_back(*arg);
      } while (match(matrix::token_kind::comma));
      // Carry the arguments on a call node; a bare application with no 'with'
      // clause is still a call with no arguments.
      std::vector<std::string> argument_names;
      std::vector<std::size_t> argument_starts;
      std::vector<ny::ast_expr_ptr> args;
      if (match(kw::with)) args = parse_with_arguments(argument_names, argument_starts);
      auto call = std::make_shared<ny::ast_call_expr>(expr, std::move(args),
                                                     std::move(type_args), line);
      call->source_file = filename_;
      if (!argument_starts.empty()) {
        const auto &last = tokens_[pos_ - 1];
        const auto end = std::max(last.start + last.length,
            recovered_signature_call_ && signature_cursor_ ? *signature_cursor_ : std::size_t(0));
        call->argument_span = ny::source_span{argument_starts.front(), end - argument_starts.front()};
        call->argument_starts = std::move(argument_starts);
        call->source_argument_names = argument_names;
      }
      call->argument_names = std::move(argument_names);
      expr = std::move(call);
      continue;
    }

    if (match(kw::with)) {
      std::vector<std::string> argument_names;
      std::vector<std::size_t> argument_starts;
      auto args = parse_with_arguments(argument_names, argument_starts);
      auto call = std::make_shared<ny::ast_call_expr>(
          expr, std::move(args), std::vector<std::string>{}, line);
      call->source_file = filename_;
      if (!argument_starts.empty()) {
        const auto &last = tokens_[pos_ - 1];
        const auto end = std::max(last.start + last.length,
            recovered_signature_call_ && signature_cursor_ ? *signature_cursor_ : std::size_t(0));
        call->argument_span = ny::source_span{argument_starts.front(), end - argument_starts.front()};
        call->argument_starts = std::move(argument_starts);
        call->source_argument_names = argument_names;
      }
      call->argument_names = std::move(argument_names);
      expr = std::move(call);
      continue;
    }

    break;
  }
  return expr;
}

// 'when c give a otherwise give b end when' as a value.
ny::ast_expr_ptr parser::parse_when_expression() {
  submission_depth_guard depth(*this);
  const int line = peek().line;
  advance(); // 'when'
  auto condition = parse_expression();
  // 'then' is optional here. The statement form is "when c then ...", so
  // writing "when c then give a" is what anyone transfers from it; 'give'
  // still marks the value, which is what the value position actually needs.
  match(kw::then);
  if (!expect(kw::give, "after the condition in a value-position 'when'"))
    return nullptr;
  auto then_value = parse_expression();

  if (!expect(kw::otherwise, "before the alternative value")) return nullptr;
  ny::ast_expr_ptr else_value;
  if (check(kw::when)) {
    else_value = parse_when_expression();
    return std::make_shared<ny::ast_if_expr>(condition, then_value, else_value,
                                             line);
  }
  if (!expect(kw::give, "after 'otherwise'")) return nullptr;
  else_value = parse_expression();
  if (check(kw::end) && peek(1).is(kw::when)) {
    advance();
    advance();
  } else {
    if (parsing_submission_ && check(kw::end) && peek(1).kind == token_kind::end_of_file)
      error(peek(1), "expected 'when' after 'end'");
    error(peek(), "expected 'end when' to close a value-position 'when'");
  }
  return std::make_shared<ny::ast_if_expr>(condition, then_value, else_value,
                                           line);
}

ny::ast_expr_ptr parser::parse_primary() {
  if (exact_phrase_word(peek(), "collect") && peek(1).is(kw::begin) &&
      exact_phrase_word(peek(2), "query"))
    return parse_query_expression();
  const token &start = peek();
  const int line = start.line;
  if (parsing_submission_ && (check(kw::now) || check(kw::reference)) &&
      peek(1).kind == token_kind::end_of_file)
    error(peek(1), "expected the rest of the value expression");

  if (check(matrix::token_kind::number_literal)) {
    auto number = std::make_shared<ny::ast_number_literal>(advance().lexeme, line);
    if (ny::is_integer_minimum_magnitude(number->value))
      integer_boundaries_.emplace(pos_ - 1, number);
    return number;
  }

  if (match(matrix::token_kind::formatted_string_begin)) {
    std::vector<ny::interpolated_part> parts;
    while (!check(matrix::token_kind::formatted_string_end) && !at_end()) {
      if (match(matrix::token_kind::formatted_text)) {
        parts.push_back({true, previous().lexeme, nullptr});
        continue;
      }
      if (!match(matrix::token_kind::interpolation_begin)) {
        error(peek(), "expected a text segment or interpolation");
        advance();
        continue;
      }
      if (check(matrix::token_kind::interpolation_end)) {
        error(peek(), "interpolation requires an expression");
        advance();
        continue;
      }
      auto expression = parse_expression();
      if (!check(matrix::token_kind::interpolation_end)) {
        error(peek(), "expected '}' after the interpolation expression");
        while (!check(matrix::token_kind::interpolation_end) &&
               !check(matrix::token_kind::formatted_string_end) && !at_end()) advance();
      }
      expect(matrix::token_kind::interpolation_end, "'}'", "after interpolation");
      if (expression) parts.push_back({false, "", std::move(expression)});
    }
    expect(matrix::token_kind::formatted_string_end, "a closing quote", "after formatted text");
    if (parts.empty()) parts.push_back({true, "", nullptr});
    auto result = std::make_shared<ny::ast_interpolated_string_expr>(std::move(parts), line);
    result->explicit_syntax = true;
    return result;
  }
  if (check(matrix::token_kind::string_literal))
    return std::make_shared<ny::ast_string_literal>(advance().lexeme, line);

  if (match(kw::true_)) return std::make_shared<ny::ast_boolean_literal>(true, line);
  if (match(kw::false_)) return std::make_shared<ny::ast_boolean_literal>(false, line);

  // Named characters replace escape sequences entirely; they are ordinary
  // text values, and 'plus' is already text concatenation.
  if (match(kw::quote)) return std::make_shared<ny::ast_string_literal>("\"", line);
  if (match(kw::newline)) return std::make_shared<ny::ast_string_literal>("\n", line);
  if (match(kw::tab)) return std::make_shared<ny::ast_string_literal>("\t", line);

  if (match(matrix::token_kind::left_paren)) {
    auto inner = parse_expression();
    if (!match(matrix::token_kind::comma)) {
      expect(matrix::token_kind::right_paren, "')'", "to close a group");
      return inner;
    }
    std::vector<std::pair<std::string, ny::ast_expr_ptr>> elements;
    elements.emplace_back("0", std::move(inner));
    while (!check(matrix::token_kind::right_paren) && !at_end()) {
      elements.emplace_back(std::to_string(elements.size()), parse_expression());
      if (!match(matrix::token_kind::comma)) break;
    }
    expect(matrix::token_kind::right_paren, "')'", "after tuple elements");
    auto tuple = std::make_shared<ny::ast_struct_literal_expr>("tuple",
        std::move(elements), std::vector<std::string>{}, line);
    tuple->tuple_literal = true;
    return tuple;
  }

  if (match(matrix::token_kind::left_bracket)) {
    std::vector<ny::ast_expr_ptr> elements;
    if (!check(matrix::token_kind::right_bracket)) {
      do {
        elements.push_back(parse_expression());
      } while (match(matrix::token_kind::comma));
    }
    expect(matrix::token_kind::right_bracket, "']'", "to close a list");
    return std::make_shared<ny::ast_array_literal_expr>(std::move(elements), line);
  }

  // 'the length of e' -> e.len(); 'the paragraph f' -> a paragraph value.
  if (match(kw::the)) {
    if (match_word("length")) {
      submission_depth_guard depth(*this);
      if (!expect(kw::of, "after 'the length'")) return nullptr;
      auto subject = parse_postfix();
      auto member =
          std::make_shared<ny::ast_member_access_expr>(subject, "len", line);
      return std::make_shared<ny::ast_call_expr>(
          member, std::vector<ny::ast_expr_ptr>{}, std::vector<std::string>{},
          line);
    }
    if (match(kw::paragraph)) {
      if (!peek().is_name()) {
        expected_name("after 'the paragraph'");
        return nullptr;
      }
      const token &name_token = peek();
      return named(std::make_shared<ny::ast_identifier_expr>(
                       resolve_name(advance().lexeme), line), name_token);
    }
    error(peek(), "expected 'length of' or 'paragraph' after 'the'");
    return nullptr;
  }

  // 'apply worker across inputs' -> parallel_map(worker, inputs). The whole
  // Ny contract is inherited: a statically resolved, nongeneric worker with
  // one scalar parameter and one scalar result, deterministic result order,
  // and a transitive call graph that cannot touch globals, handles, text,
  // arrays, I/O, clocks or faults.
  if (match(kw::apply)) {
    submission_depth_guard depth(*this);
    if (!peek().is_name()) {
      error(peek(), "expected a worker paragraph name after 'apply'",
            "the worker must be a statically resolved paragraph, not a value");
      return nullptr;
    }
    const token &worker_token = peek();
    auto worker = named(std::make_shared<ny::ast_identifier_expr>(
                            resolve_name(advance().lexeme), line), worker_token);
    if (!expect(kw::across, "after the worker name")) return nullptr;
    auto inputs = parse_postfix();
    auto callee =
        std::make_shared<ny::ast_identifier_expr>("parallel_map", line);
    std::vector<ny::ast_expr_ptr> args{worker, inputs};
    return std::make_shared<ny::ast_call_expr>(
        callee, std::move(args), std::vector<std::string>{}, line);
  }

  // 'now in milliseconds' -> the time.now intrinsic, declared on first use.
  if (check(kw::now) && peek(1).is(kw::in_)) {
    advance(); advance();
    if (!match_word("milliseconds"))
      error(peek(), "expected 'milliseconds' after 'now in'",
            "milliseconds is the only unit");
    ensure_time_intrinsic("time.now", "matrix_now_milliseconds",
                          std::string("i64"), std::nullopt);
    auto callee = std::make_shared<ny::ast_identifier_expr>(
        "matrix_now_milliseconds", line);
    return std::make_shared<ny::ast_call_expr>(
        callee, std::vector<ny::ast_expr_ptr>{}, std::vector<std::string>{},
        line);
  }

  // A noncapturing anonymous paragraph. Captures are rejected by Ny, and
  // native indirect calls are not implemented, so this is interpreted-route
  // only -- a route fact the sample comments repeat.
  if (match(kw::anonymous)) {
    std::vector<std::string> param_names;
    std::vector<ny::ast_param> params;
    if (match(kw::receives)) params = parse_parameters(param_names);
    std::optional<std::string> return_type = std::string("void");
    if (match(kw::returns)) return_type = parse_type();
    if (!expect(kw::begin, "to open an anonymous paragraph")) return nullptr;
    if (!expect(kw::paragraph, "after 'begin'")) return nullptr;
    auto body = parse_block(kw::paragraph);
    auto lambda = std::make_shared<ny::ast_lambda_expr>(params, return_type,
                                                        body, line);
    return lambda;
  }

  if (check(kw::when)) return parse_when_expression();
  if (check(kw::consider)) return parse_consider_expression();
  if (check(kw::build)) return parse_build();

  // 'reference to x' aliases an existing nominal aggregate handle.
  if (check(kw::reference) && peek(1).is(kw::to)) {
    submission_depth_guard depth(*this);
    advance(); advance();
    auto operand = parse_postfix();
    return std::make_shared<ny::ast_ref_expr>(operand, line);
  }

  // 'my x' is the receiver's field or method: it lowers to 'self.x', and
  // parse_postfix turns a following 'with' clause into a method call. Bare
  // 'my' is the receiver itself.
  if (match(kw::my)) {
    auto receiver = std::make_shared<ny::ast_identifier_expr>("self", line);
    if (peek().is_name()) {
      const token &member_token = peek();
      return named(std::make_shared<ny::ast_member_access_expr>(
                       receiver, nil_symbol(advance().lexeme), line), member_token);
    }
    return receiver;
  }

  if (start.is_name()) {
    // In EXPRESSION position a dot is member access, so only a book-qualified
    // reference may swallow dots into one identifier. '::' is the
    // discriminator: 'tour.billing::audit-level' is a single global storage
    // identity, while 'corner.x' and 'job-state.Queued' are member accesses
    // that parse_postfix must see.
    //
    // Look ahead over '.name' segments for a '::'. Without one, take only the
    // first name and leave the dots to parse_postfix.
    size_t probe = pos_ + 1;
    bool qualified = false;
    while (probe + 1 < tokens_.size() &&
           tokens_[probe].kind == matrix::token_kind::dot &&
           (tokens_[probe + 1].is_name() ||
            tokens_[probe + 1].kind == matrix::token_kind::keyword)) {
      probe += 2;
    }
    if (probe < tokens_.size() &&
        tokens_[probe].kind == matrix::token_kind::double_colon) {
      qualified = true;
    }

    if (qualified) {
      const std::string name = parse_dotted_name();
      return named(std::make_shared<ny::ast_identifier_expr>(nil_path(name), line),
                   previous());
    }
    return named(std::make_shared<ny::ast_identifier_expr>(
                     resolve_name(advance().lexeme), line), start);
  }

  // Statement-only constructs in value position. Naming the construct is the
  // point: 'own' here is not a malformed value, it is a statement whose
  // whole design is that Ny accepts an ownership operation only as a direct
  // local declaration initialiser, so there is no expression form to fall
  // back on.
  if (check(kw::own) || check(kw::borrow) || check(kw::move) ||
      check(kw::allocate) || check(kw::release) || check(kw::assembly) ||
      check(kw::attempt) || check(kw::raise) || check(kw::say) ||
      check(kw::set)) {
    error(start,
          "'" + start.lexeme + "' is a statement, not a value",
          "a binding takes an expression; write the construct as its own "
          "statement on the preceding line");
    advance();
    return std::make_shared<ny::ast_number_literal>("0", line);
  }

  error(start, "expected a value, found '" + start.lexeme + "'");
  advance();
  return std::make_shared<ny::ast_number_literal>("0", line);
}

ny::ast_expr_ptr parser::parse_query_expression() {
  const token start = advance(); // contextual collect
  advance(); // begin
  advance(); // query
  auto query = std::make_shared<ny::ast_query_expr>(start.line);
  const auto query_word = [&](const char *word) { return exact_phrase_word(peek(), word); };
  const auto take_query_word = [&](const char *word) {
    if (!query_word(word)) return false;
    advance(); return true;
  };
  const auto require_word = [&](const char *word) {
    if (take_query_word(word)) return true;
    error(peek(), std::string("expected '") + word + "' in collect query");
    return false;
  };
  skip_terminators();
  require_word("from");
  require_word("each");
  if (!peek().is_name()) {
    expected_name("after 'from each'");
  } else {
    const token binder = advance();
    query->binding.name = resolve_name(binder.lexeme);
    query->binding.name_span = name_span(binder);
    query->binding.line = binder.line;
  }
  require_word("in");
  query->source = parse_expression();
  int previous_clause = 0;
  while (!at_end() && !check(kw::end)) {
    skip_terminators();
    if (check(kw::end)) break;
    const int clause = query_word("where") ? 1 : query_word("order") ? 2 :
                       query_word("skip") ? 3 : query_word("take") ? 4 : check(kw::give) ? 5 : 0;
    if (!clause) {
      error(peek(), "expected where, order by, skip, take, give, or end query");
      advance();
      continue;
    }
    if (clause <= previous_clause)
      error(peek(), "query clauses must appear once in from, where, order by, skip, take, give order");
    previous_clause = clause;
    advance();
    if (clause == 2) require_word("by");
    auto expression = parse_expression();
    if (clause == 1) query->predicate = expression;
    if (clause == 2) {
      query->order_key = expression;
      if (take_query_word("descending")) query->descending = true;
      else if (take_query_word("ascending")) {}
    }
    if (clause == 3) query->skip = expression;
    if (clause == 4) query->take = expression;
    if (clause == 5) query->projection = expression;
  }
  expect(kw::end, "after query projection");
  require_word("query");
  if (!query->projection) error(start, "collect query requires a final give expression");
  return query;
}


// ---------------------------------------------------------------------------
// Stage 4: aggregates -- records, choices, generics, construction, matching,
// raw pointers and references.
// ---------------------------------------------------------------------------

// 'of type T [meeting B and C], type U'. Several bounds join with 'and',
// which is unambiguous because a bound is a type name.
std::vector<ny::ast_generic_param> parser::parse_generic_clause() {
  std::vector<ny::ast_generic_param> generics;
  do {
    if (!expect(kw::type, "after 'of' in a generic clause")) break;
    if (!peek().is_name()) {
      expected_name("after 'type'");
      break;
    }
    ny::ast_generic_param param;
    param.name = nil_symbol(advance().lexeme);
    if (match(kw::meeting)) {
      for (;;) {
        if (!peek().is_name()) {
          expected_name("after 'meeting'");
          break;
        }
        param.bounds.push_back(nil_symbol(advance().lexeme));
        if (!match(kw::and_)) break;
      }
    }
    generics.push_back(std::move(param));
  } while (match(matrix::token_kind::comma) && check(kw::type));
  return generics;
}

// '<name> begin record' / 'field x as T' ... 'end record'
std::shared_ptr<ny::ast_decl>
parser::parse_record(bool is_public, int line, const std::string &name,
                     std::vector<ny::ast_generic_param> generics) {
  advance(); // 'begin'
  advance(); // 'record'

  std::vector<ny::ast_struct_field> fields;
  for (;;) {
    skip_terminators();
    if (at_end()) {
      error(peek(), "expected 'end record' before the end of the page");
      break;
    }
    if (check(kw::end) && peek(1).is(kw::record)) {
      advance(); advance(); skip_terminators();
      break;
    }
    if (!expect(kw::field, "to begin a record member")) {
      // Keep the diagnostic local: skip the offending line and continue, so
      // one bad member does not cascade into the rest of the record.
      const int bad = peek().line;
      while (!at_end() && peek().line == bad) advance();
      continue;
    }
    if (!peek().is_name()) {
      expected_name("after 'field'");
      continue;
    }
    ny::ast_struct_field field;
    field.line = peek().line;
    field.name_span = name_span(peek());
    field.name = nil_symbol(advance().lexeme); // a member: written spelling
    if (!expect(kw::as, "after a field name")) continue;
    auto type = parse_type();
    // A field default would read exactly like a working initialiser, and Ny
    // rejects them, so Matrix has no spelling for one.
    if (check(kw::is)) {
      error(peek(), "a record field cannot have a default value",
            "Ny rejects field defaults; initialise every field in 'build'");
      advance();
      parse_expression();
    }
    if (!type) continue;
    field.type_name = *type;
    fields.push_back(std::move(field));
    skip_terminators();
  }
  return std::make_shared<ny::ast_struct_decl>(nil_symbol(name), fields,
                                               std::move(generics), is_public,
                                               line);
}

// '<name> begin choice' / 'when Variant [holding T]' ... 'end choice'
std::shared_ptr<ny::ast_decl>
parser::parse_choice(bool is_public, int line, const std::string &name,
                     std::vector<ny::ast_generic_param> generics) {
  advance(); // 'begin'
  advance(); // 'choice'

  std::vector<ny::ast_enum_variant> variants;
  for (;;) {
    skip_terminators();
    if (at_end()) {
      error(peek(), "expected 'end choice' before the end of the page");
      break;
    }
    if (check(kw::end) && peek(1).is(kw::choice)) {
      advance(); advance(); skip_terminators();
      break;
    }
    // A variant is introduced by 'when' -- the same word that selects one
    // later, because declaring a case and matching a case are one relation.
    if (!expect(kw::when, "to begin a choice variant")) {
      const int bad = peek().line;
      while (!at_end() && peek().line == bad) advance();
      continue;
    }
    if (!peek().is_name()) {
      expected_name("after 'when' in a choice");
      continue;
    }
    ny::ast_enum_variant variant;
    variant.line = peek().line;
    variant.name = advance().lexeme; // variants keep their written case
    if (match(kw::holding)) {
      auto payload = parse_type();
      if (payload) variant.associated_type = *payload;
    }
    variants.push_back(std::move(variant));
    skip_terminators();
  }
  return std::make_shared<ny::ast_enum_decl>(nil_symbol(name), variants,
                                             std::move(generics), is_public,
                                             line);
}

// 'build <Type> with f being v, ...'
ny::ast_expr_ptr parser::parse_build() {
  const int line = peek().line;
  advance(); // 'build'

  if (!peek().is_name()) {
    expected_name("after 'build'");
    return nullptr;
  }
  const std::string type_name = resolve_name(parse_dotted_name());

  // 'build envelope of number with ...' carries explicit type arguments.
  std::vector<std::string> type_args;
  if (match(kw::of)) {
    do {
      auto arg = parse_type();
      if (!arg) break;
      type_args.push_back(*arg);
    } while (match(matrix::token_kind::comma));
  }

  std::vector<std::pair<std::string, ny::ast_expr_ptr>> fields;
  if (match(kw::with)) {
    do {
      if (!peek().is_name()) {
        expected_name("as a field in the 'build' clause");
        break;
      }
      // A field name is a member: written spelling, for the reason given in
      // parse_postfix.
      const std::string field = nil_symbol(advance().lexeme);
      if (!expect(kw::being, "after a field name")) break;
      fields.emplace_back(field, parse_expression());
    } while (match(matrix::token_kind::comma));
  }
  return std::make_shared<ny::ast_struct_literal_expr>(
      type_name, std::move(fields), std::move(type_args), line);
}

// A literal, '_', a bare binding, or 'Type.Variant [holding binding]'.
ny::ast_pattern parser::parse_pattern() {
  submission_depth_guard depth(*this);
  ny::ast_pattern pattern;
  if (match(matrix::token_kind::left_paren)) {
    pattern.kind = ny::ast_pattern_kind::constructor_pattern;
    do {
      if (check(matrix::token_kind::right_paren)) break;
      pattern.subpatterns.push_back(parse_pattern());
    } while (match(matrix::token_kind::comma));
    expect(matrix::token_kind::right_paren, "')'", "after tuple pattern");
    return pattern;
  }

  if (peek().is_name() && peek().lexeme == "_") {
    advance();
    pattern.kind = ny::ast_pattern_kind::wildcard_pattern;
    pattern.name = "_";
    return pattern;
  }

  const bool negative_number = (check(kw::minus) || check(kw::negative) ||
      check(matrix::token_kind::numeric_sign)) &&
      peek(1).kind == matrix::token_kind::number_literal;
  if (negative_number) advance();
  if (check(matrix::token_kind::number_literal)) {
    pattern.kind = ny::ast_pattern_kind::literal_pattern;
    const auto number = advance();
    pattern.literal_value = (negative_number ? "-" : "") + number.lexeme;
    if (negative_number && ny::is_integer_minimum_magnitude(number.lexeme))
      integer_boundaries_.emplace(pos_ - 1,
          std::make_shared<ny::ast_number_literal>(pattern.literal_value, number.line));
    pattern.literal_kind = ny::ast_node_kind::number_literal_node;
    return pattern;
  }
  if (check(matrix::token_kind::string_literal)) {
    pattern.kind = ny::ast_pattern_kind::literal_pattern;
    pattern.literal_value = advance().lexeme;
    // Lexical identity matters: quoted "12" is text, not a scalar.
    pattern.literal_kind = ny::ast_node_kind::string_literal_node;
    return pattern;
  }
  if (check(kw::true_) || check(kw::false_)) {
    pattern.kind = ny::ast_pattern_kind::literal_pattern;
    pattern.literal_value = advance().lexeme;
    pattern.literal_kind = ny::ast_node_kind::boolean_literal_node;
    return pattern;
  }

  if (!peek().is_name()) {
    error(peek(), "expected a pattern after 'when'",
          "a pattern is a literal, a name, or 'Type.Variant [holding name]'");
    pattern.kind = ny::ast_pattern_kind::wildcard_pattern;
    pattern.name = "_";
    return pattern;
  }

  const std::string name = ny::canonical_std_namespace(nil_symbol(parse_dotted_name()));

  // A dotted name is a constructor; a bare name binds the value. This is the
  // same rule the Ny parser uses, so both surfaces agree on what a pattern
  // means.
  const bool qualified = name.find('.') != std::string::npos ||
                         name.find("::") != std::string::npos;
  if (match(kw::holding)) {
    pattern.kind = ny::ast_pattern_kind::constructor_pattern;
    pattern.name = name;
    ny::ast_pattern bound;
    if (peek().is_name()) {
      bound.kind = ny::ast_pattern_kind::variable_pattern;
      bound.name = nil_symbol(advance().lexeme);
    } else {
      bound.kind = ny::ast_pattern_kind::wildcard_pattern;
      bound.name = "_";
    }
    pattern.subpatterns.push_back(std::move(bound));
    return pattern;
  }
  pattern.kind = qualified ? ny::ast_pattern_kind::constructor_pattern
                           : ny::ast_pattern_kind::variable_pattern;
  pattern.name = name;
  return pattern;
}

// 'consider e / when P [provided g] then ... / otherwise ... / end consider'
std::shared_ptr<ny::ast_stmt> parser::parse_consider_statement() {
  const int line = peek().line;
  advance(); // 'consider'
  auto target = parse_expression();

  std::vector<ny::ast_match_arm> arms;
  for (;;) {
    skip_terminators();
    if (at_end()) {
      error(peek(), "expected 'end consider' before the end of the page");
      break;
    }
    if (check(kw::end) && peek(1).is(kw::consider)) {
      advance(); advance(); skip_terminators();
      break;
    }

    if (match(kw::otherwise)) {
      // The wildcard arm. Its body runs to 'end consider'.
      auto body = std::make_shared<ny::ast_block_stmt>(peek().line);
      for (;;) {
        skip_terminators();
        if (at_end() || (check(kw::end) && peek(1).is(kw::consider))) break;
        const size_t before = pos_;
        if (auto stmt = parse_statement()) body->statements.push_back(stmt);
        if (pos_ == before) advance();
      }
      ny::ast_pattern wildcard;
      wildcard.kind = ny::ast_pattern_kind::wildcard_pattern;
      wildcard.name = "_";
      arms.emplace_back(std::move(wildcard), body, nullptr);
      continue;
    }

    if (parsing_submission_ && check(kw::end) && peek(1).kind == token_kind::end_of_file)
      error(peek(1), "expected 'consider' after 'end'");
    if (!expect(kw::when, "to begin a 'consider' arm")) {
      const int bad = peek().line;
      while (!at_end() && peek().line == bad) advance();
      continue;
    }
    auto pattern = parse_pattern();
    ny::ast_expr_ptr guard;
    // A guard uses 'provided', not a second 'when', so an arm and its guard
    // stay lexically distinct.
    if (match(kw::provided)) guard = parse_expression();
    if (!expect(kw::then, "after a 'consider' arm pattern")) continue;

    // An arm body runs until the next arm, the wildcard, or the terminator.
    auto body = std::make_shared<ny::ast_block_stmt>(peek().line);
    for (;;) {
      skip_terminators();
      if (at_end()) break;
      if (check(kw::when) || check(kw::otherwise)) break;
      if (check(kw::end) && peek(1).is(kw::consider)) break;
      const size_t before = pos_;
      if (auto stmt = parse_statement()) body->statements.push_back(stmt);
      if (pos_ == before) advance();
    }
    arms.emplace_back(std::move(pattern), body, guard);
  }
  return std::make_shared<ny::ast_match_stmt>(target, std::move(arms), line);
}

// Value position: every arm says 'give', which makes Ny's "exactly one
// value-producing expression per arm" a parse rule instead of a later check.
ny::ast_expr_ptr parser::parse_consider_expression() {
  const int line = peek().line;
  advance(); // 'consider'
  auto target = parse_expression();

  std::vector<ny::ast_match_case> cases;
  for (;;) {
    skip_terminators();
    if (at_end()) {
      error(peek(), "expected 'end consider' before the end of the page");
      break;
    }
    if (check(kw::end) && peek(1).is(kw::consider)) {
      advance(); advance();
      break;
    }

    ny::ast_expr_ptr pattern_expr;
    ny::ast_expr_ptr guard;
    if (match(kw::otherwise)) {
      pattern_expr = std::make_shared<ny::ast_identifier_expr>("_", line);
    } else {
      if (parsing_submission_ && check(kw::end) && peek(1).kind == token_kind::end_of_file)
        error(peek(1), "expected 'consider' after 'end'");
      if (!expect(kw::when, "to begin a 'consider' arm")) break;
      // ast_match_case holds the pattern as an expression, and codegen reads a
      // specific shape: member_access(identifier(choice), variant) for a unit
      // variant, wrapped in a call for one carrying a payload. A dotted
      // identifier is NOT that shape and would be emitted as a variable load.
      const int arm_line = peek().line;

      if (check(matrix::token_kind::left_paren)) {
        pattern_expr = parse_primary();
        if (match(kw::provided)) guard = parse_expression();
        match(kw::then);
        if (!expect(kw::give, "before the value of a 'consider' arm")) break;
        auto value = parse_expression();
        cases.emplace_back(pattern_expr, guard,
            std::make_shared<ny::ast_expr_stmt>(value, value->line));
        continue;
      }
      // A literal arm compares the target against a value, which is an
      // ordinary expression pattern.
      if (check(matrix::token_kind::number_literal) ||
          ((check(kw::minus) || check(kw::negative) ||
            check(matrix::token_kind::numeric_sign)) &&
           peek(1).kind == matrix::token_kind::number_literal)) {
        pattern_expr = parse_unary();
        if (match(kw::provided)) guard = parse_expression();
        match(kw::then); // optional, for symmetry with the statement form
        if (!expect(kw::give, "before the value of a 'consider' arm")) break;
        auto literal_value = parse_expression();
        cases.emplace_back(
            pattern_expr, guard,
            std::make_shared<ny::ast_expr_stmt>(literal_value,
                                                literal_value->line));
        continue;
      }
      if (check(matrix::token_kind::string_literal)) {
        pattern_expr = std::make_shared<ny::ast_string_literal>(
            advance().lexeme, arm_line);
        if (match(kw::provided)) guard = parse_expression();
        match(kw::then); // optional, for symmetry with the statement form
        if (!expect(kw::give, "before the value of a 'consider' arm")) break;
        auto literal_value = parse_expression();
        cases.emplace_back(
            pattern_expr, guard,
            std::make_shared<ny::ast_expr_stmt>(literal_value,
                                                literal_value->line));
        continue;
      }
      if (check(kw::true_) || check(kw::false_)) {
        const bool truth = peek().is(kw::true_);
        advance();
        pattern_expr =
            std::make_shared<ny::ast_boolean_literal>(truth, arm_line);
        if (match(kw::provided)) guard = parse_expression();
        match(kw::then); // optional, for symmetry with the statement form
        if (!expect(kw::give, "before the value of a 'consider' arm")) break;
        auto literal_value = parse_expression();
        cases.emplace_back(
            pattern_expr, guard,
            std::make_shared<ny::ast_expr_stmt>(literal_value,
                                                literal_value->line));
        continue;
      }

      if (!peek().is_name()) {
        error(peek(), "expected a literal or a choice name in a 'consider' "
                      "arm pattern");
        break;
      }
      const std::string variant_name = nil_path(parse_dotted_name());
      const auto separator = variant_name.rfind("::");
      ny::ast_expr_ptr base;
      if (separator != std::string::npos) {
        auto owner = std::make_shared<ny::ast_identifier_expr>(
            variant_name.substr(0, separator), arm_line);
        base = std::make_shared<ny::ast_member_access_expr>(
            owner, variant_name.substr(separator + 2), arm_line);
      } else {
        error(peek(), "expected '.<variant>' after the choice name",
              "a value-position arm matches a literal or a choice variant: "
              "'when 1 give ...' or 'when choice.Variant [holding name] "
              "give ...'");
        base = std::make_shared<ny::ast_identifier_expr>(variant_name, arm_line);
      }
      if (match(kw::holding)) {
        std::vector<ny::ast_expr_ptr> bound;
        if (peek().is_name())
          bound.push_back(std::make_shared<ny::ast_identifier_expr>(
              nil_symbol(advance().lexeme), arm_line));
        base = std::make_shared<ny::ast_call_expr>(
            base, std::move(bound), std::vector<std::string>{}, arm_line);
      }
      pattern_expr = base;
      if (match(kw::provided)) guard = parse_expression();
    }

    match(kw::then); // optional, for symmetry with the statement form
    if (!expect(kw::give, "before the value of a 'consider' arm")) break;
    auto value = parse_expression();
    // codegen requires each arm body to be exactly one expression statement;
    // a block holding a 'return' would return from the enclosing paragraph
    // rather than supply this arm's value.
    auto body = std::make_shared<ny::ast_expr_stmt>(value, value->line);
    cases.emplace_back(pattern_expr, guard, body);
  }
  return std::make_shared<ny::ast_match_expr>(target, std::move(cases), line);
}

// 'allocate p as pointer to T sized n' -> let p = allocate<T>(n)
std::shared_ptr<ny::ast_stmt> parser::parse_allocate() {
  const int line = peek().line;
  advance(); // 'allocate'
  if (!peek().is_name()) {
    expected_name("after 'allocate'");
    return nullptr;
  }
  const token &name_token = peek();
  const std::string name = nil_symbol(advance().lexeme);
  if (!expect(kw::as, "after the allocation name")) return nullptr;
  auto type = parse_type();
  if (!expect(kw::sized, "before the element count")) return nullptr;
  auto count = parse_expression();
  skip_terminators();
  if (!type) return nullptr;

  // The declared type is ptr<T>; allocate<T> takes the element type.
  std::string element = *type;
  if (element.rfind("ptr<", 0) == 0 && element.back() == '>')
    element = element.substr(4, element.size() - 5);
  else
    error(peek(), "'allocate' needs a pointer type",
          "write 'allocate values as pointer to number sized 2'");

  auto init = std::make_shared<ny::ast_allocate_expr>(element, count, line);
  auto decl = std::make_shared<ny::ast_variable_decl>(name, init, type, true,
                                                      false, line);
  decl->name_span = name_span(name_token);
  return std::make_shared<ny::ast_decl_stmt>(decl, line);
}

// 'release p' -> deallocate(p)
std::shared_ptr<ny::ast_stmt> parser::parse_release() {
  const int line = peek().line;
  advance(); // 'release'
  auto target = parse_postfix();
  skip_terminators();
  return std::make_shared<ny::ast_deallocate_stmt>(target, line);
}

} // namespace matrix
