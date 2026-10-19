#include "matrix_lexer.hpp"
#include "matrix_parser.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) throw std::runtime_error(message);
}

template <typename T, typename U>
std::shared_ptr<T> cast(const std::shared_ptr<U> &node) {
  auto result = std::dynamic_pointer_cast<T>(node);
  require(result != nullptr, "unexpected AST node in source-span fixture");
  return result;
}

std::shared_ptr<ny::ast_program> parse(const std::string &source) {
  matrix::parser parser(source, {"source-spans.matrix"});
  auto result = parser.parse_program();
  if (parser.has_errors()) {
    for (const auto &diagnostic : parser.diagnostics())
      std::cerr << diagnostic.line << ": " << diagnostic.message << '\n';
  }
  require(result && !parser.has_errors(), "source-span fixture must parse");
  return result;
}

// The context distinguishes repeated spellings in comments, declarations,
// strings and uses; equal text alone must never be considered a source map.
void check_span(const std::string &source,
                const std::optional<ny::source_span> &span,
                const std::string &context, const std::string &spelling) {
  const auto context_start = source.find(context);
  require(context_start != std::string::npos, "missing fixture context: " + context);
  const auto within = context.find(spelling);
  require(within != std::string::npos, "missing fixture spelling: " + spelling);
  require(span.has_value(), "missing name span for " + context);
  require(span->start == context_start + within && span->length == spelling.size(),
          "wrong byte range for " + context);
  require(source.substr(span->start, span->length) == spelling,
          "name span changed original source spelling: " + context);
}

void test_lexer_source_ranges() {
  const std::string source =
      "!!! café-value is not a token\r\n\tCAFÉ-value `value` \"café-value\"\r\n"
      "begin text\r\n  café-value\r\nend text\r\nlast-name";
  matrix::lexer lexer(source);
  const auto tokens = lexer.scan();
  require(!lexer.has_errors() && tokens.size() == 6, "lexer source-range fixture");
  require(tokens[0].start == source.find("CAFÉ-value") &&
              tokens[0].length == std::string("CAFÉ-value").size(),
          "identifier token uses UTF-8 bytes after comment and CRLF");
  require(tokens[1].escaped_identifier &&
              source.substr(tokens[1].start, tokens[1].length) == "`value`",
          "escaped identifier token retains both original delimiters");
  require(source.substr(tokens[2].start, tokens[2].length) == "\"café-value\"",
          "string token range includes quotes");
  require(source.substr(tokens[3].start, tokens[3].length) ==
              "begin text\r\n  café-value\r\nend text\r\n",
          "dedented raw block retains its full original token range");
  require(tokens[4].start == source.find("last-name") &&
              tokens[4].length == 9,
          "token after a raw block retains original byte offset");
  require(tokens[5].start == source.size() && tokens[5].length == 0,
          "EOF is an empty range at the source end");
}

void test_names_and_uses() {
  const std::string source =
      "book span-check\r\n"
      "!!! run-check café-value local-value remote-call\r\n"
      "public run-check receives café-value as number, `value` as number returns number begin paragraph\r\n"
      "  value local-value is café-value\r\n"
      "  evaluate alpha.beta::remote-call with LOCAL_VALUE\r\n"
      "  value callback-value is the paragraph run-check\r\n"
      "  repeat for each loop-value in [1, 2]\r\n"
      "    evaluate loop-value\r\n"
      "  end repeat\r\n"
      "  repeat for iteration-count from 0 to 2\r\n"
      "    evaluate iteration-count\r\n"
      "  end repeat\r\n"
      "  value literal-value is \"local-value café-value\"\r\n"
      "  give `value`\r\n"
      "end paragraph\r\n";
  const auto program = parse(source);
  const auto fn = cast<ny::ast_function_decl>(program->declarations.at(1));
  check_span(source, fn->name_span, "public run-check", "run-check");
  require(fn->name == "run_check", "NIL name remains independent of written name");
  check_span(source, fn->params.at(0).name_span, "receives café-value", "café-value");
  check_span(source, fn->params.at(1).name_span, "`value` as", "value");
  const auto body = cast<ny::ast_block_stmt>(fn->body);
  const auto local = cast<ny::ast_variable_decl>(
      cast<ny::ast_decl_stmt>(body->statements.at(0))->declaration);
  check_span(source, local->name_span, "  value local-value is", "local-value");
  check_span(source, local->initializer->name_span, "is café-value", "café-value");
  const auto call = cast<ny::ast_call_expr>(
      cast<ny::ast_expr_stmt>(body->statements.at(1))->expression);
  check_span(source, call->callee->name_span, "beta::remote-call", "remote-call");
  check_span(source, call->args.at(0)->name_span, "with LOCAL_VALUE", "LOCAL_VALUE");
  require(cast<ny::ast_identifier_expr>(call->args.at(0))->name == "local_value",
          "source use keeps its written case while its NIL identity resolves");
  const auto callback = cast<ny::ast_variable_decl>(
      cast<ny::ast_decl_stmt>(body->statements.at(2))->declaration);
  check_span(source, callback->initializer->name_span, "the paragraph run-check", "run-check");
  const auto each = cast<ny::ast_for_in_stmt>(body->statements.at(3));
  check_span(source, each->variables.at(0).name_span, "each loop-value", "loop-value");
  check_span(source, cast<ny::ast_expr_stmt>(
      cast<ny::ast_block_stmt>(each->body)->statements.at(0))->expression->name_span,
      "evaluate loop-value", "loop-value");
  const auto counted = cast<ny::ast_for_in_stmt>(body->statements.at(4));
  check_span(source, counted->variables.at(0).name_span,
             "for iteration-count", "iteration-count");
  require(!cast<ny::ast_call_expr>(counted->iterable)->callee->name_span,
          "synthetic range callee has no source name");
  const auto literal = cast<ny::ast_variable_decl>(
      cast<ny::ast_decl_stmt>(body->statements.at(5))->declaration);
  require(!literal->initializer->name_span, "literal text is not a name occurrence");
  check_span(source, cast<ny::ast_return_stmt>(body->statements.at(6))->value->name_span,
             "give `value`", "value");
}

void test_members_and_types() {
  const std::string source =
      "book member-check\n"
      "read other.book as book-alias\n"
      "value global-value is 1\n"
      "scalar-kind means number\n"
      "data-record begin record\n"
      "  field item-value as number\n"
      "end record\n"
      "data-class begin class\n"
      "  field item-count as number\n"
      "  read-count receives added-value as number returns number begin paragraph\n"
      "    give my item-count plus added-value\n"
      "  end paragraph\n"
      "end class\n"
      "read-record receives record-value as data-record returns number begin paragraph\n"
      "  give record-value.item-value\n"
      "end paragraph\n";
  const auto program = parse(source);
  check_span(source, program->declarations.at(1)->name_span,
             "as book-alias", "book-alias");
  check_span(source, program->declarations.at(2)->name_span,
             "value global-value", "global-value");
  check_span(source, program->declarations.at(3)->name_span,
             "scalar-kind means", "scalar-kind");
  const auto record = cast<ny::ast_struct_decl>(program->declarations.at(4));
  check_span(source, record->name_span, "data-record begin", "data-record");
  check_span(source, record->fields.at(0).name_span, "field item-value", "item-value");
  const auto klass = cast<ny::ast_class_decl>(program->declarations.at(5));
  check_span(source, klass->name_span, "data-class begin", "data-class");
  check_span(source, klass->fields.at(0).name_span, "field item-count", "item-count");
  const auto method = klass->methods.at(0);
  check_span(source, method->name_span, "read-count receives", "read-count");
  require(!method->params.at(0).name_span, "synthetic self parameter has no name span");
  check_span(source, method->params.at(1).name_span, "receives added-value", "added-value");
  const auto sum = cast<ny::ast_binary_expr>(cast<ny::ast_return_stmt>(
      cast<ny::ast_block_stmt>(method->body)->statements.at(0))->value);
  const auto member = cast<ny::ast_member_access_expr>(sum->left);
  check_span(source, member->name_span, "my item-count", "item-count");
  require(!member->object->name_span, "synthetic self use has no name span");
  const auto fn = cast<ny::ast_function_decl>(program->declarations.at(6));
  const auto field = cast<ny::ast_member_access_expr>(cast<ny::ast_return_stmt>(
      cast<ny::ast_block_stmt>(fn->body)->statements.at(0))->value);
  check_span(source, field->name_span, "record-value.item-value", "item-value");
  check_span(source, field->object->name_span, "give record-value", "record-value");
}

void test_lowered_bindings() {
  const std::string source =
      "book memory-check\n"
      "run-memory begin paragraph\n"
      "  own variable data-buffer as array of number sized 2\n"
      "  borrow buffer-view from data-buffer\n"
      "  move data-buffer into moved-buffer\n"
      "  allocate pointer-value as pointer to number sized 2\n"
      "  value item-count is the length of moved-buffer\n"
      "  append 1 to moved-buffer\n"
      "  evaluate apply worker-name across moved-buffer\n"
      "  attempt\n"
      "  caught anything as fault-value\n"
      "    raise fault-value again\n"
      "  end attempt\n"
      "end paragraph\n";
  const auto program = parse(source);
  const auto body = cast<ny::ast_block_stmt>(
      cast<ny::ast_function_decl>(program->declarations.at(1))->body);
  const auto binding = [&](std::size_t index) {
    return cast<ny::ast_variable_decl>(
        cast<ny::ast_decl_stmt>(body->statements.at(index))->declaration);
  };
  check_span(source, binding(0)->name_span, "variable data-buffer", "data-buffer");
  check_span(source, binding(1)->name_span, "borrow buffer-view", "buffer-view");
  check_span(source, binding(2)->name_span, "into moved-buffer", "moved-buffer");
  check_span(source, binding(3)->name_span, "allocate pointer-value", "pointer-value");
  for (std::size_t i = 0; i < 3; ++i)
    require(!cast<ny::ast_call_expr>(binding(i)->initializer)->callee->name_span,
            "generated ownership operation has no source name");
  check_span(source, cast<ny::ast_call_expr>(binding(1)->initializer)->args.at(0)->name_span,
             "from data-buffer", "data-buffer");
  check_span(source, cast<ny::ast_call_expr>(binding(2)->initializer)->args.at(0)->name_span,
             "move data-buffer", "data-buffer");
  require(!cast<ny::ast_call_expr>(binding(4)->initializer)->callee->name_span,
          "generated length member has no source name");
  require(!cast<ny::ast_call_expr>(cast<ny::ast_expr_stmt>(body->statements.at(5))->expression)
               ->callee->name_span, "generated push member has no source name");
  const auto parallel = cast<ny::ast_call_expr>(
      cast<ny::ast_expr_stmt>(body->statements.at(6))->expression);
  require(!parallel->callee->name_span, "generated parallel map has no source name");
  check_span(source, parallel->args.at(0)->name_span, "apply worker-name", "worker-name");
  const auto handler = cast<ny::ast_attempt_stmt>(body->statements.at(7))->handlers.at(0);
  check_span(source, handler->name_span, "as fault-value", "fault-value");
  check_span(source, cast<ny::ast_raise_stmt>(
      cast<ny::ast_block_stmt>(handler->body)->statements.at(0))->fault_expr->name_span,
      "raise fault-value", "fault-value");
}

} // namespace

int main() {
  try {
    test_lexer_source_ranges();
    test_names_and_uses();
    test_members_and_types();
    test_lowered_bindings();
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << "Matrix source span tests passed\n";
}
