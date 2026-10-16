#ifndef NY_COMPILER_OWNERSHIP_CHECK_HPP
#define NY_COMPILER_OWNERSHIP_CHECK_HPP

#include "ast.hpp"
#include <string>
#include <vector>

namespace ny {
bool contains_ownership_type(const std::string &type);
bool is_ownership_type(const std::string &type);
std::string ownership_element_type(const std::string &type);

struct ownership_diagnostic { int line; std::string message; };
struct ownership_check_result {
  std::vector<ownership_diagnostic> diagnostics;
  std::vector<ast_expr *> expressions;
  std::vector<ast_function_decl *> functions;
};
// Runs after ordinary type resolution. Local affine buffers and their lexical
// borrows are deliberately separate from Ny's shared aggregate ref<T> handles.
ownership_check_result check_ownership(ast_program &program);
} // namespace ny
#endif
