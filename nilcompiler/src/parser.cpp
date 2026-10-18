/*
Implementation of the Ny parser described in parser.hpp. The parser supports
the language features exercised by the current compiler and development
fixtures, including:
 - function declarations
 - block statements
 - println(...) statements
 - string/number/identifier expressions
 - basic binary/unary expressions (+, -, *, /, ==, !=, <, <=, >, >=)
 - call expressions with comma-separated args

Design notes:
 - Naming follows K&R underscore style (parser, parse_tokens, token_kind, etc).
 - Parser performs simple error recovery (synchronize) and records diagnostics.
 - AST nodes are created using shared_ptr types defined in ast.hpp.
 - This implementation is intentionally small and focuses on robustness.
*/

#include <optional>
#include "parser.hpp"
#include "numeric_literal.hpp"
#include "standard_namespace.hpp"
#include "diagnostics.hpp"

#include <cassert>
#include <iostream>
#include <optional>
#include <string>
#include <vector>
#include <cstdlib>

namespace ny {
// ... (skip) ...
// (We just match the specific block to remove include)

static thread_local std::optional<std::string> parser_current_module;

// ----------------------------
// Utilities
// ----------------------------
static std::string token_kind_to_string(token_kind k) {
  switch (k) {
  case token_kind::left_paren:
    return "(";
  case token_kind::right_paren:
    return ")";
  case token_kind::left_brace:
    return "{";
  case token_kind::right_brace:
    return "}";
  case token_kind::left_bracket:
    return "[";
  case token_kind::right_bracket:
    return "]";
  case token_kind::comma:
    return ",";
  case token_kind::colon:
    return ":";
  case token_kind::semicolon:
    return ";";
  case token_kind::plus:
    return "+";
  case token_kind::minus:
    return "-";
  case token_kind::star:
    return "*";
  case token_kind::slash:
    return "/";
  case token_kind::equal:
    return "=";
  case token_kind::double_equal:
    return "==";
  case token_kind::not_equal:
    return "!=";
  case token_kind::less:
    return "<";
  case token_kind::greater:
    return ">";
  case token_kind::less_equal:
    return "<=";
  case token_kind::greater_equal:
    return ">=";
  case token_kind::identifier:
    return "identifier";
  case token_kind::number_literal:
    return "number";
  case token_kind::string_literal:
    return "string";
  case token_kind::char_literal:
    return "char";
  case token_kind::function_kw:
    return "function";
  case token_kind::let_kw:
    return "let";
  case token_kind::const_kw:
    return "const";
  case token_kind::if_kw:
    return "if";
  case token_kind::else_kw:
    return "else";
  case token_kind::elseif_kw:
    return "elseif";
  case token_kind::then_kw:
    return "then";
  case token_kind::return_kw:
    return "return";
  case token_kind::println_kw:
    return "println";
  case token_kind::while_kw:
    return "while";
  case token_kind::for_kw:
    return "for";
  case token_kind::in_kw:
    return "in";
  case token_kind::break_kw:
    return "break";
  case token_kind::continue_kw:
    return "continue";
  case token_kind::loop_kw:
    return "loop";
  case token_kind::mut_kw:
    return "mut";
  case token_kind::import_kw:
    return "import";
  case token_kind::package_kw:
    return "package";
  case token_kind::module_kw:
    return "module";
  case token_kind::namespace_kw:
    return "namespace";
  case token_kind::using_kw:
    return "using";
  case token_kind::pub_kw:
    return "pub";
  case token_kind::true_kw:
    return "true";
  case token_kind::false_kw:
    return "false";
  case token_kind::struct_kw:
    return "struct";
  case token_kind::enum_kw:
    return "enum";
  case token_kind::double_colon:
    return "::";
  case token_kind::dot:
    return ".";
  case token_kind::double_dot:
    return "..";
  case token_kind::annotation:
    return "annotation";
  case token_kind::arrow:
    return "->";
  case token_kind::type_kw:
    return "type";
  case token_kind::as_kw:
    return "as";
  case token_kind::end_kw:
    return "end";
  case token_kind::match_kw:
    return "match";
  case token_kind::when_kw:
    return "when";
  case token_kind::eof:
    return "eof";
  case token_kind::invalid:
    return "invalid";
  default:
    return "unknown";
  }
}

// Runtime-controlled parse trace flag. Check env NY_PARSE_TRACE once and
// cache the result. This avoids recompiling to enable/disable tracing and
// allows developers to run the instrumented parser dynamically.
static inline bool parse_trace_enabled() {
  static int cached = -1;
  if (cached != -1)
    return cached == 1;
  const char *env = std::getenv("NY_PARSE_TRACE");
  if (env && env[0] != '\0')
    cached = 1;
  else
    cached = 0;
  return cached == 1;
}

// Helper: certain tokens are keywords but may historically be used as module
// or identifier names in stdlib sources (e.g., `println`, `test`, `assert`).
// In declaration/import contexts we accept these keyword tokens as names by
// treating their lexeme as an identifier. This preserves backward
// compatibility with older stdlib layouts.
static inline bool token_can_be_name(token_kind k) {
  switch (k) {
  case token_kind::println_kw:
  case token_kind::test_kw:
  case token_kind::example_kw:
  case token_kind::property_kw:
  case token_kind::assert_kw:
  case token_kind::hotreload_kw:
    return true;
  default:
    return false;
  }
}

// A non-mutating cursor used to distinguish generic call arguments from the
// less-than operator. `>>` is exposed as two logical `>` tokens so nested type
// arguments follow the same rules as parse_type_name(), which splits that
// lexer token while consuming it.
class type_argument_lookahead_cursor {
public:
  type_argument_lookahead_cursor(const std::vector<token> &tokens,
                                 size_t position)
      : tokens_(tokens), position_(position), limit_(tokens.size()) {
    constexpr size_t maximum_lookahead = 100;
    if (position_ < tokens_.size() &&
        tokens_.size() - position_ > maximum_lookahead) {
      limit_ = position_ + maximum_lookahead;
    }
  }

  token_kind kind() const {
    if (pending_greater_ != 0)
      return token_kind::greater;
    if (position_ >= limit_)
      return token_kind::eof;
    if (tokens_[position_].kind == token_kind::greater_greater)
      return token_kind::greater;
    return tokens_[position_].kind;
  }

  bool consume(token_kind expected) {
    if (kind() != expected)
      return false;

    if (pending_greater_ != 0) {
      --pending_greater_;
      return true;
    }

    if (tokens_[position_].kind == token_kind::greater_greater) {
      ++position_;
      pending_greater_ = 1;
    } else {
      ++position_;
    }
    return true;
  }

  bool has_pending_greater() const { return pending_greater_ != 0; }
  size_t position() const { return position_; }

private:
  const std::vector<token> &tokens_;
  size_t position_;
  size_t limit_;
  unsigned pending_greater_ = 0;
};

static bool scan_type_name(type_argument_lookahead_cursor &cursor,
                           unsigned depth);

static bool scan_type_argument(type_argument_lookahead_cursor &cursor,
                               unsigned depth, bool allow_number) {
  if (allow_number && cursor.consume(token_kind::number_literal))
    return true;
  return scan_type_name(cursor, depth);
}

// Mirror the type forms accepted by parse_type_name() without changing parser
// state or producing diagnostics. This intentionally rejects adjacent
// expression/statement tokens instead of searching forward for any later `>`.
static bool scan_type_name(type_argument_lookahead_cursor &cursor,
                           unsigned depth) {
  constexpr unsigned maximum_type_depth = 32;
  if (depth > maximum_type_depth)
    return false;

  if (cursor.consume(token_kind::star))
    return scan_type_name(cursor, depth + 1);

  if (cursor.consume(token_kind::fn_kw)) {
    if (!cursor.consume(token_kind::left_paren))
      return false;
    if (cursor.kind() != token_kind::right_paren) {
      do {
        if (!scan_type_name(cursor, depth + 1))
          return false;
      } while (cursor.consume(token_kind::comma));
    }
    if (!cursor.consume(token_kind::right_paren))
      return false;
    if (cursor.consume(token_kind::arrow))
      return scan_type_name(cursor, depth + 1);
    return true;
  }

  if (cursor.consume(token_kind::left_paren)) {
    if (!cursor.consume(token_kind::right_paren))
      return false;
  } else if (!cursor.consume(token_kind::ref_kw) &&
             !cursor.consume(token_kind::identifier)) {
    return false;
  }

  while (cursor.kind() == token_kind::double_colon ||
         cursor.kind() == token_kind::dot) {
    const token_kind separator = cursor.kind();
    if (!cursor.consume(separator))
      return false;
    const token_kind segment = cursor.kind();
    if (segment != token_kind::identifier && !token_can_be_name(segment))
      return false;
    if (!cursor.consume(segment))
      return false;
  }

  if (cursor.consume(token_kind::less)) {
    if (!scan_type_argument(cursor, depth + 1, true))
      return false;
    while (cursor.consume(token_kind::comma)) {
      if (!scan_type_argument(cursor, depth + 1, true))
        return false;
    }
    if (!cursor.consume(token_kind::greater))
      return false;
  }

  return true;
}

static bool looks_like_generic_call_arguments(const std::vector<token> &tokens,
                                               size_t position) {
  type_argument_lookahead_cursor cursor(tokens, position);
  if (!cursor.consume(token_kind::less) ||
      !scan_type_argument(cursor, 0, false)) {
    return false;
  }
  while (cursor.consume(token_kind::comma)) {
    if (!scan_type_argument(cursor, 0, false))
      return false;
  }
  if (!cursor.consume(token_kind::greater) || cursor.has_pending_greater() ||
      cursor.position() >= tokens.size()) {
    return false;
  }

  const token_kind next = tokens[cursor.position()].kind;
  return next == token_kind::left_paren ||
         next == token_kind::left_brace ||
         next == token_kind::double_colon || next == token_kind::dot;
}

// ----------------------------
// Parser: constructor + public methods
// ----------------------------
parser::parser(const std::string &source, const parser_options &opts)
    : tokens_(), pos_(0), options_(opts), diagnostics_(), source_(source),
      filename_(opts.filename), preproc_options_(opts.preproc_options),
      enable_preprocessing_(opts.enable_preprocessor) {

  // If requested, run the textual preprocessor prior to tokenization. This
  // allows `@include` / `@pragma once` (and other preprocessor directives)
  // to alter the source text before we lex and parse it.
  if (enable_preprocessing_) {
    preprocessor preproc(preproc_options_);
    auto pres_res = preproc.preprocess_text(source_, filename_);

    // Map preprocessor diagnostics into the parser diagnostics set so they are
    // exposed to callers consistently.
    for (const auto &pd : pres_res.diagnostics) {
      diagnostic d;
      d.line = pd.line;
      d.message = pd.message;
      if (pd.hint.has_value())
        d.hint = pd.hint;
      diagnostics_.push_back(d);
    }

    // Use the preprocessed output as the lexer's input
    source_ = pres_res.output;
  }

  // Lex the input immediately and store the tokens.
  lexer lex(source_);
  tokens_ = lex.scan_tokens();
  // Validate every numeric token, including patterns and unchecked builds.
  // This keeps malformed/range errors visible to --analyze before lowering.
  for (const auto &number : tokens_) {
    if (number.kind == token_kind::number_literal &&
        is_floating_literal_spelling(number.lexeme)) {
      double value = 0.0;
      if (const auto *message = parse_floating_literal(number.lexeme, value))
        add_error(number, message);
    }
  }

  // If options request token recording, nothing else special for now.
  if (options_.record_tokens) {
    // token recording already satisfied by keeping tokens_ member.
  }

  // Register built-in types into the parser's symbol table so a basic
  // collection of primitives is available during parsing and early name
  // resolution.
  {
    auto reg_type = [this](const std::string &name,
                           const std::optional<std::string> &alias =
                               std::nullopt) {
      // If this registered type is an alias, use the convenience helper that
      // records both the alias target and the current parser module context.
      if (alias.has_value()) {
        symbol_table_.insert_type_alias_in_module(name, *alias, false,
                                                  parser_current_module);
      } else {
        // Register a primitive type and provide the optional current module
        // context so tooling can reason about declared ownership.
        symbol_table_.insert_symbol_with_context(name, symbol_kind::type_symbol,
                                                 nullptr, std::nullopt, false,
                                                 false, parser_current_module);
      }
    };

    // Integer/unsigned
    reg_type("i8");
    reg_type("i16");
    reg_type("i32");
    reg_type("i64");
    reg_type("u8");
    reg_type("u16");
    reg_type("u32");
    reg_type("u64");

    // Floating point
    reg_type("f32");
    reg_type("f64");

    // Primitive types
    reg_type("bool");
    reg_type("char");
    reg_type("any");
    reg_type("String");

    // Alias: `text` -> String
    reg_type("text", std::optional<std::string>("String"));

    // Missing primitives/keywords used as types
    reg_type("ref");
    reg_type("ptr");
    reg_type("Self");
    reg_type("option"); // Hack to allow option usage if unresolved (but really
                        // should be imported)
  }
}

std::shared_ptr<ast_program> parser::parse_program() {
  auto program = std::make_shared<ast_program>();
  program->filename = options_.filename; // Set filename from options
  // Reset parser module context for a new translation unit parse.
  parser_current_module = std::nullopt;
  while (!is_at_end()) {
    auto decl = parse_declaration();
    if (decl) {
      program->declarations.push_back(decl);
    } else {
      // Try to recover and continue parsing.
      synchronize();
    }
  }
  return program;
}

// Helper to parse generic parameters: <T, U>
std::vector<ast_generic_param> parser::parse_generic_params() {
  std::vector<ast_generic_param> params;
  if (match(token_kind::less)) {
    do {
      token name_tok =
          consume(token_kind::identifier, "Expected generic parameter name");
      std::vector<std::string> bounds;

      // Parse bounds: T: Trait1 + Trait2
      if (match(token_kind::colon)) {
        do {
          bounds.push_back(parse_type_name());
        } while (match(token_kind::plus));
      }

      params.push_back({name_tok.lexeme, bounds});
    } while (match(token_kind::comma));
    consume(token_kind::greater, "Expected '>' after generic parameters");
  }
  return params;
}

// Helper to parse where clauses: where T: Trait, U: Other
std::unordered_map<std::string, std::vector<std::string>>
parser::parse_where_clause() {
  std::unordered_map<std::string, std::vector<std::string>> constraints;
  if (match(token_kind::where_kw)) {
    do {
      token name_tok =
          consume(token_kind::identifier,
                  "Expected generic parameter name in where clause");
      std::string param_name = name_tok.lexeme;

      consume(token_kind::colon,
              "Expected ':' after generic parameter in where clause");

      do {
        std::string bound = parse_type_name();
        constraints[param_name].push_back(bound);
      } while (match(token_kind::plus));

    } while (match(token_kind::comma));
  }
  return constraints;
}

void parser::validate_where_parameters(
    const std::vector<ast_generic_param> &parameters,
    const std::unordered_map<std::string, std::vector<std::string>> &constraints) {
  for (const auto &constraint : constraints)
    if (std::none_of(parameters.begin(), parameters.end(),
                     [&](const ast_generic_param &parameter) {
                       return parameter.name == constraint.first;
                     }))
      add_error(previous(), "Unknown generic parameter '" + constraint.first +
                                "' in where clause");
}

// Helper to parse a type name (including generics): Box<T>, std::vector<i32>
std::string parser::parse_type_name() {
  std::string name;

  // Handle raw pointer type: *T
  if (match(token_kind::star)) {
    // Raw pointer type: *T
    std::string base_type = parse_type_name();
    name = "*" + base_type;
    return name; // Return early for pointer types
  }

  // Handle ref keyword as a type prefix
  if (match(token_kind::ref_kw)) {
    name = "ref";
  } else if (match(token_kind::fn_kw)) {
    // Function type: fn(T1, T2) -> R
    name = "fn";
    consume(token_kind::left_paren, "Expected '(' after 'fn' in type");
    name += "(";
    bool first = true;
    while (!check(token_kind::right_paren) && !is_at_end()) {
      // std::cerr << "Parsing function type parameter...\n";
      if (!first)
        name += ", ";
      first = false;
      name += parse_type_name();
      if (!match(token_kind::comma))
        break;
    }
    consume(token_kind::right_paren, "Expected ')' in function type");
    name += ")";

    if (match(token_kind::arrow)) {
      name += " -> ";
      name += parse_type_name();
    }
    return name;
  } else if (match(token_kind::left_paren)) {
    // Unit declaration "()" -> "void"
    consume(token_kind::right_paren, "Expected ')' after '(' for unit type");
    name = "void";
  } else {
    token start = consume(token_kind::identifier, "Expected type name");
    name = start.lexeme;
  }

  // Handle path segments: ::Seg or .Seg (dot-qualified types like
  // nyhttp.server.Config). Dots are normalised to `::` as they are consumed:
  // `module a.b` stores its types as `a::b::T`, imports are keyed the same
  // way, and NIL identity uses `::`. A type name that kept its dots
  // (`tour.mixed::Measurement`) resolved to a distinct, member-less type,
  // which is why a dotted annotation never agreed with the declared one.
  while (check(token_kind::double_colon) || check(token_kind::dot)) {
    bool is_dot = check(token_kind::dot);
    // Peek ahead: for '.', only consume if followed by an identifier (not '..')
    if (is_dot && pos_ + 1 < tokens_.size() &&
        tokens_[pos_ + 1].kind != token_kind::identifier &&
        !token_can_be_name(tokens_[pos_ + 1].kind)) {
      break;
    }
    advance(); // consume '::' or '.'
    if (!check(token_kind::identifier) && !token_can_be_name(peek().kind)) break;
    token seg = advance();
    name += "::" + seg.lexeme;
  }

  // Handle generics: <T, U> or <T, 64> (numeric const)
  // Only parse generics if we verify via lookahead that the <...> form is valid
  // and not a comparison operator followed by expression tokens.
  if (check(token_kind::less)) {
    // Lookahead to verify this is actually a generic, not a comparison
    bool is_generic = false;
    int depth = 0;
    size_t lookahead = 0;
    while (pos_ + lookahead < tokens_.size() && lookahead < 100) {
      token t = tokens_[pos_ + lookahead];
      if (t.kind == token_kind::less) {
        depth++;
      } else if (t.kind == token_kind::greater) {
        depth--;
        if (depth == 0) {
          // Found closing '>' - this looks like generics
          is_generic = true;
          break;
        }
      } else if (t.kind == token_kind::greater_greater) {
        // Handle '>>' as two separate '>' closers for nested generics
        // e.g. vec<tuple<text, text>> lexes the final >> as one token
        depth -= 2;
        if (depth <= 0) {
          is_generic = true;
          break;
        }
      } else if (t.kind == token_kind::semicolon ||
                 t.kind == token_kind::then_kw || t.kind == token_kind::eof ||
                 // Arithmetic operators indicate this is an expression, not a
                 // type
                 t.kind == token_kind::plus || t.kind == token_kind::minus ||
                 t.kind == token_kind::star || t.kind == token_kind::slash ||
                 t.kind == token_kind::percent || t.kind == token_kind::mod_kw ||
                 // Comparison/logical operators also indicate expression
                 // context
                 t.kind == token_kind::less_equal ||
                 t.kind == token_kind::greater_equal ||
                 t.kind == token_kind::double_equal ||
                 t.kind == token_kind::not_equal ||
                 t.kind == token_kind::amp_amp ||
                 t.kind == token_kind::pipe_pipe ||
                 // Keywords that end expressions
                 t.kind == token_kind::end_kw ||
                 t.kind == token_kind::else_kw ||
                 t.kind == token_kind::elseif_kw) {
        break; // Not a generic type
      }
      lookahead++;
    }

    if (is_generic && match(token_kind::less)) {
      name += "<";
      while (true) {
        // Check if the next token is a number literal (for array<T, N> syntax)
        if (check(token_kind::number_literal)) {
          name += advance().lexeme;
        } else {
          name += parse_type_name();
        }
        if (match(token_kind::comma)) {
          name += ", ";
        } else {
          break;
        }
      }
      // Handle closing '>': either a single '>' or the first half of '>>'
      // When nested generics close (e.g. vec<tuple<text, text>>), the lexer
      // tokenizes the final >> as a single greater_greater token. We need to
      // split it: consume one '>' and leave the other for the outer generic.
      if (match(token_kind::greater)) {
        name += ">";
      } else if (check(token_kind::greater_greater)) {
        // Split '>>' into two '>' tokens: replace the '>>' with a single '>'
        // in-place so the outer parse_type_name() call can consume it.
        token gg = tokens_[pos_];
        tokens_[pos_] = token{token_kind::greater, ">", gg.line};
        name += ">";
      } else {
        consume(token_kind::greater, "Expected '>' after generic arguments");
        name += ">";
      }
    }
  }

  return canonical_std_namespace(std::move(name));
}

// ----------------------------
// Parsing: top-level declarations
// ----------------------------
visibility_kind parser::parse_visibility_modifier() {
  // If `pub` isn't present, return the default private visibility without
  // consuming any tokens.
  if (!match(token_kind::pub_kw))
    return visibility_kind::visibility_private;

  // We matched `pub`; check for optional parenthesized scope
  // forms such as: `pub(crate)` or `pub(super)`.
  if (match(token_kind::left_paren)) {
    // Expect an identifier inside the parentheses
    if (match(token_kind::identifier)) {
      std::string scope = previous().lexeme;

      if (match(token_kind::right_paren)) {
        if (scope == "crate") {
          return visibility_kind::visibility_crate;
        }
        if (scope == "super") {
          return visibility_kind::visibility_super;
        }
        // Unknown scope, emit a diagnostic and fall back to `pub`.
        add_error(previous(), "Unknown pub scope '" + scope +
                                  "'. Supported scopes are 'crate' and "
                                  "'super'; treating as `pub`.");
        return visibility_kind::visibility_public;
      } else {
        // Missing closing paren - error recover by skipping to the right paren.
        add_error(peek(), "Expected ')' after 'pub(" + scope + ")' visibility");
        while (!is_at_end() && !match(token_kind::right_paren))
          advance();
        return visibility_kind::visibility_public;
      }
    } else {
      // No identifier in parentheses - error (recover).
      add_error(peek(),
                "Expected identifier inside pub(...) visibility clause");
      while (!is_at_end() && !match(token_kind::right_paren))
        advance();
      return visibility_kind::visibility_public;
    }
  }

  // Simple `pub` without parentheses -> public visibility
  return visibility_kind::visibility_public;
}

std::vector<std::string> parser::parse_leading_annotations() {
  // Collect any leading annotations present before this declaration.
  // Examples: @inline, @test, @deprecated, custom annotations.
  // We accept multiple annotations in a row. Annotations are currently
  // collected for diagnostics and future stages (semantic analysis/codegen).
  std::vector<std::string> collected_annotations;
  while (match(token_kind::annotation)) {
    // Store full lexeme that includes the annotation text (e.g., "@test")
    std::string ann_str = previous().lexeme;

    // @pragma directives consume a bare word argument (e.g. `@pragma once`)
    if (ann_str == "@pragma" && check(token_kind::identifier)) {
      ann_str += " " + advance().lexeme;
      collected_annotations.push_back(ann_str);
      continue;
    }
    // Also consume any optional parenthesized arguments: @interop("ny_mimic")
    if (match(token_kind::left_paren)) {
      ann_str += "(";
      int depth = 1;
      while (!is_at_end() && depth > 0) {
        if (check(token_kind::left_paren)) {
          depth++;
        } else if (check(token_kind::right_paren)) {
          depth--;
          if (depth == 0) {
            advance(); // consume the final ')'
            ann_str += ")";
            break;
          }
        }
        // Append the current token to the annotation string
        if (peek().kind == token_kind::string_literal) {
          ann_str += "\"" + peek().lexeme + "\"";
        } else {
          ann_str += peek().lexeme;
        }
        advance();
      }
    }
    collected_annotations.push_back(ann_str);
  }
  return collected_annotations;
}

std::shared_ptr<ast_decl> parser::parse_declaration() {
  const auto collected_annotations = parse_leading_annotations();

  // Handle `end module` or stray `end` tokens which may appear in some
  // stdlib sources as an explicit module terminator. Historically some
  // stdlib files include a trailing `end module` while others omit it.
  // Accept and silently consume `end module [name]` here to be forgiving
  // of such variations and avoid spuriously reporting parse errors when the
  // terminator appears at top-level.
  if (match(token_kind::end_kw)) {
    if (match(token_kind::module_kw)) {
      // Optional module name following `end module` (e.g., `end module assert`)
      if (check(token_kind::identifier) || check(token_kind::assert_kw)) {
        advance();
      }
      // Optional semicolon
      match(token_kind::semicolon);
      return nullptr; // nothing to record for this top-level terminator
    } else {
      // If it's an `end` not followed by `module`, treat as unexpected but
      // recover silently by returning nullptr so parse_program can continue.
      return nullptr;
    }
  }

  // stream and maps to the enum values.
  visibility_kind visibility = parse_visibility_modifier();

  if (match(token_kind::function_kw)) {
    return parse_function_declaration(visibility, false, collected_annotations);
  }

  // Class declaration
  if (match(token_kind::class_kw)) {
    auto decl = parse_class_declaration(visibility, false);
    if (decl && !collected_annotations.empty()) {
      for (const auto &a : collected_annotations)
        decl->add_annotation(a);
    }
    return decl;
  }

  // Abstract class declaration
  if (match(token_kind::abstract_kw)) {
    if (match(token_kind::class_kw)) {
      auto decl = parse_class_declaration(visibility, true);
      if (decl && !collected_annotations.empty()) {
        for (const auto &a : collected_annotations)
          decl->add_annotation(a);
      }
      return decl;
    }
    // TODO: abstract function outside class?
    add_error(previous(), "Expected 'class' after 'abstract'");
    return nullptr;
  }

  // Struct declaration
  if (match(token_kind::struct_kw)) {
    auto decl = parse_struct_declaration(visibility);
    if (decl && !collected_annotations.empty()) {
      for (const auto &a : collected_annotations)
        decl->add_annotation(a);
    }
    return decl;
  }

  // Enum declaration
  if (match(token_kind::enum_kw)) {
    auto decl = parse_enum_declaration(visibility);
    if (decl && !collected_annotations.empty()) {
      for (const auto &a : collected_annotations)
        decl->add_annotation(a);
    }
    return decl;
  }

  // Trait declaration
  if (match(token_kind::trait_kw)) {
    auto decl = parse_trait_declaration(visibility);
    if (decl && !collected_annotations.empty()) {
      for (const auto &a : collected_annotations)
        decl->add_annotation(a);
    }
    return decl;
  }

  // Impl declaration
  if (match(token_kind::impl_kw)) {
    // Impl blocks don't usually have visibility modifiers themselves (methods
    // do), but we pass it through if present (though typically it's
    // private/default).
    auto decl = parse_impl_declaration();
    if (decl && !collected_annotations.empty()) {
      for (const auto &a : collected_annotations)
        decl->add_annotation(a);
    }
    return decl;
  }

  // Module declaration
  if (match(token_kind::module_kw) || match(token_kind::package_kw)) {
    auto decl = parse_module_declaration(visibility);
    if (decl && !collected_annotations.empty()) {
      for (const auto &a : collected_annotations)
        decl->add_annotation(a);
    }
    return decl;
  }

  if (match(token_kind::let_kw)) {
    // For MVP, basic variable declaration parsing:
    auto decl = parse_variable_declaration(visibility);
    if (decl && !collected_annotations.empty()) {
      for (const auto &a : collected_annotations)
        decl->add_annotation(a);
    }
    return decl;
  }

  // NY-050: module-level constants — parse exactly like a let declaration
  // but set is_const=true so codegen emits STORE_GLOBAL instead of local.
  if (match(token_kind::const_kw)) {
    auto decl = parse_variable_declaration(visibility);
    if (decl) {
      auto var_decl = std::dynamic_pointer_cast<ast_variable_decl>(decl);
      if (var_decl)
        var_decl->is_const = true;
      if (!collected_annotations.empty()) {
        for (const auto &a : collected_annotations)
          decl->add_annotation(a);
      }
    }
    return decl;
  }

  // Type alias declarations
  // Support `type alias = existing_type;` and `pub type alias =
  // existing_type;`. We support general `pub` prefix handling earlier; simply
  // forward `is_pub` into the type alias parser.
  if (match(token_kind::type_kw)) {
    auto decl = parse_type_declaration(visibility);
    if (decl && !collected_annotations.empty()) {
      for (const auto &a : collected_annotations)
        decl->add_annotation(a);
    }
    return decl;
  }

  // `type` alias - handled above; keep a fallback in case `is_pub` was false.
  // This block is kept for a possible 'type' not prefixed by `pub` and should
  // flow through the `is_pub` logic.
  // NOTE: the specific `match(type_kw)` handling is earlier now.

  // Import declaration handling (MVP subset)
  if (match(token_kind::import_kw)) {
    auto decl = parse_import_declaration(visibility);
    if (decl && !collected_annotations.empty()) {
      for (const auto &a : collected_annotations)
        decl->add_annotation(a);
    }
    return decl;
  }

  // Top-level Ny test blocks.
  if (match(token_kind::test_kw) || match(token_kind::example_kw) ||
      match(token_kind::property_kw)) {
    auto decl = parse_test_block();
    if (decl && !collected_annotations.empty()) {
      for (const auto &a : collected_annotations)
        decl->add_annotation(a);
    }
    return decl;
  }

  // Using directive: `using namespace <module.path>;` - treated as a
  // glob-import (`import <module.path>::*`). We additionally accept the
  // shorthand `using <module.path>;` for convenience.
  if (match(token_kind::using_kw)) {
    token start_tok = previous();

    // Expect optional `namespace` token. Accept either `using namespace X`
    // or `using X`.
    if (match(token_kind::namespace_kw)) {
      // consumed namespace token - proceed
    } else {
      // Not `namespace` -> allow shorthand `using <module>` provided an
      // identifier or `::` follows; otherwise, emit error.
      if (!check(token_kind::identifier) && !check(token_kind::double_colon)) {
        add_error(peek(),
                  "Expected 'namespace' or module identifier after 'using'");
        return nullptr;
      }
    }

    // Parse module path segments (same as import parsing logic)
    std::vector<std::string> segments;
    bool is_absolute = false;

    if (match(token_kind::double_colon)) {
      is_absolute = true;
      if (check(token_kind::left_brace)) {
        // `::{ ... }` - nothing to add to segments; brace form will be handled
      } else if (check(token_kind::identifier) ||
                 check(token_kind::println_kw)) {
        segments.push_back(advance().lexeme);
      } else {
        add_error(peek(), "Expected module name after '::' in using path");
      }
    } else if (check(token_kind::dot)) {
      // Relative import using '.' or '..' prefix
      advance();
      if (check(token_kind::dot)) {
        // `..`
        advance();
        segments.push_back("..");
      } else {
        // `.`
        segments.push_back(".");
      }
    } else if (check(token_kind::identifier) || check(token_kind::println_kw)) {
      segments.push_back(advance().lexeme);
    } else {
      add_error(peek(), "Expected module name after 'using'");
    }

    // Collect module path segments joined by `::`
    while (check(token_kind::double_colon) || check(token_kind::dot)) {
      // If the sequence is ':: {', consume the '::' and break so we handle the
      // braced list below.
      if ((pos_ + 1 < tokens_.size()) &&
          tokens_[pos_ + 1].kind == token_kind::left_brace) {
        advance(); // consume the '::'
        break;
      }
      // Check for ':: *' (glob import) - consume both '::' and '*' then break.
      if ((pos_ + 1 < tokens_.size()) &&
          tokens_[pos_ + 1].kind == token_kind::star) {
        advance(); // consume the '::'
        advance(); // consume the '*'
        break;
      }
      advance(); // consume '::'

      std::string seg_lexeme;
      if (check(token_kind::identifier) || check(token_kind::println_kw)) {
        seg_lexeme = advance().lexeme;
      } else {
        add_error(peek(), "Expected identifier after '::' in using path");
      }
      segments.push_back(seg_lexeme);
    }

    // Convert collected segments into canonical module path string
    std::string module_path;
    if (!segments.empty()) {
      // Relative imports: resolve '.' / '..' if present
      if (segments[0] == "." || segments[0] == "..") {
        if (!parser_current_module.has_value()) {
          add_error_at_line(start_tok.line,
                            "Relative using used outside of a module");
          // Fallback to join raw segments
          for (size_t i = 0; i < segments.size(); ++i) {
            if (i)
              module_path += "::";
            module_path += segments[i];
          }
        } else {
          std::string base = *parser_current_module;
          if (segments[0] == "..") {
            // Move to parent module - strip last '::' segment
            auto last_sep = base.rfind("::");
            if (last_sep == std::string::npos) {
              base.clear();
            } else {
              base = base.substr(0, last_sep);
            }
          }
          // Append remaining segments
          module_path = base;
          for (size_t i = 1; i < segments.size(); ++i) {
            if (!module_path.empty())
              module_path += "::";
            module_path += segments[i];
          }
        }
      } else {
        // Join normal segments
        for (size_t i = 0; i < segments.size(); ++i) {
          if (i)
            module_path += "::";
          module_path += segments[i];
        }
      }
    } else {
      module_path.clear();
    }

    // Optional semicolon
    match(token_kind::semicolon);

    // Construct AST import declaration to represent the using directive. We
    // reuse `ast_import_decl` with `is_glob=true` so `type_checker` will
    // interpret and record it like a glob import.
    module_path = canonical_std_namespace(std::move(module_path));
    auto using_decl = std::make_shared<ast_import_decl>(
        module_path, std::vector<std::string>{}, true, start_tok.line,
        visibility);

    // Register as a local symbol so subsequent name resolution can consult the
    // parser symbol table in single-file compilation contexts.
    if (!module_path.empty()) {
      symbol s(module_path, symbol_kind::type_symbol, using_decl.get(),
               std::nullopt, false, false, parser_current_module);
      symbol_table_.insert_symbol(s);
    }

    // Attach collected annotations
    if (!collected_annotations.empty()) {
      for (const auto &a : collected_annotations)
        using_decl->add_annotation(a);
    }

    return using_decl;
  }

  // Namespace alias declaration (alias for long module paths)
  if (match(token_kind::namespace_kw)) {
    auto decl = parse_namespace_alias_declaration(visibility);
    if (decl && !collected_annotations.empty()) {
      for (const auto &a : collected_annotations)
        decl->add_annotation(a);
    }
    return decl;
  }

  // Module declaration handling (simple)
  if (match(token_kind::module_kw)) {
    // Delegate to helper that parses module declarations and registers the
    // symbol; implemented below. Pass the resolved `visibility` reflecting
    // optional `pub` and related modifiers (pub, pub(crate), pub(super)).
    auto decl = parse_module_declaration(visibility);
    if (decl && !collected_annotations.empty()) {
      for (const auto &a : collected_annotations)
        decl->add_annotation(a);
    }
    return decl;
  }

  // Function declaration
  if (match(token_kind::fn_kw) || match(token_kind::function_kw)) {
    return parse_function_declaration(visibility, false, collected_annotations);
  }

  // Struct declaration (supports optional pub prefix via `is_pub`)
  if (match(token_kind::struct_kw)) {
    auto decl = parse_struct_declaration(visibility);
    if (decl && !collected_annotations.empty()) {
      for (const auto &a : collected_annotations)
        decl->add_annotation(a);
    }
    return decl;
  }

  // Enum declaration (supports optional pub prefix via `is_pub`)
  if (match(token_kind::enum_kw)) {
    auto decl = parse_enum_declaration(visibility);
    if (decl && !collected_annotations.empty()) {
      for (const auto &a : collected_annotations)
        decl->add_annotation(a);
    }
    return decl;
  }

  if (match(token_kind::trait_kw)) {
    auto decl = parse_trait_declaration(visibility);
    if (decl && !collected_annotations.empty()) {
      for (const auto &a : collected_annotations)
        decl->add_annotation(a);
    }
    return decl;
  }

  if (match(token_kind::impl_kw)) {
    auto decl = parse_impl_declaration();
    if (decl && !collected_annotations.empty()) {
      for (const auto &a : collected_annotations)
        decl->add_annotation(a);
    }
    return decl;
  }

  // If not a declaration: attempt to parse a top-level statement as an
  // expr_stmt Not all languages allow top-level statements; we keep this for
  // living tests.
  auto stmt = parse_statement();
  if (stmt) {
    // Wrap statement in a top-level declaration node if needed (use a function
    // decl wrapper) For now: create a synthetic top-level function 'main' if
    // none exist.
    auto fn = std::make_shared<ast_function_decl>("__anonymous_top_level");
    fn->body = std::static_pointer_cast<ast_stmt>(stmt);
    if (!collected_annotations.empty()) {
      for (const auto &a : collected_annotations)
        fn->add_annotation(a);
    }
    return fn;
  }

  // On failure, return nullptr so higher-level code can synchronize.
  // Use explicit peek const to avoid ambiguous overload in earlier build
  add_error(static_cast<const token &>(peek()),
            "Expected declaration (function, let)");
  return nullptr;
}

std::shared_ptr<ast_decl>
parser::parse_function_declaration(visibility_kind vis, bool is_abstract,
                                   const std::vector<std::string> &annotations) {
  // The caller MAY have consumed the 'function' / 'fn' keyword already or may
  // call this helper with the keyword still present. To be tolerant of both
  // calling conventions, accept an optional leading 'function' or 'fn' token
  // here and consume it if present. This keeps the helper tolerant of callers
  // that pre-consume the keyword.
  // Historically the function assumed the keyword was already consumed; this
  // change avoids repeated "Expected function name after 'function'" errors
  // when callers sometimes leave the token in the stream.
  if (check(token_kind::function_kw) || check(token_kind::fn_kw)) {
    advance(); // consume optional leading 'function' or 'fn'
  }
  // Callers collect annotations once, before visibility/method modifiers.
  // Passing that payload explicitly avoids rescanning unrelated prior tokens
  // or dropping annotations behind virtual/abstract/override modifiers.

  token name_tok;
  // Historically some stdlib modules used names that later became keywords
  // (e.g., `true` / `false` as function names inside the `assert` module).
  // Accept either a normal identifier or these specific keyword tokens as
  // function names to preserve backward compatibility with existing stdlib
  // sources. We record the lexeme from whichever token we consume.
  if (check(token_kind::identifier)) {
    name_tok = consume(token_kind::identifier,
                       "Expected function name after 'function'");
  } else if (check(token_kind::true_kw) || check(token_kind::false_kw)) {
    // Accept 'true' and 'false' as function names in declarations (legacy
    // stdlib). They are tokenized as keywords by the lexer, so consume them
    // here and proceed.
    name_tok = advance();
  } else {
    add_error(peek(), "Expected function name after 'function'");
    return nullptr;
  }
  std::string name = name_tok.lexeme;

  // Parse optional generic parameters
  std::vector<ast_generic_param> generic_params = parse_generic_params();

  // Parse parameter list (simple; no named/typed parameters in MVP)
  consume(token_kind::left_paren, "Expected '(' after function name");
  std::vector<ast_param> params;
  if (!check(token_kind::right_paren)) {
    // Parse parameters separated by commas (identifier and optional type)
    // Parse parameters separated by commas (identifier and optional type)
    while (true) {
      bool is_mut = false;
      bool is_variadic = false;
      if (match(token_kind::mut_kw)) {
        is_mut = true;
      }
      // Variadic rest parameter: ..name: type
      if (match(token_kind::double_dot)) {
        is_variadic = true;
      }

      token p;
      if (check(token_kind::self_kw)) {
        p = advance();
      } else if (check(token_kind::identifier) && peek().lexeme == "self") {
        p = advance();
      } else {
        p = consume(token_kind::identifier, "Expected parameter name");
      }
      ast_param param;
      param.name = p.lexeme;
      param.line = p.line;
      param.is_mut = is_mut;
      param.is_variadic = is_variadic;

      // Optional type annotation: support `param: type` (MVP)
      if (match(token_kind::colon)) {
        std::string type_str = parse_type_name();
        // Variadic params are automatically treated as array<T>
        if (is_variadic) {
          type_str = "array<" + symbol_table_.canonicalize_type_name(type_str) + ">";
        }
        // Use symbol_table canonicalization for type annotations (e.g., text
        // -> String). Even if the alias is a built-in, canonicalize to the
        // underlying type for later semantic analysis.
        param.type_name = std::optional<std::string>(
            symbol_table_.canonicalize_type_name(type_str));
      } else if (is_variadic) {
        // Untyped variadic defaults to array<any>
        param.type_name = std::string("array<any>");
      }

      // Optional default value: support `param = expression`
      if (match(token_kind::equal)) {
        param.default_value = parse_expression();
      }

      params.push_back(param);
      // Variadic must be last — stop parsing params after it
      if (is_variadic) {
        match(token_kind::comma); // consume optional trailing comma
        break;
      }
      if (!match(token_kind::comma))
        break;
    }
  }
  consume(token_kind::right_paren, "Expected ')' after parameters");

  // Parse optional return type
  std::optional<std::string> return_type;
  if (match(token_kind::arrow)) {
    std::string type_str = parse_type_name();
    return_type = symbol_table_.canonicalize_type_name(type_str);
  }

  // Parse optional where clause
  auto where_constraints = parse_where_clause();
  validate_where_parameters(generic_params, where_constraints);
  // Merge where constraints into generic params
  for (auto &gp : generic_params) {
    if (where_constraints.count(gp.name)) {
      auto &bounds = where_constraints[gp.name];
      gp.bounds.insert(gp.bounds.end(), bounds.begin(), bounds.end());
    }
  }

  // Create function AST early so we can register the symbol for the function
  // and then enter a new lexical scope for its parameters and body.
  bool is_pub_bool = (vis == visibility_kind::visibility_public);
  auto fn = std::make_shared<ast_function_decl>(name);
  fn->params = params;
  fn->return_type = return_type;
  fn->body = nullptr;
  fn->generic_params = generic_params;
  fn->is_pub = is_pub_bool;
  fn->line = name_tok.line;
  fn->source_file = options_.filename;
  fn->visibility = vis;

  for (const auto &annotation : annotations)
    fn->add_annotation(annotation);

  // Register function symbol into the current (enclosing) scope.
  // Register function symbol into the current (enclosing) scope and record
  // its visibility.
  // Register function symbol with the current module and visibility flag.
  // Use a context-aware helper so the symbol entry includes module/visibility.
  // Create symbol with enum-based visibility and insert into symbol table.
  symbol func_sym(name, symbol_kind::function_symbol, fn.get(), std::nullopt,
                  false, vis, parser_current_module);
  symbol_table_.insert_symbol_with_context(
      func_sym.name, func_sym.kind, func_sym.defining_node, func_sym.type_name,
      func_sym.is_mut, vis, func_sym.module);

  // Enter a new scope for function parameters and body so parameter names do
  // not leak into the enclosing scope.
  symbol_table_.enter_scope();

  // Register parameter symbols (variable symbols) in the function scope
  for (const auto &p : fn->params) {
    // param.type_name is an optional<string>; canonicalize and register the
    // underlying type name if present.
    std::optional<std::string> canonical_param_type = std::nullopt;
    if (p.type_name.has_value()) {
      canonical_param_type = std::optional<std::string>(
          symbol_table_.canonicalize_type_name(*p.type_name));
    }
    // Preserve and register mutability flags for parameter symbols by setting
    // the `is_mut` field on the symbol.
    // Preserve `is_mut` and record module context for parameter symbols.
    symbol ps(p.name, symbol_kind::variable_symbol, fn.get(),
              canonical_param_type, p.is_mut, false, parser_current_module);
    symbol_table_.insert_symbol(ps);
  }

  // Check for intrinsic/interop annotations to skip body if implicit
  bool is_intrinsic = false;
  for (const auto &ann : fn->annotations) {
    /* std::cout << "[DEBUG] Parser annotation: '" << ann.name << "'" <<
     * std::endl; */
    if (ann.name == "intrinsic" || ann.name == "interop" ||
        ann.name == "@intrinsic" || ann.name == "@interop") {
      is_intrinsic = true;
      // Don't break yet, might have ai_context too
    }
    if ((ann.name == "ai_context" || ann.name == "@ai_context") &&
        ann.args.has_value()) {
      // std::cout << "[DEBUG] Found ai_context annotation! Args: " << *ann.args
      // << std::endl; Flatten string literal quotes if present
      std::string json = *ann.args;
      if (json.size() >= 2 && json.front() == '"' && json.back() == '"') {
        json = json.substr(1, json.size() - 2);
      }
      // Unescape \" to " if needed? usage of string literal scanner handles
      // standard escapes usually. But if it was passed as raw string in args,
      // we just take it.
      fn->ai_context = json;
    }
  }

  // If intrinsic (or abstract) and no brace, assume no body (bodiless declaration)
  // But strictly check for optional "end function" terminator to handle mixed
  // stdlib styles AND check if it has a dummy body (statements) vs being
  // bodiless (next decl).
  if ((is_intrinsic || is_abstract) && !check(token_kind::left_brace)) {
    // Case 1: Explicit empty body
    if (check(token_kind::end_kw)) {
      advance(); // consume end
      if (check(token_kind::function_kw) || check(token_kind::fn_kw)) {
        advance(); // consume 'function' or 'fn'
      }
      symbol_table_.leave_scope();
      return fn;
    }

    // Case 2: Bodiless (next token is start of new declaration)
    // Check for keywords that definitively start a new top-level declaration
    // and cannot start a statement in a function body (or are unlikely for
    // intrinsic bodies).
    token_kind k = peek().kind;
    if (k == token_kind::pub_kw || k == token_kind::annotation ||
        k == token_kind::eof || k == token_kind::struct_kw ||
        k == token_kind::enum_kw || k == token_kind::impl_kw ||
        k == token_kind::class_kw || k == token_kind::module_kw ||
        k == token_kind::import_kw || k == token_kind::trait_kw ||
        k == token_kind::function_kw || k == token_kind::fn_kw) {
      symbol_table_.leave_scope();
      return fn;
    }

    // Case 3: Has body (e.g. return 0), fall through to standard parsing logic
    // below.
  }

  // Forward declaration: non-intrinsic/abstract function whose next token is an
  // unambiguous declaration starter — treat as a bodiless signature declaration.
  if (!check(token_kind::left_brace)) {
    token_kind k = peek().kind;
    if (k == token_kind::fn_kw || k == token_kind::function_kw ||
        k == token_kind::pub_kw || k == token_kind::eof ||
        k == token_kind::struct_kw || k == token_kind::enum_kw ||
        k == token_kind::impl_kw || k == token_kind::class_kw ||
        k == token_kind::module_kw || k == token_kind::import_kw ||
        k == token_kind::trait_kw || k == token_kind::annotation) {
      // Bodiless forward declaration
      symbol_table_.leave_scope();
      return fn;
    }
  }

  // Function body
  std::shared_ptr<ast_block_stmt> block = nullptr;

  if (check(token_kind::left_brace)) {
    // C-style function body: { ... }
    // Loop vars (params) not needed for standard function block as scope is
    // already entered
    block = std::dynamic_pointer_cast<ast_block_stmt>(parse_block_statement());
    if (!block) // verify cast success or create empty
      block = std::make_shared<ast_block_stmt>(name_tok.line);
  } else {
    // VB-style: parse statements until 'end function'
    block = std::make_shared<ast_block_stmt>(name_tok.line);
    // Parse statements until we hit 'end function'
    while (!is_at_end()) {
      // Check for 'end function' / 'end fn' terminator
      if (check(token_kind::end_kw)) {
        // Support both `end`, `end function`, and `end fn`
        advance(); // consume 'end'
        if (check(token_kind::function_kw) || check(token_kind::fn_kw)) {
          advance(); // consume 'function' or 'fn'
        }
        break;
      }
      // Handle annotations inside VB-style function bodies (e.g. @intrinsic("..."))
      // These are not statements — parse them and attach to the function node.
      if (check(token_kind::annotation)) {
        std::string ann_str = advance().lexeme; // consume '@name'
        if (match(token_kind::left_paren)) {
          ann_str += "(";
          int depth = 1;
          while (!is_at_end() && depth > 0) {
            if (check(token_kind::left_paren)) depth++;
            else if (check(token_kind::right_paren)) {
              depth--;
              if (depth == 0) { advance(); ann_str += ")"; break; }
            }
            ann_str += (peek().kind == token_kind::string_literal)
                         ? "\"" + peek().lexeme + "\""
                         : peek().lexeme;
            advance();
          }
        }
        fn->add_annotation(ann_str);
        // If this is an intrinsic/interop annotation, mark is_intrinsic so the
        // body is treated as bodiless after parsing completes.
        if (ann_str == "@intrinsic" || ann_str.rfind("@intrinsic(", 0) == 0 ||
            ann_str == "@interop"   || ann_str.rfind("@interop(",   0) == 0 ||
            ann_str == "intrinsic"  || ann_str == "interop") {
          is_intrinsic = true;
        }
        continue;
      }
      auto st = parse_statement();
      if (st)
        block->statements.push_back(st);
      else
        synchronize(); // recover from statement error
    }
  }

  fn->body = block;

  // Leave the function's scope now that we've parsed parameters and the body
  symbol_table_.leave_scope();

  // any `test`, `example`, or `property` block immediately following a function
  // is considered "attached" to that function.
  while (check(token_kind::test_kw) || check(token_kind::example_kw) ||
         check(token_kind::property_kw)) {
    advance(); // Consume the keyword so parse_test_block can access it via
               // previous()
    auto test_block = parse_test_block();
    if (test_block) {
      if (test_block->type == test_type::example) {
        fn->example_blocks.push_back(test_block);
      } else {
        fn->test_blocks.push_back(test_block);
      }
    }
  }

  return fn;
}

std::shared_ptr<ast_decl>
parser::parse_variable_declaration(visibility_kind visibility) {
  // 'let' consumed already
  // Support optional mut keyword: `let mut x = ...`
  bool is_mut = false;
  if (match(token_kind::mut_kw)) {
    is_mut = true;
  }

  // Support pointer declaration syntax: `let* x = ...`
  // In Ny, `let*` means mutable global variable — set is_mut = true.
  bool is_pointer = false;
  if (match(token_kind::star)) {
    is_pointer = true;
    is_mut = true;  // let* implies mutable
  }

  token name_tok;
  if (is_pointer) {
    // For let* x, the variable name comes after the *
    name_tok = consume(token_kind::identifier, "Expected variable name after '*'");
  } else {
    name_tok =
        consume(token_kind::identifier, "Expected variable name after 'let'");
  }
  std::optional<std::string> maybe_type;
  std::shared_ptr<ast_expr> initializer = nullptr;

  // Debug: log the start of variable parsing with relevant token info
  // {
  //   token start_peek = is_at_end() ? token{token_kind::eof, "", 0} : peek();
  //   std::cerr << "[parse_variable_declaration] start: name='" <<
  //   name_tok.lexeme
  //             << "' is_mut=" << (is_mut ? "true" : "false") << " pos=" <<
  //             pos_
  //             << " next_kind=" << token_kind_to_string(start_peek.kind)
  //             << " next_lex='" << start_peek.lexeme
  //             << "' line=" << start_peek.line << "\n";
  // }

  if (match(token_kind::colon)) {
    std::string type_str = parse_type_name();
    // let* x: T — the declared type is T as written.
    // `let*` means mutable global; it does NOT implicitly add pointer indirection.
    // Users who want a pointer write the explicit type: let* x: ptr<T>.
    // Translate known aliases to canonical type names
    if (type_str == "text") {
      maybe_type = std::string("String");
    } else {
      maybe_type = type_str;
    }
    if (maybe_type.has_value()) {
      // std::cerr << "[parse_variable_declaration] type annotation: '"
      //           << *maybe_type << "'\n";
    }
  } else if (is_pointer) {
    // For let* without explicit type, we'll infer the pointer type from initializer
    // Don't set type now, let it be inferred from initializer
    // maybe_type stays empty for inference
  }

  if (match(token_kind::equal)) {
    // Record token state for debugging prior to calling parse_expression()
    token eq_tok = previous();
    token next_tok = is_at_end() ? token{token_kind::eof, "", 0} : peek();
    // std::cerr << "[parse_variable_declaration] '=' found at line "
    //           << eq_tok.line
    //           << " next token: kind=" << token_kind_to_string(next_tok.kind)
    //           << " lexeme='" << next_tok.lexeme << "' line=" << next_tok.line
    //           << "\n";

    initializer = parse_expression();

    // Diagnose when parse_expression returns null (indicates 'Expected
    // expression' or similar)
    if (!initializer) {
      token after_tok = is_at_end() ? token{token_kind::eof, "", 0} : peek();
      // std::cerr << "[parse_variable_declaration] initializer parse failed; "
      //              "token after attempted parse: kind="
      //           << token_kind_to_string(after_tok.kind) << " lexeme='"
      //           << after_tok.lexeme << "' line=" << after_tok.line << "\n";

      if (ny::is_debug_enabled()) {
        std::cerr << "[parser] variable initializer token context: ";
        for (int i = 0; i < 4; ++i) {
          size_t idx = pos_ + i;
          if (idx < tokens_.size()) {
            std::cerr << token_kind_to_string(tokens_[idx].kind) << "('"
                      << tokens_[idx].lexeme << "') ";
          } else {
            std::cerr << "EOF ";
            break;
          }
        }
        std::cerr << "\n";
      }
    }
  }

  // Optional semicolon at end of declaration.
  match(token_kind::semicolon);

  // Construct variable declaration; AST accepts is_mut flag now.
  auto var_decl = std::make_shared<ast_variable_decl>(
      name_tok.lexeme, initializer, maybe_type, is_mut,
      visibility == visibility_kind::visibility_public, name_tok.line);
  var_decl->visibility = visibility;

  // Attach any immediate annotations that directly preceded the current token
  // (safety net for cases where annotations weren't already collected).
  std::vector<std::string> local_annotations;
  long back_scan = static_cast<long>(pos_) - 1;
  while (back_scan >= 0 && tokens_[back_scan].kind == token_kind::annotation) {
    local_annotations.push_back(tokens_[back_scan].lexeme);
    --back_scan;
  }
  // Insert with preserved order (from earlier -> later)
  for (int i = static_cast<int>(local_annotations.size()) - 1; i >= 0; --i) {
    var_decl->add_annotation(local_annotations[i]);
  }

  // (Optional) Insert into early symbol table for name tracking
  symbol s(name_tok.lexeme, symbol_kind::variable_symbol, var_decl.get(),
           maybe_type, is_mut, false, parser_current_module);
  // When creating symbols programmatically we now insert via the
  // `insert_symbol_with_context` helper to ensure module/visibility info is
  // consistently captured.
  // ignore duplicate check here for MVP - symbol_table_ will reject duplicates
  symbol_table_.insert_symbol_with_context(s.name, s.kind, s.defining_node,
                                           s.type_name, s.is_mut, s.is_pub,
                                           s.module);

  // Debug: log the constructed declaration status
  // std::cerr << "[parse_variable_declaration] returning var_decl name='"
  //           << name_tok.lexeme << "' type='"
  //           << (maybe_type ? *maybe_type : std::string("<none>"))
  //           << "' is_mut=" << (is_mut ? "true" : "false")
  //           << " initializer=" << (initializer ? "present" : "null")
  //           << " line=" << name_tok.line << "\n";

  return var_decl;
}

//
// Parse a type alias declaration.
// Syntax (MVP):
//    type AliasName = ExistingType;
//    pub type AliasName = ExistingType;
// We assume the `type` token has already been consumed by the caller.
std::shared_ptr<ast_decl> parser::parse_type_declaration(visibility_kind vis) {
  // Expect an identifier (alias name)
  token name_tok =
      consume(token_kind::identifier, "Expected type alias name after 'type'");
  std::string alias_name = name_tok.lexeme;

  // Parse optional generic parameters
  std::vector<ast_generic_param> generic_params = parse_generic_params();

  // Parse optional where clause
  auto where_constraints = parse_where_clause();
  validate_where_parameters(generic_params, where_constraints);
  // Merge where constraints into generic params
  for (auto &gp : generic_params) {
    if (where_constraints.count(gp.name)) {
      auto &bounds = where_constraints[gp.name];
      gp.bounds.insert(gp.bounds.end(), bounds.begin(), bounds.end());
    }
  }

  // Expect '=' after alias name
  token eq_tok =
      consume(token_kind::equal, "Expected '=' after type alias name");

  // Parse the target/aliased type as a sequence of identifiers with optional
  // '::' path segments.
  std::string target_type = parse_type_name();

  // Optional semicolon terminator
  match(token_kind::semicolon);

  // Create AST node for type alias
  bool is_pub_bool = (vis == visibility_kind::visibility_public);
  auto alias_decl =
      make_type_alias_decl(alias_name, target_type, std::move(generic_params),
                           is_pub_bool, name_tok.line);
  // Ensure the alias AST node reflects the full visibility enum
  alias_decl->visibility = vis;

  // Parsing must preserve a user alias's spelling. Expanding its target here
  // loses the declaring module (and can even reuse another module's same-name
  // alias). The checker owns alias expansion once all modules are available.
  // Keep identity-only symbols for grammar disambiguation; builtin aliases
  // already installed in the parser symbol table still canonicalize normally.
  symbol_table_.insert_symbol_with_context(
      alias_name, symbol_kind::type_symbol, alias_decl.get(), std::nullopt,
      false, vis, parser_current_module);
  if (parser_current_module)
    symbol_table_.insert_symbol_with_context(
        *parser_current_module + "::" + alias_name, symbol_kind::type_symbol,
        alias_decl.get(), std::nullopt, false, vis, parser_current_module);

  return alias_decl;
}

// Parse an import declaration (helper for top-level and in-block parsing)
// This mirrors the earlier logic for `import` handling but provides a named
// implementation so other call sites can reuse it.
std::shared_ptr<ast_decl>
parser::parse_namespace_alias_declaration(visibility_kind vis) {
  // 'namespace' token already consumed by caller (parse_declaration()).
  // This implementation handles only the alias form:
  //   namespace <alias> = <module.path>;
  // For other complex namespace forms we emit an error and recover.
  token start_tok = previous(); // 'namespace' token

  // Expect alias identifier
  token alias_tok =
      consume(token_kind::identifier, "Expected alias name after 'namespace'");
  // Expect '='
  consume(token_kind::equal, "Expected '=' after namespace alias name");

  // Parse the target module path using the existing `parse_type_name()` helper.
  std::string target_module = parse_type_name();

  // Optional semicolon terminator
  match(token_kind::semicolon);

  // Compose AST node
  bool is_pub_bool = (vis == visibility_kind::visibility_public);
  auto alias_decl = std::make_shared<ast_namespace_alias_decl>(
      alias_tok.lexeme, target_module, is_pub_bool, alias_tok.line);
  alias_decl->visibility = vis;

  // Register alias symbol into the parser symbol table as a type-like symbol.
  // Store `type_name` as the canonical module target so later stages can
  // resolve references like `alias::SomeType` by looking up the alias
  // and expanding it.
  symbol_table_.insert_symbol_with_context(
      alias_tok.lexeme, symbol_kind::type_symbol, alias_decl.get(),
      std::optional<std::string>(target_module), false, vis,
      parser_current_module);

  return alias_decl;
}

std::shared_ptr<ast_decl>
parser::parse_import_declaration(visibility_kind visibility) {
  // 'import' was consumed by caller; gather module path segments and optional
  // imported names (single or braced) or glob star.
  token start_tok = previous(); // 'import' token for diagnostics

  std::vector<std::string> segments;
  std::vector<std::string> names;
  bool is_glob = false;

  // Parse initial module/identifier segment: accept `::` (absolute start) or
  // a leading identifier. Accept keywords like `println` (they can appear as
  // names due to being tokenized as keywords).
  if (match(token_kind::double_colon)) {
    if (check(token_kind::identifier) || token_can_be_name(peek().kind)) {
      segments.push_back(advance().lexeme);
    } else {
      add_error(peek(), "Expected identifier after '::' in import path");
      return nullptr;
    }
  } else if (check(token_kind::identifier) || token_can_be_name(peek().kind)) {
    segments.push_back(advance().lexeme);
  } else {
    add_error(peek(), "Expected module name after 'import'");
    return nullptr;
  }

  // Parse the rest of the path pieces: `::Seg`, `.Seg`, `::name`, `::{name, name}`,
  // or `::*` (glob). Both `::` and `.` are accepted as path separators.
  while (check(token_kind::double_colon) || check(token_kind::dot)) {
    // Consume the separator (either '::' or '.')
    advance();
    // glob `::*`
    if (check(token_kind::star)) {
      advance();
      is_glob = true;
      break;
    }

    // Braced list: `::{a, b}`
    if (check(token_kind::left_brace)) {
      // consume '{'
      advance();
      // parse comma-separated identifiers (allowing keyword names like
      // `println` or other historically used names).
      if (!check(token_kind::right_brace)) {
        do {
          if (check(token_kind::identifier) || token_can_be_name(peek().kind)) {
            token name_tok = advance();
            names.push_back(name_tok.lexeme);
          } else {
            add_error(peek(), "Expected identifier in import list");
            break;
          }
        } while (match(token_kind::comma));
      }
      consume(token_kind::right_brace, "Expected '}' after import list");
    break;
    }

    // Regular identifier segment (adds to module path)
    // Accept keywords as name segments (e.g. 'test', 'lang', 'pub' used as module names)
    if (check(token_kind::identifier) || token_can_be_name(peek().kind)) {
      segments.push_back(advance().lexeme);
      continue;
    }

    // Unexpected token after separator
    add_error(peek(), "Expected identifier, '{', or '*' after separator in import");
    break;
  }

  // All path segments form the module path. Named imports require explicit
  // braces: `import std::io::{println}`. `import net::http` imports the full
  // `net::http` module, not a name `http` from module `net`.
  std::string module_path;

  // Build module path string
  for (size_t i = 0; i < segments.size(); ++i) {
    if (i)
      module_path += "::";
    module_path += segments[i];
  }

  // Optional semicolon
  match(token_kind::semicolon);

  // Create AST import decl
  module_path = canonical_std_namespace(std::move(module_path));
  auto imp_decl = std::make_shared<ast_import_decl>(module_path, names, is_glob,
                                                    start_tok.line);

  // Imported-name identity helps grammar disambiguation, but the module path
  // is not the imported type. Preserve its spelling for checker resolution.
  for (const auto &n : names) {
    symbol_table_.insert_symbol_with_context(
        n, symbol_kind::type_symbol, imp_decl.get(), std::nullopt, false,
        visibility_kind::visibility_private, parser_current_module);
  }

  // For glob imports, insert a single sentinel symbol for the imported module
  // which can be used by downstream phases to mark a glob import was present.
  if (is_glob) {
    symbol_table_.insert_symbol_with_context(
        std::string("import$") + module_path, symbol_kind::type_symbol,
        imp_decl.get(), std::optional<std::string>(module_path), false,
        visibility_kind::visibility_private, parser_current_module);
  }

  return imp_decl;
}

// `using` directives (like `using namespace std::io`) are handled at the
// top-level `parse_declaration()` so they are recorded as import-like AST
// nodes at the outer level. The import decl handler purposefully does NOT
// handle `using` to avoid duplicate handling and to respect the `visibility`
// / `collected_annotations` context at the top level.

// Parse a module declaration (helper for top-level and block parsing)
/* parse_namespace_alias_declaration moved to top-level (before
 * parse_import_declaration) */

// Parse a module declaration (helper for top-level and block parsing)
std::shared_ptr<ast_decl>
parser::parse_module_declaration(visibility_kind vis) {
  // At entry point the `module` keyword is expected to have been consumed.
  token start_tok = previous();
  token name_tok;
  // Allow module names that are either identifiers or certain keywords that are
  // used as module names in the stdlib (e.g., 'assert' which is tokenized as
  // a keyword). Historically the stdlib contained a module named 'assert'.
  // Accept identifier or the `assert` keyword as a module name to preserve
  // backward compatibility with existing stdlib sources.
  if (check(token_kind::identifier)) {
    name_tok = consume(token_kind::identifier, "Expected module name after 'module'");
  } else if (check(token_kind::assert_kw)) {
    // 'assert' is lexed as a keyword; accept it as a module name here.
    name_tok = advance();
  } else {
    add_error(peek(), "Expected module name after 'module'");
    return nullptr;
  }

  std::string module_name = name_tok.lexeme;
  // Parse optional path components: module foo::bar::baz  or  module foo.bar.baz
  // Accept both '::' and '.' as separators; accept keywords as name segments.
  while (check(token_kind::double_colon) || check(token_kind::dot)) {
    bool is_dot = check(token_kind::dot);
    advance(); // consume '::' or '.'
    if (!check(token_kind::identifier) && !token_can_be_name(peek().kind)) {
      add_error(peek(), "Expected identifier after separator in module path");
      break;
    }
    token seg = advance();
    module_name += "::" + seg.lexeme;
  }

  // Optional semicolon terminator
  match(token_kind::semicolon);

  module_name = canonical_std_namespace(std::move(module_name));
  bool is_pub = (vis == visibility_kind::visibility_public);
  auto module_decl =
      std::make_shared<ast_module_decl>(module_name, is_pub, start_tok.line);
  // Set the AST node visibility explicitly (new enum-based visibility).
  module_decl->visibility = vis;

  // Register module as a symbol so the rest of the parser can use it. Record
  // that the symbol itself is declared in its own module (module owner).
  // Record the module declaration symbol using the context-aware helper so the
  // symbol entry stores visibility and the module owner (module_name).
  symbol_table_.insert_symbol_with_context(
      module_name, symbol_kind::type_symbol, module_decl.get(), std::nullopt,
      false, vis, std::optional<std::string>(module_name));
  // Update the current parser module context for subsequent declarations
  parser_current_module = std::optional<std::string>(module_name);

  return module_decl;
}

// ----------------------------
// Statements
// ----------------------------
// Duplicate `parse_statement` header removed here; canonical function body
// remains below (single definitive implementation). Annotation collection
// and handling are performed by the single `parse_statement()` definition.

std::shared_ptr<ast_stmt> parser::parse_statement() {
  // Collect any leading statement-level annotations that apply to the
  // upcoming statement. This mirrors the top-level declaration annotation
  // capture. For now we store them locally and emit a diagnostic entry.
  std::vector<std::string> stmt_annotations;
  while (match(token_kind::annotation)) {
    // Store full lexeme that includes the annotation text (e.g., "@test")
    std::string ann_str = previous().lexeme;

    // Also consume any optional parenthesized arguments: @interop("ny_mimic")
    if (match(token_kind::left_paren)) {
      ann_str += "(";
      int depth = 1;
      while (!is_at_end() && depth > 0) {
        if (check(token_kind::left_paren)) {
          depth++;
        } else if (check(token_kind::right_paren)) {
          depth--;
          if (depth == 0) {
            advance(); // consume the final ')'
            ann_str += ")";
            break;
          }
        }
        // Append the current token to the annotation string
        if (peek().kind == token_kind::string_literal) {
          ann_str += "\"" + peek().lexeme + "\"";
        } else {
          ann_str += peek().lexeme;
        }
        advance();
      }
    }
    stmt_annotations.push_back(ann_str);
  }
  if (!stmt_annotations.empty() && ny::is_debug_enabled()) {
    // Log for debugging; later phases will pick up annotations from the AST
    // or symbol table when we add those fields.
    for (const auto &a : stmt_annotations) {
      std::cerr << "[parse_statement] found annotation: " << a << "\n";
    }
  }

  // Debug: print current token
  // Block statement: if a left brace is upcoming, use the block parser which
  // handles consumption of the brace and the body. Parser: initial position /
  // lookahead debugging output removed in production build.

  // We'll route to the specific statement parser but capture the result and log
  // a concise summary right before returning - this aids debugging and tests.
  std::shared_ptr<ast_stmt> result = nullptr;

  if (check(token_kind::left_brace)) {
    result = parse_block_statement();
  } else if (match(token_kind::println_kw)) {
    result = parse_println_statement();
  } else if (match(token_kind::return_kw)) {
    result = parse_return_statement();
  } else if (match(token_kind::if_kw)) {
    result = parse_if_statement();
  } else if (match(token_kind::for_kw)) {
    result = parse_for_statement();
  } else if (match(token_kind::while_kw)) {
    result = parse_while_statement();
  } else if (match(token_kind::loop_kw)) {
    result = parse_loop_statement();
  } else if (match(token_kind::break_kw)) {
    result = parse_break_statement();
  } else if (match(token_kind::continue_kw)) {
    result = parse_continue_statement();
  } else if (match(token_kind::attempt_kw)) {
    result = parse_attempt_statement();
  } else if (match(token_kind::raise_kw)) {
    result = parse_raise_statement();
  } else if (match(token_kind::deallocate_kw)) {
    consume(token_kind::left_paren, "Expected '(' after 'deallocate'");
    auto ptr = parse_expression();
    consume(token_kind::right_paren, "Expected ')' after deallocate argument");
    result = std::make_shared<ast_deallocate_stmt>(ptr, previous().line);
    // Declaration statement forms - allow declarations inside blocks
    // parsing as regular declarations and wrapping them in ast_decl_stmt.
  } else if (check(token_kind::identifier) && peek().lexeme == "asm") {
    const token asm_token = advance(); // consume 'asm'
    consume(token_kind::string_literal, "Expected string literal after 'asm'");
    std::string code = previous().lexeme;
    auto assembly = std::make_shared<ast_asm_stmt>(code, asm_token.line);
    if (match(token_kind::comma)) {
      assembly->target = code;
      consume(token_kind::string_literal, "Expected assembly template after target");
      assembly->assembly_code = previous().lexeme;
      consume(token_kind::left_paren, "Expected '(' before assembly inputs");
      if (!check(token_kind::right_paren)) {
        do {
          assembly->inputs.push_back(parse_expression());
        } while (match(token_kind::comma));
      }
      consume(token_kind::right_paren, "Expected ')' after assembly inputs");
      if (match(token_kind::arrow)) {
        consume(token_kind::identifier, "Expected local output variable after '->'");
        assembly->output = previous().lexeme;
      }
    }
    consume(token_kind::end_kw, "Expected 'end' after asm block");
    if (check(token_kind::identifier) && peek().lexeme == "asm") {
      advance();
    } else {
      add_error(peek(), "Expected 'asm' after 'end'");
    }
    result = assembly;
  } else if (match(token_kind::let_kw)) {
    // std::cerr << "[parse_statement] matched 'let' token at pos=" << pos_
    //           << " line=" << previous().line << "\n";
    // let <name> [: type] [= expr];
    // parse_variable_declaration consumes the let token; we receive the
    // returned ast_decl_ptr and wrap it
    auto decl = parse_variable_declaration();
    // Debug: inspect the declaration (if a variable decl) and emit info
    if (decl) {
      auto var_decl = std::dynamic_pointer_cast<ast_variable_decl>(decl);
      if (var_decl) {
        std::string tname = var_decl->type_name.has_value()
                                ? *var_decl->type_name
                                : std::string("<none>");
        // std::cerr << "[parse_statement] parsed variable declaration: name='"
        //           << var_decl->name << "' type='" << tname
        //           << "' is_mut=" << (var_decl->is_mut ? "true" : "false")
        //           << " line=" << var_decl->line << "\n";
      } else {
        ny::diag_debug(
            "[parser] declaration statement kind=" +
            std::to_string(static_cast<int>(decl->kind)));
      }
    } else {
      ny::diag_debug("[parser] variable declaration returned null");
    }
    result = std::make_shared<ast_decl_stmt>(decl, decl ? decl->line
                                                        : previous().line);
  } else if (match(token_kind::const_kw)) {
    // NY-050: const <name> [: type] = expr  — treated like let but immutable
    auto decl = parse_variable_declaration();
    if (decl) {
      auto var_decl = std::dynamic_pointer_cast<ast_variable_decl>(decl);
      if (var_decl)
        var_decl->is_const = true;
    }
    result = std::make_shared<ast_decl_stmt>(decl, decl ? decl->line
                                                        : previous().line);
  } else if (check(token_kind::pub_kw) && (pos_ + 1 < tokens_.size()) &&
             tokens_[pos_ + 1].kind == token_kind::type_kw) {
    // pub type ... (in-block); consume both 'pub' and 'type' and parse alias
    advance(); // consume 'pub'
    advance(); // consume 'type'
    auto decl = parse_type_declaration(visibility_kind::visibility_public);
    result = std::make_shared<ast_decl_stmt>(decl, decl ? decl->line
                                                        : previous().line);
  } else if (match(token_kind::type_kw)) {
    // type alias inside a block
    auto decl = parse_type_declaration(visibility_kind::visibility_private);
    result = std::make_shared<ast_decl_stmt>(decl, decl ? decl->line
                                                        : previous().line);
  } else if (check(token_kind::import_kw) || check(token_kind::module_kw)) {
    // Delegate to parse_declaration for import/module declarations. The token
    // will be consumed by parse_declaration internally.
    auto decl = parse_declaration();
    result = std::make_shared<ast_decl_stmt>(decl, decl ? decl->line
                                                        : previous().line);
  } else if (match(token_kind::match_kw)) {
    // Parse `match` as a statement, producing an `ast_match_stmt`.
    // The `match` token has been consumed by match() above.
    result = parse_match_statement();
  } else {
    // Otherwise, treat as an expression statement (may be a call or assignment)
    result = parse_expr_statement();
  }

  // Parser: statement-return debug logging removed for production.

  if (!stmt_annotations.empty() && result) {
    // Attach to the result node itself
    for (const auto &a : stmt_annotations) {
      result->add_annotation(a);
    }
    // If the result is a declaration wrapper, attach the annotations to the
    // underlying declaration as well for consistent propagation.
    if (result->kind == ast_node_kind::decl_stmt_node) {
      auto ds = std::dynamic_pointer_cast<ast_decl_stmt>(result);
      if (ds && ds->declaration) {
        for (const auto &a : stmt_annotations) {
          ds->declaration->add_annotation(a);
        }
      }
    }
  }

  return result;
}

std::shared_ptr<ast_stmt>
parser::parse_block_statement(const std::vector<ast_param> &predeclared) {
  // We expect '{' at current token. If not present, add an error and attempt to
  // continue.
  if (!match(token_kind::left_brace)) {
    add_error(tokens_[pos_], "Expected '{' to start block");
    // return empty block
    return std::make_shared<ast_block_stmt>(tokens_[pos_].line);
  }

  // Attempt to collect annotations that immediately precede the brace in the
  // token stream (in case parse_statement didn't do so earlier). We scan back
  // skipping over the brace token to find contiguous annotation tokens.
  std::vector<std::string> block_annotations;
  long back_scan =
      static_cast<long>(pos_) - 2; // pos_ points after '{' -> scan before it
  while (back_scan >= 0 && tokens_[back_scan].kind == token_kind::annotation) {
    block_annotations.push_back(tokens_[back_scan].lexeme);
    --back_scan;
  }
  // Reverse to keep annotation order as they appear textually
  for (int i = static_cast<int>(block_annotations.size()) - 1; i >= 0; --i) {
    // reversed order will be applied after block is created
  }

  // Enter a new lexical scope when parsing a block so that declarations
  // are recorded in a nested scope that will be discarded when the block ends.
  symbol_table_.enter_scope();

  // Register any predeclared loop variables (or other predeclared symbols) in
  // this block scope. If `predeclared` is empty (default), this loop is skipped
  // quickly.
  for (const auto &pv : predeclared) {
    // Canonicalize any predeclared variable type in the block scope before
    // inserting into the symbol table.
    std::optional<std::string> canonical_pv_type = std::nullopt;
    if (pv.type_name.has_value()) {
      canonical_pv_type = std::optional<std::string>(
          symbol_table_.canonicalize_type_name(*pv.type_name));
    }
    // Respect the mutability and record the module context for the predeclared
    // symbol.
    symbol s(pv.name, symbol_kind::variable_symbol, nullptr, canonical_pv_type,
             pv.is_mut, false, parser_current_module);
    symbol_table_.insert_symbol_with_context(s.name, s.kind, s.defining_node,
                                             s.type_name, s.is_mut, s.is_pub,
                                             s.module);
  }

  auto block = std::make_shared<ast_block_stmt>(previous().line);
  while (!check(token_kind::right_brace) && !is_at_end()) {
    auto st = parse_statement();
    if (st)
      block->statements.push_back(st);
    else
      synchronize();
  }

  consume(token_kind::right_brace, "Expected '}' after block");

  // Leave the block's lexical scope before returning the AST node.
  symbol_table_.leave_scope();

  // Attach any detected block annotations
  for (int i = static_cast<int>(block_annotations.size()) - 1; i >= 0; --i) {
    block->add_annotation(block_annotations[i]);
  }

  return block;
}

std::shared_ptr<ast_stmt> parser::parse_println_statement() {
  // 'println' already consumed so current token should be '('
  consume(token_kind::left_paren, "Expected '(' after 'println'");

  std::vector<ast_expr_ptr> args;
  if (!check(token_kind::right_paren)) {
    do {
      auto e = parse_expression();
      if (e)
        args.push_back(e);
      else {
        // On parse fail, recover to next comma or right_paren
        synchronize();
        if (check(token_kind::right_paren))
          break;
      }
    } while (match(token_kind::comma));
  }

  consume(token_kind::right_paren, "Expected ')' after println arguments");
  // Accept optional semicolon
  match(token_kind::semicolon);

  auto println_node = std::make_shared<ast_println_stmt>(args, previous().line);
  return println_node;
}

std::shared_ptr<ast_stmt> parser::parse_return_statement() {
  // 'return' already consumed
  ast_expr_ptr value = nullptr;

  // Check if next token implies end of statement or block (implicit void
  // return) This prevents 'return' from greedily consuming a following 'end',
  // 'else', or '}' as an expression
  bool is_terminator =
      check(token_kind::semicolon) || check(token_kind::right_brace) ||
      check(token_kind::end_kw) || // end if / end match / end while / end
      check(token_kind::else_kw) || check(token_kind::elseif_kw) ||
      check(token_kind::right_paren); // e.g. usage in expr group

  if (!is_terminator) {
    value = parse_expression();
  }
  // optional semicolon
  match(token_kind::semicolon);

  auto ret_node =
      std::make_shared<ast_return_stmt>(value, tokens_[pos_ - 1].line);
  return ret_node;
}

std::shared_ptr<ast_stmt> parser::parse_if_statement() {
  // VB-style: if condition then ... [elseif condition then ...] [else ...] end
  // if 'if' already consumed
  // NOTE: Only 'elseif' (single token) is supported. 'else if' (two tokens)
  // conflicts with nested if statements inside else blocks.
  token if_tok = previous();

  // Parse condition (no parentheses required)
  auto cond = parse_expression();

  // Expect 'then' keyword
  consume(token_kind::then_kw, "Expected 'then' after if condition");

  // Parse then-branch statements until elseif, else, or end if
  auto then_block = std::make_shared<ast_block_stmt>(if_tok.line);

  while (!is_at_end()) {
    // Check for elseif, else, or end if
    if (check(token_kind::elseif_kw) || check(token_kind::else_kw)) {
      break;
    }
    if (check(token_kind::end_kw)) {
      // Break on any 'end' to avoid consuming it as an expression.
      break;
    }

    auto stmt = parse_statement();
    if (stmt) {
      then_block->statements.push_back(stmt);
    } else {
      synchronize();
    }
  }

  // Build the else branch (which may contain nested elseif chains)
  std::shared_ptr<ast_stmt> else_branch = nullptr;

  // Use iteration to build elseif chain from the end
  // Collect all elseif conditions and blocks first
  std::vector<std::pair<ast_expr_ptr, std::shared_ptr<ast_block_stmt>>>
      elseif_chain;
  std::shared_ptr<ast_block_stmt> final_else = nullptr;

  while (true) {
    if (match(token_kind::elseif_kw)) {
      // Single token 'elseif'
    } else if (check(token_kind::else_kw) && (pos_ + 1 < tokens_.size()) &&
               tokens_[pos_ + 1].kind == token_kind::if_kw &&
               tokens_[pos_].line == tokens_[pos_ + 1].line) {
      // Two-token 'else if' on the same line
      advance(); // consume 'else'
      advance(); // consume 'if'
    } else {
      break;
    }

    auto elseif_cond = parse_expression();
    consume(token_kind::then_kw, "Expected 'then' after elseif condition");

    auto elseif_block = std::make_shared<ast_block_stmt>(previous().line);

    while (!is_at_end()) {
      if (check(token_kind::elseif_kw) || check(token_kind::else_kw)) {
        break;
      }
      if (check(token_kind::end_kw)) {
        // Break on any 'end' to avoid consuming it as an expression.
        break;
      }

      auto stmt = parse_statement();
      if (stmt) {
        elseif_block->statements.push_back(stmt);
      } else {
        synchronize();
      }
    }

    elseif_chain.push_back({elseif_cond, elseif_block});
  }

  // Check for final else
  if (match(token_kind::else_kw)) {
    final_else = std::make_shared<ast_block_stmt>(previous().line);

    while (!is_at_end()) {
      if (check(token_kind::end_kw)) {
        // Break on any 'end' to avoid consuming it as an expression.
        // We will verify if it is 'end if' below.
        break;
      }

      auto stmt = parse_statement();
      if (stmt) {
        final_else->statements.push_back(stmt);
      } else {
        synchronize();
      }
    }
  }

  // Build nested if-else chain from the end backwards
  if (!elseif_chain.empty() || final_else) {
    // Start with final else (or nullptr)
    std::shared_ptr<ast_stmt> current_else = final_else;

    // Build from the last elseif backwards
    for (int i = static_cast<int>(elseif_chain.size()) - 1; i >= 0; --i) {
      auto &[ec, eb] = elseif_chain[i];
      current_else =
          std::make_shared<ast_if_stmt>(ec, eb, current_else, eb->line);
    }

    else_branch = current_else;
  }

  // Consume 'end if'
  if (check(token_kind::end_kw)) {
    advance(); // consume 'end'
    if (!match(token_kind::if_kw)) {
      add_error(peek(), "Expected 'if' after 'end' to close if statement");
    }
  } else {
    add_error(peek(), "Expected 'end if' to close if statement");
  }

  return std::make_shared<ast_if_stmt>(cond, then_block, else_branch,
                                       if_tok.line);
}

std::shared_ptr<ast_stmt> parser::parse_while_statement() {
  // VB-style: while condition ... end while (no parentheses)
  // 'while' consumed
  token while_tok = previous();

  // Parse condition (no parentheses)
  auto cond = parse_expression();

  // Optionally consume 'do' keyword (e.g., "while condition do ... end while")
  match(token_kind::do_kw);

  // Parse body until 'end while'
  auto body_block = std::make_shared<ast_block_stmt>(while_tok.line);

  while (!is_at_end()) {
    if (check(token_kind::end_kw)) {
      if (pos_ + 1 < tokens_.size() &&
          tokens_[pos_ + 1].kind == token_kind::while_kw) {
        advance(); // consume 'end'
        advance(); // consume 'while'
        break;
      }
    }

    auto stmt = parse_statement();
    if (stmt) {
      body_block->statements.push_back(stmt);
    } else {
      synchronize();
    }
  }

  return std::make_shared<ast_while_stmt>(cond, body_block, while_tok.line);
}

std::shared_ptr<ast_stmt> parser::parse_for_statement() {
  // 'for' consumed
  // support: for [mut] <ident> in <iterable> <body>    (block style using
  // braces or single-statement) and extended tuple/unpack forms:
  //   for a, b in zip(...) { ... }
  //   for (a, b) in zip(...) end for
  //
  // Support optional 'mut' prefix either applied to each variable or to
  // the whole variable-list (e.g., `for mut (a, b) in ...`).
  // Accept either scenario: the caller may have already `match`ed/consumed the
  // `for` token (and `previous()` points at it), or we may be called directly
  // in a recovery situation where `for` is still the current token. Consume
  // `for` if necessary to ensure the parser position is aligned consistently
  // after this function.
  token for_tok;
  if (pos_ > 0 && tokens_[pos_ - 1].kind == token_kind::for_kw) {
    // `for` was already consumed by caller; use the previous token for
    // diagnostics.
    for_tok = tokens_[pos_ - 1];
  } else if (check(token_kind::for_kw)) {
    // `for` not yet consumed; consume it now.
    for_tok = advance();
  } else {
    // Recovery: create a synthetic token for diagnostics so we don't deref
    // invalid tokens.
    token synthetic;
    synthetic.kind = token_kind::invalid;
    synthetic.lexeme = "";
    synthetic.line = (pos_ < tokens_.size() ? tokens_[pos_].line : 0);
    for_tok = synthetic;
  }

  bool loop_prefix_mut = false;
  // Special-case: If 'mut' immediately precedes '(' (i.e. `for mut (a,b) in
  // ...`), treat `mut` as a prefix for the entire tuple/list. Use lookahead to
  // avoid consuming `mut` incorrectly in other contexts.
  if (check(token_kind::mut_kw) && (pos_ + 1 < tokens_.size()) &&
      tokens_[pos_ + 1].kind == token_kind::left_paren) {
    loop_prefix_mut = true;
    // consume the `mut` so the following `check(left_paren)` sees '('.
    advance();
  } else if (match(token_kind::mut_kw)) {
    // general `for mut x in ...` case or `for mut a, b in ...` - consume `mut`
    // and mark prefix.
    loop_prefix_mut = true;
  }

  // Parse variable list: either parenthesized (a, b)
  // or a single identifier possibly followed by comma-separated identifiers (a,
  // b)
  std::vector<ast_param> loop_vars;
  // Single parenthesized form or single/multi variable form:
  //  - for (a, b) in ...
  //  - for a, b in ...
  //  - for mut (a, b) in ... / for mut a in ...
  if (check(token_kind::left_paren)) {
    // Parenthesized variable list
    consume(token_kind::left_paren,
            "Expected '(' after 'for' when using tuple variable list");
    if (!check(token_kind::right_paren)) {
      do {
        bool var_mut = loop_prefix_mut;
        if (match(token_kind::mut_kw))
          var_mut = true;

        if (!check(token_kind::identifier)) {
          add_error(peek(), "Expected loop variable name inside '(' ')'");
          // Recover: skip until a comma or closing paren
          while (!check(token_kind::comma) && !check(token_kind::right_paren) &&
                 !is_at_end()) {
            advance();
          }
          if (!match(token_kind::comma))
            break;
          else
            continue;
        }

        token name_tok = advance(); // safe after check
        ast_param p;
        p.name = name_tok.lexeme;
        p.line = name_tok.line;
        p.is_mut = var_mut;
        loop_vars.push_back(p);
      } while (match(token_kind::comma));
    }
    consume(token_kind::right_paren, "Expected ')' after loop variable list");
  } else {
    // Non-parenthesized form: possibly prefixed with `mut`
    bool var_mut = loop_prefix_mut;
    if (match(token_kind::mut_kw))
      var_mut = true;

    if (!check(token_kind::identifier)) {
      add_error(peek(), "Expected loop variable name after 'for'");
      ast_param p;
      p.name = ""; // placeholder for recovery
      p.line = (pos_ < tokens_.size() ? tokens_[pos_].line : 0);
      p.is_mut = var_mut;
      loop_vars.push_back(p);
    } else {
      token first_tok = advance();
      ast_param p;
      p.name = first_tok.lexeme;
      p.line = first_tok.line;
      p.is_mut = var_mut;
      loop_vars.push_back(p);
    }

    // Additional comma-separated variables
    while (match(token_kind::comma)) {
      bool next_var_mut = loop_prefix_mut;
      if (match(token_kind::mut_kw))
        next_var_mut = true;

      if (!check(token_kind::identifier)) {
        add_error(peek(), "Expected loop variable name after ','");
        // Recover until a comma or 'in' and stop if no identifier present
        while (!check(token_kind::comma) && !check(token_kind::in_kw) &&
               !is_at_end()) {
          advance();
        }
        if (!check(token_kind::identifier))
          break;
      }

      token name_tok = advance();
      ast_param p2;
      p2.name = name_tok.lexeme;
      p2.line = name_tok.line;
      p2.is_mut = next_var_mut;
      loop_vars.push_back(p2);
    }
  }

  // Parsed loop variables exist in `loop_vars` and have been recorded (is_mut
  // flags set).

      // Expect 'in' after the variable list

      consume(token_kind::in_kw, "Expected 'in' after loop variables");



      // Iterable expression

      auto iterable = parse_expression();



      // Optionally consume 'do' keyword (e.g., "for x in items do ... end for")

      match(token_kind::do_kw);



      // Parse body: support braced `{ ... }` or VB-style `end for` forms. Braced


  // blocks will let the block parser register predeclared loop variables, but
  // otherwise we default to VB-style `end for` block parsing for clarity and
  // predictable scope behavior.
  std::shared_ptr<ast_block_stmt> body_block = nullptr;

  if (check(token_kind::left_brace)) {
    // Braced block form - parse with `loop_vars` registered by
    // parse_block_statement.
    auto blk_stmt_as_base = parse_block_statement(loop_vars);
    body_block = std::dynamic_pointer_cast<ast_block_stmt>(blk_stmt_as_base);
    if (!body_block)
      body_block = std::make_shared<ast_block_stmt>(for_tok.line);
  } else {
    // VB-style: `for ... end for`. Create a new lexical scope for loop
    // variables and parse statements until we encounter `end for`.
    symbol_table_.enter_scope();
    for (const auto &pv : loop_vars) {
      symbol s(pv.name, symbol_kind::variable_symbol, nullptr, pv.type_name,
               pv.is_mut, visibility_kind::visibility_private,
               parser_current_module);
      symbol_table_.insert_symbol_with_context(
          s.name, s.kind, s.defining_node, s.type_name, s.is_mut,
          visibility_kind::visibility_private, s.module);
    }

    body_block = std::make_shared<ast_block_stmt>(for_tok.line);

    while (!is_at_end()) {
      // Check for 'end for'
      if (check(token_kind::end_kw)) {
        if (pos_ + 1 < tokens_.size() &&
            tokens_[pos_ + 1].kind == token_kind::for_kw) {
          advance(); // consume 'end'
          advance(); // consume 'for'
          break;
        }
      }

      auto stmt = parse_statement();
      if (stmt) {
        body_block->statements.push_back(stmt);
      } else {
        synchronize();
      }
    }

    symbol_table_.leave_scope();
  }

  // Create AST node using the tuple-capable constructor
  auto node = std::make_shared<ast_for_in_stmt>(loop_vars, iterable, body_block,
                                                for_tok.line);

  return node;
}

std::shared_ptr<ast_stmt> parser::parse_loop_statement() {
  // VB-style: loop ... end loop
  // 'loop' consumed
  token tok = previous();

  // Parse body until 'end loop'
  auto body_block = std::make_shared<ast_block_stmt>(tok.line);

  while (!is_at_end()) {
    if (check(token_kind::end_kw)) {
      if (pos_ + 1 < tokens_.size() &&
          tokens_[pos_ + 1].kind == token_kind::loop_kw) {
        advance(); // consume 'end'
        advance(); // consume 'loop'
        break;
      }
    }

    auto stmt = parse_statement();
    if (stmt) {
      body_block->statements.push_back(stmt);
    } else {
      synchronize();
    }
  }

  return std::make_shared<ast_loop_stmt>(body_block, tok.line);
}

std::shared_ptr<ast_stmt> parser::parse_break_statement() {
  // 'break' consumed
  token tok = previous(); // break token
  // optional semicolon
  match(token_kind::semicolon);
  return std::make_shared<ast_break_stmt>(tok.line);
}

std::shared_ptr<ast_stmt> parser::parse_continue_statement() {
  // 'continue' consumed
  token tok = previous(); // continue token
  // optional semicolon
  match(token_kind::semicolon);
  return std::make_shared<ast_continue_stmt>(tok.line);
}

std::shared_ptr<ast_stmt> parser::parse_expr_statement() {
  auto expr = parse_expression();
  // optional semicolon
  match(token_kind::semicolon);
  // Return a generic expression statement; the expression may be a call (e.g.,
  // println) and will be specialized/handled later by subsequent passes.
  return std::make_shared<ast_expr_stmt>(expr, previous().line);
}

// ----------------------------
// Expressions (precedence climbing)
// ----------------------------
  std::shared_ptr<ast_expr> parser::parse_expression() {
  return parse_assignment();
}

std::shared_ptr<ast_expr> parser::parse_assignment() {
  auto expr = parse_logic_or();
  // Range expression: start..end  (e.g. 0..count, 1..core::length(arr))
  if (match(token_kind::double_dot)) {
    auto op = previous();
    auto end_expr = parse_logic_or();
    return std::make_shared<ast_binary_expr>(expr, token_kind::double_dot, end_expr, op.line);
  }
  if (match(token_kind::equal)) {
    auto equals_tok = previous();
    auto value = parse_assignment();
    // Allow identifier, member access, index, and unary (dereference) as
    // targets
    if (expr->kind == ast_node_kind::identifier_expr_node ||
        expr->kind == ast_node_kind::member_access_expr_node ||
        expr->kind == ast_node_kind::index_expr_node ||
        expr->kind == ast_node_kind::unary_expr_node) {
      return std::make_shared<ast_assignment_expr>(expr, value,
                                                   equals_tok.line);
    } else {
      add_error(equals_tok, "Invalid assignment target");
    }
  }
  return expr;
}

std::shared_ptr<ast_expr> parser::parse_logic_or() {
  auto expr = parse_logic_and();
  while (true) {
    if (match(token_kind::pipe_pipe)) {
      auto op = previous();
      auto right = parse_logic_and();
      expr = std::make_shared<ast_binary_expr>(expr, op.kind, right, op.line);
    } else if (check(token_kind::identifier) && peek().lexeme == "or") {
      auto op = advance();             // consume 'or'
      op.kind = token_kind::pipe_pipe; // Treat as ||
      auto right = parse_logic_and();
      expr = std::make_shared<ast_binary_expr>(expr, op.kind, right, op.line);
    } else {
      break;
    }
  }
  return expr;
}

std::shared_ptr<ast_expr> parser::parse_logic_and() {
  auto expr = parse_bitwise_or();
  while (true) {
    if (match(token_kind::amp_amp)) {
      auto op = previous();
      auto right = parse_bitwise_or();
      expr = std::make_shared<ast_binary_expr>(expr, op.kind, right, op.line);
    } else if (check(token_kind::identifier) && peek().lexeme == "and") {
      auto op = advance();           // consume 'and'
      op.kind = token_kind::amp_amp; // Treat as &&
      auto right = parse_bitwise_or();
      expr = std::make_shared<ast_binary_expr>(expr, op.kind, right, op.line);
    } else {
      break;
    }
  }
  return expr;
}


  std::shared_ptr<ast_expr> parser::parse_bitwise_or() {
    auto expr = parse_bitwise_xor();
    while (check(token_kind::pipe) && !check_ahead(token_kind::pipe)) { match(token_kind::pipe);
      auto op = previous();
      auto right = parse_bitwise_xor();
      expr = std::make_shared<ast_binary_expr>(expr, op.kind, right, op.line);
    }
    return expr;
  }

  std::shared_ptr<ast_expr> parser::parse_bitwise_xor() {
    auto expr = parse_bitwise_and();
    while (match(token_kind::caret)) {
      auto op = previous();
      auto right = parse_bitwise_and();
      expr = std::make_shared<ast_binary_expr>(expr, op.kind, right, op.line);
    }
    return expr;
  }

  std::shared_ptr<ast_expr> parser::parse_bitwise_and() {
    auto expr = parse_equality();
    while (check(token_kind::amp) && !check_ahead(token_kind::amp_amp)) { match(token_kind::amp);
      auto op = previous();
      auto right = parse_equality();
      expr = std::make_shared<ast_binary_expr>(expr, op.kind, right, op.line);
    }
    return expr;
  }

  std::shared_ptr<ast_expr> parser::parse_equality() {
  auto expr = parse_comparison();
  while (match(token_kind::double_equal) || match(token_kind::not_equal)) {
    auto op = previous();
    auto right = parse_comparison();
    expr = std::make_shared<ast_binary_expr>(expr, op.kind, right, op.line);
  }
  return expr;
}

std::shared_ptr<ast_expr> parser::parse_comparison() {
  auto expr = parse_shift();
  while (match(token_kind::less) || match(token_kind::less_equal) ||
         match(token_kind::greater) || match(token_kind::greater_equal)) {
    auto op = previous();
    auto right = parse_shift();
    expr = std::make_shared<ast_binary_expr>(expr, op.kind, right, op.line);
  }
  return expr;
}


  std::shared_ptr<ast_expr> parser::parse_shift() {
    auto expr = parse_term();
    while (match(token_kind::less_less) || match(token_kind::greater_greater)) {
      auto op = previous();
      auto right = parse_term();
      expr = std::make_shared<ast_binary_expr>(expr, op.kind, right, op.line);
    }
    return expr;
  }

  std::shared_ptr<ast_expr> parser::parse_term() {
  auto expr = parse_factor();
  while (match(token_kind::plus) || match(token_kind::minus)) {
    auto op = previous();
    auto right = parse_factor();
    expr = std::make_shared<ast_binary_expr>(expr, op.kind, right, op.line);
  }
  return expr;
}

std::shared_ptr<ast_expr> parser::parse_factor() {
  auto expr = parse_cast();
  while (true) {
    // `*` as binary multiply is only valid on the same line as the left
    // operand.  A `*` at the start of a new line is a dereference prefix for
    // the next statement (e.g. `*ptr = val`), not a continuation of the
    // current expression.  `/` and `%` have no prefix meaning so they are
    // safe to consume across lines.
    if (check(token_kind::star) && peek().line != expr->line) break;
    if (match(token_kind::star) || match(token_kind::slash) ||
        match(token_kind::percent) || match(token_kind::mod_kw)) {
      auto op = previous();
      auto right = parse_cast();
      expr = std::make_shared<ast_binary_expr>(expr, op.kind, right, op.line);
    } else {
      break;
    }
  }
  return expr;
}

std::shared_ptr<ast_expr> parser::parse_cast() {
  auto expr = parse_unary();
  while (match(token_kind::as_kw)) {
    token as_tok = previous();
    std::string type_name = parse_type_name();
    type_name = symbol_table_.canonicalize_type_name(type_name);
    expr = std::make_shared<ast_cast_expr>(expr, type_name, as_tok.line);
  }
  return expr;
}

std::shared_ptr<ast_expr> parser::parse_unary() {
  if (match(token_kind::minus) || match(token_kind::star) ||
      match(token_kind::amp) || match(token_kind::ref_kw) ||
      match(token_kind::tilde)) {
    auto op = previous();
    auto right = parse_unary();
    return std::make_shared<ast_unary_expr>(op.kind, right, op.line);
  } else if (match(token_kind::not_kw)) {
    auto op = previous();
    auto right = parse_unary();
    return std::make_shared<ast_unary_expr>(op.kind, right, op.line);
  }
  return parse_call(parse_primary());
}

std::shared_ptr<ast_expr> parser::parse_call(std::shared_ptr<ast_expr> callee) {
  std::vector<std::string> pending_type_args;

  while (true) {
    // 1. Generic arguments: <T, U>
    // if (ny::is_debug_enabled()) {
    //    ny::diag_debug(std::string("[parse_call] pos=") + std::to_string(pos_)
    //    + " peek='" + peek().lexeme + "'");
    // }
    // Only parse if it looks like a generic call/struct init to avoid
    // stealing
    // '<' from binary ops
    if (pending_type_args.empty() &&
        looks_like_generic_call_arguments(tokens_, pos_)) {
      consume(token_kind::less, "Expected '<'");
      do {
        // Parse type name using the dedicated helper
        pending_type_args.push_back(parse_type_name());
      } while (match(token_kind::comma));
      consume(token_kind::greater, "Expected '>' after generic arguments");
      continue; // Continue to expect '(' or '{'
    }

    // 2. Call expression: (...) — only for callable expression types
    // Guard: do NOT consume '(' if callee is a non-callable (e.g. integer literal,
    // arithmetic result). This prevents the parser from greedily treating the '('
    // on the next line (e.g. (*ptr).field = val) as a function call argument list.
    bool is_callable = (callee->kind == ast_node_kind::identifier_expr_node ||
                        callee->kind == ast_node_kind::call_expr_node ||
                        callee->kind == ast_node_kind::member_access_expr_node ||
                        callee->kind == ast_node_kind::unary_expr_node);
    // Additional same-line guard: only treat '(' as call if it appears on the
    // same line as the callee. This prevents `let q = base + idx\n(*q).x = v`
    // from parsing `idx((*q)).x` across newline boundaries.
    bool paren_same_line = check(token_kind::left_paren) &&
                           peek().line == callee->line;
    if (is_callable && paren_same_line && match(token_kind::left_paren)) {
      std::vector<ast_expr_ptr> args;
      if (!check(token_kind::right_paren)) {
        do {
          args.push_back(parse_expression());
        } while (match(token_kind::comma));
      }
      token rp =
          consume(token_kind::right_paren, "Expected ')' after call arguments");

      callee = make_call_expr(callee, args, pending_type_args, rp.line);
      pending_type_args.clear(); // Consumed
      continue;
    }

    // 3. Struct literal: { field: val, ... }
    // Only parse as struct literal if callee is an identifier AND the brace
    // is followed by `identifier :` (field assignment pattern) or is empty
    // `{}`. This avoids ambiguity with block statements.
    if (check(token_kind::left_brace) &&
        callee->kind == ast_node_kind::identifier_expr_node) {
      // if (ny::is_debug_enabled()) {
      //   ny::diag_debug(std::string("[parse_call] struct literal check
      //   callee=") + std::to_string((int)callee->kind));
      // }
      // Lookahead: check if this looks like a struct literal
      // struct literal: { identifier : expr, ... } or {}
      // block: { statement ... }
      bool looks_like_struct_literal = false;
      if (pos_ + 1 < tokens_.size()) {
        if (tokens_[pos_ + 1].kind == token_kind::right_brace) {
          // Empty braces {} - treat as struct literal
          looks_like_struct_literal = true;
        } else if (tokens_[pos_ + 1].kind == token_kind::identifier &&
                   pos_ + 2 < tokens_.size() &&
                   tokens_[pos_ + 2].kind == token_kind::colon) {
          // { identifier : ... - struct literal pattern
          looks_like_struct_literal = true;
        }
      }

      if (looks_like_struct_literal) {
        advance(); // consume '{'
        // Callee must be an identifier (the struct name)
        std::string struct_name;
        if (callee->kind == ast_node_kind::identifier_expr_node) {
          struct_name =
              std::static_pointer_cast<ast_identifier_expr>(callee)->name;
        } else {
          add_error(previous(),
                    "Struct literal must be preceded by a struct name");
          struct_name = "<unknown>";
        }

        std::vector<std::pair<std::string, ast_expr_ptr>> fields;
        if (!check(token_kind::right_brace)) {
          do {
            std::string field_name;
            if (match(token_kind::identifier)) {
              field_name = previous().lexeme;
            } else {
              add_error(peek(), "Expected field name in struct literal");
            }

            consume(token_kind::colon, "Expected ':' after field name");
            auto val = parse_expression();
            fields.push_back({field_name, val});
          } while (match(token_kind::comma));
        }
        token rb = consume(token_kind::right_brace,
                           "Expected '}' after struct fields");

        callee = make_struct_literal_expr(struct_name, fields,
                                          pending_type_args, rb.line);
        pending_type_args.clear(); // Consumed
        continue;
      }
    }

    // 4. Member access: .member
    if (match(token_kind::dot)) {
      token member_tok =
          consume(token_kind::identifier, "Expected member name after '.'");
      callee = std::make_shared<ast_member_access_expr>(
          callee, member_tok.lexeme, member_tok.line);
      continue;
    }

    // 5. Try operator: ?
    if (match(token_kind::question)) {
      int line = previous().line;
      callee = std::make_shared<ast_try_expr>(std::move(callee), line);
      continue;
    }

    // 6. Index expression: [index]
    if (match(token_kind::left_bracket)) {
      auto index = parse_expression();
      token rb = consume(token_kind::right_bracket, "Expected ']' after index");
      callee =
          std::static_pointer_cast<ast_expr>(std::make_shared<ast_index_expr>(
              std::move(callee), std::move(index), rb.line));
      continue;
    }

    // 7. Static member access: ::member
    if (match(token_kind::double_colon)) {
      token member_tok =
          consume(token_kind::identifier, "Expected member name after '::'");
      // If we have generic arguments, they apply to the callee before the '::'
      // We should probably represent this in the AST.
      // For now, we reuse ast_member_access_expr.
      callee = std::make_shared<ast_member_access_expr>(
          callee, member_tok.lexeme, member_tok.line);
      continue;
    }

    break;
  }

  if (!pending_type_args.empty()) {
    // We parsed generic args but didn't find a call or struct init.
    // This is an error (e.g. `func<T> + 1`).
    // For now, we just drop them and warn/error?
    add_error(
        peek(),
        "Unexpected generic arguments not followed by call or struct init");
  }

  return callee;
}

std::shared_ptr<ast_expr> parser::parse_primary() {
  // Debug helper to print the token at peek (for parser debugging)
  auto debug_peek = [&](const std::string &label) {
    // if (!is_at_end()) {
    //   std::cerr << "[parse_primary] " << label << " peek(kind='"
    //             << token_kind_to_string(peek().kind) << "' lex='"
    //             << peek().lexeme << "' line=" << peek().line << ")\n";
    // } else {
    //   std::cerr << "[parse_primary] " << label << " peek(EOF)\n";
    // }
  };

  debug_peek("enter");

  // Intrinsic assertion: assert.method(args)
  if (match(token_kind::assert_kw)) {
    debug_peek("matched assert_kw -> parsing assert expression");
    return parse_assert_expr();
  }

  // Intrinsic hot reload: hotreload.method(args)
  if (match(token_kind::hotreload_kw)) {
    debug_peek("matched hotreload_kw -> parsing hotreload expression");
    return parse_hotreload_expr();
  }

  if (match(token_kind::char_literal)) {
    auto char_tok = previous();
    debug_peek("matched char_literal");
    std::string literal_content = char_tok.lexeme;
    // Remove single quotes from char literal
    if (literal_content.size() >= 2 && literal_content.front() == '\'' &&
        literal_content.back() == '\'') {
      literal_content = literal_content.substr(1, literal_content.size() - 2);
    }
    return std::make_shared<ast_char_literal>(literal_content, char_tok.line);
  }

  if (match(token_kind::true_kw)) {
    debug_peek("matched true_kw");
    return std::make_shared<ast_boolean_literal>(true, previous().line);
  }
  if (match(token_kind::false_kw)) {
    debug_peek("matched false_kw");
    return std::make_shared<ast_boolean_literal>(false, previous().line);
  }

  // Support `match` as an expression-level form (MVP placeholder).
  // Will call a parse_match_expression stub which consumes the structure and
  // returns an expression placeholder (call to `__match_expr`) for now.
  if (match(token_kind::match_kw)) {
    debug_peek("matched match_kw -> parsing match expression");
    return parse_match_expression();
  }

  // Support `if` as an expression (ternary-style): if cond then expr else expr
  // end if
  if (match(token_kind::if_kw)) {
    debug_peek("matched if_kw -> parsing if expression");
    return parse_if_expression();
  }

  if (match(token_kind::allocate_kw)) {
    // allocate<T>(size)
    consume(token_kind::less, "Expected '<' after 'allocate'");
    std::string type_name = parse_type_name();
    consume(token_kind::greater, "Expected '>' after type name");

    consume(token_kind::left_paren, "Expected '(' after type arguments");
    auto size = parse_expression();
    consume(token_kind::right_paren, "Expected ')' after size argument");

    return std::make_shared<ast_allocate_expr>(type_name, size,
                                               previous().line);
  }

  if (match(token_kind::ref_kw)) {
    // ref x - takes a reference to a lvalue
    token ref_tok = previous();
    auto operand = parse_primary(); // Parse the lvalue
    return std::make_shared<ast_ref_expr>(operand, ref_tok.line);
  }

  if (match(token_kind::self_kw)) {
    return std::make_shared<ast_identifier_expr>("self", previous().line);
  } else if (check(token_kind::identifier) && peek().lexeme == "self") {
    advance();
    return std::make_shared<ast_identifier_expr>("self", previous().line);
  }

  if (match(token_kind::function_kw)) {
    // Anonymous function literal: function(params) -> RetType ... end function
    token fn_tok = previous();
    consume(token_kind::left_paren, "Expected '(' after 'function'");

    std::vector<ast_param> params;
    while (!check(token_kind::right_paren) && !is_at_end()) {
      ast_param p;
      if (match(token_kind::mut_kw)) p.is_mut = true;
      p.name = consume(token_kind::identifier, "Expected parameter name").lexeme;
      p.line = previous().line;
      if (match(token_kind::colon)) p.type_name = parse_type_name();
      if (match(token_kind::equal))  p.default_value = parse_expression();
      params.push_back(std::move(p));
      if (!match(token_kind::comma)) break;
    }
    consume(token_kind::right_paren, "Expected ')' after parameters");

    std::optional<std::string> return_type;
    if (match(token_kind::arrow)) return_type = parse_type_name();

    // Parse body until 'end function' / 'end fn'
    std::vector<std::shared_ptr<ast_stmt>> stmts;
    while (!is_at_end()) {
      if (check(token_kind::end_kw)) {
        size_t save = pos_;
        advance(); // consume 'end'
        if (check(token_kind::function_kw) || check(token_kind::fn_kw)) {
          advance(); // consume 'function'/'fn'
          break;
        }
        pos_ = save; // not 'end function', restore
      }
      stmts.push_back(parse_statement());
    }

    auto body = std::make_shared<ast_block_stmt>(fn_tok.line);
    body->statements = std::move(stmts);
    return std::make_shared<ast_lambda_expr>(std::move(params), return_type,
                                             body, fn_tok.line);
  }

  if (match(token_kind::fn_kw)) {
    // Lambda: fn(params) -> RetType ... end fn
    token fn_tok = previous();
    consume(token_kind::left_paren, "Expected '(' after 'fn'");

    std::vector<ast_param> params;
    while (!check(token_kind::right_paren) && !is_at_end()) {
      ast_param p;
      p.name =
          consume(token_kind::identifier, "Expected parameter name").lexeme;
      p.line = previous().line;
      if (match(token_kind::colon)) {
        p.type_name = parse_type_name();
      }
      // Optional default value for lambda parameters
      if (match(token_kind::equal)) {
        p.default_value = parse_expression();
      }
      params.push_back(std::move(p));
      if (!match(token_kind::comma))
        break;
    }
    consume(token_kind::right_paren, "Expected ')' after parameters");

    std::optional<std::string> return_type;
    if (match(token_kind::arrow)) {
      return_type = parse_type_name();
    }

    // Parse body statements until 'end fn'
    std::vector<std::shared_ptr<ast_stmt>> stmts;
    while (!check(token_kind::end_kw) && !is_at_end()) {
      stmts.push_back(parse_statement());
    }
    consume(token_kind::end_kw, "Expected 'end' after lambda body");
    consume(token_kind::fn_kw, "Expected 'fn' after 'end'");

    auto body = std::make_shared<ast_block_stmt>(fn_tok.line);
    body->statements = std::move(stmts);
    return std::make_shared<ast_lambda_expr>(std::move(params), return_type,
                                             body, fn_tok.line);
  }

  if (match(token_kind::number_literal)) {
    auto num_tok = previous();
    debug_peek("matched number_literal");
    return std::make_shared<ast_number_literal>(num_tok.lexeme, num_tok.line);
  }
  if (match(token_kind::string_literal)) {
    auto str_tok = previous();
    debug_peek("matched string_literal");

    // Check if string contains interpolation: {expr}
    // Rules:
    //   {{   → literal '{'  (escape)
    //   }}   → literal '}'  (escape)
    //   {id} → interpolated identifier 'id' (must be simple [a-zA-Z0-9_:] name)
    //          if expr is not a simple identifier, treat as literal text
    const std::string &raw = str_tok.lexeme;
    // Only enter interpolation mode if the string contains a real '{' that
    // is not doubled and is followed by a valid identifier then '}'
    auto has_real_interp = [&]() -> bool {
      for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '{') {
          if (i + 1 < raw.size() && raw[i + 1] == '{') continue; // escaped
          // Check if next chars are a valid identifier followed by '}'
          size_t j = i + 1;
          while (j < raw.size() && (std::isalnum((unsigned char)raw[j]) ||
                 raw[j] == '_' || raw[j] == ':'))
            ++j;
          if (j > i + 1 && j < raw.size() && raw[j] == '}')
            return true;
        }
      }
      return false;
    };
    if (has_real_interp()) {
      // Parse as interpolated string
      std::vector<interpolated_part> parts;
      size_t pos = 0;
      while (pos < raw.size()) {
        if (raw[pos] == '{' && pos + 1 < raw.size() && raw[pos + 1] == '{') {
          // Escaped {{ → literal '{'
          interpolated_part p;
          p.is_literal = true;
          p.literal_value = "{";
          parts.push_back(std::move(p));
          pos += 2;
          continue;
        }
        if (raw[pos] == '}' && pos + 1 < raw.size() && raw[pos + 1] == '}') {
          // Escaped }} → literal '}'
          interpolated_part p;
          p.is_literal = true;
          p.literal_value = "}";
          parts.push_back(std::move(p));
          pos += 2;
          continue;
        }

        size_t brace_start = raw.find('{', pos);
        if (brace_start == std::string::npos) {
          // Rest is literal
          if (pos < raw.size()) {
            interpolated_part p;
            p.is_literal = true;
            p.literal_value = raw.substr(pos);
            parts.push_back(std::move(p));
          }
          break;
        }
        // Add literal part before {
        // Strip a trailing '$' if it immediately precedes '{' to support
        // "${name}" style interpolation (same as "{name}")
        if (brace_start > pos) {
          size_t lit_end = brace_start;
          if (lit_end > 0 && raw[lit_end - 1] == '$') {
            lit_end -= 1; // exclude the '$' sigil
          }
          if (lit_end > pos) {
            interpolated_part p;
            p.is_literal = true;
            p.literal_value = raw.substr(pos, lit_end - pos);
            parts.push_back(std::move(p));
          }
        }
        // Check for {{ escape
        if (brace_start + 1 < raw.size() && raw[brace_start + 1] == '{') {
          interpolated_part p;
          p.is_literal = true;
          p.literal_value = "{";
          parts.push_back(std::move(p));
          pos = brace_start + 2;
          continue;
        }
        // Find matching }
        size_t brace_end = raw.find('}', brace_start);
        if (brace_end == std::string::npos) {
          // Unclosed brace — treat as literal
          interpolated_part p;
          p.is_literal = true;
          p.literal_value = raw.substr(brace_start);
          parts.push_back(std::move(p));
          break;
        }
        // Extract expression: must be a simple identifier (alphanum/_/::)
        std::string expr_name =
            raw.substr(brace_start + 1, brace_end - brace_start - 1);
        bool is_simple = !expr_name.empty();
        for (char c : expr_name)
          if (!std::isalnum((unsigned char)c) && c != '_' && c != ':')
            is_simple = false;
        if (!is_simple) {
          // Not a valid identifier — emit as literal text including braces
          interpolated_part p;
          p.is_literal = true;
          p.literal_value = "{" + expr_name + "}";
          parts.push_back(std::move(p));
          pos = brace_end + 1;
          continue;
        }
        interpolated_part p;
        p.is_literal = false;
        p.expression =
            std::make_shared<ast_identifier_expr>(expr_name, str_tok.line);
        parts.push_back(std::move(p));
        pos = brace_end + 1;
      }
      return std::make_shared<ast_interpolated_string_expr>(std::move(parts),
                                                            str_tok.line);
    }

    std::string literal_content = str_tok.lexeme;
    if (literal_content.size() >= 2 && literal_content.front() == '"' &&
        literal_content.back() == '"') {
      literal_content = literal_content.substr(1, literal_content.size() - 2);
    }
    return std::make_shared<ast_string_literal>(literal_content, str_tok.line);
  }
  if (match(token_kind::identifier)) {
    auto id_tok = previous();
    std::string name = id_tok.lexeme;
    // A qualified reference may spell its module path with dots:
    // `tour.mixed::Measurement` is how an Ny client names a type in a
    // dotted book, and both `import` and type position already accept it.
    // Expression position needs the same rule, or the dots become member
    // accesses and the path never resolves.
    //
    // The discriminator is a `::` anywhere in the run: a path SELECTION
    // (`tour.mixed::Measurement`), while `sample.level` stays member
    // access. Look ahead before consuming, then normalise dots to `::`
    // because that is the separator the resolver and NIL both use.
    size_t probe = pos_;
    bool qualified_path = false;
    while (probe + 1 < tokens_.size() &&
           (tokens_[probe].kind == token_kind::double_colon ||
            tokens_[probe].kind == token_kind::dot) &&
           (tokens_[probe + 1].kind == token_kind::identifier ||
            token_can_be_name(tokens_[probe + 1].kind))) {
      if (tokens_[probe].kind == token_kind::double_colon)
        qualified_path = true;
      probe += 2;
    }
    if (qualified_path) {
      while (check(token_kind::double_colon) || check(token_kind::dot)) {
        bool is_dot = check(token_kind::dot);
        advance(); // consume the separator
        if (check(token_kind::identifier) || check(token_kind::println_kw)) {
          name += "::" + advance().lexeme;
        } else {
          add_error(peek(),
                    std::string("Expected identifier after '") +
                        (is_dot ? "." : "::") + "'");
          break;
        }
      }
      return std::make_shared<ast_identifier_expr>(
          canonical_std_namespace(std::move(name)), id_tok.line);
    }
    while (match(token_kind::double_colon)) {
      std::string seg;
      if (check(token_kind::identifier) || check(token_kind::println_kw)) {
        seg = advance().lexeme;
      } else {
        add_error(peek(), "Expected identifier after '::'");
      }
      name += "::" + seg;
    }
    // debug_peek("matched identifier");
    return std::make_shared<ast_identifier_expr>(
        canonical_std_namespace(std::move(name)), id_tok.line);
  }
  if (match(token_kind::left_paren)) {
    debug_peek("matched left_paren -> parse sub-expression");
    auto expr = parse_expression();
    consume(token_kind::right_paren, "Expected ')' after expression");
    return expr;
  }
  if (match(token_kind::left_bracket)) {
    debug_peek("matched left_bracket -> parse array literal");
    std::vector<ast_expr_ptr> elements;
    if (!check(token_kind::right_bracket)) {
      do {
        elements.push_back(parse_expression());
      } while (match(token_kind::comma));
    }
    token rb =
        consume(token_kind::right_bracket, "Expected ']' after array elements");
    return std::static_pointer_cast<ast_expr>(
        std::make_shared<ast_array_literal_expr>(std::move(elements), rb.line));
  }

  // Unknown primary -- add diagnostics and print debug context for easier
  // triage
  debug_peek("unknown primary -> error path");
  add_error(peek(), "Expected expression");

  // Dump token context around the current position to help locate errors
  // Fallback: treat as identifier usage
  token t = advance();
  return std::make_shared<ast_string_literal>(t.lexeme, t.line);
}

std::shared_ptr<ast_expr> parser::parse_match_expression() {
  // 'match' already consumed
  token start_tok = previous();

  // Parse target expression
  auto target = parse_expression();

  std::vector<ast_match_case> cases;

  // Check for optional brace
  bool use_braces = false;
  if (match(token_kind::left_brace)) {
    use_braces = true;
  }

  while (!is_at_end()) {
    if (use_braces && check(token_kind::right_brace))
      break;
    if (!use_braces && check(token_kind::end_kw))
      break;

    // Pattern
    ast_expr_ptr pattern = nullptr;

    // Support wildcard pattern `_` as a special-case. We detect underscore as
    // an identifier token with lexeme "_" and treat it as wildcard (still
    // parse it as an identifier expression, but label / note it here).
    if (check(token_kind::identifier) && peek().lexeme == "_") {
      // consume underscore
      token underscore_tok = advance();
      pattern = std::make_shared<ast_identifier_expr>(underscore_tok.lexeme,
                                                      underscore_tok.line);
    } else {
      // Otherwise parse the full expression pattern
      pattern = parse_expression();
    }

    // Guard -- `when` is only a guard, not a pattern token. This is the form:
    //   <pattern> when <expr> -> <body>
    // So if we see `when` next, parse the guard expression.
    ast_expr_ptr guard = nullptr;
    if (match(token_kind::when_kw)) {
      guard = parse_expression();
    }

    consume(token_kind::arrow, "Expected '->' after match pattern");

    // Body: For expression-style match arms we accept a single expression
    // statement. For block-style match arms, parse_statement handles both.
    auto body = parse_statement();

    cases.emplace_back(pattern, guard, body);
  }

  if (use_braces) {
    consume(token_kind::right_brace, "Expected '}' after match cases");
  } else {
    consume(token_kind::end_kw, "Expected 'end' to close match expression");
    // Optional 'match' after 'end'
    if (check(token_kind::match_kw)) {
      advance();
    }
  }

  return make_match_expr(target, cases, start_tok.line);
}

// Parse an if expression: if cond then expr else expr end if
// Returns ast_if_expr for expression-form conditionals.
std::shared_ptr<ast_expr> parser::parse_if_expression() {
  // 'if' already consumed
  token start_tok = previous();

  // Parse condition
  auto condition = parse_expression();

  // Expect 'then'
  consume(token_kind::then_kw, "Expected 'then' after if condition");

  // Parse then expression
  auto then_expr = parse_expression();

  // Expect 'else'
  consume(token_kind::else_kw, "Expected 'else' in if expression");

  // Parse else expression
  auto else_expr = parse_expression();

  // Optional 'end' / 'end if'
  if (match(token_kind::end_kw)) {
    match(token_kind::if_kw);
  }

  return std::make_shared<ast_if_expr>(condition, then_expr, else_expr,
                                       start_tok.line);
}

// ----------------------------
// Pattern parsing & statement-level match
// ----------------------------

// Parse a structural pattern for use by `match` arms in statement-style
// matches. The grammar implements a conservative subset of patterns suitable
// for early development:
//  - Wildcard: `_`
//  - Literal: number, string
//  - Variable binding: <identifier>
//  - Constructor / tuple: ident(pat, pat, ...) or (pat, pat, ...)
ast_pattern parser::parse_pattern() {
  ast_pattern pat;
  pat.kind = ast_pattern_kind::wildcard_pattern; // default

  // Wildcard `_` is lexed as an identifier with the lexeme "_".
  if (check(token_kind::identifier) && peek().lexeme == "_") {
    token u = advance();
    pat.kind = ast_pattern_kind::wildcard_pattern;
    pat.name = "_";
    return pat;
  }

  // Parenthesized tuple pattern: (p1, p2, ...)
  if (match(token_kind::left_paren)) {
    std::vector<ast_pattern> subs;
    if (!check(token_kind::right_paren)) {
      do {
        subs.push_back(parse_pattern());
      } while (match(token_kind::comma));
    }
    consume(token_kind::right_paren, "Expected ')' after pattern list");
    pat.kind = ast_pattern_kind::constructor_pattern;
    pat.name = ""; // unnamed tuple constructor
    pat.subpatterns = std::move(subs);
    return pat;
  }

  // Identifier-based pattern -- either a variable binding or a constructor
  // pattern with nested subpatterns: `Ident(...)` or `Enum::Variant(...)`
  if (match(token_kind::identifier)) {
    token idtok = previous();
    std::string name = idtok.lexeme;
    while (check(token_kind::double_colon) || check(token_kind::dot)) {
      bool is_dot = check(token_kind::dot);
      advance(); // consume '::' or '.'
      if (check(token_kind::identifier) || token_can_be_name(peek().kind)) {
        name += (is_dot ? "." : "::") + advance().lexeme;
      } else {
        add_error(peek(), "Expected identifier after separator in pattern");
        break;
      }
    }

    name = canonical_std_namespace(std::move(name));
    if (check(token_kind::left_paren)) {
      // Constructor-like pattern: Ident(p1, p2, ...)
      advance(); // consume '('
      std::vector<ast_pattern> subs;
      if (!check(token_kind::right_paren)) {
        do {
          subs.push_back(parse_pattern());
        } while (match(token_kind::comma));
      }
      consume(token_kind::right_paren,
              "Expected ')' after constructor pattern");
      pat.kind = ast_pattern_kind::constructor_pattern;
      pat.name = name;
      pat.subpatterns = std::move(subs);
      return pat;
    } else {
      // Simple variable binding pattern OR unit variant pattern
      // (Enum::Variant) If it looks like a qualified name, treat as
      // constructor pattern with no args? Or just variable pattern? In this
      // pattern grammar, `None` is a constructor pattern with 0 args. `x` is a
      // variable pattern. `Option::None` is a constructor pattern. How to
      // distinguish?
      // For now, if it has `::`, it's a constructor. If it's a simple
      // identifier, it's a variable (unless we know it's a type/const, but
      // parser doesn't know types). Wait, `None` should be a constructor even
      // without `::` if it's imported. But `x` is a variable. Usually,
      // uppercase = constructor, lowercase = variable is a common convention.
      // Or we treat everything as variable pattern unless it has `::` or
      // `(...)`? But `None` needs to be a constructor. Let's assume for now:
      // if it has `::`, it's a constructor. If it's simple identifier, it's a
      // variable. This breaks `None` if used without qualification. But
      // `Option::None` will work. Let's stick to that for now.

      if (name.find("::") != std::string::npos || name.find('.') != std::string::npos) {
        pat.kind = ast_pattern_kind::constructor_pattern;
        pat.name = name;
        return pat;
      }

      pat.kind = ast_pattern_kind::variable_pattern;
      pat.name = name;
      return pat;
    }
  }

  // Numeric literal pattern
  if (match(token_kind::number_literal)) {
    token n = previous();
    pat.kind = ast_pattern_kind::literal_pattern;
    pat.literal_value = n.lexeme;
    return pat;
  }

  // String literal pattern
  if (match(token_kind::string_literal)) {
    token s = previous();
    pat.kind = ast_pattern_kind::literal_pattern;
    pat.literal_kind = ast_node_kind::string_literal_node;
    pat.literal_value = s.lexeme;
    return pat;
  }

  if (match(token_kind::true_kw) || match(token_kind::false_kw)) {
    pat.kind = ast_pattern_kind::literal_pattern;
    pat.literal_kind = ast_node_kind::boolean_literal_node;
    pat.literal_value = previous().lexeme;
    return pat;
  }

  if (match(token_kind::char_literal)) {
    pat.kind = ast_pattern_kind::literal_pattern;
    pat.literal_kind = ast_node_kind::char_literal_node;
    pat.literal_value = previous().lexeme;
    if (pat.literal_value.size() >= 2 && pat.literal_value.front() == '\'' &&
        pat.literal_value.back() == '\'')
      pat.literal_value = pat.literal_value.substr(1, pat.literal_value.size() - 2);
    return pat;
  }

  // No recognizable pattern: emit diagnostic & fall back to wildcard
  add_error(peek(), "Expected pattern");
  pat.kind = ast_pattern_kind::wildcard_pattern;
  return pat;
}

// Look ahead from the current position to determine if what follows is the
// start of a new match arm, i.e., `<pattern> ->`.
//
// A pattern in Ny has one of these shapes (all ending in `->` at depth 0):
//   identifier ->
//   identifier :: identifier ->
//   identifier :: identifier ( payload ) ->
//   identifier ( payload ) ->
//   _ ->
//   number_literal ->
//   string_literal ->
//
// Statement-level keywords (return, let, if, while, for, match, raise, …)
// immediately disqualify the position as an arm start.
//
// This bounded scan avoids the greedy-scan problem where looking too far
// forward finds `->` from a *later* arm rather than the current position.
bool parser::is_match_arm_start() const {
  size_t i = pos_;
  if (i >= tokens_.size())
    return false;

  const token &first = tokens_[i];

  // Statement keywords can never start a pattern.
  switch (first.kind) {
    case token_kind::eof:
    case token_kind::end_kw:
    case token_kind::right_brace:
    case token_kind::return_kw:
    case token_kind::let_kw:
    case token_kind::if_kw:
    case token_kind::elseif_kw:
    case token_kind::else_kw:
    case token_kind::while_kw:
    case token_kind::for_kw:
    case token_kind::match_kw:
    case token_kind::raise_kw:
    case token_kind::attempt_kw:
    case token_kind::break_kw:
    case token_kind::continue_kw:
      return false;
    default:
      break;
  }

  // First token must be an identifier (variant, `_`, bare name) or literal.
  bool first_ok = (first.kind == token_kind::identifier ||
                   first.kind == token_kind::number_literal ||
                   first.kind == token_kind::true_kw ||
                   first.kind == token_kind::false_kw ||
                   first.kind == token_kind::char_literal ||
                   first.kind == token_kind::string_literal);
  if (!first_ok)
    return false;

  i++; // consume the identifier / literal

  // Skip optional `:: qualifier` or `.qualifier` sequences
  while (i + 1 < tokens_.size() &&
         (tokens_[i].kind == token_kind::double_colon ||
          tokens_[i].kind == token_kind::dot) &&
         tokens_[i + 1].kind == token_kind::identifier) {
    i += 2;
  }

  // Skip optional payload `( ... )`.
  if (i < tokens_.size() && tokens_[i].kind == token_kind::left_paren) {
    int depth = 1;
    i++;
    while (i < tokens_.size() && depth > 0) {
      switch (tokens_[i].kind) {
        case token_kind::left_paren:  depth++; break;
        case token_kind::right_paren: depth--; break;
        case token_kind::eof:         return false;
        default:                      break;
      }
      i++;
    }
  }

  // The token immediately after the pattern shape must be `->`.
  return (i < tokens_.size() && tokens_[i].kind == token_kind::arrow);
}

// Parse a statement-level match: `match <expr> { ... }` or the line-style
// form using `end match`. Produce an `ast_match_stmt` containing a vector of
// `ast_match_arm` entries (pattern, guard, body).
std::shared_ptr<ast_stmt> parser::parse_match_statement() {
  // 'match' already consumed by the caller (i.e., parse_statement)
  token start_tok = previous();

  // Parse the target expression for the match statement.
  auto target = parse_expression();

  std::vector<ast_match_arm> arms;

  // Optional braces vs. `end match`
  bool use_braces = false;
  if (match(token_kind::left_brace)) {
    use_braces = true;
  }

  while (!is_at_end()) {
    if (use_braces && check(token_kind::right_brace))
      break;
    if (!use_braces && check(token_kind::end_kw))
      break;

    // Parse the structural pattern for this arm
    ast_pattern pattern = parse_pattern();

    // Optional guard
    ast_expr_ptr guard = nullptr;
    if (match(token_kind::when_kw)) {
      guard = parse_expression();
    }

    // Expect arrow '->'
    consume(token_kind::arrow, "Expected '->' after match pattern");

    // Body: collect one or more statements until the next arm or end-of-match.
    // A single `->` after a pattern-like token signals a new arm; statement
    // keywords (return, let, if, …) signal more statements in this arm.
    int body_line = previous().line;
    auto body_block = std::make_shared<ast_block_stmt>(body_line);
    body_block->statements.push_back(parse_statement());
    while (!is_at_end() &&
           !check(token_kind::end_kw) &&
           !check(token_kind::right_brace) &&
           !is_match_arm_start()) {
      body_block->statements.push_back(parse_statement());
    }
    // Unwrap single-statement arms so the AST is the same as before.
    std::shared_ptr<ast_stmt> body;
    if (body_block->statements.size() == 1) {
      body = body_block->statements[0];
    } else {
      body = body_block;
    }
    arms.emplace_back(pattern, body, guard);
  }

  if (use_braces) {
    consume(token_kind::right_brace, "Expected '}' after match cases");
  } else {
    consume(token_kind::end_kw, "Expected 'end' to close match statement");
    // Optional 'match' after 'end' (e.g., `end match`)
    if (check(token_kind::match_kw)) {
      advance();
    }
  }

  return make_match_stmt(target, arms, start_tok.line);
}

// ----------------------------
// Token stream utilities
// ----------------------------
bool parser::is_at_end() const {
  return (pos_ >= tokens_.size()) || (tokens_[pos_].kind == token_kind::eof);
}

const token &parser::peek() const {
  assert(pos_ < tokens_.size());
  return tokens_[pos_];
}

token &parser::peek() {
  assert(pos_ < tokens_.size());
  return tokens_[pos_];
}

token &parser::previous() {
  assert(pos_ > 0 && pos_ - 1 < tokens_.size());
  return tokens_[pos_ - 1];
}

token parser::advance() {
  if (!is_at_end())
    ++pos_;
  return tokens_[pos_ - 1];
}

bool parser::match(const token_kind &k) {
  if (is_at_end())
    return false;
  if (tokens_[pos_].kind == k) {
    ++pos_;
    return true;
  }
  return false;
}

bool parser::check(const token_kind &k) const {
  if (is_at_end())
    return false;
  return tokens_[pos_].kind == k;
}

bool parser::check_ahead(const token_kind &k) const {
  if (pos_ + 1 >= tokens_.size())
    return false;
  return tokens_[pos_ + 1].kind == k;
}

token parser::consume(const token_kind &expected, const std::string &msg) {
  if (check(expected)) {
    return advance();
  }
  // Record error and try to continue
  add_error(static_cast<const token &>(peek()), msg);
  // Create a synthetic token to use for error recovery context
  token synthetic;
  synthetic.kind = token_kind::invalid;
  synthetic.lexeme = "";
  synthetic.line = (pos_ < tokens_.size() ? tokens_[pos_].line : 0);
  return synthetic;
}

// ----------------------------
// Error handling & recovery
// ----------------------------
void parser::add_error(const token &t, const std::string &msg,
                       const std::optional<std::string> &hint) {
  diagnostic d;
  d.line = t.line;
  d.message = msg;
  d.hint = hint;
  diagnostics_.push_back(d);
}

void parser::add_error_at_line(int line, const std::string &msg,
                               const std::optional<std::string> &hint) {
  diagnostic d;
  d.line = line;
  d.message = msg;
  d.hint = hint;
  diagnostics_.push_back(d);
}

void parser::synchronize() {
  // Discard tokens until we find a reasonable statement boundary or a keyword
  advance();

  while (!is_at_end()) {
    if (previous().kind == token_kind::semicolon)
      return;

    switch (peek().kind) {
    case token_kind::function_kw:
    case token_kind::let_kw:
    case token_kind::if_kw:
    case token_kind::println_kw:
    case token_kind::return_kw:
    case token_kind::for_kw:
    case token_kind::while_kw:
    case token_kind::loop_kw:
    case token_kind::break_kw:
    case token_kind::continue_kw:
      return;
    default:
      break;
    }

    advance();
  }
}

// ----------------------------
// Struct and Enum Declarations
// ----------------------------
std::shared_ptr<ast_decl>
parser::parse_struct_declaration(visibility_kind vis) {
  // 'struct' keyword already consumed
  token name_tok =
      consume(token_kind::identifier, "Expected struct name after 'struct'");
  std::string name = name_tok.lexeme;

  // Parse optional generic parameters
  std::vector<ast_generic_param> generic_params = parse_generic_params();

  // Parse optional where clause
  auto where_constraints = parse_where_clause();
  validate_where_parameters(generic_params, where_constraints);
  // Merge where constraints into generic params
  for (auto &gp : generic_params) {
    if (where_constraints.count(gp.name)) {
      auto &bounds = where_constraints[gp.name];
      gp.bounds.insert(gp.bounds.end(), bounds.begin(), bounds.end());
    }
  }

  // Support both C-style { } and VB-style end struct
  bool uses_braces = match(token_kind::left_brace);

  // Parse struct fields
  std::vector<ast_struct_field> fields;
  while (!is_at_end()) {
    // C-style: check for closing brace
    if (uses_braces && check(token_kind::right_brace)) {
      advance(); // consume '}'
      break;
    }
    // VB-style: check for `end struct` terminator
    if (!uses_braces && check(token_kind::end_kw)) {
      advance(); // consume 'end'
      if (check(token_kind::struct_kw)) {
        advance(); // consume 'struct'
        break;
      } else {
        add_error(peek(), "Expected 'struct' after 'end'");
        break;
      }
    }

    // Check for optional visibility modifier
    if (match(token_kind::pub_kw)) {
      // For now, valid but we might ignoring it or using it?
      // The parser stores 'canonical_type'.
      // We should probably flag the field as public.
      // But struct_field struct (line 2916) only has name, type, line.
      // We can ignore it for now to fix the hang, assuming default public or
      // ignored. Or we can update AST later. Priority: Fix Hang.
    }

    // Check if we hit a start of a new declaration (missing end struct/brace?)
    if (check(token_kind::struct_kw) || check(token_kind::function_kw) ||
        check(token_kind::fn_kw)) {
      if (!uses_braces) {
        add_error(peek(), "Missing 'end struct'? Implicitly closing struct.");
      }
      break;
    }

    if (!check(token_kind::identifier)) {
      add_error(peek(), "Expected field name, found keyword or symbol");
      advance(); // Skip bad token to avoid infinite loop
      continue;
    }

    token field_name = consume(token_kind::identifier, "Expected field name");

    consume(token_kind::colon, "Expected ':' after field name");
    std::string type_str = parse_type_name();
    // Canonicalize field type name
    std::string canonical_type = symbol_table_.canonicalize_type_name(type_str);

    // Field defaults have no AST storage or initialization lowering yet. Do not
    // accept an expression only to silently discard its value and effects.
    if (match(token_kind::equal)) {
      add_error(previous(), "Struct field default initializers are unsupported; "
                            "initialize the field in each struct literal");
      parse_expression(); // Consume the expression for diagnostic recovery.
    }

    fields.push_back({field_name.lexeme, canonical_type, field_name.line});

    // Optional comma between fields (not required in VB-style)
    match(token_kind::comma);
  }

  // Create AST node
  bool is_pub_bool = (vis == visibility_kind::visibility_public);
  auto struct_decl = std::make_shared<ast_struct_decl>(
      name, std::move(fields), std::move(generic_params), is_pub_bool,
      name_tok.line);
  // Ensure AST node reflects enum-based visibility
  struct_decl->visibility = vis;
  // Register struct as a type symbol and record its visibility flag. Provide
  // module context (if any).
  symbol_table_.insert_symbol_with_context(name, symbol_kind::type_symbol,
                                           struct_decl.get(), std::nullopt,
                                           false, vis, parser_current_module);

  return struct_decl;
}

std::shared_ptr<ast_decl> parser::parse_enum_declaration(visibility_kind vis) {
  // 'enum' keyword already consumed
  token name_tok =
      consume(token_kind::identifier, "Expected enum name after 'enum'");
  std::string name = name_tok.lexeme;

  // Parse optional generic parameters
  std::vector<ast_generic_param> generic_params = parse_generic_params();

  // Parse optional where clause
  auto where_constraints = parse_where_clause();
  validate_where_parameters(generic_params, where_constraints);
  // Merge where constraints into generic params
  for (auto &gp : generic_params) {
    if (where_constraints.count(gp.name)) {
      auto &bounds = where_constraints[gp.name];
      gp.bounds.insert(gp.bounds.end(), bounds.begin(), bounds.end());
    }
  }

  // VB-style: enum variants terminated by `end enum`
  std::vector<ast_enum_variant> variants;
  while (!is_at_end()) {
    // Check for `end enum` terminator
    if (check(token_kind::end_kw)) {
      advance(); // consume 'end'
      if (check(token_kind::enum_kw)) {
        advance(); // consume 'enum'
        break;
      } else {
        add_error(peek(), "Expected 'enum' after 'end'");
        break;
      }
    }

    token variant_name =
        consume(token_kind::identifier, "Expected variant name");
    // consume() records a mismatch without advancing, so a token that is
    // neither 'end enum' nor a variant name leaves the cursor where it was.
    // Without this the loop appends a nameless variant and another diagnostic
    // on every turn and never terminates: 'enum a::b' is ten bytes that run
    // the process out of memory. The error is already recorded; stop here
    // rather than grow. struct, class and trait recover on their own.
    if (variant_name.kind == token_kind::invalid) break;
    std::optional<std::string> associated_type = std::nullopt;

    // Optional associated type: Variant(Type) or Variant(Type1, Type2, ...)
    if (match(token_kind::left_paren)) {
      // Collect all types (comma-separated) into a tuple-like type string
      std::string type_str = parse_type_name();
      while (match(token_kind::comma)) {
        type_str += ", " + parse_type_name();
      }
      // Wrap multiple types in tuple notation for consistency
      if (type_str.find(", ") != std::string::npos) {
        type_str = "(" + type_str + ")";
      }
      associated_type = symbol_table_.canonicalize_type_name(type_str);
      consume(token_kind::right_paren, "Expected ')' after associated type");
    }

    variants.push_back(
        {variant_name.lexeme, associated_type, variant_name.line});

    // Optional comma between variants (not required in VB-style)
    match(token_kind::comma);
  }

  // Create AST node
  bool is_pub_bool = (vis == visibility_kind::visibility_public);
  auto enum_decl = std::make_shared<ast_enum_decl>(name, std::move(variants),
                                                   std::move(generic_params),
                                                   is_pub_bool, name_tok.line);
  enum_decl->visibility = vis;
  symbol_table_.insert_symbol_with_context(name, symbol_kind::type_symbol,
                                           enum_decl.get(), std::nullopt, false,
                                           vis, parser_current_module);

  return enum_decl;
}

std::shared_ptr<ast_decl> parser::parse_trait_declaration(visibility_kind vis) {
  // trait_kw already consumed by caller
  token name_tok = consume(token_kind::identifier, "Expected trait name");
  std::string name = name_tok.lexeme;

  // Parse generic parameters if present
  std::vector<ast_generic_param> generic_params = parse_generic_params();

  // Parse optional where clause
  auto where_constraints = parse_where_clause();
  for (const auto &constraint : where_constraints) {
    if (std::none_of(generic_params.begin(), generic_params.end(),
                     [&](const ast_generic_param &gp) {
                       return gp.name == constraint.first;
                     }))
      add_error(name_tok, "Unknown generic parameter '" + constraint.first +
                             "' in trait where clause");
  }
  // Merge where constraints into generic params
  for (auto &gp : generic_params) {
    if (where_constraints.count(gp.name)) {
      auto &bounds = where_constraints[gp.name];
      gp.bounds.insert(gp.bounds.end(), bounds.begin(), bounds.end());
    }
  }

  // VB-style: trait body terminated by `end trait`
  std::vector<std::shared_ptr<ast_function_decl>> methods;
  bool terminated = false;
  while (!is_at_end()) {
    // Check for `end trait` terminator
    if (check(token_kind::end_kw)) {
      advance(); // consume 'end'
      if (check(token_kind::trait_kw)) {
        advance(); // consume 'trait'
        terminated = true;
        break;
      } else {
        add_error(peek(), "Expected 'trait' after 'end'");
        break;
      }
    }

    if (match(token_kind::function_kw) || match(token_kind::fn_kw)) {
      // Parse function signature (no body for now in traits, though default
      // impls could be added later)

      token fn_name_tok =
          consume(token_kind::identifier, "Expected function name");

      std::vector<ast_generic_param> fn_gen_params = parse_generic_params();

      consume(token_kind::left_paren, "Expected '(' after function name");
      std::vector<ast_param> params;
      if (!check(token_kind::right_paren)) {
        do {
          bool is_mut = false;
          if (match(token_kind::mut_kw)) {
            is_mut = true;
          }

          std::string param_name;
          std::string param_type;

          if (check(token_kind::self_kw)) {
            param_name = advance().lexeme;
            if (match(token_kind::colon)) {
              param_type = parse_type_name();
            } else {
              param_type = "Self";
            }
          } else if (check(token_kind::identifier) && peek().lexeme == "self") {
            param_name = advance().lexeme;
            if (match(token_kind::colon)) {
              param_type = parse_type_name();
            } else {
              param_type = "Self";
            }
          } else {
            param_name =
                consume(token_kind::identifier, "Expected parameter name")
                    .lexeme;
            consume(token_kind::colon, "Expected ':' after parameter name");
            param_type = parse_type_name();
          }

          ast_param p;
          p.name = param_name;
          p.type_name = param_type;
          p.is_mut = is_mut;
          p.line = fn_name_tok.line;
          params.push_back(p);
        } while (match(token_kind::comma));
      }
      consume(token_kind::right_paren, "Expected ')' after parameters");

      std::optional<std::string> return_type;
      if (match(token_kind::arrow)) {
        return_type = parse_type_name();
      }

      auto fn_where_constraints = parse_where_clause();
      for (const auto &constraint : fn_where_constraints) {
        auto parameter = std::find_if(
            fn_gen_params.begin(), fn_gen_params.end(),
            [&](const ast_generic_param &gp) { return gp.name == constraint.first; });
        if (parameter == fn_gen_params.end()) {
          add_error(fn_name_tok, "Unknown generic parameter '" +
                                    constraint.first + "' in trait method where clause");
        } else {
          parameter->bounds.insert(parameter->bounds.end(),
                                   constraint.second.begin(), constraint.second.end());
        }
      }

      // Signature only (no body in trait) - look for `end function` or method
      // is just a signature For trait methods without default impl, we parse
      // until `end function` For signature-only declarations, check if next
      // is another function or end trait
      bool has_body = !is_at_end() &&
          !check(token_kind::function_kw) && !check(token_kind::fn_kw) &&
          !check(token_kind::end_kw);

      if (has_body) {
        add_error(fn_name_tok,
                  "Default trait methods are not supported; provide a signature "
                  "in the trait and an explicit method in each impl");
        // VB-style function body: parse statements until `end function`
        auto body_block = std::make_shared<ast_block_stmt>(fn_name_tok.line);
        bool method_terminated = false;
        while (!is_at_end()) {
          if (check(token_kind::end_kw)) {
            size_t save_pos = pos_;
            advance(); // consume 'end'
            if (check(token_kind::function_kw) || check(token_kind::fn_kw)) {
              advance(); // consume 'function' or 'fn'
              method_terminated = true;
              break;
            } else {
              // Not 'end function', restore position and stop parsing body
              pos_ = save_pos;
              break;
            }
          }
          const size_t statement_start = pos_;
          auto stmt = parse_statement();
          if (stmt) {
            body_block->statements.push_back(stmt);
          }
          if (pos_ == statement_start)
            advance();
        }
        if (!method_terminated)
          add_error(fn_name_tok, "Unterminated trait method; expected 'end fn'");
        auto fn = std::shared_ptr<ast_function_decl>(
            new ast_function_decl(fn_name_tok.lexeme));
        fn->params = params;
        fn->return_type = return_type;
        fn->body = body_block;
        fn->generic_params = fn_gen_params;
        fn->is_pub = false;
        fn->line = fn_name_tok.line;
        methods.push_back(fn);
      } else {
        // Signature only
        auto fn = std::shared_ptr<ast_function_decl>(
            new ast_function_decl(fn_name_tok.lexeme));
        fn->params = params;
        fn->return_type = return_type;
        fn->body = std::shared_ptr<ast_stmt>(nullptr);
        fn->generic_params = fn_gen_params;
        fn->is_pub = false;
        fn->line = fn_name_tok.line;
        methods.push_back(fn);
        // An explicit empty `end fn` is also a signature-only declaration.
        if (check(token_kind::end_kw) && pos_ + 1 < tokens_.size() &&
            (tokens_[pos_ + 1].kind == token_kind::function_kw ||
             tokens_[pos_ + 1].kind == token_kind::fn_kw)) {
          advance();
          advance();
        }
      }
    } else {
      add_error(peek(), "Unsupported trait member; expected a method signature "
                        "or 'end trait'");
      advance();
    }
  }
  if (!terminated)
    add_error(name_tok, "Unterminated trait declaration; expected 'end trait'");

  bool is_pub_bool_t = (vis == visibility_kind::visibility_public);
  auto trait_decl = std::make_shared<ast_trait_decl>(
      name, generic_params, methods, is_pub_bool_t, name_tok.line);
  trait_decl->visibility = vis;

  // Register symbol for the trait using the trait node and resolved
  // visibility
  symbol_table_.insert_symbol_with_context(name, symbol_kind::type_symbol,
                                           trait_decl.get(), std::nullopt,
                                           false, vis, parser_current_module);

  return trait_decl;
}

std::shared_ptr<ast_decl> parser::parse_impl_declaration() {
  // impl_kw already consumed by caller
  int line = previous().line;

  // Parse generic parameters for the impl block itself: impl<T> ...
  std::vector<ast_generic_param> generic_params = parse_generic_params();

  // Parse optional where clause
  auto where_constraints = parse_where_clause();
  for (const auto &constraint : where_constraints) {
    if (std::none_of(generic_params.begin(), generic_params.end(),
                     [&](const ast_generic_param &gp) {
                       return gp.name == constraint.first;
                     }))
      add_error(previous(), "Unknown generic parameter '" + constraint.first +
                                "' in impl where clause");
  }
  // Merge where constraints into generic params
  for (auto &gp : generic_params) {
    if (where_constraints.count(gp.name)) {
      auto &bounds = where_constraints[gp.name];
      gp.bounds.insert(gp.bounds.end(), bounds.begin(), bounds.end());
    }
  }

  // Parse the type or trait name
  std::string first_type = parse_type_name();

  std::optional<std::string> trait_name;
  std::string target_type;

  // Primary form: `impl Trait for Type`
  if (match(token_kind::for_kw)) {
    trait_name = first_type;
    target_type = parse_type_name();
  } else {
    // Historical/alternate form: some stdlib sources used `impl Trait Type`
    // (no 'for' token). Detect this pattern via lightweight lookahead and
    // accept it to preserve backward compatibility with older stdlib files.
    bool looks_like_trait_form = false;
    if (!is_at_end()) {
      token_kind pk = peek().kind;
      // If the next token could start a type (identifier, keyword used as
      // name, double_colon, ref, pointer, or parenthesized/tuple start) then
      // it's likely the `Trait Type` historical form.
      if (pk == token_kind::identifier || token_can_be_name(pk) ||
          pk == token_kind::double_colon || pk == token_kind::ref_kw ||
          pk == token_kind::star || pk == token_kind::left_paren) {
        if (pk == token_kind::identifier || token_can_be_name(pk)) {
          // If an identifier is followed by generics or path segs, it's a
          // strong signal this is a type name (e.g., `map < K, V >`).
          if (pos_ + 1 < tokens_.size()) {
            token_kind nk = tokens_[pos_ + 1].kind;
            if (nk == token_kind::less || nk == token_kind::double_colon) {
              looks_like_trait_form = true;
            }
          }
        } else {
          looks_like_trait_form = true;
        }
      }
    }

    if (looks_like_trait_form) {
      // Treat as `impl Trait Type` where `first_type` is the trait name and
      // the following token(s) form the target type.
      trait_name = first_type;
      target_type = parse_type_name();
    } else {
      // Fallback: inherent implementation `impl Type`
      target_type = first_type;
    }
  }

  // VB-style: impl body terminated by `end impl`
  std::vector<std::shared_ptr<ast_function_decl>> methods;
  bool terminated = false;
  while (!is_at_end()) {
    // Check for `end impl` terminator
    if (check(token_kind::end_kw)) {
      advance(); // consume 'end'
      if (check(token_kind::impl_kw)) {
        advance(); // consume 'impl'
        terminated = true;
        break;
      } else {
        add_error(peek(), "Expected 'impl' after 'end'");
        break;
      }
    }

    // Use standard declaration parser to handle function definitions.
    // This supports annotations, visibility, and intrinsic logic uniformly.
    auto decl = parse_declaration();
    if (decl) {
      if (decl->kind == ast_node_kind::function_decl_node) {
        methods.push_back(std::static_pointer_cast<ast_function_decl>(decl));
      } else {
        add_error(previous(), "Unsupported impl member; only methods are supported");
      }
    } else {
      // Error recovery
      synchronize();
    }
  }
  if (!terminated)
    add_error(previous(), "Unterminated impl declaration; expected 'end impl'");

  return std::make_shared<ast_impl_decl>(target_type, trait_name,
                                         generic_params, methods, line);
}

// Parse a class declaration:
//   [abstract] class Name [inherits Base1, Base2] { ... }
std::shared_ptr<ast_decl>
parser::parse_class_declaration(visibility_kind visibility, bool is_abstract) {
  // Consume 'class' keyword (caller might have consumed 'abstract' already)
  // consume(token_kind::class_kw, "Expected 'class' keyword"); // REMOVED:
  // caller consumes it via match()

  token name_tok = consume(token_kind::identifier, "Expected class name");
  std::string name = name_tok.lexeme;

  // Parse generic parameters <T, U>
  std::vector<ast_generic_param> generic_params = parse_generic_params();

  // Parse inheritance: inherits Base1, Base2
  std::vector<std::string> base_classes;
  if (match(token_kind::inherits_kw)) {
    do {
      base_classes.push_back(parse_type_name());
    } while (match(token_kind::comma));
  }

  // Parse where clause
  auto where_constraints = parse_where_clause();
  validate_where_parameters(generic_params, where_constraints);
  for (auto &gp : generic_params) {
    if (where_constraints.count(gp.name)) {
      auto &bounds = where_constraints[gp.name];
      gp.bounds.insert(gp.bounds.end(), bounds.begin(), bounds.end());
    }
  }

  // Support both '{' ... '}' AND 'end class' styles (or implicit block start)
  // For 'end class' style, we don't strictly require a start token, but
  // checking for '{' is good compat.
  bool using_braces = false;
  if (match(token_kind::left_brace)) {
    using_braces = true;
  }

  std::vector<ast_struct_field> fields;
  std::vector<std::shared_ptr<ast_function_decl>> methods;

    int loop_safety = 0;
    while (!is_at_end()) {
      if (++loop_safety > 100) {
        break;
      }
      if (parse_trace_enabled()) {
        // Brief trace: current position, peek token, and line for diagnostics
        const token &pk = peek();
        std::cerr << "[NY_PARSE_TRACE] class_loop pos=" << pos_
                  << " peek_kind=" << token_kind_to_string(pk.kind)
                  << " peek_lex='" << pk.lexeme << "' line=" << pk.line
                  << std::endl;
      }
    // printf("[ClassParse] " ...
    // Check for termination
    if (using_braces) {
      if (check(token_kind::right_brace)) {
        advance(); // consume '}'
        break;
      }
    } else {
      // Check for 'end class'
      if (check(token_kind::end_kw)) {
        // Be robust: if we see 'end' and next is 'class', consume both.
        // If we see 'end' and next is NOT class, it might be 'end' of a nested
        // construct (if logic was flawed) or just 'end' meaning end of class
        // (if we allow loose 'end'). Current spec prefers 'end class'.

        // Peek next to confirm 'class'
        if (pos_ + 1 < tokens_.size() &&
            tokens_[pos_ + 1].kind == token_kind::class_kw) {
          advance(); // consume 'end'
          advance(); // consume 'class'
          break;
        }

        // Fallback: if we are at top level of class, 'end' probably means end
        // of class if no other match. But let's check strictness. If we error
        // on "end" without "class", we risk an infinite loop if we don't
        // advance. So if we see 'end', we should probably assume it closes the
        // class or is an error that ends the block. Let's assume valid 'end
        // class' is required, but if we see plain 'end' that doesn't match a
        // method/field, we consume it and break to avoid loop. advance();
        // break;
        // Actually, better to error if missing 'class' but still break.
      }
    }

    // Safety break for 'end' keyword if not handled above (prevent infinite
    // loop)
    if (check(token_kind::end_kw)) {
      if (using_braces) {
        // 'end' found inside braces? Weird.
        advance(); // consume to progress
      } else {
        // 'end' found but didn't match 'end class'?
        // It might be 'end' from a previous error or just missing 'class'.
        // Consume 'end' and 'class' if present, or just 'end'.
        advance();
        if (check(token_kind::class_kw))
          advance();
        break;
      }
    }

    const token annotation_start = peek();
    const auto member_annotations = parse_leading_annotations();

    // Parse fields or methods
    // Methods may be prefixed by a visibility modifier (pub) and then by
    // additional modifiers such as 'virtual'/'abstract'/'override'. To be
    // tolerant and consistent we first capture an optional per-member
    // visibility and then parse modifiers. This ensures we pass the correct
    // visibility into parse_function_declaration and avoid double-consuming
    // the 'function'/'fn' token (parse_function_declaration handles that).
    visibility_kind member_visibility = parse_visibility_modifier();

    if (parse_trace_enabled()) {
      // Log the resolved member visibility
      std::string vis_s = (member_visibility == visibility_kind::visibility_public) ? "pub" : "priv";
      std::cerr << "[NY_PARSE_TRACE] member_visibility=" << vis_s << " pos=" << pos_ << std::endl;
    }

    // Parse method modifiers (virtual/abstract/override) after visibility
    bool is_virtual = false;
    bool is_abstract_method = false;
    bool is_override = false;

    if (match(token_kind::virtual_kw))
      is_virtual = true;
    if (match(token_kind::abstract_kw))
      is_abstract_method = true;
    if (match(token_kind::override_kw))
      is_override = true;

    // If this looks like a method (fn/function) parse it and pass through the
    // collected member visibility. parse_function_declaration itself will
    // consume an optional leading 'function' / 'fn' token if present.
    if (check(token_kind::function_kw) || check(token_kind::fn_kw)) {
      // Trace before attempting to parse a method
      if (parse_trace_enabled())
        std::cerr << "[NY_PARSE_TRACE] attempting parse_function_declaration at pos=" << pos_
                  << " peek='" << peek().lexeme << "'" << std::endl;
      auto decl = parse_function_declaration(member_visibility, is_abstract_method,
                                             member_annotations);
      if (decl) {
        auto fn = std::static_pointer_cast<ast_function_decl>(decl);
        fn->is_virtual = is_virtual;
        fn->is_abstract = is_abstract_method;
        fn->is_override = is_override;
        methods.push_back(fn);
      } else {
        // On failure, emit compact trace and recover to avoid repeated
        // identical diagnostics for the same token. synchronize() will
        // advance to a likely statement boundary.
        if (parse_trace_enabled()) {
          std::cerr << "[NY_PARSE_TRACE] parse_function_declaration FAILED at pos=" << pos_
                    << " peek_kind=" << token_kind_to_string(peek().kind)
                    << " peek_lex='" << peek().lexeme << "' line=" << peek().line
                    << std::endl;
          // Dump a short forward token context
          std::cerr << "[NY_PARSE_TRACE] token_context:";
          for (int _i = 0; _i < 6; ++_i) {
            size_t idx = pos_ + _i;
            if (idx < tokens_.size())
              std::cerr << " " << token_kind_to_string(tokens_[idx].kind) << "('" << tokens_[idx].lexeme << "')";
            else
              std::cerr << " EOF";
          }
          std::cerr << std::endl;
        }
        synchronize();
      }
    } else {
      if (!member_annotations.empty())
        add_error(annotation_start,
                  "Class member annotations are supported only on methods");
      // Not a method: proceed to field parsing below (fields may have had a
      // preceding visibility that was consumed into member_visibility).
      if (check(token_kind::identifier)) {
        token field_name = consume(token_kind::identifier, "Expected field name");
        consume(token_kind::colon, "Expected ':' after field name");
        std::string type_name = parse_type_name();
        // Class fields share the same no-default AST representation as structs.
        if (match(token_kind::equal)) {
          add_error(previous(), "Class field default initializers are unsupported; "
                                "initialize the field in each class literal");
          parse_expression(); // Consume the expression for diagnostic recovery.
        }

        if (check(token_kind::semicolon))
          advance();
        fields.push_back(ast_struct_field{field_name.lexeme, type_name, field_name.line});
      } else {
        // Not a method, not a field (identifier), not end. To avoid infinite
        // loop, we MUST advance/synchronize.
        add_error(peek(), "Unexpected token in class body");
        if (parse_trace_enabled())
          std::cerr << "[NY_PARSE_TRACE] unexpected token in class body at pos=" << pos_
                    << " kind=" << token_kind_to_string(peek().kind)
                    << " lex='" << peek().lexeme << "' line=" << peek().line << std::endl;
        advance();
      }
    }
  }

  auto cls = std::make_shared<ast_class_decl>(name, base_classes, fields,
                                              methods, generic_params,
                                              is_abstract, name_tok.line);
  cls->visibility = visibility;

  // Register symbol
  symbol_table_.insert_symbol_with_context(
      name, symbol_kind::type_symbol, cls.get(), name, false,
      visibility == visibility_kind::visibility_public, parser_current_module);

  return cls;
}
// Parse a class declaration:
//   [abstract] class Name [inherits Base1, Base2] { ... }

// =============================================================================
// Fault Handling: attempt/fault/recover/raise
// =============================================================================

// Parse raise statement: raise FaultExpression
std::shared_ptr<ast_stmt> parser::parse_raise_statement() {
  // 'raise' already consumed
  token raise_tok = previous();

  // Parse fault expression
  auto fault_expr = parse_expression();
  if (!fault_expr) {
    add_error(peek(), "Expected fault expression after 'raise'");
    return nullptr;
  }

  return std::make_shared<ast_raise_stmt>(fault_expr, raise_tok.line);
}

// Parse fault handler: fault [FaultType] as variable [when condition]
std::shared_ptr<ast_fault_handler> parser::parse_fault_handler() {
  // 'fault' already consumed
  token fault_tok = previous();

  std::optional<std::string> fault_type = std::nullopt;
  std::string variable;
  ast_expr_ptr guard = nullptr;

  // Fault filters use the same qualified/generic syntax as declarations.
  // Only the immediate `as` spelling denotes an opaque catch-all binding.
  if (!check(token_kind::as_kw))
    fault_type = parse_type_name();

  // Expect 'as'
  if (!match(token_kind::as_kw)) {
    add_error(peek(), "Expected 'as' after fault type");
    return nullptr;
  }

  // Variable name
  if (!check(token_kind::identifier)) {
    add_error(peek(), "Expected variable name after 'as'");
    return nullptr;
  }
  variable = advance().lexeme;

  // Optional guard: when <condition>
  if (match(token_kind::when_kw)) {
    guard = parse_expression();
    if (!guard) {
      add_error(peek(), "Expected condition after 'when'");
    }
  }

  // Parse handler body (statements until next fault/recover/end)
  auto body = std::make_shared<ast_block_stmt>(fault_tok.line);

  while (!is_at_end()) {
    // Stop at next fault, recover, or end attempt
    if (check(token_kind::fault_kw) || check(token_kind::recover_kw) ||
        (check(token_kind::end_kw) && pos_ + 1 < tokens_.size() &&
         tokens_[pos_ + 1].kind == token_kind::attempt_kw)) {
      break;
    }

    auto stmt = parse_statement();
    if (stmt) {
      body->statements.push_back(stmt);
    } else {
      synchronize();
    }
  }

  return std::make_shared<ast_fault_handler>(fault_type, variable, guard, body,
                                             fault_tok.line);
}

// Parse attempt statement:
//   attempt
//     <body>
//   [fault [Type] as var [when cond]
//     <handler>]+
//   [recover
//     <cleanup>]
//   end attempt
std::shared_ptr<ast_stmt> parser::parse_attempt_statement() {
  // 'attempt' already consumed
  token attempt_tok = previous();

  // Parse attempt body
  auto body = std::make_shared<ast_block_stmt>(attempt_tok.line);

  while (!is_at_end()) {
    // Stop at fault, recover, or end attempt
    if (check(token_kind::fault_kw) || check(token_kind::recover_kw) ||
        (check(token_kind::end_kw) && pos_ + 1 < tokens_.size() &&
         tokens_[pos_ + 1].kind == token_kind::attempt_kw)) {
      break;
    }

    auto stmt = parse_statement();
    if (stmt) {
      body->statements.push_back(stmt);
    } else {
      synchronize();
    }
  }

  // Parse fault handlers (zero or more)
  std::vector<std::shared_ptr<ast_fault_handler>> handlers;
  while (match(token_kind::fault_kw)) {
    auto handler = parse_fault_handler();
    if (handler) {
      handlers.push_back(handler);
    }
  }

  // Parse optional recover block
  std::shared_ptr<ast_block_stmt> recover_block = nullptr;
  if (match(token_kind::recover_kw)) {
    recover_block = std::make_shared<ast_block_stmt>(previous().line);

    while (!is_at_end()) {
      // Stop at end attempt
      if (check(token_kind::end_kw) && pos_ + 1 < tokens_.size() &&
          tokens_[pos_ + 1].kind == token_kind::attempt_kw) {
        break;
      }

      auto stmt = parse_statement();
      if (stmt) {
        recover_block->statements.push_back(stmt);
      } else {
        synchronize();
      }
    }
  }

  // Expect 'end attempt'
  consume(token_kind::end_kw, "Expected 'end' to close attempt block");
  if (!match(token_kind::attempt_kw)) {
    ny::diag_debug("[parser] Expected attempt after end; got '" +
                   peek().lexeme + "'");
    add_error(peek(), "Expected 'attempt' after 'end'");
  }

  return std::make_shared<ast_attempt_stmt>(body, handlers, recover_block,
                                            attempt_tok.line);
}

// =============================================================================
// Intrinsic Assertions: assert.method(args)
// =============================================================================

// Parse assert expression: assert.method(args)
std::shared_ptr<ast_expr> parser::parse_assert_expr() {
  // 'assert' already consumed
  token assert_tok = previous();
  int line = assert_tok.line;

  // Expect dot after 'assert'
  if (!match(token_kind::dot)) {
    add_error(peek(), "Expected '.' after 'assert'");
    return nullptr;
  }

  // Parse method name
  std::string method;
  if (check(token_kind::identifier) || check(token_kind::true_kw) ||
      check(token_kind::false_kw)) {
    method = advance().lexeme;
  } else {
    add_error(peek(), "Expected assertion method name after 'assert.'");
    return nullptr;
  }

  // Expect '(' for arguments
  if (!match(token_kind::left_paren)) {
    add_error(peek(), "Expected '(' after assertion method name");
    return nullptr;
  }

  // Parse argument list
  std::vector<ast_expr_ptr> args;
  while (!check(token_kind::right_paren) && !is_at_end()) {
    auto arg = parse_expression();
    if (arg) {
      args.push_back(arg);
    } else {
      add_error(peek(), "Expected expression in assertion arguments");
      synchronize();
      break;
    }

    if (!match(token_kind::comma)) {
      break;
    }
  }

  // Expect ')'
  if (!match(token_kind::right_paren)) {
    add_error(peek(), "Expected ')' after assertion arguments");
    return nullptr;
  }

  return std::make_shared<ast_assert_expr>(method, std::move(args), line);
}

std::shared_ptr<ast_expr> parser::parse_hotreload_expr() {
  // 'hotreload' already consumed
  token hotreload_tok = previous();
  int line = hotreload_tok.line;

  // Expect dot after 'hotreload'
  if (!match(token_kind::dot)) {
    add_error(peek(), "Expected '.' after 'hotreload'");
    return nullptr;
  }

  // Parse method name
  if (!check(token_kind::identifier)) {
    add_error(peek(), "Expected hot reload method name after 'hotreload.'");
    return nullptr;
  }

  std::string method = advance().lexeme;

  // Expect '(' for arguments
  if (!match(token_kind::left_paren)) {
    add_error(peek(), "Expected '(' after hot reload method name");
    return nullptr;
  }

  // Parse argument list
  std::vector<ast_expr_ptr> args;
  while (!check(token_kind::right_paren) && !is_at_end()) {
    auto arg = parse_expression();
    if (arg) {
      args.push_back(arg);
    } else {
      add_error(peek(), "Expected expression in hot reload arguments");
      synchronize();
      break;
    }

    if (!match(token_kind::comma)) {
      break;
    }
  }

  // Expect ')'
  if (!match(token_kind::right_paren)) {
    add_error(peek(), "Expected ')' after hot reload arguments");
    return nullptr;
  }

  return std::make_shared<ast_hotreload_expr>(method, std::move(args), line);
}

// ----------------------------
// Ny test-block parsing
// ----------------------------
std::shared_ptr<ast_test_block> parser::parse_test_block() {
  test_type type = test_type::unit;
  std::vector<ast_annotation> generators;

  if (previous().kind == token_kind::test_kw) {
    type = test_type::unit;
  } else if (previous().kind == token_kind::example_kw) {
    type = test_type::example;
  } else if (previous().kind == token_kind::property_kw) {
    type = test_type::property;
  }

  // Optional description string
  std::string description;
  if (check(token_kind::string_literal)) {
    description = advance().lexeme;
  }

  std::shared_ptr<ast_block_stmt> test_body;

  if (check(token_kind::left_brace)) {
    // C-style block: { ... }
    // parse_block_statement consumes the braces
    test_body =
        std::dynamic_pointer_cast<ast_block_stmt>(parse_block_statement());
  } else {
    // Ruby-style block: parse statements until `end`
    test_body = std::make_shared<ast_block_stmt>(previous().line);
    while (!check(token_kind::end_kw) && !is_at_end()) {
      test_body->statements.push_back(parse_statement());
    }
    consume(token_kind::end_kw, "Expected 'end' to close test block");
    // Optional `test`, `example`, `property` after `end`
    if (check(token_kind::test_kw) || check(token_kind::example_kw) ||
        check(token_kind::property_kw)) {
      advance();
    }
  }

  auto block = std::make_shared<ast_test_block>(description, test_body, type,
                                                previous().line);
  block->generators = generators;
  return block;
}

} // namespace ny
