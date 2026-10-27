#include "matrix_parser.hpp"
#include "formatted_text.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using matrix::submission_state;

void require(bool condition, const std::string &message) {
  if (!condition) throw std::runtime_error(message);
}

matrix::submission_parse_result parse(const std::string &source,
                                     submission_state expected = submission_state::complete) {
  matrix::parser_options options;
  options.filename = "submission.matrix";
  options.submission = true;
  matrix::parser parser(source, options);
  auto result = parser.parse_submission();
  if (result.state != expected) {
    std::cerr << "Source: " << source.substr(0, 300) << '\n';
    for (const auto &diagnostic : result.diagnostics)
      std::cerr << diagnostic.primary.line << ':' << diagnostic.primary.column
                << ": " << diagnostic.message << '\n';
  }
  require(result.state == expected, "unexpected submission state");
  if (expected == submission_state::complete) {
    require(result.body && result.diagnostics.empty(), "complete submission has a body and no diagnostics");
  } else {
    require(!result.body && !result.value && !result.diagnostics.empty(),
            "unfinished or invalid input must not expose a partial executable AST");
  }
  return result;
}

template <typename T, typename U>
std::shared_ptr<T> cast(const std::shared_ptr<U> &node) {
  auto result = std::dynamic_pointer_cast<T>(node);
  require(result != nullptr, "unexpected AST node");
  return result;
}

void values_and_statements() {
  for (const auto *source : {"", " \t\r\n", "!!! an empty submission\n", "; ."}) {
    const auto result = parse(source);
    require(result.body->statements.empty() && !result.value, "empty submission is void");
  }
  const auto arithmetic = cast<ny::ast_binary_expr>(parse("1 plus 2 times 3").value);
  require(arithmetic->op == ny::token_kind::plus &&
              cast<ny::ast_binary_expr>(arithmetic->right)->op == ny::token_kind::star,
          "ordinary Matrix operator precedence is preserved");
  const auto source = std::string("!!! café\r\nvalue Sensor-Reading is 7\r\nSENSOR_reading plus 1");
  const auto result = parse(source);
  require(result.body->statements.size() == 1 && result.body->source_file == "submission.matrix",
          "local binding and final value share the original submission");
  const auto declaration = cast<ny::ast_variable_decl>(
      cast<ny::ast_decl_stmt>(result.body->statements[0])->declaration);
  const auto reference = cast<ny::ast_identifier_expr>(cast<ny::ast_binary_expr>(result.value)->left);
  require(declaration->name == "Sensor_Reading" && reference->name == declaration->name,
          "local references fold case and separators");
  require(declaration->line == 2 && reference->line == 3 && declaration->name_span && reference->name_span,
          "submission lines and editable spans are retained");
  require(source.substr(declaration->name_span->start, declaration->name_span->length) == "Sensor-Reading" &&
              source.substr(reference->name_span->start, reference->name_span->length) == "SENSOR_reading",
          "source spans use original UTF-8 bytes, without a synthetic wrapper");
  const auto shadow = parse("value sample is 1\nwhen true then\n  value SAMPLE is 2\nend when\nsample");
  const auto conditional = cast<ny::ast_if_stmt>(shadow.body->statements[1]);
  const auto inner = cast<ny::ast_variable_decl>(cast<ny::ast_decl_stmt>(
      cast<ny::ast_block_stmt>(conditional->then_branch)->statements[0])->declaration);
  require(inner->name == "sample", "shadowing declarations and references share their folded symbol");
  const auto tuple = parse("value (Frame-Count, other) is (1, 2)\nFRAME_COUNT");
  const auto destructuring = cast<ny::ast_variable_decl>(
      cast<ny::ast_decl_stmt>(tuple.body->statements[0])->declaration);
  require(destructuring->tuple_bindings[0]->name == "Frame_Count" &&
              cast<ny::ast_identifier_expr>(tuple.value)->name == "Frame_Count",
          "tuple destructuring records folded local names");
  const auto duplicate = parse("value sample is 1\nvalue SAMPLE is 2");
  require(cast<ny::ast_variable_decl>(cast<ny::ast_decl_stmt>(duplicate.body->statements[1])->declaration)->name == "sample",
          "same-scope folded duplicates reach shared duplicate-binding checks");
  parse("value rows is [(\"renderer\", 3), (\"desktop\", 0)]\n"
        "collect begin query\nfrom each row in rows\nwhere row.1 is greater than 0\n"
        "order by row.1 descending\ngive row.0\nend query");
  const auto call_source = std::string("status with vessel-name being \"renderer\", verbose being true");
  const auto call = cast<ny::ast_call_expr>(parse(call_source).value);
  require(call->argument_names.size() == 2 && call->argument_names[0] == "vessel-name" &&
              call->argument_span && call->argument_span->start == call_source.find("with") + 4,
          "named arguments and their original spans survive");
  parse("when true give 1 otherwise give 2 end when");
  parse("(when true then give 1 otherwise give 2 end when)");
  parse("(consider 1 when 1 give 2 otherwise give 3 end consider)");
  // Parse effects with the ordinary grammar; the separate profile validates them.
  parse("variable x is 1\nset x to 2\nsay x\nevaluate status with nothing");
  parse("repeat while false\nsay 1\nend repeat");
  parse("attempt\nsay 1\ncaught anything as problem\nsay 2\nend attempt");
  for (const auto *source : {"1\n2", "1; say 2", "1\nvalue x is 2", "1 plus )", "value is 1"})
    parse(source, submission_state::invalid);
}

void text_and_interpolation() {
  require(cast<ny::ast_string_literal>(parse("\"C:\\dir\\file\"").value)->value == "C:\\dir\\file",
          "quoted Matrix text is raw");
  require(cast<ny::ast_string_literal>(parse("begin text\n  hello \"world\"\n  C:\\dir\nend text\n").value)->value ==
              "hello \"world\"\nC:\\dir", "raw blocks use existing dedentation");
  const auto source = std::string("!!! λ\nvalue frame-count is 3\nformatted \"count {FRAME_COUNT plus 1} {{literal}}\"");
  const auto formatted = cast<ny::ast_interpolated_string_expr>(parse(source).value);
  const auto reference = cast<ny::ast_identifier_expr>(cast<ny::ast_binary_expr>(formatted->parts[1].expression)->left);
  require(reference->line == 3 && reference->name_span &&
              source.substr(reference->name_span->start, reference->name_span->length) == "FRAME_COUNT",
          "formatted expressions preserve original byte ranges");
  parse("formatted \"outer {formatted \"inner {1 plus 2}\"}\"");
  for (const auto *source : {"\"unfinished", "`unfinished", "begin text\nhello", "formatted \"unfinished",
                            "formatted \"{1 plus", "formatted \"{\"unfinished", "formatted \"{0x",
                            "formatted \"{formatted \"nested {1"})
    parse(source, submission_state::incomplete);
  for (const auto *source : {"\"unfinished\n", "`unfinished\n", "formatted \"line\nbreak\"",
                            "formatted \"{}\"", "formatted \"bad }", "formatted \"{1 plus }\"",
                            "formatted \"{1 @", "formatted \"{1 plus )", "formatted \"{0x}\"",
                            "value is 1\n\"unfinished", "value x is )\nformatted \"unfinished"})
    parse(source, submission_state::invalid);

  // Shared formatted scanning keeps full-page Ny/Matrix success/error behavior.
  for (bool matrix : {false, true}) {
    const auto valid = ny::scan_formatted_text("\"a {1 plus 2} {{b}}\"", 0, matrix);
    require(valid.error.empty() && !valid.incomplete && valid.pieces.size() == 3,
            "complete formatted scanner behavior is unchanged");
    const auto unfinished = ny::scan_formatted_text("\"a {1", 0, matrix);
    require(!unfinished.error.empty() && unfinished.incomplete, "EOF status is structured");
    const auto bad = ny::scan_formatted_text("\"a }", 0, matrix);
    require(!bad.error.empty() && !bad.incomplete, "invalid brace is never a continuation");
  }
  matrix::parser page("book format\nmain begin paragraph\nsay formatted \"{1 plus 2}\"\nend paragraph\n");
  require(page.parse_program() && !page.has_errors(), "normal Matrix page still parses");
  matrix::parser malformed_page("book format\nmain begin paragraph\nsay formatted \"{1");
  malformed_page.parse_program();
  require(malformed_page.has_errors(), "normal Matrix page rejects unfinished formatting");
}

void continuations_and_rejections() {
  for (const auto *source : {"value", "value total", "value total is", "value total as array of",
                            "1 plus", "negative", "not", "bitwise", "1 bitwise", "1 does not",
                            "1 is", "1 is less", "1 shifted", "1 shifted right", "1 shifted right by",
                            "(", "[1,", "call with", "call with arg being", "the length of", "reference", "now",
                            "when true", "when true then", "when true then\nsay 1\nend",
                            "repeat", "repeat forever", "repeat forever\nend", "attempt\nsay 1",
                            "consider 1\nwhen 1 then\nsay 2\nend", "0x", "1e", "1e-",
                            "(when true give 1 otherwise give 2 end"})
    parse(source, submission_state::incomplete);
  for (const auto *source : {"book test", "library test", "read nil.std.core", "cite nil.std.core",
                            "helper begin paragraph\nend paragraph", "Point begin record\nend record",
                            "helper of type T returns T begin paragraph\nend paragraph",
                            "pattern-name begin pattern\nend pattern", "note intrinsic \"unsafe\"",
                            "repeat forever\nread nil.std.core\nend repeat", "end", "1 is bogus",
                            "1 shifted sideways", "1 does wrong", "1 plus )", "[1,)",
                            "when true then\nvalue is 1", "repeat forever\nend when",
                            "(when true give 1 otherwise give 2 end)", "0xg", "1e9999"})
    parse(source, submission_state::invalid);
  const auto incomplete = parse("value answer is\n", submission_state::incomplete);
  require(incomplete.diagnostics.back().primary.line == 2 &&
              incomplete.diagnostics.back().primary.column == 1,
          "missing operands point to the original EOF location");
  const auto invalid = parse("\nvalue is 1", submission_state::invalid);
  require(invalid.diagnostics.front().primary.line == 2 &&
              invalid.diagnostics.front().primary.column == 7,
          "malformed earlier tokens retain original diagnostic columns");
}

void integer_boundaries_and_limits() {
  for (const auto *source : {"9223372036854775807", "-9223372036854775808", "negative 9223372036854775808",
                            "minus 9223372036854775808", "-0x8000000000000000", "-1.6"})
    parse(source);
  for (const auto *source : {"9223372036854775808", "-9223372036854775809", "0x8000000000000000",
                            "value x is 9223372036854775808\n1 plus", "9223372036854775808 plus"})
    parse(source, submission_state::invalid);
  parse(std::string("!!!") + std::string(matrix::submission_source_limit - 3, ' '));
  parse(std::string(matrix::submission_source_limit + 1, ' '), submission_state::invalid);
  parse(std::string(matrix::submission_token_limit, ';'));
  parse(std::string(matrix::submission_token_limit + 1, ';'), submission_state::invalid);
  parse(std::string(63, '(') + "1" + std::string(63, ')'));
  parse(std::string(64, '(') + "1" + std::string(64, ')'), submission_state::invalid);
  std::string unary, type, blocks, reference;
  for (int i = 0; i < 70; ++i) {
    unary += "negative "; type += "array of "; blocks += "repeat forever\n";
    reference += "reference to ";
  }
  parse(unary + "1", submission_state::invalid);
  parse("value x as " + type + "number is []", submission_state::invalid);
  parse(blocks, submission_state::invalid);
  parse(reference + "x", submission_state::invalid);
  // Normal full pages retain their previous unbounded nesting behavior.
  matrix::parser page("book depth\nmain begin paragraph\nsay " + std::string(70, '(') + "1" +
                      std::string(70, ')') + "\nend paragraph\n");
  require(page.parse_program() && !page.has_errors(), "submission limits do not alter page parsing");
}
} // namespace

int main() {
  try {
    values_and_statements();
    text_and_interpolation();
    continuations_and_rejections();
    integer_boundaries_and_limits();
    std::cout << "Matrix submission parser tests passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
