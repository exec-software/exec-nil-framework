#include "lexer.hpp"
#include "formatted_text.hpp"
#include <algorithm>
#include <cstdlib> // for getenv
#include <iostream>

namespace ny {

static token make_token(token_kind kind, const std::string &lexeme, int line) {
  return token{kind, lexeme, line};
}

std::vector<token> lexer::scan_tokens() {
  std::vector<token> tokens;

  // Skip UTF-8 BOM (\xEF\xBB\xBF) if present at the start of the file
  if (src.size() >= 3 &&
      (unsigned char)src[0] == 0xEF &&
      (unsigned char)src[1] == 0xBB &&
      (unsigned char)src[2] == 0xBF) {
    current += 3;
  }

  // Optional runtime lexical debug dump. If environment variable
  // NY_LEX_DUMP is set (non-empty), the lexer will emit each token it
  // produces to stderr as a single-line record: "TOKEN <line> <kind> <lexeme>"
  // This is temporary instrumentation to diagnose stdlib parse issues and
  // will be removed once the root cause is fixed.
  bool lex_dump = false;
  if (const char* e = std::getenv("NY_LEX_DUMP")) {
    if (e && e[0] != '\0')
      lex_dump = true;
  }

  while (true) {
    skip_whitespace(tokens);
    start = current;

    if (is_at_end()) {
      break;
    }

    char c = advance();
    const size_t emitted_before = tokens.size();

    switch (c) {
    case '$':
      if (peek() == '"' && peek_next() == '"' && current + 2 < src.size() &&
          src[current + 2] == '"') {
        formatted_block(tokens);
        continue; // Every emitted token already has its original range.
      }
      if (peek() == '"') {
        formatted_string(tokens);
        continue; // Every emitted token already has its original range.
      }
      tokens.push_back(make_token(token_kind::invalid, "$", line));
      break;
    case '(':
      tokens.push_back(make_token(token_kind::left_paren, "(", line));
      if (lex_dump) std::cerr << "TOKEN " << line << " left_paren (" << "(" << ")\n";
      break;
    case ')':
      tokens.push_back(make_token(token_kind::right_paren, ")", line));
      if (lex_dump) std::cerr << "TOKEN " << line << " right_paren ) )\n";
      break;
    case '{':
      tokens.push_back(make_token(token_kind::left_brace, "{", line));
      break;
    case '}':
      tokens.push_back(make_token(token_kind::right_brace, "}", line));
      break;
    case '[':
      tokens.push_back(make_token(token_kind::left_bracket, "[", line));
      break;
    case ']':
      tokens.push_back(make_token(token_kind::right_bracket, "]", line));
      break;
    case ',':
      tokens.push_back(make_token(token_kind::comma, ",", line));
      break;
    case ':':
      // If next char is also ':' then this is a double-colon (`::`) operator,
      // commonly used for path qualification (e.g., `module::type`).
      if (peek() == ':') {
        // Consume the second colon and emit a double_colon token
        advance();
        tokens.push_back(make_token(token_kind::double_colon, "::", line));
        if (lex_dump) std::cerr << "TOKEN " << line << " double_colon ::\n";
      } else {
        tokens.push_back(make_token(token_kind::colon, ":", line));
        if (lex_dump) std::cerr << "TOKEN " << line << " colon :\n";
      }
      break;
    case ';':
      tokens.push_back(make_token(token_kind::semicolon, ";", line));
      break;
    case '.':
      if (peek() == '.') {
        advance(); // consume second '.'
        tokens.push_back(make_token(token_kind::double_dot, "..", line));
      } else {
        tokens.push_back(make_token(token_kind::dot, ".", line));
      }
      break;
    case '+':
      tokens.push_back(make_token(token_kind::plus, "+", line));
      break;
    case '-':
      // Arrow operator '->' (commonly used for match arms and type annotations)
      if (peek() == '>') {
        // consume '>' and emit an arrow token
        advance();
        tokens.push_back(make_token(token_kind::arrow, "->", line));
      } else {
        tokens.push_back(make_token(token_kind::minus, "-", line));
      }
      break;
    case '*':
      tokens.push_back(make_token(token_kind::star, "*", line));
      break;
    case '@':
      // Annotation tokens such as @inline, @deprecated; lex the identifier
      // following the '@' and emit an annotation token. We intentionally only
      // lex the annotation name here; arguments, parentheses, and further
      // tokens are parsed by the parser or preprocessor stage.
      tokens.push_back(annotation());
      if (lex_dump) std::cerr << "TOKEN " << line << " at_sign @\n";
      break;
    case '/':
      // support `//` comments inline (should already be skipped by
      // skip_whitespace)
      if (peek() == '/') {
        // consume rest of line and continue (skip)
        while (!is_at_end() && peek() != '\n')
          advance();
      } else {
        tokens.push_back(make_token(token_kind::slash, "/", line));
        if (lex_dump) std::cerr << "TOKEN " << line << " slash /\n";
      }
      break;
    case '=':
      if (match('=')) {
        tokens.push_back(make_token(token_kind::double_equal, "==", line));
        if (lex_dump) std::cerr << "TOKEN " << line << " double_equal ==\n";
      } else {
        tokens.push_back(make_token(token_kind::equal, "=", line));
        if (lex_dump) std::cerr << "TOKEN " << line << " equal =\n";
      }
      break;
    case '!':
      if (match('=')) {
        tokens.push_back(make_token(token_kind::not_equal, "!=", line));
        if (lex_dump) std::cerr << "TOKEN " << line << " not_equal !=\n";
      } else {
        // standalone '!' is treated as invalid for now
        tokens.push_back(make_token(token_kind::invalid, "!", line));
      }
      break;
    case '<':
      if (match('<')) { tokens.push_back(make_token(token_kind::less_less, "<<", line)); break; }
      if (match('=')) {
        tokens.push_back(make_token(token_kind::less_equal, "<=", line));
        if (lex_dump) std::cerr << "TOKEN " << line << " less_equal <=\n";
      } else {
        tokens.push_back(make_token(token_kind::less, "<", line));
        if (lex_dump) std::cerr << "TOKEN " << line << " less <\n";
      }
      break;
    case '>':
      if (match('>')) { tokens.push_back(make_token(token_kind::greater_greater, ">>", line)); break; }
      if (match('=')) {
        tokens.push_back(make_token(token_kind::greater_equal, ">=", line));
        if (lex_dump) std::cerr << "TOKEN " << line << " greater_equal >=\n";
      } else {
        tokens.push_back(make_token(token_kind::greater, ">", line));
        if (lex_dump) std::cerr << "TOKEN " << line << " greater >\n";
      }
      break;
    case '%':
      tokens.push_back(make_token(token_kind::percent, "%", line));
      break;
    case '&':
      if (match('&')) {
        tokens.push_back(make_token(token_kind::amp_amp, "&&", line));
        if (lex_dump) std::cerr << "TOKEN " << line << " amp_amp &&\n";
      } else {
        tokens.push_back(make_token(token_kind::amp, "&", line));
        if (lex_dump) std::cerr << "TOKEN " << line << " amp &\n";
      }
      break;
    case '|':
      if (match('|')) {
        tokens.push_back(make_token(token_kind::pipe_pipe, "||", line));
        if (lex_dump) std::cerr << "TOKEN " << line << " pipe_pipe ||\n";
      } else {
        tokens.push_back(make_token(token_kind::pipe, "|", line));
        if (lex_dump) std::cerr << "TOKEN " << line << " pipe |\n";
      }
      break;
    case '?':
      tokens.push_back(make_token(token_kind::question, "?", line));
      break;
    case '^':
      tokens.push_back(make_token(token_kind::caret, "^", line));
      break;
    case '~':
      tokens.push_back(make_token(token_kind::tilde, "~", line));
      break;
    case '"':
      tokens.push_back(string());
      if (lex_dump) std::cerr << "TOKEN " << line << " string_literal \"...\"\n";
      break;
    case '\'':
      tokens.push_back(character());
      break;
    default:
      if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        // current points after the start char, so identifier() should use
        // start..current but it expects that `start` is already set accordingly
        // Move back one char so identifier() reconsiders the start char (it
        // expects `start` set and current pointing just after) However,
        // header's approach uses start set to current before calling advance()
        // so we're aligned. We already advanced; our identifier() will perform
        // loops based on peek.
        --current; // rewind so identifier() sees the full identifier
        token t = identifier();
        tokens.push_back(t);
        if (lex_dump) std::cerr << "TOKEN " << t.line << " " << static_cast<int>(t.kind) << " " << t.lexeme << "\n";
      } else if (std::isdigit(static_cast<unsigned char>(c))) {
        --current; // rewind for number parser
        token t = number(!tokens.empty() && tokens.back().kind == token_kind::dot);
        tokens.push_back(t);
        if (lex_dump) std::cerr << "TOKEN " << t.line << " number_literal " << t.lexeme << "\n";
      } else {
        // unknown/invalid character
        std::string txt(1, c);
        tokens.push_back(make_token(token_kind::invalid, txt, line));
        if (lex_dump) std::cerr << "TOKEN " << line << " invalid " << txt << "\n";
      }
      break;
    } // switch
    if (tokens.size() != emitted_before) {
      tokens.back().offset = start;
      tokens.back().length = current - start;
    }
  } // while

  tokens.push_back(make_token(token_kind::eof, "", line));
  tokens.back().offset = current;
  if (lex_dump) std::cerr << "TOKEN " << line << " eof \n";
  return tokens;
}

void lexer::skip_whitespace(std::vector<token> &tokens) {
  while (!is_at_end()) {
    char c = peek();
    switch (c) {
    case ' ':
    case '\t':
    case '\r':
      advance();
      break;
    case '\n':
      ++line;
      advance();
      break;
    case '/':
      if (peek_next() == '/') {
        // single-line comment: consume until end of line
        advance(); // consume first '/'
        advance(); // consume second '/'
        while (!is_at_end() && peek() != '\n')
          advance();
      } else if (peek_next() == '*') {
        // multi-line comment: consume until '*/'
        const auto opening = current;
        const int opening_line = line;
        bool closed = false;
        advance(); // consume '/'
        advance(); // consume '*'
        while (!is_at_end()) {
          if (peek() == '*' && peek_next() == '/') {
            advance(); // '*'
            advance(); // '/'
            closed = true;
            break;
          }
          if (peek() == '\n')
            ++line;
          advance();
        }
        if (!closed)
          tokens.push_back({token_kind::invalid,
              "Unterminated block comment; expected '*/'", opening_line,
              opening, 2});
      } else {
        // not a comment
        return;
      }
      break;
    case '#':
      // Support `#` single-line comments (e.g. "# Hello") and `###` block
      // comments (e.g., "### ... ###") used by Ny examples and documentation.
      // Determine if this is the triple-hash block comment or a single-line
      // hash.
      if ((current + 2 < src.size()) && src[current] == '#' &&
          src[current + 1] == '#' && src[current + 2] == '#') {
        const auto opening = current;
        const int opening_line = line;
        bool closed = false;
        // Consume the opening '###'
        advance(); // '#'
        advance(); // '#'
        advance(); // '#'
        // consume until closing '###' (allow newlines)
        while (!is_at_end()) {
          // If we find the closing sequence '###', consume it and break
          if (peek() == '#' && peek_next() == '#' &&
              (current + 2 < src.size() && src[current + 2] == '#')) {
            advance(); // '#'
            advance(); // '#'
            advance(); // '#'
            closed = true;
            break;
          }
          if (peek() == '\n')
            ++line;
          advance();
        }
        if (!closed)
          tokens.push_back({token_kind::invalid,
              "Unterminated block comment; expected '###'", opening_line,
              opening, 3});
      } else {
        // Single-line '#' comment
        advance(); // consume '#'
        while (!is_at_end() && peek() != '\n')
          advance();
      }
      break;
    default:
      return;
    } // switch
  } // while
}

token lexer::identifier() {
  while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') {
    advance();
  }
  std::string text = src.substr(start, current - start);

  // Keyword mapping
  token_kind kind = token_kind::identifier;
  if (text == "function" || text == "fun")
    kind = token_kind::function_kw;
  else if (text == "let")
    kind = token_kind::let_kw;
  else if (text == "const")
    kind = token_kind::const_kw;
  else if (text == "if")
    kind = token_kind::if_kw;
  else if (text == "else")
    kind = token_kind::else_kw;
  else if (text == "elseif")
    kind = token_kind::elseif_kw;
  else if (text == "then")
    kind = token_kind::then_kw;
  else if (text == "return")
    kind = token_kind::return_kw;
  else if (text == "println")
    kind = token_kind::println_kw;
  else if (text == "while")
    kind = token_kind::while_kw;
  else if (text == "where")
    kind = token_kind::where_kw;
  else if (text == "for")
    kind = token_kind::for_kw;
  else if (text == "in")
    kind = token_kind::in_kw;
  else if (text == "break")
    kind = token_kind::break_kw;
  else if (text == "continue")
    kind = token_kind::continue_kw;
  else if (text == "loop")
    kind = token_kind::loop_kw;
  else if (text == "do")
    kind = token_kind::do_kw;
  else if (text == "mut" || text == "mutable")
    kind = token_kind::mut_kw;
  else if (text == "import")
    kind = token_kind::import_kw;
  else if (text == "package")
    kind = token_kind::package_kw;
  else if (text == "module")
    kind = token_kind::module_kw;
  else if (text == "namespace")
    kind = token_kind::namespace_kw;
  else if (text == "using" || text == "use")
    kind = token_kind::using_kw;
  else if (text == "pub")
    kind = token_kind::pub_kw;
  else if (text == "type")
    kind = token_kind::type_kw;
  else if (text == "as")
    kind = token_kind::as_kw;
  else if (text == "end")
    kind = token_kind::end_kw;
  else if (text == "true" || text == "yes")
    kind = token_kind::true_kw;
  else if (text == "false" || text == "no")
    kind = token_kind::false_kw;
  else if (text == "match")
    kind = token_kind::match_kw;
  else if (text == "when")
    kind = token_kind::when_kw;
  else if (text == "struct")
    kind = token_kind::struct_kw;
  else if (text == "enum")
    kind = token_kind::enum_kw;
  else if (text == "trait")
    kind = token_kind::trait_kw;
  else if (text == "impl")
    kind = token_kind::impl_kw;
  else if (text == "allocate")
    kind = token_kind::allocate_kw;
  else if (text == "deallocate")
    kind = token_kind::deallocate_kw;
  else if (text == "ref" || text == "reference")
    kind = token_kind::ref_kw;
  else if (text == "fn")
    kind = token_kind::fn_kw;
  else if (text == "mod")
    kind = token_kind::mod_kw;
  else if (text == "class")
    kind = token_kind::class_kw;
  else if (text == "abstract")
    kind = token_kind::abstract_kw;
  else if (text == "virtual")
    kind = token_kind::virtual_kw;
  else if (text == "override")
    kind = token_kind::override_kw;
  else if (text == "inherits")
    kind = token_kind::inherits_kw;
  else if (text == "self")
    kind = token_kind::self_kw;
  else if (text == "attempt")
    kind = token_kind::attempt_kw;
  else if (text == "fault")
    kind = token_kind::fault_kw;
  else if (text == "recover")
    kind = token_kind::recover_kw;
  else if (text == "raise")
    kind = token_kind::raise_kw;
  else if (text == "assert")
    kind = token_kind::assert_kw;
  else if (text == "hotreload")
    kind = token_kind::hotreload_kw;
  else if (text == "not")
    kind = token_kind::not_kw;

  // Ny testing keywords
  else if (text == "test")
    kind = token_kind::test_kw;
  else if (text == "example")
    kind = token_kind::example_kw;
  else if (text == "property")
    kind = token_kind::property_kw;
  else if (text == "generate")
    kind = token_kind::generate_kw;

  return token{kind, text, line};
}

token lexer::number(bool tuple_position) {
  // Check for Hex literal 0x...
  if (!tuple_position && peek() == '0' && (peek_next() == 'x' || peek_next() == 'X')) {
     advance(); // 0
     advance(); // x
     while (std::isxdigit(static_cast<unsigned char>(peek()))) {
       advance();
     }
     // Keep an invalid adjacent identifier suffix in the numeric token so it
     // remains a source-literal error even when type-check errors are ignored.
     while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')
       advance();
     std::string text = src.substr(start, current - start);
     return token{token_kind::number_literal, text, line};
  }

  while (std::isdigit(static_cast<unsigned char>(peek())))
    advance();

  // Optional fractional part
  if (!tuple_position && peek() == '.' && std::isdigit(static_cast<unsigned char>(peek_next()))) {
    advance(); // consume '.'
    while (std::isdigit(static_cast<unsigned char>(peek())))
      advance();
  }

  // Keep an incomplete exponent in the number token so the parser can report
  // its actual cause instead of interpreting the e/E as an identifier.
  if (!tuple_position && (peek() == 'e' || peek() == 'E')) {
    advance();
    if (peek() == '+' || peek() == '-') advance();
    while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
  }

  std::string text = src.substr(start, current - start);
  return token{token_kind::number_literal, text, line};
}

token lexer::string() {
  // we assume the opening quote has already been consumed (advance() done)
  // Build the string value with escape sequence processing
  std::string value;

  while (!is_at_end() && peek() != '"') {
    if (peek() == '\n') {
      ++line;
      value += '\n';
      advance();
    } else if (peek() == '\\') {
      // Escape sequence - consume backslash
      advance();
      if (!is_at_end()) {
        char escaped = advance();
        switch (escaped) {
          case '"':  value += '"';  break;  // Escaped quote
          case '\\': value += '\\'; break;  // Escaped backslash
          case 'n':  value += '\n'; break;  // Newline
          case 't':  value += '\t'; break;  // Tab
          case 'r':  value += '\r'; break;  // Carriage return
          case 'v':  value += '\v'; break;  // Vertical tab
          case 'f':  value += '\f'; break;  // Form feed
          case '0':  value += '\0'; break;  // Null character
          default:
            // For unrecognized escapes, just include the character as-is
            value += escaped;
            break;
        }
      }
    } else {
      value += advance();
    }
  }

  if (is_at_end()) {
    return token{token_kind::invalid, "", line}; // unterminated string
  }

  // consume the closing quote
  advance();

  return token{token_kind::string_literal, value, line};
}

// A multiline formatted block: $""" on its own line, content, then a line
// whose only content is """. It is the Ny spelling of Matrix's
// 'formatted begin text' (NIL-0027 section 2) and keeps the same rules: the
// block's common indentation is removed from the literal text, deeper
// indentation is kept, leading and trailing blank lines are dropped, and
// nothing inside is escaped -- a backslash in a block is a backslash, which is
// what makes a path, a regular expression or embedded JSON writable here.
// Indentation removal applies to the literal segments only, so a hole keeps
// its original file coordinates and stays editable.
void lexer::formatted_block(std::vector<token> &tokens) {
  const int opening_line = line;
  const auto opening = start;
  current += 3; // the three opening quotes
  while (!is_at_end() && peek() != '\n') advance();
  if (!is_at_end()) advance();
  const auto header_end = current;

  const auto trimmed = [](const std::string &value) {
    const auto first = value.find_first_not_of(" \t\r");
    if (first == std::string::npos) return std::string();
    const auto last = value.find_last_not_of(" \t\r");
    return value.substr(first, last - first + 1);
  };

  struct block_line { std::size_t start; std::string text; };
  std::vector<block_line> lines;
  bool terminated = false;
  std::size_t terminator = current;
  while (!is_at_end()) {
    const auto begin = current;
    std::string raw;
    while (!is_at_end() && peek() != '\n') raw.push_back(advance());
    const bool had_newline = !is_at_end();
    if (had_newline) advance();
    if (!raw.empty() && raw.back() == '\r') raw.pop_back();
    // Ny is expression oriented, so the closing delimiter cannot own its line
    // the way Matrix's 'end text' does: println($"""...""") has to work.
    // The terminator is the first line whose content STARTS with three quotes,
    // and lexing resumes immediately after them.
    const auto first = raw.find_first_not_of(" \t");
    if (first != std::string::npos && raw.compare(first, 3, "\"\"\"") == 0) {
      terminated = true;
      terminator = begin + first;
      current = terminator + 3;
      break;
    }
    lines.push_back({begin, raw});
    if (!had_newline) break;
  }

  const formatted_source_positions positions(src, opening, current, opening_line);
  const auto emit = [&](token_kind kind, std::string value, std::size_t offset,
                        std::size_t length) {
    tokens.push_back(token{kind, std::move(value), positions.line(offset), offset, length});
  };
  if (!terminated) {
    emit(token_kind::invalid,
         "unterminated formatted block; close it with a line whose only content is \"\"\"",
         opening, 1);
    line = positions.line(current);
    return;
  }

  while (!lines.empty() && trimmed(lines.front().text).empty())
    lines.erase(lines.begin());
  while (!lines.empty() && trimmed(lines.back().text).empty()) lines.pop_back();

  std::size_t indent = std::string::npos;
  for (const auto &entry : lines) {
    const auto first = entry.text.find_first_not_of(" \t");
    if (first == std::string::npos) continue; // a blank line sets no indent
    indent = std::min(indent, first);
  }
  if (indent == std::string::npos) indent = 0;

  std::string content;
  std::vector<std::size_t> logical_starts, source_starts;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (i != 0) content.push_back('\n');
    const auto strip = std::min(indent, lines[i].text.size());
    logical_starts.push_back(content.size());
    source_starts.push_back(lines[i].start + strip);
    content += lines[i].text.substr(strip);
  }
  const auto source_offset = [&](std::size_t logical) -> std::size_t {
    if (logical_starts.empty()) return header_end;
    const auto row = static_cast<std::size_t>(
        std::upper_bound(logical_starts.begin(), logical_starts.end(), logical) -
        logical_starts.begin() - 1);
    return source_starts[row] + (logical - logical_starts[row]);
  };

  const auto scanned = scan_formatted_block(content, false, interpolation_depth_);
  if (!scanned.error.empty()) {
    emit(token_kind::invalid, scanned.error, source_offset(scanned.error_offset), 1);
    line = positions.line(current);
    return;
  }
  tokens.push_back(token{token_kind::formatted_string_begin, "$\"\"\"", opening_line,
                         opening, header_end - opening});
  for (const auto &piece : scanned.pieces) {
    const auto begin = source_offset(piece.start);
    const auto end = source_offset(piece.start + piece.length);
    if (!piece.expression) {
      emit(token_kind::formatted_text, piece.text, begin, end - begin);
      continue;
    }
    emit(token_kind::interpolation_begin, "{", begin - 1, 1);
    lexer expression(piece.text, interpolation_depth_ + 1);
    for (auto value : expression.scan_tokens()) {
      if (value.kind == token_kind::eof) continue;
      value.offset += begin;
      value.line += positions.line(begin) - 1;
      tokens.push_back(std::move(value));
    }
    emit(token_kind::interpolation_end, "}", end, 1);
  }
  emit(token_kind::formatted_string_end, "\"\"\"", terminator, 3);
  line = positions.line(current);
}

void lexer::formatted_string(std::vector<token> &tokens) {
  const auto quote = current;
  const int opening_line = line;
  const auto scanned = scan_formatted_text(src, quote, false, interpolation_depth_);
  const formatted_source_positions positions(src, quote, scanned.end, opening_line);
  const auto line_at = [&](std::size_t offset) {
    return positions.line(offset);
  };
  const auto emit = [&](token_kind kind, std::string value, std::size_t offset,
                        std::size_t length) {
    tokens.push_back(token{kind, std::move(value), line_at(offset), offset, length});
  };
  if (!scanned.error.empty()) {
    emit(token_kind::invalid, scanned.error, scanned.error_offset, 1);
  } else {
    tokens.push_back(token{token_kind::formatted_string_begin, "$\"", opening_line, start, 2});
    for (const auto &piece : scanned.pieces) {
      if (!piece.expression) {
        emit(token_kind::formatted_text, piece.text, piece.start, piece.length);
        continue;
      }
      emit(token_kind::interpolation_begin, "{", piece.start - 1, 1);
      lexer expression(piece.text, interpolation_depth_ + 1);
      for (auto value : expression.scan_tokens()) {
        if (value.kind == token_kind::eof) continue;
        value.offset += piece.start;
        value.line += line_at(piece.start) - 1;
        tokens.push_back(std::move(value));
      }
      emit(token_kind::interpolation_end, "}", piece.start + piece.length, 1);
    }
    emit(token_kind::formatted_string_end, "\"", scanned.end - 1, 1);
  }
  line = line_at(scanned.end);
  current = scanned.end;
}

token lexer::annotation() {
  // We assume the '@' character has already been consumed (advance() called),
  // and `start` points to the '@' position. We lex the annotation name only
  // (e.g., @inline, @deprecated, @extern); arguments (paren content) are
  // parsed by the parser or preprocessor. We also accept namespaced annotations
  // such as @ns::annot.
  //
  // Require at least one identifier character (alpha or underscore) immediately
  // after the '@' symbol. If the next character is not a valid identifier
  // start, return an `invalid` token for a bare '@' (and similar malformed
  // cases); this matches the unit tests expecting '@' to be invalid.
  if (is_at_end() ||
      !(std::isalpha(static_cast<unsigned char>(peek())) || peek() == '_')) {
    // Create an invalid token for cases like bare '@' or '@ ' or '@('
    return make_token(token_kind::invalid, "@", line);
  }
  while (!is_at_end()) {
    char p = peek();
    // Accept identifier characters
    if (std::isalnum(static_cast<unsigned char>(p)) || p == '_') {
      advance();
      continue;
    }
    // Accept '::' in namespaced annotation names.
    if (p == ':' && peek_next() == ':') {
      advance(); // first ':'
      advance(); // second ':'
      continue;
    }
    // Stop when encountering '(' or whitespace or any token delimiter
    break;
  }

  std::string text = src.substr(start, current - start);
  if (text.empty()) {
    // Something malformed like just '@' -- emit invalid token
    return make_token(token_kind::invalid, "@", line);
  }
  // Emit annotation token lexeme with the leading '@' included (convenient
  // for downstream processors).
  return token{token_kind::annotation, text, line};
}

token lexer::character() {
  // Opening quote already consumed
  if (is_at_end()) return make_token(token_kind::invalid, "'", line);

  char c = advance();
  // Handle escape? Minimal support for now
  if (c == '\\') {
     if (!is_at_end()) advance(); // consume escaped char
  }

  if (is_at_end() || peek() != '\'') {
      return make_token(token_kind::invalid, "Unterminated char", line);
  }
  advance(); // consume closing '

  std::string text = src.substr(start, current - start);
  return token{token_kind::char_literal, text, line};
}


// Force rebuild - 2025-12-28
} // namespace ny// Force rebuild
