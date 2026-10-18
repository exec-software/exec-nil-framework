#ifndef NIL_COMPILER_INTEGER_CONVERSION_HPP
#define NIL_COMPILER_INTEGER_CONVERSION_HPP

#include <cstdint>
#include <limits>
#include <string_view>

namespace ny::detail {

struct IntegerParseResult {
  bool ok = false;
  std::int64_t value = 0;
  const char *error = nullptr;
};

inline int ascii_digit_value(char character) noexcept {
  const unsigned char byte = static_cast<unsigned char>(character);
  if (byte >= '0' && byte <= '9') return byte - '0';
  if (byte >= 'a' && byte <= 'z') return byte - 'a' + 10;
  if (byte >= 'A' && byte <= 'Z') return byte - 'A' + 10;
  return -1;
}

// Runtime text conversion is distinct from source-literal inference. Consume
// the complete byte span, with no whitespace, separators, or implicit radix.
// Error reasons are static storage so native noexcept callers cannot allocate
// or throw while validating input. Each caller adds its public API prefix.
inline IntegerParseResult parse_signed_integer(std::string_view text, int base,
                                               bool accept_prefix) noexcept {
  if (base < 2 || base > 36)
    return {false, 0, "base must be between 2 and 36"};
  if (text.empty())
    return {false, 0, "value is empty"};

  std::size_t position = 0;
  bool negative = false;
  if (text[position] == '+' || text[position] == '-') {
    negative = text[position] == '-';
    ++position;
  }
  if (accept_prefix && position + 2 <= text.size() && text[position] == '0') {
    const char prefix = text[position + 1];
    if ((base == 16 && (prefix == 'x' || prefix == 'X')) ||
        (base == 2 && (prefix == 'b' || prefix == 'B')) ||
        (base == 8 && (prefix == 'o' || prefix == 'O')))
      position += 2;
  }
  if (position == text.size())
    return {false, 0, "value has no digits"};

  const std::uint64_t positive_limit =
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
  const std::uint64_t limit = negative ? positive_limit + 1U : positive_limit;
  std::uint64_t magnitude = 0;
  for (; position < text.size(); ++position) {
    const int digit = ascii_digit_value(text[position]);
    if (digit < 0 || digit >= base)
      return {false, 0, "value contains a digit outside the requested base"};
    if (magnitude > (limit - static_cast<std::uint64_t>(digit)) /
                        static_cast<std::uint64_t>(base))
      return {false, 0, "value is outside the i64 range"};
    magnitude = magnitude * static_cast<std::uint64_t>(base) +
                static_cast<std::uint64_t>(digit);
  }

  if (!negative)
    return {true, static_cast<std::int64_t>(magnitude), nullptr};
  if (magnitude == positive_limit + 1U)
    return {true, std::numeric_limits<std::int64_t>::min(), nullptr};
  return {true, -static_cast<std::int64_t>(magnitude), nullptr};
}

} // namespace ny::detail

#endif
