#ifndef NIL_MATRIX_SUBMISSION_SESSION_CLI_HPP
#define NIL_MATRIX_SUBMISSION_SESSION_CLI_HPP

#include <string>

namespace matrix {
int run_submission_evaluate_cli(bool request, const std::string &path);
int run_matrix_shell_cli();
} // namespace matrix
#endif
