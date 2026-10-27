#ifndef NY_COMPILER_SUBMISSION_PROFILE_HPP
#define NY_COMPILER_SUBMISSION_PROFILE_HPP

#include "ast.hpp"
#include <string>
#include <vector>

namespace ny {
struct nil_module;
struct submission_diagnostic {
  int line = 0;
  std::string message;
  std::string source_file;
};

// Admission for the initial pure submission profile, not an execution sandbox.
// The body and optional final expression share the outer lexical scope. Ordinary
// type checking must still succeed before generating and admitting NIL.
std::vector<submission_diagnostic>
validate_submission_ast(const ast_block_stmt &body, const ast_expr_ptr &value);

// Independently inspect the complete generated artifact, including instructions
// that would be skipped at runtime. Throws std::runtime_error on rejection.
void validate_nil_submission_profile(const nil_module &module);
} // namespace ny
#endif
