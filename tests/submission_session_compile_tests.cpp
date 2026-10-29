#include "submission_session_compile.hpp"
#include "codegen.hpp"
#include <iostream>
#include <string>

namespace {
int checks = 0, failures = 0;
void expect(bool condition, const std::string &message) {
  ++checks;
  if (!condition) { ++failures; std::cerr << "FAIL " << message << '\n'; }
}
matrix::prepared_session_submission compile(
    const std::string &source, const std::vector<matrix::submission_binding> &bindings = {},
    bool request = false, const std::string &status = "complete") {
  auto result = matrix::prepare_session_submission(source, request, "session.matrix", bindings);
  expect(result.report.status == status, source + ": expected " + status + ", got " + result.report.status);
  if (result.report.status != status)
    for (const auto &issue : result.report.diagnostics)
      std::cerr << issue.location << ": " << issue.message << '\n';
  expect(static_cast<bool>(result.module) == (status == "complete"), "module available only after successful admission");
  return result;
}
ny::submission_value scalar(std::string type, std::string value) {
  return {std::move(type), std::move(value), {}};
}
std::string request(const std::string &name) {
  return "nildata 1\nobject\n"
         "  schema is \"nil.matrix.submission-request.v1\"\n"
         "  profile is \"matrix.pure.v1\"\n"
         "  bindings is an array\n  end array\n"
         "  value is an object\n    kind is \"name\"\n    name is \"" + name + "\"\n"
         "  end object\nend object\n";
}
}
int main() {
  const std::vector<matrix::submission_binding> prior = {{"Frame_Count", scalar("i64", "41")}};
  auto source = compile("frame-count plus 1", prior);
  expect(source.report.result_type == "i64" && source.has_value && source.binding_names.empty(),
         "retained folded name and scalar width survive compilation");
  expect(source.module && source.module->functions.front().params.empty(), "snapshot compilation requires no imported VM locals");
  const auto machine = compile(request("FRAMECOUNT"), prior, true);
  expect(machine.report.result_type == "i64", "request resolves existing folded names");
  compile("value FRAMECOUNT is 2", prior, false, "invalid");
  compile("unknown plus 1", prior, false, "invalid");
  compile("frame-count plus", prior, false, "incomplete");
  compile("say frame-count", prior, false, "invalid");

  auto defined = compile("value total is frame-count plus 1\nvalue (left, _) is (2, 3)\ntotal", prior);
  expect(defined.binding_names == std::vector<std::string>{"total", "left"}, "exports new top-level names in source order");
  expect(defined.module && defined.module->functions.front().return_type == "tuple<i64,i64,i32>",
         "return envelope contains final expression then export values");
  auto scoped = compile("when true then\n  value hidden is 1\nend when\nvalue hidden is 2\nhidden");
  expect(scoped.binding_names == std::vector<std::string>{"hidden"}, "nested bindings do not become session exports");
  expect(scoped.report.nil_text.find("$ny.local.") != std::string::npos,
         "test exercises codegen-renamed top-level storage");
  auto nested_only = compile("when true then\n  value private-name is 1\nend when");
  expect(nested_only.binding_names.empty() && nested_only.report.result_type == "void", "block-local state remains local");
  auto empty = compile("", prior);
  expect(!empty.has_value && empty.report.result_type == "void" && empty.binding_names.empty(), "empty submission returns void");
  auto declaration = compile("value count is 1");
  expect(!declaration.has_value && declaration.report.result_type == "void" && declaration.binding_names.size() == 1,
         "binding-only result hides its internal tuple from the user result type");

  for (const auto &value : {scalar("u8", "255"), scalar("i32", "-2147483648"),
                          scalar("i64", "-9223372036854775808"), scalar("f64", "-0.0"),
                          scalar("f64", "4.9406564584124654e-324"), scalar("bool", "true"),
                          scalar("text", "a quote \" and newline\n")}) {
    auto restored = compile("kept", {{"kept", value}});
    expect(restored.report.result_type == value.type, "restored scalar retains exact type " + value.type);
  }
  ny::submission_value empty_array{"array<i64>", "", {}};
  auto array = compile("items", {{"items", empty_array}});
  expect(array.report.result_type == "array<i64>", "empty array retains its element type");
  ny::submission_value tuple{"tuple<i64,text>", "", {scalar("i64", "1"), scalar("text", "two")}};
  auto tuple_result = compile("pair", {{"pair", tuple}});
  expect(tuple_result.report.result_type == tuple.type, "tuple fields retain exact scalar types");
  ny::submission_value nested{"array<tuple<i64,text>>", "", {tuple}};
  auto nested_result = compile("items", {{"items", nested}});
  expect(nested_result.report.result_type == nested.type, "array tuple snapshots retain structure");

  // Generic resolution must visit each nested argument once. Previously the
  // built-in array path repeated resolution for nominal instantiation, making
  // this tiny input take exponential time (the test timeout catches regression).
  constexpr std::size_t nested_depth = 24;
  const auto deep_source = "value deep is " + std::string(nested_depth, '[') + "1" +
                           std::string(nested_depth, ']');
  std::string deep_type = "i32";
  ny::submission_value deep_value = scalar("i32", "1");
  for (std::size_t i = 0; i < nested_depth; ++i) {
    deep_type = "array<" + deep_type + ">";
    deep_value = {deep_type, "", {std::move(deep_value)}};
  }
  const auto checked_deep = matrix::check_submission_source(deep_source, "nested.matrix");
  expect(checked_deep.status == "complete", "deep array check-only admission remains practical");
  const auto compiled_deep = compile(deep_source);
  expect(compiled_deep.module && compiled_deep.module->functions.front().return_type == "tuple<" + deep_type + ">",
         "deep array session compilation preserves its structural type");
  const auto restored_deep = compile("kept", {{"kept", deep_value}});
  expect(restored_deep.report.result_type == deep_type, "deep array restoration remains practical and exact");

  compile("kept", {{"kept", scalar("u8", "256")}}, false, "invalid");
  compile("kept", {{"kept", scalar("i32", "2147483648")}}, false, "invalid");
  compile("kept", {{"kept", scalar("i64", "01")}}, false, "invalid");
  compile("kept", {{"kept", scalar("f64", "nan")}}, false, "invalid");
  compile("kept", {{"kept", scalar("bool", "1")}}, false, "invalid");
  compile("kept", {{"kept", {"array<i64>", "", {scalar("i32", "1")}}}}, false, "invalid");
  compile("kept", {{"kept", {"tuple<i64>", "", {}}}}, false, "invalid");
  compile("kept", {{"kept", {"text", "scalar", {scalar("i32", "1")}}}}, false, "invalid");
  compile("kept", {{"kept", scalar("ptr<i32>", "0")}}, false, "invalid");
  compile("kept", {{"kept", scalar("text", std::string(1, char(0xff)))}}, false, "invalid");
  compile("", {{"Frame_Count", scalar("i32", "1")}, {"framecount", scalar("i32", "2")}}, false, "invalid");
  compile("", {{"say", scalar("i32", "1")}}, false, "invalid");
  std::vector<matrix::submission_binding> full;
  for (int i = 0; i < 64; ++i) full.push_back({"item" + std::to_string(i), scalar("i32", "1")});
  compile("item0", full);
  compile("value extra is 1", full, false, "invalid");
  full.push_back({"overflow", scalar("i32", "1")});
  compile("", full, false, "invalid");
  compile(std::string(65537, ' '), {}, false, "invalid");
  compile(std::string(1, char(0xff)), {}, false, "invalid");
  std::cout << checks << " session compilation checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
