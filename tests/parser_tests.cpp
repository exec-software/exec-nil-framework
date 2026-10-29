#include "parser.hpp"
#include "codegen.hpp"
#include "nil_loader.hpp"
#include "type_checker.hpp"
#include "matrix_lexer.hpp"
#include "matrix_parser.hpp"
#include "numeric_literal.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <map>
#include <sstream>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void test_leading_method_annotations() {
  ny::parser parser(
      "@ai_context(\"top-level context\")\n"
      "pub fn top() -> i32 { return 1 }\n"
      "@intrinsic(\"math.abs\")\n"
      "pub fn absolute(value: i32) -> i32\n"
      "class Reader\n"
      "value: i32\n"
      "@ai_context(\"method context\")\n"
      "@custom(outer(inner(\"payload\")))\n"
      "pub virtual fn read(self) -> i32 { return self.value }\n"
      "end class\n"
      "class Child inherits Reader\n"
      "@ai_context(\"override context\")\n"
      "pub override fn read(self) -> i32 { return self.value + 1 }\n"
      "end class\n"
      "abstract class Abstract\n"
      "@ai_context(\"abstract context\")\n"
      "pub abstract fn read(self) -> i32\n"
      "end fn\n"
      "end class\n");
  const auto program = parser.parse_program();
  check(program && !parser.has_errors(),
        "leading function and class-method annotations should parse");
  if (!program || parser.has_errors() || program->declarations.size() != 5) {
    check(false, "annotation fixture should retain both functions and three classes");
    return;
  }
  const auto top = std::dynamic_pointer_cast<ny::ast_function_decl>(program->declarations[0]);
  const auto intrinsic = std::dynamic_pointer_cast<ny::ast_function_decl>(program->declarations[1]);
  check(top && top->annotations.size() == 1,
        "top-level annotations must remain unique (actual count " +
            std::to_string(top ? top->annotations.size() : 0) + ")");
  check(top && top->ai_context == std::optional<std::string>("top-level context"),
        "top-level annotations must set AI context before body parsing (actual '" +
            (top ? top->ai_context.value_or("<missing>") : "<missing function>") + "')");
  check(intrinsic && intrinsic->annotations.size() == 1 &&
            intrinsic->annotations[0].name == "intrinsic" &&
            intrinsic->annotations[0].args == std::optional<std::string>("\"math.abs\""),
        "bodyless intrinsic declarations must retain their quoted annotation payload");
  const auto reader = std::dynamic_pointer_cast<ny::ast_class_decl>(program->declarations[2]);
  const auto child = std::dynamic_pointer_cast<ny::ast_class_decl>(program->declarations[3]);
  const auto abstract = std::dynamic_pointer_cast<ny::ast_class_decl>(program->declarations[4]);
  check(reader && reader->methods.size() == 1 && child && child->methods.size() == 1 &&
            abstract && abstract->methods.size() == 1,
        "annotated class methods must stay attached to their declaring classes");
  if (!reader || reader->methods.size() != 1 || !child || child->methods.size() != 1 ||
      !abstract || abstract->methods.size() != 1) return;
  const auto &method = reader->methods.front();
  check(method->is_public() && method->is_virtual && method->annotations.size() == 2 &&
            method->ai_context == std::optional<std::string>("method context") &&
            method->annotations[0].name == "ai_context" &&
            method->annotations[1].name == "custom" &&
            method->annotations[1].args == std::optional<std::string>("outer(inner(\"payload\"))"),
        "method annotations must preserve order, nested arguments, visibility and virtual modifier");
  check(child->methods.front()->is_override &&
            child->methods.front()->ai_context == std::optional<std::string>("override context"),
        "override method annotations must survive modifiers");
  check(abstract->methods.front()->is_abstract &&
            abstract->methods.front()->ai_context == std::optional<std::string>("abstract context"),
        "abstract method annotations must survive bodyless parsing");

  for (const auto &source : {
           "class Invalid\n@ai_context(\"field\")\nvalue: i32\nend class\n",
           "class Invalid {\n@ai_context(\"dangling\")\n}\n"}) {
    ny::parser invalid(source);
    invalid.parse_program();
    bool diagnosed = false;
    for (const auto &diagnostic : invalid.diagnostics())
      if (diagnostic.message == "Class member annotations are supported only on methods")
        diagnosed = true;
    check(invalid.has_errors() && diagnosed,
          "nonmethod class annotations must be rejected rather than silently discarded");
  }
}

void test_variable_visibility_and_source_lines() {
  ny::parser parser(
      "\n"
      "let private_value: i32 = 1\n"
      "pub let public_value: i32 = 2\n"
      "const private_constant: i32 = 3\n"
      "pub const public_constant: i32 = 4\n"
      "fn locals()\n"
      " let local_value: i32 = 5\n"
      " const local_constant: i32 = 6\n"
      "end fn\n");
  const auto program = parser.parse_program();
  check(program && !parser.has_errors(),
        "variable visibility and source-line fixture should parse");
  if (!program || parser.has_errors() || program->declarations.size() != 5) {
    check(false, "visibility fixture must retain four globals and a function");
    return;
  }
  const auto inspect = [](const ny::ast_decl_ptr &declaration,
                          const std::string &name, bool is_public,
                          bool is_const, int line) {
    const auto variable = std::dynamic_pointer_cast<ny::ast_variable_decl>(declaration);
    check(variable && variable->name == name, name + " must remain a variable declaration");
    if (!variable) return;
    check(variable->get_visibility() == (is_public
              ? ny::visibility_kind::visibility_public
              : ny::visibility_kind::visibility_private) &&
              variable->is_pub == is_public && variable->is_public() == is_public,
          name + " must preserve its visibility independently of its source line");
    check(variable->line == line, name + " must preserve its exact nonzero source line");
    check(variable->is_const == is_const, name + " must preserve let/const distinction");
  };
  inspect(program->declarations[0], "private_value", false, false, 2);
  inspect(program->declarations[1], "public_value", true, false, 3);
  inspect(program->declarations[2], "private_constant", false, true, 4);
  inspect(program->declarations[3], "public_constant", true, true, 5);

  const auto function = std::dynamic_pointer_cast<ny::ast_function_decl>(
      program->declarations[4]);
  const auto block = function
      ? std::dynamic_pointer_cast<ny::ast_block_stmt>(function->body) : nullptr;
  check(block && block->statements.size() == 2,
        "visibility fixture must retain both local declarations");
  if (!block || block->statements.size() != 2) return;
  for (std::size_t index = 0; index < 2; ++index) {
    const auto statement = std::dynamic_pointer_cast<ny::ast_decl_stmt>(block->statements[index]);
    check(statement != nullptr, "local variables must remain declaration statements");
    if (statement)
      inspect(statement->declaration, index == 0 ? "local_value" : "local_constant",
              false, index == 1, 7 + static_cast<int>(index));
  }
}

void test_decoded_string_quotes_are_content() {
  ny::parser parser(R"ny(
let quoted = "\"word\""
let two_quotes = "\"\""
let one_quote = "\""
let left = "\"left"
let right = "right\""
let json_escape = "\"a\\u0000b\""
)ny");
  const auto program = parser.parse_program();
  const std::vector<std::string> expected{
      "\"word\"", "\"\"", "\"", "\"left", "right\"", "\"a\\u0000b\""};
  check(program && !parser.has_errors() && program->declarations.size() == expected.size(),
        "quoted string contents should parse without losing declarations");
  if (!program || parser.has_errors() || program->declarations.size() != expected.size()) return;
  for (std::size_t i = 0; i < expected.size(); ++i) {
    const auto variable = std::dynamic_pointer_cast<ny::ast_variable_decl>(program->declarations[i]);
    const auto literal = variable
        ? std::dynamic_pointer_cast<ny::ast_string_literal>(variable->initializer) : nullptr;
    check(literal && literal->value == expected[i],
          "decoded quotes and backslashes remain literal contents at position " + std::to_string(i));
  }
}

void test_bitwise_complement_precedence() {
  ny::parser parser("fn main() -> i32\nreturn ~~1 & 3\nend fn\n");
  const auto program = parser.parse_program();
  check(!parser.has_errors(), "bitwise complement should parse as a unary prefix");
  if (!program || program->declarations.size() != 1) {
    check(false, "complement fixture must retain its function");
    return;
  }
  const auto function = std::dynamic_pointer_cast<ny::ast_function_decl>(
      program->declarations[0]);
  const auto block = function
      ? std::dynamic_pointer_cast<ny::ast_block_stmt>(function->body) : nullptr;
  const auto result = block && block->statements.size() == 1
      ? std::dynamic_pointer_cast<ny::ast_return_stmt>(block->statements[0]) : nullptr;
  const auto binary = result
      ? std::dynamic_pointer_cast<ny::ast_binary_expr>(result->value) : nullptr;
  const auto outer = binary
      ? std::dynamic_pointer_cast<ny::ast_unary_expr>(binary->left) : nullptr;
  const auto inner = outer
      ? std::dynamic_pointer_cast<ny::ast_unary_expr>(outer->operand) : nullptr;
  check(binary && binary->op == ny::token_kind::amp &&
        outer && outer->op == ny::token_kind::tilde &&
        inner && inner->op == ny::token_kind::tilde,
        "complement must nest rightward and bind more tightly than binary AND");
}

void test_inline_assembly_ast() {
  const std::string source = "fn main() -> i32\n"
                             "    asm \"mov rax, rax\"\n"
                             "    end asm\n"
                             "    return 0\n"
                             "end fn\n";

  ny::parser parser(source);
  const auto program = parser.parse_program();

  check(!parser.has_errors(), "an inline assembly block should parse cleanly");
  check(program && program->declarations.size() == 1,
        "the parser should retain the containing function");
  if (!program || program->declarations.size() != 1)
    return;

  const auto function = std::dynamic_pointer_cast<ny::ast_function_decl>(
      program->declarations[0]);
  check(function && function->body,
        "the parsed declaration should have a function body");
  if (!function || !function->body)
    return;

  const auto block =
      std::dynamic_pointer_cast<ny::ast_block_stmt>(function->body);
  check(block && block->statements.size() == 2,
        "the function body should retain asm and return statements");
  if (!block || block->statements.empty())
    return;

  const auto assembly =
      std::dynamic_pointer_cast<ny::ast_asm_stmt>(block->statements[0]);
  check(assembly != nullptr, "the first statement should be an asm AST node");
  if (!assembly)
    return;

  check(assembly->assembly_code == "mov rax, rax",
        "the asm AST node should preserve its source payload");
  check(assembly->line == 2,
        "the asm AST node should point at the opening asm keyword");
}

void test_comparison_before_later_greater_is_not_a_generic_call() {
  const std::string source =
      "fn scan(offset: i32, item: range, limit: i32)\n"
      "    while offset < item.finish\n"
      "        offset = offset + 1\n"
      "    end while\n"
      "    if offset > (limit) then\n"
      "        return\n"
      "    end if\n"
      "end fn\n";

  ny::parser parser(source);
  const auto program = parser.parse_program();

  if (parser.has_errors()) {
    for (const auto &diagnostic : parser.diagnostics()) {
      std::cerr << "parser diagnostic at line " << diagnostic.line << ": "
                << diagnostic.message << '\n';
    }
  }
  check(!parser.has_errors(),
        "a member comparison must not consume a later greater-than token as "
        "generic syntax");
  check(program && program->declarations.size() == 1,
        "the comparison regression should retain its function declaration");
  if (!program || program->declarations.size() != 1)
    return;

  const auto function = std::dynamic_pointer_cast<ny::ast_function_decl>(
      program->declarations[0]);
  const auto block = function
                         ? std::dynamic_pointer_cast<ny::ast_block_stmt>(
                               function->body)
                         : nullptr;
  const auto loop = block && !block->statements.empty()
                        ? std::dynamic_pointer_cast<ny::ast_while_stmt>(
                              block->statements[0])
                        : nullptr;
  const auto comparison = loop
                              ? std::dynamic_pointer_cast<ny::ast_binary_expr>(
                                    loop->condition)
                              : nullptr;
  const auto member = comparison
                          ? std::dynamic_pointer_cast<ny::ast_member_access_expr>(
                                comparison->right)
                          : nullptr;
  check(comparison && comparison->op == ny::token_kind::less,
        "the loop condition should remain a less-than expression");
  check(member && member->member_name == "finish",
        "the less-than right operand should remain item.finish");
}

void test_inline_assembly_operands() {
  ny::parser parser(
      "fn main() -> i32\n"
      " let mut result: i64 = 0\n"
      " asm \"x86_64\", \"movq $1, $0\" (42 as i64) -> result end asm\n"
      " return 0\nend fn\n");
  const auto program = parser.parse_program();
  check(!parser.has_errors(), "targeted assembly operands should parse");
  if (!program || program->declarations.empty())
    return;
  auto fn = std::dynamic_pointer_cast<ny::ast_function_decl>(program->declarations[0]);
  auto block = fn ? std::dynamic_pointer_cast<ny::ast_block_stmt>(fn->body) : nullptr;
  if (!block || block->statements.size() < 2) {
    check(false, "targeted assembly statement should be retained");
    return;
  }
  auto assembly = std::dynamic_pointer_cast<ny::ast_asm_stmt>(block->statements[1]);
  check(assembly && assembly->target == "x86_64" &&
            assembly->inputs.size() == 1 && assembly->output == "result" &&
            assembly->assembly_code == "movq $1, $0",
        "assembly target, template, input, and output must survive parsing");
}

void test_assembly_and_registration_constant_pools() {
  ny::parser parser(
      "fn main() -> i32\n"
      " asm \"x86_64\", \"nop\" () end asm\n"
      " println(\"literal from main\")\n"
      " return 0\nend fn\n"
      "test \"registration description\"\n"
      " println(\"literal from test\")\nend test\n");
  const auto program = parser.parse_program();
  check(!parser.has_errors(), "assembly with attached test must parse");
  if (!program || parser.has_errors())
    return;
  ny::codegen_options options;
  options.mode = ny::build_mode::test_mode;
  ny::codegen generator(options);
  std::vector<ny::codegen_diagnostic> diagnostics;
  auto module = generator.generate_module(*program, &diagnostics);
  check(module && diagnostics.empty(), "test-mode assembly must generate NIL");
  if (!module)
    return;
  const auto inspect = [](const ny::nil_module &mod) {
    std::map<std::string, std::string> constants;
    bool has_registration = false;
    for (const auto &entry : mod.string_constants) {
      check(constants.emplace(entry).second, "constant pool names must be unique");
      has_registration |= entry.second == "registration description";
    }
    check(has_registration, "test-mode registration constants must be present");
    bool has_assembly = false;
    for (const auto &function : mod.functions) {
      for (const auto &instruction : function.body) {
        if (instruction.opname == "INLINE_ASM") {
          has_assembly = true;
          check(instruction.args.size() == 4 &&
                    constants.at(instruction.args[1]) == "nop",
                "assembly must retain its own template after pool merging");
        }
      }
    }
    check(has_assembly, "test-mode NIL must contain assembly");
  };
  inspect(*module);
  for (const bool binary : {false, true}) {
    std::stringstream stream;
    ny::nil_writer writer(stream, options);
    if (binary)
      writer.emit_binary_module(*module);
    else
      writer.emit_text_module(*module);
    try {
      auto decoded = binary ? ny::parseBinaryNilModule(stream)
                            : ny::parseTextNilModule(stream);
      inspect(*decoded);
    } catch (const std::exception &error) {
      check(false, std::string("test-mode constant roundtrip failed: ") + error.what());
    }
  }
}

void test_nested_qualified_generic_call_still_parses() {
  const std::string source =
      "fn convert(input: i32)\n"
      "    let value = tools::convert<collections::box<model::leaf>>(input)\n"
      "end fn\n";

  ny::parser parser(source);
  const auto program = parser.parse_program();

  if (parser.has_errors()) {
    for (const auto &diagnostic : parser.diagnostics()) {
      std::cerr << "parser diagnostic at line " << diagnostic.line << ": "
                << diagnostic.message << '\n';
    }
  }
  check(!parser.has_errors(),
        "nested, qualified generic-call syntax should parse cleanly");
  check(program && program->declarations.size() == 1,
        "the generic-call regression should retain its function declaration");
}

void test_checker_sessions_do_not_share_impl_visitation() {
  ny::parser parser(
      "struct Record\nvalue: i64\nend struct\n"
      "impl Record\nfn invalid(self) -> i64\nreturn \"not an integer\"\nend fn\nend impl\n");
  const auto program = parser.parse_program();
  check(program && !parser.has_errors(), "repeated checker fixture should parse");
  if (!program || parser.has_errors()) return;
  ny::type_checker first;
  check(!first.check(*program), "the first checker must reject an invalid impl body");
  check(!first.check(*program), "a reused checker must revalidate the same impl body");
  ny::type_checker second;
  check(!second.check(*program), "another checker must not inherit visited AST addresses");

  ny::parser helper_parser("fn helper() -> i32\nreturn 1\nend fn\n");
  const auto helper_program = helper_parser.parse_program();
  ny::parser consumer_parser("fn main() -> i32\nreturn helper()\nend fn\n");
  const auto consumer_program = consumer_parser.parse_program();
  check(helper_program && consumer_program && !helper_parser.has_errors() &&
            !consumer_parser.has_errors(), "isolated checker fixtures should parse");
  if (helper_program && consumer_program) {
    check(first.check(*helper_program), "a later valid program must clear old diagnostics");
    check(!first.check(*consumer_program), "a later program must not inherit old declarations");
  }

  bool stable = true;
  for (int i = 0; i < 100005; ++i) {
    const auto parts = first.parse_generic_type_string("Box<Pair<i32, i64>>");
    stable = stable && parts.base_name == "Box" && parts.type_args.size() == 1 &&
             parts.type_args.front() == "Pair<i32, i64>";
  }
  check(stable, "generic type parsing must not degrade after a process-wide call limit");
}

void test_enum_variant_mismatch_terminates() {
  // 'enum a::b' is ten bytes. The variant loop used to meet a token that is
  // neither 'end enum' nor a variant name, fail to advance past it, and append
  // a nameless variant and another diagnostic on every turn -- unbounded, so
  // the process died of std::bad_alloc rather than reporting the fault. A NIL
  // artifact handed to the compiler as source reaches this through its 'enum'
  // lines, which is how it was found. Reaching the assertions at all is most
  // of what this checks; the bounded count is what says it stopped early
  // rather than merely slowly.
  ny::parser parser("enum a::b\n");
  const auto program = parser.parse_program();
  check(parser.has_errors(), "a malformed enum variant list must be reported");
  check(parser.diagnostics().size() < 100,
        "a malformed enum variant list must stop, not accumulate diagnostics");

  // The same shape with a symbol the lexer does produce inside a body.
  ny::parser trailing("enum a\n    +\n");
  trailing.parse_program();
  check(trailing.has_errors(), "a symbol where a variant belongs must be reported");
  check(trailing.diagnostics().size() < 100,
        "a symbol where a variant belongs must stop the variant loop");

  // A well-formed enum must still parse, so the guard did not cost the feature.
  ny::parser good("enum Visibility\n    Hidden\n    Visible\nend enum\n");
  const auto good_program = good.parse_program();
  check(good_program && !good.has_errors(), "a well-formed enum must still parse");
  check(good_program && good_program->declarations.size() == 1,
        "a well-formed enum must keep its declaration");
}

void test_scientific_numeric_literals() {
  const std::string source = "1e3 1.5e-3 1E+2 5e-324 0xDEAD 0x1E3";
  const std::vector<std::string> expected = {
      "1e3", "1.5e-3", "1E+2", "5e-324", "0xDEAD", "0x1E3"};
  ny::lexer ny_lexer(source);
  const auto ny_tokens = ny_lexer.scan_tokens();
  matrix::lexer matrix_lexer(source);
  const auto matrix_tokens = matrix_lexer.scan();
  check(ny_tokens.size() == expected.size() + 1 &&
            matrix_tokens.size() == expected.size() + 1 && !matrix_lexer.has_errors(),
        "both lexers preserve exponent signs inside a single number token");
  for (std::size_t i = 0; i < expected.size(); ++i) {
    check(i < ny_tokens.size() && ny_tokens[i].kind == ny::token_kind::number_literal &&
              ny_tokens[i].lexeme == expected[i], "Ny numeric token " + expected[i]);
    check(i < matrix_tokens.size() && matrix_tokens[i].kind == matrix::token_kind::number_literal &&
              matrix_tokens[i].lexeme == expected[i], "Matrix numeric token " + expected[i]);
  }
  const std::string ny_range = "1..3";
  const auto range = ny::lexer(ny_range).scan_tokens();
  check(range.size() == 4 && range[1].kind == ny::token_kind::double_dot,
        "numeric scanning preserves Ny ranges");
  const std::string matrix_period = "say 1e3.";
  const auto sentence = matrix::lexer(matrix_period).scan();
  check(sentence.size() == 4 && sentence[2].kind == matrix::token_kind::statement_end,
        "numeric scanning preserves Matrix sentence periods");

  double number = 0.0;
  check(!ny::parse_floating_literal("5e-324", number) &&
            number == std::numeric_limits<double>::denorm_min(),
        "the smallest representable subnormal is accepted and rounded");
  check(!ny::parse_floating_literal("-0e999999999999999999999", number) &&
            number == 0.0 && std::signbit(number), "negative exact zero preserves its sign");
  check(!ny::parse_floating_literal("+1E+2", number) && number == 100.0,
        "signed textual NIL exponent literal");
  for (const auto *spelling : {"1e", "1e+", "1e-", "1e309", "1e-324", "1e-9999999999"}) {
    check(ny::parse_floating_literal(spelling, number) != nullptr,
          std::string("invalid or unrepresentable float is rejected: ") + spelling);
  }
  for (const auto *spelling : {"0xDEAD", "0xfeed", "0x1E3", "-0x1E3"}) {
    check(!ny::is_floating_literal_spelling(spelling),
          std::string("hexadecimal e/E remains an integer digit: ") + spelling);
  }
}

void test_integer_literal_ranges_and_unchecked_arrays() {
  for (const auto &entry : std::vector<std::pair<std::string, std::string>>{
           {"2147483647", "i32"}, {"-2147483648", "i32"},
           {"2147483648", "i64"}, {"-2147483649", "i64"},
           {"9223372036854775807", "i64"}, {"-9223372036854775808", "i64"},
           {"0xFFFFFFFF", "i32"}, {"0x100000000", "i64"},
           {"-0x8000000000000000", "i64"}, {"0002147483648", "i64"}}) {
    ny::integer_literal_info info;
    check(!ny::parse_integer_literal(entry.first, info) &&
              info.inferred_type() == entry.second,
          "integer literal width for " + entry.first);
  }
  for (const auto *spelling : {"9223372036854775808", "-9223372036854775809",
                              "0x8000000000000000", "0xFFFFFFFFFFFFFFFF",
                              "18446744073709551616", "0x", "12junk"}) {
    ny::integer_literal_info info;
    check(ny::parse_integer_literal(spelling, info) != nullptr,
          std::string("invalid signed integer is rejected: ") + spelling);
  }
  for (const auto *spelling : {"9223372036854775808", "0x8000000000000000"}) {
    ny::parser valid("fn main() -> i32\nlet value = -(" + std::string(spelling) +
                     ")\nreturn 0\nend fn\n");
    valid.parse_program();
    check(!valid.has_errors(), "Ny parenthesized unary minimum parses");
    matrix::parser valid_matrix("book tests.integer-boundaries\nmain returns number begin paragraph\n"
        "value sample is minus (" + std::string(spelling) + ")\ngive 0\nend paragraph\n");
    valid_matrix.parse_program();
    check(!valid_matrix.has_errors(), "Matrix parenthesized unary minimum parses");
    ny::parser invalid("fn unused(value: i64 = " + std::string(spelling) +
                       ")\nend fn\n");
    invalid.parse_program();
    check(invalid.has_errors(), "unused default cannot hide a positive minimum magnitude");
  }

  ny::parser unchecked(
      "fn main() -> i32\n"
      " let wide = 2147483648\n let small: i32 = 1\n"
      " let a = [1, 2147483648]\n let b = [2147483648, 1]\n"
      " let c = [small, wide]\n let d = [wide, small]\n"
      " let e = [-2147483648, 0xFFFFFFFF]\n"
      " let f = [1, 0x100000000]\n"
      " let legacy = 2147483647 + 1\n return 0\nend fn\n");
  const auto program = unchecked.parse_program();
  check(program && !unchecked.has_errors(), "unchecked array fixture parses");
  if (!program || unchecked.has_errors()) return;
  // Deliberately skip type_checker::check. The CLI's --no-typecheck flag still
  // invokes the checker and therefore cannot qualify this fallback by itself.
  ny::codegen generator;
  std::vector<ny::codegen_diagnostic> diagnostics;
  const auto module = generator.generate_module(*program, &diagnostics);
  check(module && diagnostics.empty(), "unchecked literal/variable arrays lower");
  if (module) {
    std::vector<std::string> elements;
    bool legacy_word_add = false;
    for (const auto &function : module->functions)
      for (const auto &instruction : function.body) {
        if (instruction.opname == "NEW_ARRAY_LITERAL" && instruction.args.size() == 2)
          elements.push_back(instruction.args[1]);
        if (instruction.opname == "ADD" && instruction.args.empty()) legacy_word_add = true;
      }
    check(elements == std::vector<std::string>{"i64", "i64", "i64", "i64", "i32", "i64"},
          "unchecked arrays join every literal/variable width symmetrically");
    check(legacy_word_add, "unchecked arithmetic keeps legacy untyped word semantics");
  }
  for (const auto *elements : {"1, true, 2147483648", "2147483648, true, 1"}) {
    ny::parser mixed("fn main() -> i32\nlet values = [" + std::string(elements) +
                     "]\nreturn 0\nend fn\n");
    const auto mixed_program = mixed.parse_program();
    ny::type_checker checker;
    check(mixed_program && !checker.check(*mixed_program),
          "final integer array width cannot inherit bool/i32 compatibility");
  }
  ny::parser arms("fn main() -> i32\nmatch 7\n"
                  "0 -> return 2\n - 1\n"
                  "-2 -> return 3\n_ -> return 0\nend match\nreturn 0\nend fn\n");
  const auto arm_program = arms.parse_program();
  check(arm_program && !arms.has_errors(),
        "negative later arms coexist with multiline subtraction");
  if (arm_program && !arms.has_errors()) {
    const auto function = std::dynamic_pointer_cast<ny::ast_function_decl>(arm_program->declarations[0]);
    const auto block = std::dynamic_pointer_cast<ny::ast_block_stmt>(function->body);
    const auto match = std::dynamic_pointer_cast<ny::ast_match_stmt>(block->statements[0]);
    const auto result = match && !match->arms.empty()
        ? std::dynamic_pointer_cast<ny::ast_return_stmt>(match->arms[0].body) : nullptr;
    const auto subtraction = result ? std::dynamic_pointer_cast<ny::ast_binary_expr>(result->value) : nullptr;
    check(match && match->arms.size() == 3 && subtraction &&
              subtraction->op == ny::token_kind::minus,
          "only a numeric pattern followed by an arrow ends the previous expression");
  }
  ny::parser arithmetic_patterns(
      "fn main() -> i32\n"
      "println(match 1 { 2 - 1 -> 7; _ -> 0; })\n"
      "println(match 1 { 1 when 2 == 3 - 1 -> 8; _ -> 0; })\n"
      "match 7\n-1 -> return 1\n"
      "7 -> println(match 1 { 2 - 1 -> 7; _ -> 0; })\n"
      "-2 -> return 2\n_ -> return 0\nend match\n"
      "return 0\nend fn\n");
  const auto arithmetic_program = arithmetic_patterns.parse_program();
  check(arithmetic_program && !arithmetic_patterns.has_errors(),
        "subtraction in patterns/guards and nested matches is not an arm boundary");
  if (arithmetic_program && !arithmetic_patterns.has_errors()) {
    ny::type_checker checker;
    check(checker.check(*arithmetic_program),
          "arithmetic expression patterns and guards retain their checked meaning");
  }
}

} // namespace

int main() {
  test_leading_method_annotations();
  test_variable_visibility_and_source_lines();
  test_decoded_string_quotes_are_content();
  test_bitwise_complement_precedence();
  test_inline_assembly_ast();
  test_inline_assembly_operands();
  test_assembly_and_registration_constant_pools();
  test_comparison_before_later_greater_is_not_a_generic_call();
  test_nested_qualified_generic_call_still_parses();
  test_checker_sessions_do_not_share_impl_visitation();
  test_enum_variant_mismatch_terminates();
  test_scientific_numeric_literals();
  test_integer_literal_ranges_and_unchecked_arrays();
  if (failures != 0)
    return 1;

  std::cout << "parser tests passed\n";
  return 0;
}
