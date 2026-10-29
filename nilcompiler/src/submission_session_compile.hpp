#ifndef NIL_SUBMISSION_SESSION_COMPILE_HPP
#define NIL_SUBMISSION_SESSION_COMPILE_HPP

#include "matrix_submission.hpp"
#include "submission_value.hpp"
#include <memory>

namespace ny { struct nil_module; }
namespace matrix {

inline constexpr std::size_t submission_session_max_bindings = 64;
struct submission_binding {
  std::string name;
  ny::submission_value value;
};
struct prepared_session_submission {
  submission_report report;
  std::shared_ptr<ny::nil_module> module;
  bool has_value = false;
  // The result tuple contains the final expression first, when present,
  // followed by these new top-level bindings in declaration order.
  std::vector<std::string> binding_names;
};

// Compile only the new submission. Retained values become fresh typed data
// literals; no prior source or AST is replayed. This does not execute or publish
// state. The caller must evaluate and validate the complete result before
// atomically committing any new binding.
prepared_session_submission prepare_session_submission(
    const std::string &source, bool request, const std::string &filename,
    const std::vector<submission_binding> &bindings);

} // namespace matrix
#endif
