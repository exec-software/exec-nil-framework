#include "pattern_engine.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using ny::patterns::result;
using ny::patterns::status;
using span = std::pair<std::int64_t, std::int64_t>;
std::size_t checks = 0;

void require(bool condition, const std::string &message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}

void matched(std::string_view pattern, std::string_view input,
             std::vector<span> spans, std::int64_t start = 0, bool full = false) {
  const auto actual = ny::patterns::search(pattern, input, start, full);
  require(actual.code == status::match && actual.error.empty() && actual.spans == spans,
          "unexpected match/captures for pattern '" + std::string(pattern) +
          "': status " + std::to_string(static_cast<std::int64_t>(actual.code)) + " " + actual.error);
}

void missed(std::string_view pattern, std::string_view input,
            std::int64_t start = 0, bool full = false) {
  const auto actual = ny::patterns::search(pattern, input, start, full);
  require(actual.code == status::no_match && actual.spans.empty() && actual.error.empty(),
          "unexpected no-match result for pattern '" + std::string(pattern) + "': " + actual.error);
}

void rejected(std::string_view pattern, std::string_view input, status expected,
              std::string_view diagnostic, std::int64_t start = 0, bool full = false) {
  const auto actual = ny::patterns::search(pattern, input, start, full);
  require(actual.code == expected && actual.spans.empty() &&
              actual.error.find(diagnostic) != std::string::npos,
          "unexpected error result: " + actual.error);
}

void literals_and_spans() {
  matched("", "", {{0, 0}});
  matched("", "abc", {{3, 0}}, 3);
  matched("bc", "abcabc", {{1, 2}});
  matched("bc", "abcabc", {{4, 2}}, 2);
  missed("bc", "abcabc", 5);
  matched("abc", "abc", {{0, 3}}, 0, true);
  missed("abc", "abcd", 0, true);
  missed("abc", "xabc", 0, true);
  matched(R"(\.\^\$\|\?\*\+\(\)\[\]\{\}\\)", ".^$|?*+()[]{}\\", {{0, 14}}, 0, true);
  matched(R"(\n\r\t\f\v\x41)", "\n\r\t\f\vA", {{0, 6}}, 0, true);
  matched("^abc$", "abc", {{0, 3}});
  missed("^abc$", "abc\n");
  missed("^abc", "xabc");
  missed("^", "abc", 1);
  matched("$", "abc", {{3, 0}});
  matched("^$", "", {{0, 0}}, 0, true);
  missed(".", "\n");
  matched(".", "\r", {{0, 1}});
  matched(R"([\s\S])", "\n", {{0, 1}});
  // Text positions are bytes, not Unicode scalar or grapheme positions.
  matched(".", "\xC3\xA9", {{0, 1}});
  missed(".", "\xC3\xA9", 0, true);
  matched("..", "\xC3\xA9", {{0, 2}}, 0, true);
  matched("\xC3\xA9", "x\xC3\xA9y", {{1, 2}});
  matched(R"(\xFF)", std::string(1, static_cast<char>(0xff)), {{0, 1}});
  rejected("a", "a", status::invalid_offset, "start", -1);
  rejected("a", "a", status::invalid_offset, "start", 2);
  rejected("a", "a", status::invalid_offset, "start", std::numeric_limits<std::int64_t>::max());
  rejected("a", "ba", status::invalid_offset, "start zero", 1, true);
  rejected(".", std::string("a\0b", 3), status::error, "NUL");
}

void classes_and_repetitions() {
  matched("[a-zA-Z0-9_]+", "!!Ab_09!!", {{2, 5}});
  matched("[^a-c]+", "abcXYZ", {{3, 3}});
  matched("[]-]+", "]--]", {{0, 4}}, 0, true);
  matched("[-a]+", "-aa-", {{0, 4}}, 0, true);
  matched("[a-]+", "-aa-", {{0, 4}}, 0, true);
  matched("[^]]+", "xy]", {{0, 2}});
  matched(R"(\d+\D+)", "12ab", {{0, 4}}, 0, true);
  matched(R"(\w+\W+)", "a_2!?", {{0, 5}}, 0, true);
  matched(R"(\s+\S+)", " \t\r\n\v\ftext", {{0, 10}}, 0, true);
  missed(R"(\d)", "\xD9\xA1"); // Arabic-Indic digit is not an ASCII digit.
  missed(R"(\w)", "\xC3\xA9");
  matched("ab?", "a", {{0, 1}});
  matched("ab?", "ab", {{0, 2}});
  matched("ab??", "ab", {{0, 1}});
  matched("a*", "aaa", {{0, 3}});
  matched("a*?", "aaa", {{0, 0}});
  matched("a+", "aaa", {{0, 3}});
  matched("a+?", "aaa", {{0, 1}});
  matched("a{2}", "aaa", {{0, 2}});
  matched("a{1,3}", "aaaa", {{0, 3}});
  matched("a{1,3}?", "aaaa", {{0, 1}});
  matched("a{2,}", "aaaa", {{0, 4}});
  matched("a{2,}?", "aaaa", {{0, 2}});
  matched("a{0}", "aaa", {{0, 0}});
  missed("a{2,3}", "a");
  matched("a*a", "aaa", {{0, 3}}, 0, true);
  matched("a*?a", "aaa", {{0, 1}});
  matched("a*?a", "aaa", {{0, 3}}, 0, true);
  matched("a{0,3}?ab", "aaab", {{0, 4}}, 0, true);
  matched("a|ab", "ab", {{0, 1}});
  matched("a|ab", "ab", {{0, 2}}, 0, true);
  matched("ab|a", "ab", {{0, 2}});
  matched("(?:ab|a)c", "ac", {{0, 2}}, 0, true);
  matched("|a", "a", {{0, 0}});
  matched("a|", "b", {{0, 0}});
  matched("(?:)*", "abc", {{0, 0}});
  matched("(?:)+", "", {{0, 0}});
  matched("(?:){2,3}", "", {{0, 0}});
  matched("(?:|a)*b", "aab", {{0, 3}}, 0, true);
  matched("(?:a?)*b", "aaab", {{0, 4}}, 0, true);
  // Repeated use must not accumulate mutable matcher state in cached programs.
  matched("a*", std::string(10000, 'a'), {{0, 10000}}, 0, true);
  matched("a*", "b", {{0, 0}});
}

void captures_and_assertions() {
  matched("(ab)(c)?", "ab", {{0, 2}, {0, 2}, {-1, -1}});
  matched("(ab)(c)?", "abc", {{0, 3}, {0, 2}, {2, 1}});
  matched("()(a?)", "", {{0, 0}, {0, 0}, {0, 0}});
  matched("(a(b)|ac)", "ac", {{0, 2}, {0, 2}, {-1, -1}});
  matched("(a(b)?)+", "aba", {{0, 3}, {2, 1}, {1, 1}}, 0, true);
  matched("(a?)*", "aaa", {{0, 3}, {3, 0}});
  matched("(a?)*?", "aaa", {{0, 0}, {-1, -1}});
  matched("(a?){2}", "", {{0, 0}, {0, 0}});
  matched("(a|(b))*c", "abc", {{0, 3}, {1, 1}, {1, 1}}, 0, true);
  matched("(?<word>[A-Za-z]+)([0-9]+)?", "hello42", {{0, 7}, {0, 5}, {5, 2}}, 0, true);
  matched("(?<word>[A-Za-z]+)([0-9]+)?", "hello", {{0, 5}, {0, 5}, {-1, -1}}, 0, true);
  require(ny::patterns::group_index("(?<word>[A-Za-z]+)([0-9]+)?", "word") == 1, "named capture index");
  require(ny::patterns::group_index("(?<word>a)", "Word") == -1, "capture names are case-sensitive");
  require(ny::patterns::group_index("(?<word>a)", "missing") == -1, "missing named capture");
  require(ny::patterns::group_index("(?<word>a)", "") == -1, "empty capture name");
  require(ny::patterns::group_index("(?<word>a)", std::string("word\0", 5)) == -1, "NUL capture name");
  require(ny::patterns::group_index("(?<word>", "word") == -1, "invalid pattern has no named capture index");
  matched("a(?=b)", "ab", {{0, 1}});
  missed("a(?=b)", "ac");
  matched("a(?!b)", "ac", {{0, 1}});
  missed("a(?!b)", "ab");
  matched("(?=(ab|a))a", "ab", {{0, 1}, {0, 2}});
  matched("(?=(a|ab))ab", "ab", {{0, 2}, {0, 1}}, 0, true);
  matched("(?!(a))b", "b", {{0, 1}, {-1, -1}});
  matched("(a(?!(b))|ab)", "ab", {{0, 2}, {0, 2}, {-1, -1}}, 0, true);
  matched("(?=(a(?=b)))ab", "ab", {{0, 2}, {0, 1}}, 0, true);
  matched("(?!b)(?=(a))a", "a", {{0, 1}, {0, 1}}, 0, true);
  matched("(?=(?!b).).", "a", {{0, 1}}, 0, true);
  missed("(?=(?!b).).", "b");
  matched("(?=)", "", {{0, 0}});
  missed("(?!)", "abc");
  matched("a(?=$)", "a", {{0, 1}});
  missed("a(?=$)", "a\n");
  matched("(?<outer>a(?<inner>b))", "ab", {{0, 2}, {0, 2}, {1, 1}});
}

void invalid_patterns_and_limits() {
  const std::vector<std::string> invalid = {
      "(", ")", "[", "[]", "[a-", "[z-a]", "[\\d-a]", "[a-\\d]",
      "\\", "\\1", "(a)\\1", "\\0", "\\k<name>", "\\b", "\\B", "\\p{L}",
      "\\u0041", "\\q", "\\x", "\\x4", "\\xGG", "\\x00", "[\\x00]",
      "(?<=a)b", "(?<!a)b", "(?i)a", "(?i:a)", "(?>a)", "(?P<name>a)",
      "(?<1name>a)", "(?<na-me>a)", "(?<>a)", "(?<name>a)(?<name>b)",
      "*a", "+a", "?a", "{2}", "}", "a**", "a++", "a?+", "a{2,1}",
      "a{,2}", "a{2", "a{2,3", "^*", "$+", "(?=a)*", "(?!a)?", "[[:alpha:]]",
      std::string("a\0b", 3),
  };
  for (const auto &pattern : invalid) {
    require(!ny::patterns::validate(pattern).empty(), "invalid pattern validated successfully");
    rejected(pattern, "abc", status::invalid_pattern, "pattern byte");
  }
  require(ny::patterns::validate("a{0,1000}").empty(), "supported repetition maximum rejected");
  rejected("a{1001}", "", status::limit, "repetition count");
  rejected(std::string(ny::patterns::limits::pattern_bytes + 1, 'a'), "", status::limit, "pattern bytes");
  rejected(".", std::string(ny::patterns::limits::input_bytes + 1, 'a'), status::limit, "input bytes");
  rejected(std::string(33, '(') + "a" + std::string(33, ')'), "a", status::limit, "group nesting");
  std::string captures;
  for (int index = 0; index < 65; ++index) captures += "()";
  rejected(captures, "", status::limit, "capture groups");
  rejected("(?<" + std::string(65, 'x') + ">a)", "a", status::limit, "capture name bytes");
  rejected("(a{1000}){1000}", "", status::limit, "compiled instructions");
  rejected("(?:(?:){1000}){1000}", "", status::limit, "compile steps");
  rejected("(a*){300}", "", status::limit, "loop slots");
  rejected("^(a+)+$", std::string(28, 'a') + "!", status::limit, "execution steps");
  captures.clear();
  for (int index = 0; index < 64; ++index) captures += "()";
  rejected(captures + "a*", std::string(10000, 'a'), status::limit, "backtracking storage");
  matched("(?<fresh>a)", "a", {{0, 1}, {0, 1}});
}

void cache_reuse() {
  for (std::size_t index = 0; index < ny::patterns::limits::cache_entries * 3; ++index) {
    const auto literal = "entry" + std::to_string(index);
    matched("(?<value>" + literal + ")", literal,
            {{0, static_cast<std::int64_t>(literal.size())},
             {0, static_cast<std::int64_t>(literal.size())}}, 0, true);
  }
  matched("(?<value>entry0)", "entry0", {{0, 6}, {0, 6}}, 0, true);
  require(ny::patterns::group_index("(?<value>entry0)", "value") == 1,
          "cache eviction changed capture metadata");
  matched("(?<value>entry0)", "xentry0", {{1, 6}, {1, 6}});
  missed("(?<value>entry0)", "entry1");
}

} // namespace

int main() {
  static_assert(static_cast<std::int64_t>(status::no_match) == 0);
  static_assert(static_cast<std::int64_t>(status::match) == 1);
  static_assert(static_cast<std::int64_t>(status::invalid_pattern) == 2);
  static_assert(static_cast<std::int64_t>(status::invalid_offset) == 3);
  static_assert(static_cast<std::int64_t>(status::limit) == 4);
  static_assert(static_cast<std::int64_t>(status::error) == 5);
  try {
    literals_and_spans();
    classes_and_repetitions();
    captures_and_assertions();
    invalid_patterns_and_limits();
    cache_reuse();
    std::cout << "pattern engine: " << checks << " contracts passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
