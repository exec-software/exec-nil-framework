#pragma once

#include "ast.hpp"

namespace ny {
struct semantic_symbol {
  std::size_t id;
  std::string kind, name, type, path;
  source_span span;
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
} // namespace ny
