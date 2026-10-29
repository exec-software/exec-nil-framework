#ifndef NIL_MATRIX_SUBMISSION_HPP
#define NIL_MATRIX_SUBMISSION_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace matrix {

inline constexpr std::size_t submission_max_input_bytes = 64 * 1024;

struct submission_issue {
  std::string stage;
  std::string code;
  std::string message;
  std::string location;
  int line = 0;
  int column = 0;
  int length = 0;
};

// Admission/inspection only. The report is not an execution authorization or
// a runtime resource guarantee; no VM or native executor is entered here.
struct submission_report {
  std::string status = "invalid";
  std::string input_kind;
  std::string result_type;
  std::string nil_text;
  std::size_t instruction_count = 0;
  std::vector<submission_issue> diagnostics;
};

submission_report check_submission_source(const std::string &source,
                                          const std::string &filename);
submission_report check_submission_request(const std::string &source,
                                           const std::string &filename);
std::string submission_report_json(const submission_report &report);
int run_submission_check_cli(bool request, const std::string &path);

} // namespace matrix
#endif
