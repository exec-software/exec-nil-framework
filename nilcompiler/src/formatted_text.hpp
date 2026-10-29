#ifndef NIL_COMPILER_FORMATTED_TEXT_HPP
#define NIL_COMPILER_FORMATTED_TEXT_HPP

#include <cctype>
#include <algorithm>
#include <string>
#include <vector>

namespace ny {

struct formatted_source_positions {
  std::vector<std::size_t> lines;
  int first_line, first_column;
  formatted_source_positions(const std::string &source, std::size_t begin,
                             std::size_t end, int line, int column = 1)
      : lines{begin}, first_line(line), first_column(column) {
    for (auto offset = begin; offset < end; ++offset)
      if (source[offset] == '\n') lines.push_back(offset + 1);
  }
  std::size_t index(std::size_t offset) const {
    return std::upper_bound(lines.begin(), lines.end(), offset) - lines.begin() - 1;
  }
  int line(std::size_t offset) const { return first_line + static_cast<int>(index(offset)); }
  int column(std::size_t offset) const {
    const auto row = index(offset);
    return static_cast<int>(offset - lines[row]) + (row ? 1 : first_column);
  }
};

// Text decoding never rewrites an expression. Its range always selects the
// original lexer input, so normal expression tokens retain editable spans.
struct formatted_text_piece {
  bool expression = false;
  std::string text;
  std::size_t start = 0;
  std::size_t length = 0;
  bool closed = true;
};

struct formatted_text_scan {
  std::vector<formatted_text_piece> pieces;
  std::size_t end = 0;
  std::size_t error_offset = 0;
  std::string error;
  bool incomplete = false;
};

inline bool formatted_name_byte(unsigned char c) {
  return std::isalnum(c) || c == '_' || c == '-' || c >= 128;
}

inline formatted_text_scan scan_formatted_text(const std::string &source,
                                                std::size_t quote,
                                                bool matrix,
                                                unsigned depth = 0);

inline std::size_t formatted_expression_end(const std::string &source,
                                           std::size_t begin, bool matrix,
                                           unsigned depth,
                                           formatted_text_scan &result) {
  std::size_t cursor = begin;
  unsigned braces = 1;
  const auto fail = [&](std::size_t at, const std::string &message, bool incomplete = false) {
    result.error_offset = at;
    result.error = message;
    result.incomplete = incomplete;
    return source.size();
  };
  while (cursor < source.size()) {
    const char c = source[cursor];
    if (c == '\n' || c == '\r')
      return fail(cursor, "interpolation expressions must stay on one line");
    if ((!matrix && source.compare(cursor, 2, "//") == 0) ||
        (!matrix && c == '#') ||
        (matrix && source.compare(cursor, 3, "!!!") == 0)) {
      if (!matrix && source.compare(cursor, 3, "###") == 0) {
        const auto end = source.find("###", cursor + 3);
        if (end == std::string::npos)
          return fail(cursor, "unterminated comment in interpolation", true);
        if (source.find_first_of("\r\n", cursor) < end)
          return fail(cursor, "interpolation expressions must stay on one line");
        cursor = end + 3;
      } else {
        return fail(cursor, "a line comment cannot close an interpolation; expected '}' before the comment");
      }
      continue;
    }
    if (!matrix && source.compare(cursor, 2, "/*") == 0) {
      const auto end = source.find("*/", cursor + 2);
      if (end == std::string::npos)
        return fail(cursor, "unterminated comment in interpolation", true);
      if (source.find_first_of("\r\n", cursor) < end)
        return fail(cursor, "interpolation expressions must stay on one line");
      cursor = end + 2;
      continue;
    }
    std::size_t nested_quote = std::string::npos;
    if (!matrix && c == '$' && cursor + 1 < source.size() && source[cursor + 1] == '"')
      nested_quote = cursor + 1;
    if (matrix && (cursor == begin || !formatted_name_byte(source[cursor - 1])) &&
        cursor + 9 <= source.size()) {
      std::string word = source.substr(cursor, 9);
      for (auto &letter : word)
        if (letter >= 'A' && letter <= 'Z') letter += 'a' - 'A';
      if (word == "formatted" &&
          (cursor + 9 == source.size() || !formatted_name_byte(source[cursor + 9]))) {
        auto next = cursor + 9;
        while (next < source.size() && (source[next] == ' ' || source[next] == '\t')) ++next;
        if (next < source.size() && source[next] == '"') nested_quote = next;
      }
    }
    if (nested_quote != std::string::npos) {
      auto nested = scan_formatted_text(source, nested_quote, matrix, depth);
      if (!nested.error.empty()) {
        result.error_offset = nested.error_offset;
        result.error = nested.error;
        result.incomplete = nested.incomplete;
        return source.size();
      }
      cursor = nested.end;
      continue;
    }
    if (c == '"' || (!matrix && c == '\'') || (matrix && c == '`')) {
      const auto opening = cursor++;
      bool closed = false;
      while (cursor < source.size()) {
        if (source[cursor] == '\n' || source[cursor] == '\r')
          return fail(cursor, "interpolation expressions must stay on one line");
        if (!matrix && source[cursor] == '\\') {
          if (cursor + 1 < source.size() &&
              (source[cursor + 1] == '\n' || source[cursor + 1] == '\r'))
            return fail(cursor + 1, "interpolation expressions must stay on one line");
          cursor += cursor + 1 < source.size() ? 2 : 1;
        } else if (source[cursor++] == c) {
          closed = true;
          break;
        }
      }
      if (!closed) return fail(opening, "unterminated literal in interpolation", true);
      continue;
    }
    if (c == '{') ++braces;
    if (c == '}' && --braces == 0) return cursor;
    ++cursor;
  }
  return fail(begin - 1, "unterminated interpolation; expected '}'", true);
}

inline formatted_text_scan scan_formatted_text(const std::string &source,
                                                std::size_t quote,
                                                bool matrix,
                                                unsigned depth) {
  formatted_text_scan result;
  result.end = source.size();
  const auto fail = [&](std::size_t at, const std::string &message, bool incomplete = false) {
    result.error_offset = at;
    result.error = message;
    result.incomplete = incomplete;
  };
  if (depth >= 64) {
    fail(quote, "interpolation nesting exceeds 64 levels");
    return result;
  }
  auto cursor = quote + 1;
  auto literal_start = cursor;
  std::string literal;
  const auto flush = [&]() {
    if (!literal.empty())
      result.pieces.push_back({false, std::move(literal), literal_start, cursor - literal_start});
    literal.clear();
  };
  while (cursor < source.size()) {
    const char c = source[cursor];
    if (c == '"') {
      flush();
      result.end = cursor + 1;
      return result;
    }
    if (c == '\n' || c == '\r') {
      fail(cursor, "formatted text must stay on one line");
      return result;
    }
    if (c == '{') {
      if (cursor + 1 < source.size() && source[cursor + 1] == '{') {
        literal += '{';
        cursor += 2;
        continue;
      }
      flush();
      const auto begin = cursor + 1;
      const auto end = formatted_expression_end(source, begin, matrix, depth + 1, result);
      if (!result.error.empty()) {
        // Preserve an unfinished expression for Matrix submission parsing.
        // Its normal lexer/parser can still reject a malformed earlier token.
        if (result.incomplete)
          result.pieces.push_back({true, source.substr(begin), begin,
                                   source.size() - begin, false});
        return result;
      }
      result.pieces.push_back({true, source.substr(begin, end - begin), begin, end - begin});
      cursor = end + 1;
      literal_start = cursor;
      continue;
    }
    if (c == '}') {
      if (cursor + 1 < source.size() && source[cursor + 1] == '}') {
        literal += '}';
        cursor += 2;
        continue;
      }
      fail(cursor, "unmatched '}' in formatted text; write '}}' for a literal brace");
      return result;
    }
    if (!matrix && c == '\\') {
      ++cursor;
      if (cursor == source.size()) break;
      if (source[cursor] == '\n' || source[cursor] == '\r') {
        fail(cursor, "formatted text must stay on one line");
        return result;
      }
      switch (source[cursor++]) {
      case 'n': literal += '\n'; break;
      case 't': literal += '\t'; break;
      case 'r': literal += '\r'; break;
      case 'v': literal += '\v'; break;
      case 'f': literal += '\f'; break;
      case '0': literal += '\0'; break;
      default: literal += source[cursor - 1]; break;
      }
    } else {
      literal += c;
      ++cursor;
    }
  }
  flush();
  fail(quote, "unterminated formatted text; expected a closing quote", true);
  return result;
}

} // namespace ny
#endif
