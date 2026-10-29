#include "submission_profile.hpp"
#include "codegen.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
using namespace ny;
int failures = 0, checks = 0;
using body = std::vector<nil_instruction>;
void expect(const std::string &name, bool actual, bool expected) {
  ++checks;
  if (actual != expected) { ++failures; std::cerr << "FAIL " << name << '\n'; }
}
nil_module module(body instructions = {{"PUSH_CONST", {"3"}}, {"CAST", {"i32"}}, {"RET", {}}}, std::string result = "i32") {
  nil_module value;
  value.module_name = "root";
  nil_function function;
  function.name = "__nil_submission";
  function.return_type = std::move(result);
  function.body = std::move(instructions);
  value.functions.push_back(std::move(function));
  return value;
}
bool admitted(const nil_module &value, bool verbose = false) {
  try { validate_nil_submission_profile(value); return true; }
  catch (const std::runtime_error &error) { if (verbose) std::cerr << error.what() << '\n'; return false; }
}
void nil_case(const std::string &name, const nil_module &value, bool accepted) {
  expect(name, admitted(value, accepted), accepted);
}
void mutation(const std::string &name, const std::function<void(nil_module &)> &change) {
  auto value = module(); change(value); nil_case(name, value, false);
}
ast_expr_ptr number(std::string value = "1") { return std::make_shared<ast_number_literal>(std::move(value), 7); }
ast_expr_ptr call() { return std::make_shared<ast_call_expr>(std::make_shared<ast_identifier_expr>("effect")); }
ast_stmt_ptr local(const std::string &name, ast_expr_ptr value, bool mutable_value = false) {
  return std::make_shared<ast_decl_stmt>(std::make_shared<ast_variable_decl>(name, std::move(value), std::nullopt, mutable_value));
}
void ast_case(const std::string &name, const std::vector<ast_stmt_ptr> &statements, ast_expr_ptr value, bool accepted) {
  ast_block_stmt block; block.statements = statements;
  expect(name, validate_submission_ast(block, value).empty(), accepted);
}
}
int main() {
  using namespace ny;
  nil_case("scalar", module(), true);
  nil_case("void", module({{"RET", {}}}, "void"), true);
  nil_case("typed arithmetic", module({{"PUSH_CONST", {"2"}}, {"PUSH_CONST", {"3"}}, {"ADD", {"i32"}}, {"RET", {}}}), true);
  nil_case("float arithmetic", module({{"PUSH_CONST", {"2.5"}}, {"NEG", {"f64"}}, {"RET", {}}}, "f64"), true);
  nil_case("local", module({{"PUSH_CONST", {"2"}}, {"STORE_LOCAL", {"x"}}, {"LOAD_LOCAL", {"x"}}, {"RET", {}}}), true);
  nil_case("array index", module({{"PUSH_CONST", {"2"}}, {"PUSH_CONST", {"3"}}, {"NEW_ARRAY_LITERAL", {"2","i32"}},
    {"STORE_LOCAL", {"values"}}, {"LOAD_LOCAL", {"values"}}, {"PUSH_CONST", {"0"}}, {"GET_INDEX", {}}, {"RET", {}}}), true);
  nil_case("empty concrete array", module({{"NEW_ARRAY_LITERAL", {"0","i32"}}, {"RET", {}}}, "array<i32>"), true);
  auto text = module({{"PUSH_CONST_STR", {"S1"}}, {"PUSH_CONST", {"42"}}, {"TO_STRING", {"i32"}}, {"STR_CONCAT", {}}, {"RET", {}}}, "text");
  text.string_constants.emplace_back("S1", "literal CALL external\n# safe data");
  nil_case("formatted text", text, true);
  auto tuple = module({{"NEW_STRUCT", {"tuple<i32,text>"}}, {"PUSH_CONST", {"2"}}, {"CAST", {"i32"}}, {"SET_FIELD", {"0"}},
    {"PUSH_CONST_STR", {"S1"}}, {"SET_FIELD", {"1"}}, {"RET", {}}}, "tuple<i32,text>");
  tuple.string_constants.emplace_back("S1", "text");
  tuple.structs.push_back({"tuple<i32,text>", {{"0","i32"},{"1","text"}}, {"@nil_tuple"}});
  nil_case("fresh tuple", tuple, true);
  auto tuple_field = tuple; tuple_field.functions[0].body.pop_back();
  tuple_field.functions[0].body.insert(tuple_field.functions[0].body.end(), {{"STORE_LOCAL", {"tuple"}}, {"LOAD_LOCAL", {"tuple"}}, {"GET_FIELD", {"1"}}, {"RET", {}}});
  tuple_field.functions[0].return_type = "text";
  nil_case("immutable tuple field", tuple_field, true);
  auto nested = module({{"NEW_STRUCT", {"tuple<tuple<i32,text>>"}}}, "tuple<tuple<i32,text>>");
  nested.structs = tuple.structs; nested.structs.push_back({"tuple<tuple<i32,text>>", {{"0","tuple<i32,text>"}}, {"@nil_tuple"}});
  nested.string_constants = tuple.string_constants;
  auto &nested_body = nested.functions[0].body;
  nested_body.insert(nested_body.end(), tuple.functions[0].body.begin(), tuple.functions[0].body.end()-1);
  nested_body.insert(nested_body.end(), {{"SET_FIELD", {"0"}}, {"RET", {}}});
  nil_case("nested fresh tuple", nested, true);
  auto conditional = module({{"PUSH_CONST", {"1"}}, {"JMP_IF_NOT", {"else"}}, {"PUSH_CONST", {"2"}},
    {"STORE_LOCAL", {"result"}}, {"JMP", {"end"}}, {"LABEL", {"else"}}, {"PUSH_CONST", {"3"}},
    {"STORE_LOCAL", {"result"}}, {"LABEL", {"end"}}, {"LOAD_LOCAL", {"result"}}, {"RET", {}}});
  nil_case("branch-specific immutable result", conditional, true);

  for (const auto &opcode : {"CALL", "CALL_METHOD", "CALL_INDIRECT", "PUSH_FN_PTR", "INTEROP_CALL", "INLINE_ASM",
       "CONSOLE_WRITE", "TIME_NOW", "ALLOC", "READ_PTR", "WRITE_PTR", "PTR_ADD", "REF_LOCAL", "OWN_ALLOC",
       "STORE_GLOBAL", "LOAD_GLOBAL", "PARALLEL_MAP", "NEW_VARIANT", "ARRAY_PUSH", "SET_INDEX", "FAULT_RAISE", "UNKNOWN", "push_const"}) {
    mutation(std::string("forbidden ") + opcode, [&](auto &value) { value.functions[0].body.insert(value.functions[0].body.begin(), {opcode, {}}); });
    mutation(std::string("dead forbidden ") + opcode, [&](auto &value) { value.functions[0].body.push_back({opcode, {}}); });
  }
  mutation("extra function", [](auto &value) { value.functions.push_back(value.functions[0]); });
  mutation("wrong function", [](auto &value) { value.functions[0].name = "main"; });
  mutation("forged module", [](auto &value) { value.module_name = "outside"; });
  mutation("library", [](auto &value) { value.is_library = true; });
  mutation("dependency", [](auto &value) { value.dependencies.push_back("host"); });
  mutation("embedded source", [](auto &value) { value.embedded_sources.push_back({"evil.ny", "effect()"}); });
  mutation("class", [](auto &value) { value.classes.push_back({}); });
  mutation("enum", [](auto &value) { value.enums.push_back({}); });
  mutation("tests metadata", [](auto &value) { value.tests.push_back("__nil_submission"); });
  mutation("test flag", [](auto &value) { value.functions[0].is_test = true; });
  mutation("inline flag", [](auto &value) { value.functions[0].is_inline = true; });
  mutation("parameter", [](auto &value) { value.functions[0].params.push_back("capability: ptr<i32>"); });
  mutation("generic", [](auto &value) { value.functions[0].generic_params.push_back("T"); });
  mutation("invalid declaration line", [](auto &value) { value.functions[0].source_line = -1; });
  mutation("constant name syntax injection", [](auto &value) { value.string_constants.emplace_back("s;CALL", "data"); });
  for (const auto &annotation : {"@intrinsic(\"io.open\")", "@interop(\"libhost\")", "@extern", "@custom", "@nil_tuple"})
    mutation(std::string("function annotation ") + annotation, [&](auto &value) { value.functions[0].annotations.push_back(annotation); });
  for (const auto &type : {"ptr<i32>", "fn()->i32", "owned<i32>", "Custom", "any", "array<any>", "tuple<ptr<i32>>", "array<i32,i64>", "tuple<>", "i32 trailing"})
    mutation(std::string("forbidden return ") + type, [&](auto &value) { value.functions[0].return_type = type; });
  for (const auto &value : {"NaN", "Infinity", "1 trailing", "1e9999", "9223372036854775808", "0;CALL"})
    mutation(std::string("invalid constant ") + value, [&](auto &result) { result.functions[0].body[0].args = {value}; });
  mutation("extra operand", [](auto &value) { value.functions[0].body[0].args.push_back("CALL"); });
  mutation("missing operand", [](auto &value) { value.functions[0].body[0].args.clear(); });
  mutation("bad cast", [](auto &value) { value.functions[0].body[1].args = {"ptr<i32>"}; });
  mutation("extra return operand", [](auto &value) { value.functions[0].body.back().args = {"outside"}; });
  mutation("missing return", [](auto &value) { value.functions[0].body.pop_back(); });
  mutation("empty function", [](auto &value) { value.functions[0].body.clear(); });
  nil_case("stack underflow", module({{"ADD", {"i32"}}, {"RET", {}}}), false);
  nil_case("unbound local", module({{"LOAD_LOCAL", {"x"}}, {"RET", {}}}), false);
  nil_case("mutated local", module({{"PUSH_CONST", {"1"}}, {"STORE_LOCAL", {"x"}}, {"PUSH_CONST", {"2"}}, {"STORE_LOCAL", {"x"}}, {"RET", {}}}, "void"), false);
  nil_case("leftover operand", module({{"PUSH_CONST", {"1"}}, {"PUSH_CONST", {"2"}}, {"RET", {}}}), false);
  nil_case("cycle", module({{"LABEL", {"again"}}, {"JMP", {"again"}}, {"RET", {}}}, "void"), false);
  nil_case("missing branch label", module({{"JMP", {"missing"}}, {"RET", {}}}, "void"), false);
  nil_case("duplicate label", module({{"LABEL", {"x"}}, {"LABEL", {"x"}}, {"RET", {}}}, "void"), false);
  nil_case("dead safe code", module({{"RET", {}}, {"PUSH_CONST", {"1"}}, {"POP", {}}}, "void"), false);
  auto bad_join = conditional; bad_join.string_constants.emplace_back("S", "wrong"); bad_join.functions[0].body[6] = {"PUSH_CONST_STR", {"S"}};
  nil_case("incompatible branch local", bad_join, false);
  auto maybe_local = conditional; maybe_local.functions[0].body[7] = {"STORE_LOCAL", {"other"}};
  nil_case("conditionally uninitialized local", maybe_local, false);
  auto duplicate_constant = text; duplicate_constant.string_constants.push_back(duplicate_constant.string_constants[0]);
  nil_case("duplicate string constant", duplicate_constant, false);
  auto unknown_constant = text; unknown_constant.functions[0].body[0].args = {"absent"};
  nil_case("unknown string constant", unknown_constant, false);
  auto bad_text_arithmetic = text; bad_text_arithmetic.functions[0].body[3] = {"ADD", {"i32"}};
  nil_case("text arithmetic operand", bad_text_arithmetic, false);
  nil_case("untyped array", module({{"NEW_ARRAY_LITERAL", {"0","any"}}, {"RET", {}}}, "array<i32>"), false);
  nil_case("forged array metadata", module({{"PUSH_CONST", {"1.5"}}, {"NEW_ARRAY_LITERAL", {"1","i32"}}, {"RET", {}}}, "array<i32>"), false);
  nil_case("malformed array count", module({{"NEW_ARRAY_LITERAL", {"-1","i32"}}, {"RET", {}}}, "array<i32>"), false);
  for (const auto &change : std::vector<std::function<void(nil_module &)>>{
    [](auto &value) { value.structs[0].annotations.push_back("@intrinsic"); },
    [](auto &value) { value.structs[0].annotations.clear(); },
    [](auto &value) { value.structs[0].fields[0].second = "text"; },
    [](auto &value) { value.structs[0].fields[0].first = "danger"; },
    [](auto &value) { value.structs.push_back(value.structs[0]); },
    [](auto &value) { value.structs.clear(); },
    [](auto &value) { value.functions[0].body[3].args = {"1"}; },
    [](auto &value) { value.functions[0].body.erase(value.functions[0].body.begin()+5); },
    [](auto &value) { value.functions[0].body.insert(value.functions[0].body.end()-1, {{"PUSH_CONST", {"3"}}, {"SET_FIELD", {"0"}}}); }
  }) { auto value = tuple; change(value); nil_case("forged or mutable tuple", value, false); }
  mutation("instruction count bound", [](auto &value) { value.functions[0].body.resize(4097, {"RET", {}}); });
  mutation("text budget", [](auto &value) { value.string_constants.emplace_back("huge", std::string(1024*1024+1, 'x')); });
  mutation("instruction debug injection", [](auto &value) { value.functions[0].body[0].source_file = "untrusted"; });

  ast_case("empty AST", {}, {}, true);
  ast_case("literal AST", {}, number(), true);
  ast_case("immutable local AST", {local("value", number())}, std::make_shared<ast_identifier_expr>("value"), true);
  ast_case("arithmetic AST", {}, std::make_shared<ast_binary_expr>(number(), token_kind::plus, number()), true);
  ast_case("minimum literal AST", {}, std::make_shared<ast_unary_expr>(token_kind::minus, number("9223372036854775808")), true);
  ast_case("numeric cast AST", {}, std::make_shared<ast_cast_expr>(number(), "f64"), true);
  ast_case("array AST", {}, std::make_shared<ast_array_literal_expr>(std::vector<ast_expr_ptr>{number(),number()}), true);
  ast_case("conditional AST", {}, std::make_shared<ast_if_expr>(std::make_shared<ast_boolean_literal>(true), number(), number()), true);
  auto ast_tuple = std::make_shared<ast_struct_literal_expr>("tuple", std::vector<std::pair<std::string, ast_expr_ptr>>{{"0",number()},{"1",std::make_shared<ast_string_literal>("text")}});
  ast_tuple->tuple_literal = true;
  auto destructuring = std::make_shared<ast_variable_decl>("", ast_tuple);
  destructuring->tuple_bindings = {std::make_shared<ast_variable_decl>("left"),std::make_shared<ast_variable_decl>("right")};
  ast_case("tuple destructuring AST", {std::make_shared<ast_decl_stmt>(destructuring)}, std::make_shared<ast_identifier_expr>("left"), true);
  ast_case("tuple field AST", {}, std::make_shared<ast_member_access_expr>(ast_tuple,"0"), true);
  auto typed_tuple = std::make_shared<ast_variable_decl>("typed", ast_tuple, "tuple<i32, String>");
  ast_case("source tuple aliases and spacing", {std::make_shared<ast_decl_stmt>(typed_tuple)}, {}, true);
  ast_case("interpolation AST", {}, std::make_shared<ast_interpolated_string_expr>(std::vector<interpolated_part>{{true,"value ",{}},{false,"",number()}}), true);
  ast_case("call AST", {}, call(), false);
  ast_case("function value AST", {}, std::make_shared<ast_identifier_expr>("__nil_submission"), false);
  ast_case("global AST", {}, std::make_shared<ast_identifier_expr>("outside"), false);
  ast_case("mutable local AST", {local("value",number(),true)}, {}, false);
  ast_case("escaped punctuation local AST", {local("value;CALL",number())}, {}, false);
  ast_case("mutation AST", {local("value",number())}, std::make_shared<ast_assignment_expr>(std::make_shared<ast_identifier_expr>("value"),number()), false);
  ast_case("dead conditional call AST", {}, std::make_shared<ast_if_expr>(std::make_shared<ast_boolean_literal>(false), call(), number()), false);
  ast_case("dead statement call AST", {std::make_shared<ast_if_stmt>(std::make_shared<ast_boolean_literal>(false),std::make_shared<ast_expr_stmt>(call()))}, {}, false);
  ast_case("hidden interpolation call AST", {}, std::make_shared<ast_interpolated_string_expr>(std::vector<interpolated_part>{{false,"",call()}}), false);
  ast_case("hidden interpolation literal AST", {}, std::make_shared<ast_interpolated_string_expr>(std::vector<interpolated_part>{{true,"safe",call()}}), false);
  ast_case("array hidden call AST", {}, std::make_shared<ast_array_literal_expr>(std::vector<ast_expr_ptr>{call()}), false);
  ast_case("pointer dereference AST", {}, std::make_shared<ast_unary_expr>(token_kind::star,number()), false);
  ast_case("pointer cast AST", {}, std::make_shared<ast_cast_expr>(number(),"ptr<i32>"), false);
  ast_case("reference AST", {}, std::make_shared<ast_unary_expr>(token_kind::ref_kw,number()), false);
  ast_case("method access AST", {}, std::make_shared<ast_member_access_expr>(number(),"danger"), false);
  ast_case("query AST", {}, std::make_shared<ast_query_expr>(), false);
  ast_case("return AST", {std::make_shared<ast_return_stmt>(number())}, {}, false);
  ast_case("print AST", {std::make_shared<ast_println_stmt>(std::vector<ast_expr_ptr>{number()})}, {}, false);
  ast_case("assembly AST", {std::make_shared<ast_asm_stmt>("syscall")}, {}, false);
  ast_case("loop AST", {std::make_shared<ast_while_stmt>(std::make_shared<ast_boolean_literal>(false),std::make_shared<ast_block_stmt>())}, {}, false);
  ast_case("import AST", {std::make_shared<ast_decl_stmt>(std::make_shared<ast_import_decl>("nil.std.io"))}, {}, false);
  auto annotated = number(); annotated->annotations.push_back({});
  ast_case("annotated expression AST", {}, annotated, false);
  auto illegal_type = std::make_shared<ast_variable_decl>("x",number(),"ptr<i32>");
  ast_case("pointer local type AST", {std::make_shared<ast_decl_stmt>(illegal_type)}, {}, false);
  auto branch_block = std::make_shared<ast_block_stmt>(); branch_block->statements = {local("hidden",number())};
  ast_case("branch local cannot escape AST", {std::make_shared<ast_if_stmt>(number(),branch_block)}, std::make_shared<ast_identifier_expr>("hidden"), false);
  auto deep = number();
  for (int i=0;i<70;++i) deep = std::make_shared<ast_unary_expr>(token_kind::minus,deep);
  ast_case("AST depth bound", {}, deep, false);
  std::vector<ast_stmt_ptr> many(4097,std::make_shared<ast_expr_stmt>(number()));
  ast_case("AST node bound", many, {}, false);
  auto diagnostics = validate_submission_ast(ast_block_stmt{}, annotated);
  expect("source line retained", !diagnostics.empty() && diagnostics[0].line == 7, true);
  auto missing = std::make_shared<ast_identifier_expr>("missing");
  missing->source_file = "$.value.left";
  auto invalid_binary = std::make_shared<ast_binary_expr>(missing, token_kind::plus, number());
  invalid_binary->source_file = "$.value";
  diagnostics = validate_submission_ast(ast_block_stmt{}, invalid_binary);
  expect("innermost request node path retained", diagnostics.size() == 1 && diagnostics[0].source_file == "$.value.left", true);
  auto forbidden = call(); forbidden->source_file = "$.value";
  diagnostics = validate_submission_ast(ast_block_stmt{}, forbidden);
  expect("forbidden request node path retained", diagnostics.size() == 1 && diagnostics[0].source_file == "$.value", true);
  std::cout << checks << " submission admission checks; " << failures << " failures\n";
  return failures ? 1 : 0;
}
