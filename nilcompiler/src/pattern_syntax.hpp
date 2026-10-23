#ifndef NIL_PATTERN_SYNTAX_HPP
#define NIL_PATTERN_SYNTAX_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace ny::patterns {

// Frontends retain their own token spans and string rules. This deliberately
// small adapter keeps the prose grammar identical in Ny and Matrix.
struct prose_token {
  std::string text;
  bool literal = false;
  bool separator = false;
  bool quoted_name = false;
};
struct prose_result {
  std::string source;
  std::string error;
  std::size_t error_token = 0;
};

// Input starts after the declaration header and includes its 'end pattern'.
// Literal atoms are escaped; they never interpolate or inject regex syntax.
prose_result parse_prose(const std::vector<prose_token> &tokens);

} // namespace ny::patterns
#endif
