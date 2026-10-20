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
};
} // namespace ny
