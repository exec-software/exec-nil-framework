#include "submission_request.hpp"
#include "nildata.hpp"
#include "numeric_literal.hpp"
#include "../common/strict_json.hpp"

#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using nil::detail::json;
int failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

json object(std::initializer_list<std::pair<const std::string, json>> fields) {
  auto result = json::map();
  result.object = fields;
  return result;
}

json list(std::initializer_list<json> items) {
  auto result = json::list();
  result.array = items;
  return result;
}

json number(const std::string &spelling) {
  return object({{"kind", json::str("number")}, {"value", json::str(spelling)}});
}

json name(const std::string &spelling) {
  return object({{"kind", json::str("name")}, {"name", json::str(spelling)}});
}

json boolean(bool value) {
  return object({{"kind", json::str("boolean")}, {"value", json::flag(value)}});
}

json unary(const std::string &op, json value) {
  return object({{"kind", json::str("unary")}, {"operator", json::str(op)},
                 {"operand", std::move(value)}});
}

json envelope(json value = {}, json bindings = json::list()) {
  return object({{"schema", json::str("nil.matrix.submission-request.v1")},
                 {"profile", json::str("matrix.pure.v1")},
                 {"bindings", std::move(bindings)}, {"value", std::move(value)}});
}

std::string document(const json &value) {
  return nil::data::from_json(nil::detail::dump(value));
}

matrix::decoded_submission_request decode(json value, json bindings = json::list()) {
  return matrix::decode_submission_request(document(envelope(std::move(value), std::move(bindings))));
}

void rejects_source(const std::string &source, const std::string &path,
                    const std::string &reason, const std::string &description) {
  try {
    matrix::decode_submission_request(source);
    check(false, description + " must be rejected");
  } catch (const matrix::submission_request_error &error) {
    const std::string message = error.what();
    check(error.path == path && message.rfind(path, 0) == 0 && message.find(reason) != std::string::npos,
          description + " must identify its request path and reason: " + message);
  }
}

void rejects(json request, const std::string &path, const std::string &reason,
             const std::string &description) {
  rejects_source(document(request), path, reason, description);
}

void test_envelope_and_names() {
  auto empty = decode({});
  check(empty.body && empty.body->statements.empty() && !empty.value,
        "null final value represents a void submission");
  check(empty.body->source_file == "$.bindings" && empty.body->line == 0,
        "synthetic AST locations carry request paths without invented source lines");

  auto bindings = list({object({{"name", json::str("Frame-Count")}, {"value", number("7")}}),
                        object({{"name", json::str("saved")}, {"value", name("FRAMECOUNT")}})});
  auto request = decode(name("frame_count"), bindings);
  auto statement = std::dynamic_pointer_cast<ny::ast_decl_stmt>(request.body->statements[0]);
  auto declaration = std::dynamic_pointer_cast<ny::ast_variable_decl>(statement->declaration);
  auto reference = std::dynamic_pointer_cast<ny::ast_identifier_expr>(request.value);
  check(declaration && declaration->name == "Frame_Count" && !declaration->is_mut &&
            !declaration->is_const && !declaration->type_name,
        "bindings use ordinary immutable Matrix value declarations");
  check(reference && reference->name == "Frame_Count" && !reference->resolved_global_name,
        "references use declaration spelling after Matrix case/separator folding");
  auto saved_stmt = std::dynamic_pointer_cast<ny::ast_decl_stmt>(request.body->statements[1]);
  auto saved = std::dynamic_pointer_cast<ny::ast_variable_decl>(saved_stmt->declaration);
  auto saved_reference = std::dynamic_pointer_cast<ny::ast_identifier_expr>(saved->initializer);
  check(saved_reference && saved_reference->name == "Frame_Count" &&
            saved_reference->source_file == "$.bindings[1].value",
        "binding initializers resolve through the same name table and retain paths");

  for (const auto &spelling : {"normal", "CamelCase", "two-parts", "two_parts", "a_s", "caf\xc3\xa9"}) {
    auto allowed = decode(name(spelling));
    check(allowed.value && allowed.value->kind == ny::ast_node_kind::identifier_expr_node,
          std::string("ordinary Matrix identifier accepted: ") + spelling);
  }
  for (const auto &spelling : {"", "name ", " name", "a b", "`escaped`", "book::name",
                              "host.call", "name\n", "name // ignored", "begin", "42", "bad--name"})
    rejects(envelope(name(spelling)), "$.value.name", "unescaped Matrix identifier",
            std::string("non-simple identifier ") + nil::detail::quote(spelling));

  bindings.array.push_back(object({{"name", json::str("frame_count")}, {"value", number("8")}}));
  rejects(envelope({}, bindings), "$.bindings[2].name", "duplicate binding",
          "case and separator folded binding collision");
  rejects(envelope({}, list({object({{"name", json::str("foo")}, {"value", {}}})})),
          "$.bindings[0].value", "expression object", "null binding value");
}

void test_numbers() {
  for (const auto &spelling : {"0", "2147483647", "2147483648", "9223372036854775807",
                              "1.5", "6.02e23", "5e-324", "0e999999999999999999"}) {
    auto value = std::dynamic_pointer_cast<ny::ast_number_literal>(decode(number(spelling)).value);
    check(value && value->value == spelling, std::string("numeric lexeme preserved: ") + spelling);
  }
  for (const auto &spelling : {"-1", "-0", "-0.0", "-1.5", "-5e-324"}) {
    auto value = std::dynamic_pointer_cast<ny::ast_unary_expr>(decode(number(spelling)).value);
    auto magnitude = value ? std::dynamic_pointer_cast<ny::ast_number_literal>(value->operand) : nullptr;
    check(value && value->op == ny::token_kind::minus && magnitude &&
              magnitude->value == std::string(spelling).substr(1),
          std::string("signed number uses Matrix unary semantics: ") + spelling);
  }
  for (const auto &spelling : {"-2147483648", "-9223372036854775808"}) {
    auto raw = std::dynamic_pointer_cast<ny::ast_number_literal>(decode(number(spelling)).value);
    auto explicit_negative = std::dynamic_pointer_cast<ny::ast_number_literal>(
        decode(unary("negative", number(std::string(spelling).substr(1)))).value);
    ny::integer_literal_info info;
    check(raw && raw->value == spelling && explicit_negative && explicit_negative->value == spelling &&
              !ny::parse_integer_literal(raw->value, info),
          std::string("signed boundary matches raw and explicit unary forms: ") + spelling);
    check(std::string(info.inferred_type()) == (std::string(spelling) == "-2147483648" ? "i32" : "i64"),
          "signed boundaries keep their Matrix inferred scalar width");
  }
  auto double_negative = std::dynamic_pointer_cast<ny::ast_unary_expr>(
      decode(unary("negative", number("-9223372036854775808"))).value);
  check(double_negative && std::dynamic_pointer_cast<ny::ast_number_literal>(double_negative->operand),
        "double negation of a signed minimum remains ordinary typed arithmetic");

  for (const auto &spelling : {"", " 1", "1 ", "+1", "01", "-01", "0x10", "NaN", "Infinity",
                              "true", "null", "[]", "1,2", "1.", ".5", "1e", "1\n"})
    rejects(envelope(number(spelling)), "$.value.value", "exact JSON numeric token",
            std::string("invalid exact numeric token ") + nil::detail::quote(spelling));
  for (const auto &spelling : {"9223372036854775808", "-9223372036854775809", "18446744073709551616",
                              "1e309", "1e-400", "-1e400", "-1e-400"})
    rejects(envelope(number(spelling)), "$.value.value", "representable",
            std::string("out of range numeric token ") + spelling);
  rejects(envelope(unary("not", number("9223372036854775808"))),
          "$.value.operand.value", "representable", "positive signed minimum without negative operator");
  auto wrong_number = number("1");
  wrong_number.object["value"] = nil::detail::json_parser("1").parse();
  rejects(envelope(wrong_number), "$.value.value", "expected text", "numeric value must be lexical text");
}

void test_expression_shapes() {
  const std::string literal_text = "line\n\"quoted\"\\path\t\xc3\xa9";
  auto literal = std::dynamic_pointer_cast<ny::ast_string_literal>(
      decode(object({{"kind", json::str("text")}, {"value", json::str(literal_text)}})).value);
  check(literal && literal->value == literal_text, "text is decoded once without source-language escaping");
  auto flag = std::dynamic_pointer_cast<ny::ast_boolean_literal>(decode(boolean(true)).value);
  check(flag && flag->value, "boolean uses its typed schema value");
  for (const auto &operation : std::map<std::string, ny::token_kind>{
           {"negative", ny::token_kind::minus}, {"not", ny::token_kind::not_kw},
           {"bitwise-not", ny::token_kind::tilde}}) {
    auto value = std::dynamic_pointer_cast<ny::ast_unary_expr>(decode(unary(operation.first, number("4"))).value);
    check(value && value->op == operation.second, "canonical unary operator maps to shared AST: " + operation.first);
  }
  for (const auto &operation : std::map<std::string, ny::token_kind>{
           {"plus", ny::token_kind::plus}, {"minus", ny::token_kind::minus},
           {"times", ny::token_kind::star}, {"divided-by", ny::token_kind::slash},
           {"modulo", ny::token_kind::percent}, {"equals", ny::token_kind::double_equal},
           {"not-equals", ny::token_kind::not_equal}, {"less-than", ny::token_kind::less},
           {"less-or-equal", ny::token_kind::less_equal}, {"greater-than", ny::token_kind::greater},
           {"greater-or-equal", ny::token_kind::greater_equal}, {"and", ny::token_kind::amp_amp},
           {"or", ny::token_kind::pipe_pipe}, {"bitwise-and", ny::token_kind::amp},
           {"bitwise-or", ny::token_kind::pipe}, {"bitwise-xor", ny::token_kind::caret},
           {"shift-left", ny::token_kind::less_less}, {"shift-right", ny::token_kind::greater_greater}}) {
    auto value = std::dynamic_pointer_cast<ny::ast_binary_expr>(decode(object({
        {"kind", json::str("binary")}, {"operator", json::str(operation.first)},
        {"left", number("3")}, {"right", number("2")}})).value);
    check(value && value->op == operation.second && value->left->source_file == "$.value.left" &&
              value->right->source_file == "$.value.right", "binary child order and opcode: " + operation.first);
  }

  auto tuple = object({{"kind", json::str("tuple")}, {"items", list({number("1"), boolean(false)})}});
  auto tuple_ast = std::dynamic_pointer_cast<ny::ast_struct_literal_expr>(decode(tuple).value);
  check(tuple_ast && tuple_ast->tuple_literal && tuple_ast->type_name == "tuple" &&
            tuple_ast->fields.size() == 2 && tuple_ast->fields[0].first == "0" &&
            tuple_ast->fields[1].first == "1", "tuple uses ordered shared structural fields");
  auto member = std::dynamic_pointer_cast<ny::ast_member_access_expr>(decode(object({
      {"kind", json::str("member")}, {"target", tuple}, {"index", json::str("1")}})).value);
  check(member && member->member_name == "1" && member->object->source_file == "$.value.target",
        "tuple projection decodes a static position");
  auto array = object({{"kind", json::str("array")}, {"items", list({number("1"), number("2")})}});
  auto array_ast = std::dynamic_pointer_cast<ny::ast_array_literal_expr>(decode(array).value);
  check(array_ast && array_ast->elements.size() == 2, "array retains ordered elements");
  auto index = std::dynamic_pointer_cast<ny::ast_index_expr>(decode(object({
      {"kind", json::str("index")}, {"target", array}, {"index", number("0")}})).value);
  check(index && index->object && index->index && index->index->source_file == "$.value.index",
        "index retains both target and subscript expressions");
  auto conditional = std::dynamic_pointer_cast<ny::ast_if_expr>(decode(object({
      {"kind", json::str("conditional")}, {"condition", boolean(true)},
      {"then", number("1")}, {"otherwise", number("2")}})).value);
  check(conditional && conditional->condition && conditional->then_expr && conditional->else_expr,
        "conditional retains all three independent expressions");
  for (const auto &type : {"bool", "i32", "i64", "u8", "f64"}) {
    auto cast = std::dynamic_pointer_cast<ny::ast_cast_expr>(decode(object({
        {"kind", json::str("cast")}, {"value", number("1")}, {"type", json::str(type)}})).value);
    check(cast && cast->target_type == type && !cast->checked_identity,
          std::string("primitive cast is decoded without forging checker proof: ") + type);
  }
  // These are structurally valid requests. The independent checker must still
  // reject undeclared references, heterogeneous arrays, invalid casts, etc.
  check(decode(name("undefined-name")).value != nullptr,
        "decoder does not invent bindings for unresolved names");
  check(decode(object({{"kind", json::str("array")}, {"items", list({})}})).value != nullptr,
        "empty array type inference remains a checker responsibility");
}

void test_schema_rejections() {
  for (const auto &field : {"schema", "profile", "bindings", "value"}) {
    auto missing = envelope(); missing.object.erase(field);
    rejects(missing, std::string("$.") + field, "missing field", std::string("missing envelope ") + field);
  }
  for (const auto &field : {"capabilities", "authority", "trusted", "imports", "source", "statements", "execute"}) {
    auto forged = envelope(); forged.object[field] = json::flag(true);
    rejects(forged, "$[" + nil::detail::quote(field) + "]", "unknown field",
            std::string("forged envelope field ") + field);
  }
  auto punctuated_key = envelope(); punctuated_key.object["unknown: field"] = json::flag(true);
  rejects(punctuated_key, "$[\"unknown: field\"]", "unknown field",
          "diagnostic path remains structural when an unknown key contains a colon");
  auto schema = envelope(); schema.object["schema"] = json::str("nil.matrix.submission-request.v2");
  rejects(schema, "$.schema", "unsupported", "unknown schema version");
  auto profile = envelope(); profile.object["profile"] = json::str("matrix.privileged.v1");
  rejects(profile, "$.profile", "unsupported", "unknown authority profile");
  auto wrong_bindings = envelope(); wrong_bindings.object["bindings"] = json::str("none");
  rejects(wrong_bindings, "$.bindings", "array", "wrong binding collection type");
  rejects(envelope({}, list({number("1")})), "$.bindings[0][\"kind\"]", "unknown field", "expression in binding slot");
  rejects(envelope({}, list({object({{"name", json::str("x")}, {"value", number("1")},
                                    {"mutable", json::flag(true)}})})),
          "$.bindings[0][\"mutable\"]", "unknown field", "mutable binding request");
  rejects(envelope(json::str("say dangerous")), "$.value", "expression object", "source text in expression slot");
  rejects(envelope(object({})), "$.value.kind", "missing field", "expression without kind");
  for (const auto &kind : {"call", "query", "import", "assembly", "allocate", "statement", "null", "Number"})
    rejects(envelope(object({{"kind", json::str(kind)}})), "$.value.kind", "unknown expression kind",
            std::string("unsupported expression kind ") + kind);
  for (const auto &field : {"inferred_type", "checked_identity", "resolved_global_name", "annotations", "source_file"}) {
    auto forged = number("1"); forged.object[field] = json::str("trusted");
    rejects(envelope(forged), "$.value[" + nil::detail::quote(field) + "]", "unknown field",
            std::string("forged AST metadata ") + field);
  }
  rejects(envelope(object({{"kind", json::str("boolean")}, {"value", json::str("true")}})),
          "$.value.value", "boolean", "text instead of boolean");
  rejects(envelope(object({{"kind", json::str("tuple")}, {"items", list({})}})),
          "$.value.items", "at least one", "empty tuple");
  rejects(envelope(unary("minus", number("1"))), "$.value.operator", "unknown unary operator",
          "noncanonical unary alias");
  rejects(envelope(object({{"kind", json::str("binary")}, {"operator", json::str("assign")},
                           {"left", number("1")}, {"right", number("2")}})),
          "$.value.operator", "unknown binary operator", "assignment operator");
  for (const auto &position : {"", "-1", "+1", "01", "1.0", "1e1", "1 ", "9223372036854775808", "run"})
    rejects(envelope(object({{"kind", json::str("member")}, {"target", name("pair")},
                             {"index", json::str(position)}})), "$.value.index", "tuple position",
            std::string("noncanonical tuple position ") + nil::detail::quote(position));
  for (const auto &type : {"number", "floating", "text", "f32", "i16", "u64", "ptr<i32>", "array<i32>", "Host", "void"})
    rejects(envelope(object({{"kind", json::str("cast")}, {"value", number("1")},
                             {"type", json::str(type)}})), "$.value.type", "primitive type",
            std::string("noncanonical or unsupported cast ") + type);
}

void test_encoding_and_limits() {
  rejects_source("{}", "$", "nildata", "unversioned JSON input");
  rejects_source("nildata 1\nobject\n", "$", "end object", "truncated NILData");
  rejects_source("nildata 1\nobject\nx is 1\nx is 2\nend object\n", "$", "duplicate", "duplicate source fields");
  auto raw_nul = document(envelope()); raw_nul += '\0';
  rejects_source(raw_nul, "$", "NUL", "raw embedded NUL");
  auto invalid_utf8 = document(envelope()); invalid_utf8 += char(0xff);
  rejects_source(invalid_utf8, "$", "UTF-8", "invalid UTF-8 source");
  rejects(envelope(object({{"kind", json::str("text")}, {"value", json::str(std::string("a\0b", 3))}})),
          "$[\"value\"][\"value\"]", "NUL", "escaped NUL in text value");
  auto nul_key = envelope(); nul_key.object[std::string("a\0b", 3)] = json::flag(true);
  rejects(nul_key, "$[\"a\\u0000b\"]", "NUL", "escaped NUL in field name");

  auto exact_limit = document(envelope()); exact_limit.resize(64 * 1024, ' ');
  check(!matrix::decode_submission_request(exact_limit).value, "exactly 64 KiB remains within the source bound");
  exact_limit += ' ';
  rejects_source(exact_limit, "$", "64 KiB", "source one byte beyond limit");

  // Build malformed depth directly: the NILData encoder independently enforces
  // its nesting bound and would reject before the decoder under test is called.
  std::string deep = "nildata 1\nobject\nschema is \"nil.matrix.submission-request.v1\"\n"
                     "profile is \"matrix.pure.v1\"\nbindings is an array\nend array\nvalue is an object\n";
  for (int i = 0; i < 35; ++i)
    deep += "kind is \"unary\"\noperator is \"not\"\noperand is an object\n";
  deep += "kind is \"boolean\"\nvalue is true\nend object\n";
  for (int i = 0; i < 35; ++i) deep += "end object\n";
  deep += "end object\n";
  rejects_source(deep, "$", "32", "excessively nested expression document");

  auto many = json::list();
  for (int i = 0; i < 400; ++i) many.array.push_back(number(std::to_string(i)));
  auto wide = decode(object({{"kind", json::str("array")}, {"items", std::move(many)}}));
  auto array = std::dynamic_pointer_cast<ny::ast_array_literal_expr>(wide.value);
  check(array && array->elements.size() == 400, "a bounded wide expression is retained without truncation");
}
} // namespace

int main() {
  try {
    test_envelope_and_names();
    test_numbers();
    test_expression_shapes();
    test_schema_rejections();
    test_encoding_and_limits();
  } catch (const std::exception &error) {
    check(false, std::string("unexpected exception: ") + error.what());
  }
  if (failures) {
    std::cerr << failures << " submission request test(s) failed\n";
    return 1;
  }
  std::cout << "submission request tests passed\n";
  return 0;
}
