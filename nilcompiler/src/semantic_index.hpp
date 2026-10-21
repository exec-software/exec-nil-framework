#pragma once

#include "ast.hpp"

namespace ny {
struct semantic_symbol {
  std::size_t id;
  std::string kind, name, type, path;
  source_span span;
  // Only lexical body/loop declarations can be closed over a single source.
  // Named function parameters are deliberately excluded: Matrix callers can
  // use their names as argument labels outside this compilation graph.
  bool lexical_local = false;
  bool all_uses_located = true;
};
struct semantic_reference {
  std::size_t symbol_id;
  std::string path, name;
  source_span span;
};
struct semantic_index {
  bool available = true;
  std::vector<semantic_symbol> symbols;
  std::vector<semantic_reference> references;
};
struct completion_request {
  std::string path;
  std::size_t cursor;
};
struct completion_candidate {
  std::string name, kind, type, path;
  source_span span;
};
struct completion_result {
  bool available = true;
  std::string path, name;
  source_span span;
  std::vector<completion_candidate> candidates;
  // Member spellings are resolved through the receiver, never the Matrix
  // parser's page-wide map of unqualified declaration names.
  bool member = false;
};
struct signature_request {
  std::string path;
  std::size_t cursor;
};
struct signature_parameter {
  std::string name, type;
  bool variadic = false;
  std::optional<source_span> span;
};
struct signature_result {
  bool available = false;
  std::string path, name, declaration_path, return_type;
  source_span span;
  std::optional<source_span> declaration_span;
  std::vector<signature_parameter> parameters;
  std::optional<std::size_t> active_parameter;
};
} // namespace ny
