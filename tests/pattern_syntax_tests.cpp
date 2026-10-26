#include "parser.hpp"
#include "matrix_parser.hpp"
#include "pattern_syntax.hpp"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool value, const std::string &message) {
  if (!value) { std::cerr << message << '\n'; ++failures; }
}
std::string parse(const std::string &body, bool matrix, bool valid = true) {
  std::shared_ptr<ny::ast_program> program;
  bool errors = false;
  if (matrix) {
    matrix::parser p("book patterns\npublic sample begin pattern\n" + body + "\nend pattern\n");
    program = p.parse_program();
    errors = p.has_errors();
    if (errors && valid) for (const auto &d : p.diagnostics()) std::cerr << d.message << '\n';
  } else {
    ny::parser p("pub pattern sample\n" + body + "\nend pattern\n");
    program = p.parse_program();
    errors = p.has_errors();
    if (errors && valid) for (const auto &d : p.diagnostics()) std::cerr << d.message << '\n';
  }
  check(errors != valid, std::string(matrix ? "Matrix: " : "Ny: ") + body);
  if (!valid || errors || !program || program->declarations.empty()) return {};
  std::shared_ptr<ny::ast_variable_decl> binding;
  for (const auto &declaration : program->declarations) {
    if (auto value = std::dynamic_pointer_cast<ny::ast_variable_decl>(declaration)) binding = value;
  }
  check(binding && binding->is_const && binding->is_public() && binding->name_span,
        "pattern keeps immutable/public binding and name span");
  if (!binding) return {};
  auto literal = std::dynamic_pointer_cast<ny::ast_string_literal>(binding->initializer);
  check(bool(literal), "pattern lowers to constant text");
  return literal ? literal->value : "";
}
}

int main() {
  const std::vector<std::string> valid = {
    "",
    "capture amount matching one or more ascii digits\nrequire next to match literal \" USD\"",
    "start of input\nexactly 2 ascii letters\noptional literal \"-\"\nbetween 1 and 3 ascii digits\nend of input",
    "either begin pattern\nliteral \"cat\"\nend pattern\nor begin pattern\nliteral \"dog\"\nend pattern",
    "zero or more any character\nrequire next not to match begin pattern\nliteral \"x\"\none or more ascii digits\nend pattern",
    "fewest one or more character from \"0\" through \"9\"\nliteral tab\nascii whitespace",
    "literal \"{name}.$*+?()[]|^\"",
    "exactly 000000001 ascii digits"
  };
  for (const auto &body : valid) check(parse(body, false) == parse(body, true), "frontend pattern parity");
  const auto literal = parse("literal \"{name}\"", false);
  check(literal.find("\\{name\\}") != std::string::npos, "pattern literals must not interpolate");
  for (const auto &body : {
      "require next match literal \"x\"", "between 4 and 2 ascii digits",
      "character from \"ab\" through \"z\"", "exactly -1 ascii digits",
      "exactly 10001 ascii digits", "fewest literal \"x\"", "ascii dragons",
      "capture same matching literal \"a\"\ncapture same matching literal \"b\"",
      "literal \"x\"\nbegin pattern\nliteral \"y\""}) {
    parse(body, false, false); parse(body, true, false);
  }
  std::string nested;
  for (int i = 0; i < 70; ++i) nested += "optional ";
  nested += "literal \"x\"";
  parse(nested, false, false); parse(nested, true, false);
  // The contextual word remains a legal ordinary variable and parameter.
  ny::parser ny("fn echo(pattern: text) -> text\nreturn pattern\nend fn\n");
  ny.parse_program(); check(!ny.has_errors(), "pattern must remain contextual in Ny");
  matrix::parser mx("book patterns\necho receives pattern as text returns text begin paragraph\ngive pattern\nend paragraph\n");
  mx.parse_program(); check(!mx.has_errors(), "pattern must remain contextual in Matrix");
  for (const auto &source : {
      "book p\nx begin \"pattern\"\nliteral \"a\"\nend pattern\n",
      "book p\nx begin `pattern`\nliteral \"a\"\nend pattern\n",
      "book p\nx begin pat-tern\nliteral \"a\"\nend pattern\n",
      "book p\nx begin pattern\nliteral \"a\"\nend \"pattern\"\n",
      "book p\nx begin pattern\ncapture `x>a)(?<b` matching literal \"z\"\nend pattern\n"}) {
    matrix::parser invalid(source);
    invalid.parse_program(); check(invalid.has_errors(), "pattern delimiters and capture labels must be validated");
  }
  return failures ? 1 : 0;
}
