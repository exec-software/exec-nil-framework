#ifndef NIL_PATTERN_ENGINE_HPP
#define NIL_PATTERN_ENGINE_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ny::patterns {

// These values are part of the shared VM/native pattern-provider contract.
enum class status : std::int64_t {
  no_match = 0,
  match = 1,
  invalid_pattern = 2,
  invalid_offset = 3,
  limit = 4,
  error = 5,
};

struct limits {
  static constexpr std::size_t pattern_bytes = 4096;
  static constexpr std::size_t input_bytes = 1024 * 1024;
  static constexpr std::size_t nodes = 4096;
  static constexpr std::size_t nesting = 32;
  static constexpr std::size_t captures = 64;
  static constexpr std::size_t repetition = 1000;
  static constexpr std::size_t instructions = 16384;
  static constexpr std::size_t compile_steps = 100000;
  static constexpr std::size_t loop_slots = 256;
  static constexpr std::size_t execution_steps = 8000000;
  static constexpr std::size_t backtracking_bytes = 8 * 1024 * 1024;
  static constexpr std::size_t backtracking_states = 65536;
  static constexpr std::size_t cache_entries = 32;
  static constexpr std::size_t cache_bytes = 8 * 1024 * 1024;
};

struct result {
  status code = status::no_match;
  // (byte start, byte length), with group zero first. An absent capture is
  // (-1, -1); a participating empty capture has a valid start and length zero.
  std::vector<std::pair<std::int64_t, std::int64_t>> spans;
  std::string error;
};

// NIL byte-pattern profile, not PCRE/ECMAScript/RE2 compatibility:
// - NUL-free bytes, ASCII \d/\w/\s and their negations; no Unicode decoding.
// - Dot excludes LF. ^ and $ mean absolute input start/end, including in a
//   lookahead; $ has no special exemption for a final newline.
// - Leftmost-first search, ordered alternatives, greedy/lazy repetition.
// - Capturing (), noncapturing (?:), named (?<name>), (?=) and (?!) lookahead.
//   Positive lookahead is atomic and retains captures; negative lookahead
//   discards them. Repeated captures retain their last successful participation.
// - Repetition stops after an empty iteration, preventing empty-body loops.
// - Backreferences, lookbehind, flags, word boundaries, Unicode properties,
//   possessive quantifiers and other extensions fail explicitly.
// Pattern parsing/compilation and matching all have fixed resource budgets.
// Immutable compiled programs use a bounded thread-local cache.

// Empty means valid; otherwise a bounded diagnostic describes syntax/limits.
std::string validate(std::string_view pattern);

// Start is a byte offset, including input.size(). Full matching requires
// start == 0 and consumes the entire input. Failure and limit results never
// contain partial captures. Search budgets cover every attempted start position.
result search(std::string_view pattern, std::string_view input,
              std::int64_t start = 0, bool full = false);

// Names are case-sensitive ASCII identifiers. -1 means absent or invalid.
std::int64_t group_index(std::string_view pattern, std::string_view name);

} // namespace ny::patterns

#endif
