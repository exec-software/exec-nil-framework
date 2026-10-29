#ifndef NIL_SUBMISSION_VALUE_HPP
#define NIL_SUBMISSION_VALUE_HPP

#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace ny {
// Owned data only: no VM addresses, mutable aliases, or executable history.
struct submission_value {
  std::string type;
  std::string scalar;
  std::vector<submission_value> elements;
};

struct pure_execution_limits {
  std::size_t max_instructions = 16384;
  std::size_t max_work_bytes = 16 * 1024 * 1024;
  std::size_t max_text_bytes = 1024 * 1024;
  std::size_t max_snapshot_nodes = 4096;
  std::size_t max_snapshot_depth = 32;
  std::size_t max_snapshot_bytes = 1024 * 1024;
  // Called between instructions and snapshot nodes; true requests cancellation.
  std::function<bool()> cancelled;
};

class pure_execution_error : public std::runtime_error {
public:
  enum class kind { budget, cancelled };
  kind reason;
  pure_execution_error(kind reason, const std::string &message)
      : std::runtime_error(message), reason(reason) {}
};
} // namespace ny
#endif
