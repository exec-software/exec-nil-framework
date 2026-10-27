#ifndef NIL_SUBMISSION_SESSION_HPP
#define NIL_SUBMISSION_SESSION_HPP

#include "submission_session_compile.hpp"
#include <optional>

namespace matrix {

struct session_limits {
  ny::pure_execution_limits execution;
  std::size_t max_state_bytes = 1024 * 1024;
  std::size_t max_state_nodes = 1024;
  std::size_t max_report_bytes = 1024 * 1024;
  std::size_t max_execution_ms = 1000;
};

struct evaluation_report {
  submission_report check;
  bool executed = false;
  bool committed = false;
  std::optional<ny::submission_value> value;
  std::vector<submission_binding> added;
  std::size_t instructions_executed = 0;
  std::size_t binding_count = 0;
};

// One thread owns a session. Only fully checked owned data crosses transactions;
// each evaluation uses a new VM and publishes all new bindings atomically.
class submission_session {
public:
  explicit submission_session(session_limits limits = {});
  evaluation_report evaluate(const std::string &source, bool request = false,
                             const std::string &filename = "<submission>");
  void reset() { bindings_.clear(); }
  const std::vector<submission_binding> &bindings() const { return bindings_; }
private:
  session_limits limits_;
  std::vector<submission_binding> bindings_;
};

std::string submission_value_json(const ny::submission_value &value);
std::string evaluation_report_json(const evaluation_report &report);

} // namespace matrix
#endif
