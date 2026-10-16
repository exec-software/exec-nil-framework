// Standalone lexer harness: tokenises a .matrix file and reports counts plus any
// diagnostics. Used by tests/matrix/run_lexer_tests.py.
#include "../../nilcompiler/src/matrix_lexer.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char **argv) {
  if (argc < 2) { std::cerr << "usage: lex_dump <file.matrix> [--tokens]\n"; return 2; }
  std::ifstream in(argv[1]);
  if (!in) { std::cerr << "cannot open " << argv[1] << "\n"; return 2; }
  std::stringstream buffer; buffer << in.rdbuf();
  const std::string source = buffer.str();

  matrix::lexer lex(source);
  const auto tokens = lex.scan();
  const bool dump = argc > 2 && std::string(argv[2]) == "--tokens";

  if (dump) {
    for (const auto &t : tokens) {
      if (t.kind == matrix::token_kind::end_of_file) break;
      const char *kind = "?";
      switch (t.kind) {
      case matrix::token_kind::identifier: kind = "name"; break;
      case matrix::token_kind::keyword: kind = "kw"; break;
      case matrix::token_kind::number_literal: kind = "num"; break;
      case matrix::token_kind::string_literal: kind = "str"; break;
      
      case matrix::token_kind::invalid: kind = "BAD"; break;
      case matrix::token_kind::dot: kind = "dot"; break;
      case matrix::token_kind::statement_end: kind = "stmt-end"; break;
      default: kind = "punct"; break;
      }
      std::cout << t.line << ":" << t.column << "\t" << kind << "\t'"
                << t.lexeme << "'";
      if (t.kind == matrix::token_kind::identifier) std::cout << "\tkey=" << t.key;
      std::cout << "\n";
    }
  }

  size_t names = 0, keywords = 0;
  for (const auto &t : tokens) {
    if (t.kind == matrix::token_kind::identifier) ++names;
    if (t.kind == matrix::token_kind::keyword) ++keywords;
  }
  std::cout << "tokens=" << tokens.size() - 1 << " keywords=" << keywords
            << " names=" << names << " errors=" << lex.errors().size() << "\n";
  for (const auto &e : lex.errors()) {
    std::cout << "  error " << e.line << ":" << e.column << ": " << e.message << "\n";
    if (!e.hint.empty()) std::cout << "    hint: " << e.hint << "\n";
  }
  return lex.has_errors() ? 1 : 0;
}
