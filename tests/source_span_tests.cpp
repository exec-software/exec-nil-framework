#include "lexer.hpp"
#include "parser.hpp"
#include "type_checker.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const std::string &message) {
  if (!condition) throw std::runtime_error(message);
}

void expect_span(const std::string &source,
                 const std::optional<ny::source_span> &span,
                 size_t offset, const std::string &name) {
  require(span.has_value(), "missing source span for " + name);
  require(span->start == offset && span->length == name.size(),
          "incorrect source byte range for " + name);
  require(source.substr(span->start, span->length) == name,
          "source byte range does not select " + name);
}

template <typename T, typename U>
std::shared_ptr<T> node(const std::shared_ptr<U> &value) {
  auto result = std::dynamic_pointer_cast<T>(value);
  require(bool(result), "unexpected AST shape");
  return result;
}

std::shared_ptr<ny::ast_variable_decl> variable(const ny::ast_stmt_ptr &statement) {
  return node<ny::ast_variable_decl>(node<ny::ast_decl_stmt>(statement)->declaration);
}

void test_original_source(const std::string &newline, bool preprocess, bool markers = false) {
  const std::string prefix = "\xef\xbb\xbf// UTF-8: café 🧭" + newline + " \t" + newline;
  std::string source = prefix;
  if (markers) source += "  @pragma once\t" + newline + "\t@schema " + newline;
  for (const auto *line : {
      "struct Sample", "  value: i32", "end struct", "type Count = i32",
      "fn compute(input: i32) -> i32", "  let local = input",
      "  let object = Sample { value: local }",
      "  for (index, value) in [local]", "    println(index, value)", "  end for",
      "  let closure = fn(arg: i32) -> i32", "    return arg", "  end fn",
      "  return object.value + pkg.part::read(local, input)", "end fn"}) {
    source += line;
    source += newline;
  }
  source.resize(source.size() - newline.size()); // Missing final newline is supported.
  ny::parser_options options;
  options.enable_preprocessor = preprocess;
  ny::parser parser(source, options);
  const auto program = parser.parse_program();
  for (const auto &diagnostic : parser.diagnostics())
    std::cerr << diagnostic.line << ": " << diagnostic.message << '\n';
  require(program && !parser.has_errors(), "source-span fixture must parse");
  require(program->declarations.size() == 3, "source-span declaration count");
  const auto record = node<ny::ast_struct_decl>(program->declarations[0]);
  expect_span(source, record->name_span, source.find("Sample"), "Sample");
  expect_span(source, record->fields.at(0).name_span, source.find("value"), "value");
  const auto alias = node<ny::ast_type_alias_decl>(program->declarations[1]);
  expect_span(source, alias->name_span, source.find("Count"), "Count");
  const auto function = node<ny::ast_function_decl>(program->declarations[2]);
  expect_span(source, function->name_span, source.find("compute"), "compute");
  expect_span(source, function->params.at(0).name_span, source.find("input"), "input");
  const auto body = node<ny::ast_block_stmt>(function->body);
  const auto local = variable(body->statements.at(0));
  expect_span(source, local->name_span, source.find("local ="), "local");
  expect_span(source, local->initializer->name_span, source.find("= input") + 2, "input");
  const auto object = variable(body->statements.at(1));
  expect_span(source, object->initializer->name_span, source.find("= Sample") + 2, "Sample");
  const auto loop = node<ny::ast_for_in_stmt>(body->statements.at(2));
  expect_span(source, loop->variables.at(0).name_span, source.find("index"), "index");
  expect_span(source, loop->variables.at(1).name_span, source.find("index, value") + 7, "value");
  const auto lambda = node<ny::ast_lambda_expr>(variable(body->statements.at(3))->initializer);
  expect_span(source, lambda->params.at(0).name_span, source.find("arg:"), "arg");
  const auto result = node<ny::ast_return_stmt>(body->statements.at(4));
  const auto addition = node<ny::ast_binary_expr>(result->value);
  const auto member = node<ny::ast_member_access_expr>(addition->left);
  expect_span(source, member->name_span, source.find("object.value") + 7, "value");
  expect_span(source, member->object->name_span, source.find("return object") + 7, "object");
  const auto call = node<ny::ast_call_expr>(addition->right);
  expect_span(source, call->callee->name_span, source.find("::read") + 2, "read");
  const auto arguments = source.find("read(local") + 5;
  expect_span(source, call->argument_span, arguments, "local, input");
  require(call->argument_starts == std::vector<std::size_t>{arguments, source.find(", input)") + 1},
          "call slots must retain original byte positions after delimiters");
}

void test_transformed_source_is_not_editable() {
  ny::parser parser("@define READ compute\nfn READ(input: i32) -> i32 { return input }\n");
  const auto program = parser.parse_program();
  require(program && !parser.has_errors(), "macro fixture must parse");
  const auto function = node<ny::ast_function_decl>(program->declarations.at(0));
  require(function->name == "compute", "macro must expand");
  require(!function->name_span && !function->params.at(0).name_span,
          "transformed source must not expose editable declaration ranges");
  const auto body = node<ny::ast_block_stmt>(function->body);
  require(!node<ny::ast_return_stmt>(body->statements.at(0))->value->name_span,
          "transformed source must not expose editable identifier ranges");
}

void test_other_directives_remain_unmapped() {
  const std::string declaration = "fn compute(input: i32) -> i32 { return input }\n";
  for (const auto *directive : {"@define UNUSED 1\n", "@pragma once_extra\n",
                                "@schema extra\n", "@pragma link(\"example\")\n"}) {
    ny::parser parser(std::string(directive) + declaration);
    const auto program = parser.parse_program();
    require(program && !parser.has_errors(), "unsupported directive fixture must parse");
    const auto function = node<ny::ast_function_decl>(program->declarations.at(0));
    require(!function->name_span && !function->params.at(0).name_span,
            "other removed directives must not acquire guessed locations");
  }
  ny::parser_options options;
  options.filename = "/span-include.ny";
  options.preproc_options.resolve_source = [](const std::filesystem::path &path) {
    return std::optional<std::filesystem::path>(path);
  };
  options.preproc_options.read_source = [](const std::filesystem::path &) {
    return std::optional<std::string>("fn included() -> i32 { return 1 }\n");
  };
  ny::parser parser("@pragma once\n@include \"other.inc\"\n" + declaration, options);
  const auto program = parser.parse_program();
  require(program && !parser.has_errors(), "included span fixture must parse");
  require(program->declarations.size() == 2, "include fixture declaration count");
  for (const auto &declaration_node : program->declarations)
    require(!declaration_node->name_span, "includes must not inherit native root locations");
}

void test_methods_and_fields() {
  const std::string source =
      "class Reader\nvalue: i32\n"
      "fn read(self) -> i32 { return self.value }\nend class\n"
      "trait Readable\nfn measure(self, amount: i32) -> i32\nend trait\n";
  ny::parser parser(source);
  const auto program = parser.parse_program();
  require(program && !parser.has_errors(), "method span fixture must parse");
  const auto record = node<ny::ast_class_decl>(program->declarations.at(0));
  expect_span(source, record->name_span, source.find("Reader"), "Reader");
  expect_span(source, record->fields.at(0).name_span, source.find("value"), "value");
  const auto &method = record->methods.at(0);
  expect_span(source, method->name_span, source.find("read("), "read");
  expect_span(source, method->params.at(0).name_span, source.find("self"), "self");
  const auto result = node<ny::ast_return_stmt>(
      node<ny::ast_block_stmt>(method->body)->statements.at(0));
  const auto member = node<ny::ast_member_access_expr>(result->value);
  expect_span(source, member->object->name_span, source.find("return self") + 7, "self");
  const auto trait = node<ny::ast_trait_decl>(program->declarations.at(1));
  expect_span(source, trait->name_span, source.find("Readable"), "Readable");
  expect_span(source, trait->methods.at(0)->name_span, source.find("measure"), "measure");
  expect_span(source, trait->methods.at(0)->params.at(1).name_span,
              source.find("amount"), "amount");
}

void test_mapping_preserves_preprocessed_literal_semantics() {
  const std::string source =
      "fn literal() { let value = \"a\n \t\nb\"\nreturn value }\n";
  ny::parser parser(source);
  const auto program = parser.parse_program();
  require(program && !parser.has_errors(), "multiline literal fixture must parse");
  const auto function = node<ny::ast_function_decl>(program->declarations.at(0));
  const auto body = node<ny::ast_block_stmt>(function->body);
  const auto text = node<ny::ast_string_literal>(variable(body->statements.at(0))->initializer);
  require(text->value == "a\n\nb", "source mapping must preserve existing preprocessing semantics");
  const auto result = node<ny::ast_return_stmt>(body->statements.at(1));
  expect_span(source, result->value->name_span, source.find("return value") + 7, "value");
}

void test_explicit_interpolation_source_spans() {
  for (const bool preprocess : {false, true}) {
    const std::string source =
        "# λ 🧭\r\n\r\nfn message(input: i32) -> i32\r\n"
        "let rendered = $\"\\n{{literal}} {input + 1} {helper(input)}\"\r\n"
        "return input\r\nend fn\r\n";
    ny::parser_options options;
    options.enable_preprocessor = preprocess;
    ny::parser parser(source, options);
    const auto program = parser.parse_program();
    require(program && !parser.has_errors(), "explicit interpolation must parse");
    const auto body = node<ny::ast_block_stmt>(node<ny::ast_function_decl>(program->declarations.at(0))->body);
    const auto text = node<ny::ast_interpolated_string_expr>(variable(body->statements.at(0))->initializer);
    require(text->explicit_syntax && text->parts.size() == 4, "explicit interpolation keeps ordered parts");
    require(text->parts.at(0).literal_value == "\n{literal} ", "formatted literal decoding");
    const auto addition = node<ny::ast_binary_expr>(text->parts.at(1).expression);
    expect_span(source, addition->left->name_span, source.find("{input") + 1, "input");
    const auto call = node<ny::ast_call_expr>(text->parts.at(3).expression);
    expect_span(source, call->callee->name_span, source.find("helper(input)"), "helper");
    expect_span(source, call->args.at(0)->name_span, source.find("helper(input)") + 7, "input");
    require(call->argument_span && call->argument_span->start == source.find("helper(input)") + 7,
            "call signature region retains original interpolation coordinates");
    const auto result = node<ny::ast_return_stmt>(body->statements.at(1));
    expect_span(source, result->value->name_span, source.find("return input") + 7, "input");
    ny::lexer lexer(source);
    bool found = false;
    for (const auto &token : lexer.scan_tokens())
      if (token.kind == ny::token_kind::identifier && token.offset == source.find("{input") + 1) {
        found = token.lexeme == "input" && token.length == 5 && token.line == 4;
      }
    require(found, "formatted hole exposes an ordinary exact-range identifier token");
  }
  ny::parser nested("fn nested(input: i32)\nprintln($\"outer {$\"inner {input}\"}\")\nend fn\n");
  require(nested.parse_program() && !nested.has_errors(), "nested formatted expressions parse without escaping their quotes");
  for (const auto *literal : {"$\"{}\"", "$\"{1, 2}\"", "$\"{1\"", "$\"bad }\"", "$\"unfinished", "$\"one\ntwo\"", "$\"{1\n+ 2}\"", "$\"{\"a\\\nb\"}\""}) {
    ny::parser broken(std::string("fn broken()\nprintln(") + literal + ")\nend fn\n");
    broken.parse_program();
    require(broken.has_errors(), "malformed interpolation must be diagnosed");
  }
}

void test_interpolated_identifier_is_not_guessed() {
  ny::parser parser("fn message(value: i32) { println(\"\\n{value}\") }\n");
  const auto program = parser.parse_program();
  require(program && !parser.has_errors(), "interpolation fixture must parse");
  const auto function = node<ny::ast_function_decl>(program->declarations.at(0));
  const auto body = node<ny::ast_block_stmt>(function->body);
  const auto print = node<ny::ast_println_stmt>(body->statements.at(0));
  const auto text = node<ny::ast_interpolated_string_expr>(print->args.at(0));
  bool found = false;
  for (const auto &part : text->parts) {
    if (!part.is_literal) {
      found = true;
      require(!part.expression->name_span,
              "decoded interpolation must not guess original byte coordinates");
    }
  }
  require(found, "interpolation expression missing");
}

void test_lexer_source_ranges() {
  const std::string source = "// λ 🧭\r\nlet value = \"a\\nb\"";
  ny::lexer lexer(source);
  const auto tokens = lexer.scan_tokens();
  require(tokens.size() == 5, "lexer fixture token count");
  require(tokens[1].offset == source.find("value") && tokens[1].length == 5,
          "lexer identifier byte range must count UTF-8 and CRLF bytes");
  require(source.substr(tokens[3].offset, tokens[3].length) == "\"a\\nb\"",
          "lexer literal range must describe raw bytes rather than decoded text");
  require(tokens.back().offset == source.size() && tokens.back().length == 0,
          "EOF source range must be empty at end of input");
  const ny::token synthesized{ny::token_kind::identifier, "generated", 1};
  require(synthesized.length == 0, "synthesized token must have no source range");
}

void test_switch_source_spans() {
  const std::string source =
      "// λ switch input\r\nfn select(input: i32) -> i32\r\n"
      "let result = switch input\r\ncase 1:\r\nlet local = input\r\n"
      "break local\r\ndefault:\r\nbreak input\r\nend switch\r\n"
      "switch (input) { case 1: break; default: println(input); }\r\n"
      "switch input { default: break; }\r\nswitch input {}\r\n"
      "switch (input) + 1 { default: break; }\r\n"
      "switch (input) +\r\n1\r\ncase 2:\r\nbreak\r\nend switch\r\n"
      "let called = switch(input) + 1\r\n"
      "return result\r\nend fn\r\n";
  ny::parser parser(source);
  const auto program = parser.parse_program();
  require(program && !parser.has_errors(), "switch source-span fixture must parse");
  const auto body = node<ny::ast_block_stmt>(
      node<ny::ast_function_decl>(program->declarations.at(0))->body);
  const auto binding = node<ny::ast_variable_decl>(
      node<ny::ast_decl_stmt>(body->statements.at(0))->declaration);
  const auto value = node<ny::ast_switch_expr>(binding->initializer);
  require(value->arms.size() == 2 && !value->arms.at(1).label,
          "switch expression preserves ordered default arm");
  expect_span(source, value->target->name_span,
              source.find("switch input\r\ncase") + 7, "input");
  const auto first = node<ny::ast_block_stmt>(value->arms.at(0).body);
  const auto exit = node<ny::ast_break_stmt>(first->statements.at(1));
  require(exit->target == ny::break_target_kind::nearest && exit->value,
          "Ny value break targets the nearest breakable construct");
  expect_span(source, exit->value->name_span, source.find("break local") + 6, "local");
  const auto statement = node<ny::ast_switch_stmt>(body->statements.at(1));
  expect_span(source, statement->target->name_span,
              source.find("switch (input)") + 8, "input");
  require(!node<ny::ast_break_stmt>(
              node<ny::ast_block_stmt>(statement->arms.at(0).body)->statements.at(0))->value,
          "semicolon keeps ordinary break distinct from value break");
  require(!node<ny::ast_switch_stmt>(body->statements.at(2))->arms.at(0).label &&
              node<ny::ast_switch_stmt>(body->statements.at(3))->arms.empty(),
          "default-first and empty switch bodies are not record literals");
  for (size_t i : {size_t(4), size_t(5)})
    require(node<ny::ast_switch_stmt>(body->statements.at(i))->target->kind ==
                ny::ast_node_kind::binary_expr_node,
            "parenthesized selector may continue through a binary expression");
  const auto called = node<ny::ast_variable_decl>(
      node<ny::ast_decl_stmt>(body->statements.at(6))->declaration);
  require(node<ny::ast_binary_expr>(called->initializer)->left->kind ==
              ny::ast_node_kind::call_expr_node,
          "ordinary switch(input) plus expression remains a call");
}

void test_repeated_completion_occurrence_is_unavailable() {
  const std::string source = "fn compute(sample_input: i32) -> i32\nprintln(sample_input)\nreturn sample_input\nend fn\n";
  ny::parser_options options;
  options.filename = "/completion-occurrence.ny";
  ny::parser parser(source, options);
  const auto program = parser.parse_program();
  require(program && !parser.has_errors(), "completion fixture must parse");
  const auto function = node<ny::ast_function_decl>(program->declarations.at(0));
  const auto body = node<ny::ast_block_stmt>(function->body);
  const auto print = node<ny::ast_println_stmt>(body->statements.at(0));
  const auto result = node<ny::ast_return_stmt>(body->statements.at(1));
  const ny::completion_request request{options.filename, source.find("println(sample_input)") + 11};
  ny::type_checker ordinary(true, request);
  require(ordinary.check(*program), "ordinary completion fixture must check");
  require(ordinary.get_completion() && ordinary.get_completion()->available,
          "one original occurrence must retain completion");
  // Model a lowering that revisits the same physical source expression. Its
  // second inferred context must not silently inherit the first query result.
  result->value = print->args.at(0);
  ny::type_checker repeated(true, request);
  require(repeated.check(*program), "repeated completion fixture must check");
  require(repeated.get_completion() && !repeated.get_completion()->available &&
          repeated.get_completion()->candidates.empty(),
          "repeated physical source occurrences must withhold completion");
}
} // namespace

int main() {
  try {
    test_lexer_source_ranges();
    for (const auto &newline : {std::string("\n"), std::string("\r\n")})
      for (const bool preprocess : {false, true})
        test_original_source(newline, preprocess);
    for (const auto &newline : {std::string("\n"), std::string("\r\n")})
      test_original_source(newline, true, true);
    test_transformed_source_is_not_editable();
    test_other_directives_remain_unmapped();
    test_methods_and_fields();
    test_mapping_preserves_preprocessed_literal_semantics();
    test_interpolated_identifier_is_not_guessed();
    test_explicit_interpolation_source_spans();
    test_switch_source_spans();
    test_repeated_completion_occurrence_is_unavailable();
    std::cout << "source span tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
