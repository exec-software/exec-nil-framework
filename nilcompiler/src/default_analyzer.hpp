#pragma once
#include "ast.hpp"
#include <string>

namespace ny {
struct analysis_session;
// Built-in rule identity is shared by the CLI catalogue and policy validation.
bool is_default_analysis_rule(const std::string &code);
std::string default_analysis_rules_json();
void run_default_analyzer(const ast_program &program, analysis_session &session);
} // namespace ny
