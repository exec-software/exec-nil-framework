#ifndef NY_COMPILER_NUMERIC_LITERAL_HPP
#define NY_COMPILER_NUMERIC_LITERAL_HPP

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

} // namespace ny

#endif // NY_COMPILER_NUMERIC_LITERAL_HPP
