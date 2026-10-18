#ifndef NY_COMPILER_NUMERIC_LITERAL_HPP
#define NY_COMPILER_NUMERIC_LITERAL_HPP

#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace ny {

// Hexadecimal integer digits legitimately include e/E.  Test the radix prefix
// before using exponent characters to distinguish decimal floating literals.
inline bool has_hexadecimal_integer_prefix(std::string_view spelling) {
  std::size_t prefix = 0;
  if (!spelling.empty() && (spelling.front() == '+' || spelling.front() == '-'))
    prefix = 1;
  return spelling.size() >= prefix + 2 && spelling[prefix] == '0' &&
         (spelling[prefix + 1] == 'x' || spelling[prefix + 1] == 'X');
}

inline bool is_floating_literal_spelling(std::string_view spelling) {
  return !has_hexadecimal_integer_prefix(spelling) &&
         spelling.find_first_of(".eE") != std::string_view::npos;
}

struct integer_literal_info {
  std::uint64_t magnitude = 0;
  bool negative = false;
  bool hexadecimal = false;

  const char *inferred_type() const {
    // Preserve hexadecimal masks' historical 32-bit arithmetic contract.
    const std::uint64_t narrow_limit = hexadecimal ? UINT32_MAX
        : negative ? std::uint64_t(INT32_MAX) + 1 : INT32_MAX;
    return magnitude <= narrow_limit ? "i32" : "i64";
  }
};

// Parse a source integer without overflowing a host signed integer. The one
// extra positive magnitude is permitted only while recognizing unary minimum
// literals; it must never reach a PUSH_CONST as a positive signed word.
inline const char *parse_integer_literal(std::string_view spelling,
                                        integer_literal_info &result,
                                        bool allow_minimum_magnitude = false) {
  result = {};
  if (!spelling.empty() && (spelling.front() == '+' || spelling.front() == '-')) {
    result.negative = spelling.front() == '-';
    spelling.remove_prefix(1);
  }
  result.hexadecimal = has_hexadecimal_integer_prefix(spelling);
  if (result.hexadecimal) spelling.remove_prefix(2);
  if (spelling.empty()) return "integer literal requires digits";
  const auto converted = std::from_chars(spelling.data(),
      spelling.data() + spelling.size(), result.magnitude,
      result.hexadecimal ? 16 : 10);
  if (converted.ec == std::errc::invalid_argument ||
      converted.ptr != spelling.data() + spelling.size())
    return "invalid character in integer literal";
  const std::uint64_t limit = std::uint64_t(INT64_MAX) +
      (result.negative || allow_minimum_magnitude ? 1 : 0);
  if (converted.ec == std::errc::result_out_of_range || result.magnitude > limit)
    return "integer literal is outside the representable signed i64 range";
  return nullptr;
}

inline bool is_integer_minimum_magnitude(std::string_view spelling) {
  integer_literal_info info;
  return !parse_integer_literal(spelling, info, true) && !info.negative &&
         info.magnitude == std::uint64_t(INT64_MAX) + 1;
}

// Fold only the two signed boundaries. Other negations remain ordinary typed
// arithmetic, including hexadecimal i32 masks and double-negation wrapping.
inline std::optional<std::string>
negated_integer_boundary(std::string_view spelling) {
  integer_literal_info info;
  if (parse_integer_literal(spelling, info, true) || info.negative) return {};
  if ((!info.hexadecimal && info.magnitude == std::uint64_t(INT32_MAX) + 1) ||
      info.magnitude == std::uint64_t(INT64_MAX) + 1)
    return "-" + std::string(spelling);
  return {};
}

// Source literals and textual/binary NIL must agree on both spelling and
// rounding. stod is locale-sensitive and rejects representable subnormals on
// platforms whose strtod sets ERANGE for them; from_chars does neither.
// Return a stable diagnostic rather than throwing through compiler/editor APIs.
inline const char *parse_floating_literal(std::string_view spelling,
                                         double &value) {
  const auto digit = [](char c) { return c >= '0' && c <= '9'; };
  std::size_t cursor = 0;
  bool negative = false;
  if (!spelling.empty() && (spelling[0] == '+' || spelling[0] == '-')) {
    negative = spelling[0] == '-';
    ++cursor;
  }
  bool any_digit = false;
  bool nonzero = false;
  while (cursor < spelling.size() && digit(spelling[cursor])) {
    any_digit = true;
    nonzero |= spelling[cursor++] != '0';
  }
  if (cursor < spelling.size() && spelling[cursor] == '.') {
    ++cursor;
    while (cursor < spelling.size() && digit(spelling[cursor])) {
      any_digit = true;
      nonzero |= spelling[cursor++] != '0';
    }
  }
  if (!any_digit)
    return "floating-point literal requires decimal digits";
  if (cursor < spelling.size() &&
      (spelling[cursor] == 'e' || spelling[cursor] == 'E')) {
    ++cursor;
    if (cursor < spelling.size() &&
        (spelling[cursor] == '+' || spelling[cursor] == '-'))
      ++cursor;
    const auto exponent_start = cursor;
    while (cursor < spelling.size() && digit(spelling[cursor]))
      ++cursor;
    if (cursor == exponent_start)
      return "floating-point exponent requires at least one digit";
  }
  if (cursor != spelling.size())
    return "invalid character in floating-point literal";

  // Zero remains exactly representable regardless of exponent length/magnitude.
  if (!nonzero) {
    value = negative ? -0.0 : 0.0;
    return nullptr;
  }
  if (spelling.front() == '+') spelling.remove_prefix(1);
  const auto converted = std::from_chars(spelling.data(),
      spelling.data() + spelling.size(), value, std::chars_format::general);
  if (converted.ec == std::errc::result_out_of_range ||
      (converted.ec == std::errc{} && (!std::isfinite(value) || value == 0.0)))
    return "floating-point literal is outside the representable f64 range "
           "(overflow or underflow to zero)";
  if (converted.ec != std::errc{} ||
      converted.ptr != spelling.data() + spelling.size())
    return "invalid floating-point literal";
  return nullptr;
}

} // namespace ny

#endif // NY_COMPILER_NUMERIC_LITERAL_HPP
