#include "pattern_syntax.hpp"

#include <algorithm>
#include <stdexcept>

namespace ny::patterns {
namespace {
struct prose_error { std::string message; std::size_t token; };

class prose_parser {
  const std::vector<prose_token> &tokens;
  std::size_t pos = 0;
  std::size_t depth = 0;

  void separators() {
    while (pos < tokens.size() && tokens[pos].separator) ++pos;
  }
  bool at(const char *word) {
    separators();
    return pos < tokens.size() && !tokens[pos].literal && !tokens[pos].quoted_name &&
        tokens[pos].text == word;
  }
  bool take(const char *word) {
    if (!at(word)) return false;
    ++pos;
    return true;
  }
  [[noreturn]] void fail(const std::string &message) const {
    throw prose_error{message, pos};
  }
  void need(const char *word) {
    if (!take(word)) fail(std::string("Expected '") + word + "' in text pattern");
  }
  std::string literal() {
    separators();
    if (pos < tokens.size() && tokens[pos].literal) return tokens[pos++].text;
    if (take("newline")) return "\n";
    if (take("tab")) return "\t";
    if (take("quote")) return "\"";
    fail("Expected a text literal, newline, tab or quote in text pattern");
  }
  std::string count() {
    separators();
    if (pos == tokens.size() || tokens[pos].literal || tokens[pos].text.empty())
      fail("Expected a nonnegative decimal repetition count");
    const auto value = tokens[pos].text;
    if (!std::all_of(value.begin(), value.end(),
        [](char c) { return c >= '0' && c <= '9'; }))
      fail("Expected a nonnegative decimal repetition count (at most 1000)");
    unsigned count = 0;
    for (const auto digit : value) {
      if (count > (1000U - static_cast<unsigned>(digit - '0')) / 10U)
        fail("Pattern repetition count exceeds 1000");
      count = count * 10U + static_cast<unsigned>(digit - '0');
    }
    ++pos;
    return std::to_string(count);
  }
  static std::string escape(const std::string &value) {
    std::string result;
    for (char c : value) {
      if (std::string("\\.^$|?*+()[]{}-").find(c) != std::string::npos)
        result += '\\';
      result += c;
    }
    return result;
  }
  std::string atom() {
    if (++depth > 64) fail("Text pattern nesting exceeds 64 levels");
    std::string result;
    if (take("literal")) {
      result = "(?:" + escape(literal()) + ")";
    } else if (take("begin")) {
      need("pattern");
      result = "(?:" + body() + ")";
    } else if (take("capture")) {
      separators();
      if (pos == tokens.size() || tokens[pos].literal)
        fail("Expected a capture name");
      auto name = tokens[pos++].text;
      std::replace(name.begin(), name.end(), '-', '_');
      const auto letter = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
      };
      if (name.empty() || name.size() > 64 || !letter(name.front()) ||
          !std::all_of(name.begin(), name.end(), [&](char c) {
            return letter(c) || (c >= '0' && c <= '9');
          }))
        fail("Capture name must be an ASCII identifier of at most 64 bytes");
      need("matching");
      result = "(?<" + name + ">" + atom() + ")";
    } else if (take("require")) {
      need("next");
      const bool negative = take("not");
      need("to"); need("match");
      result = std::string(negative ? "(?!" : "(?=") + atom() + ")";
    } else if (take("one")) {
      need("or"); need("more");
      result = "(?:" + atom() + ")+";
    } else if (take("zero")) {
      need("or"); need("more");
      result = "(?:" + atom() + ")*";
    } else if (take("optional")) {
      result = "(?:" + atom() + ")?";
    } else if (take("exactly")) {
      const auto n = count();
      result = "(?:" + atom() + "){" + n + "}";
    } else if (take("between")) {
      const auto low = count(); need("and"); const auto high = count();
      if (std::stoul(low) > std::stoul(high)) fail("Pattern repetition bounds are reversed");
      result = "(?:" + atom() + "){" + low + "," + high + "}";
    } else if (take("fewest")) {
      // Prefix scopes the lazy modifier to one explicit repetition phrase.
      if (!(at("one") || at("zero") || at("optional") || at("between")))
        fail("'fewest' requires one or more, zero or more, optional, or between");
      result = atom() + "?";
    } else if (take("ascii")) {
      if (take("digit") || take("digits")) result = "[0-9]";
      else if (take("letter") || take("letters")) result = "[A-Za-z]";
      else if (take("whitespace")) result = "\\s";
      else fail("Expected ascii digits, letters or whitespace");
    } else if (take("any")) {
      need("character"); result = "[\\s\\S]";
    } else if (take("character")) {
      need("from"); const auto low = literal(); need("through"); const auto high = literal();
      if (low.size() != 1 || high.size() != 1 ||
          static_cast<unsigned char>(low[0]) > static_cast<unsigned char>(high[0]))
        fail("Character range requires ordered single-byte endpoints");
      result = "[" + escape(low) + "-" + escape(high) + "]";
    } else if (take("start")) {
      need("of"); need("input"); result = "^";
    } else if (take("end")) {
      need("of"); need("input"); result = "$";
    } else if (take("either")) {
      // Each arm is one atom. Use begin/end pattern for a sequence arm.
      result = "(?:" + atom(); need("or"); result += "|" + atom();
      while (take("or")) result += "|" + atom();
      result += ")";
    } else {
      fail("Expected a text pattern phrase (literal, capture, repetition, character class or lookahead)");
    }
    --depth;
    return result;
  }
  std::string body() {
    std::string result;
    while (true) {
      separators();
      if (pos >= tokens.size()) fail("Expected 'end pattern'");
      if (at("end")) {
        const auto saved = pos++;
        if (take("pattern")) return result;
        pos = saved;
      }
      result += atom();
      if (result.size() > 32768) fail("Text pattern source exceeds 32768 bytes");
    }
  }
public:
  explicit prose_parser(const std::vector<prose_token> &input) : tokens(input) {}
  prose_result parse() {
    try {
      if (tokens.size() > 16384) fail("Text pattern exceeds 16384 tokens");
      auto source = body();
      separators();
      if (pos != tokens.size()) fail("Unexpected tokens after 'end pattern'");
      return {std::move(source), {}, 0};
    } catch (const prose_error &error) {
      return {{}, error.message, error.token};
    }
  }
};
} // namespace
prose_result parse_prose(const std::vector<prose_token> &tokens) {
  return prose_parser(tokens).parse();
}
} // namespace ny::patterns
