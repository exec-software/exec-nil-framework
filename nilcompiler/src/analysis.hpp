#pragma once
#include "ast.hpp"
#include "semantic_index.hpp"
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ny {
struct analysis_fix {
  std::size_t start, length;
  std::string replacement, description;
};
struct analysis_diagnostic {
  std::string code, severity, message, path, hint;
  std::optional<std::size_t> start, length;
  std::optional<analysis_fix> fix;
};
struct reflected_declaration {
  std::string kind, name, qualified_name, type_name, source_path;
  int line = 0, parent_id = -1;
  bool is_public = false, semantic_checked = false;
};
struct reflected_annotation {
  int owner_id;
  std::string name, arguments;
  int line;
};
struct analysis_session {
  std::string primary_path, logical_path, language, stdin_source;
  bool source_stdin = false, workspace_stdin = false, semantic_checked = false, truncated = false;
  bool semantic_requested = false;
  bool semantic_binary_dependencies = false;
  std::optional<semantic_index> semantics;
  bool warnings_as_errors = false;
  std::set<std::string> disabled_rules;
  std::size_t reflection_bytes = 0;
  std::map<std::string, std::string> sources;
  // Canonical native source paths only. Binary-library identities never use
  // this map, and unrelated buffers are not added to the compilation graph.
  std::map<std::string, std::string> workspace_sources;
  std::size_t workspace_read_bytes = 0;
  std::vector<analysis_diagnostic> diagnostics;
  std::size_t diagnostic_bytes = 0;
  std::vector<reflected_declaration> declarations;
  std::vector<reflected_annotation> annotations;
  void add(std::string code, std::string severity, std::string message,
           const std::string &path = {}, int line = 0, int column = 0,
           int length = 0, std::string hint = {});
  void reflect(const ast_program &program, const std::string &path);
  void lint();
  std::string json(bool success) const;
  std::string semantic_json() const;
  std::string companion_source() const;
  bool workspace_source_exists(const std::string &path) const;
  std::optional<std::string> read_workspace_source(const std::string &path);
  void record_workspace_source(const std::string &path, const std::string &source);
};
std::string analysis_source_identity(const std::string &path);
void validate_analysis_utf8(const std::string &text);
using compilation_entry = int (*)(int, char **, analysis_session *);
int run_analysis_cli(int argc, char **argv, compilation_entry compile);
} // namespace ny
