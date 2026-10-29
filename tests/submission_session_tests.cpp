#include "submission_session.hpp"
#include "../common/strict_json.hpp"

#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void expect(bool condition, const std::string &message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
matrix::evaluation_report complete(matrix::submission_session &session,
                                    const std::string &source) {
  auto report = session.evaluate(source);
  expect(report.check.status == "complete", source + ": " + matrix::evaluation_report_json(report));
  expect(report.executed && report.committed, "successful transaction must execute and commit");
  return report;
}
void scalar(matrix::submission_session &session, const std::string &source,
            const std::string &type, const std::string &value) {
  const auto report = complete(session, source);
  expect(report.value && report.value->type == type && report.value->scalar == value,
         source + ": unexpected typed result " + matrix::evaluation_report_json(report));
}
void rollback(matrix::submission_session &session, const std::string &source,
              const std::string &status, bool executed) {
  const auto size = session.bindings().size();
  const auto report = session.evaluate(source);
  expect(report.check.status == status, source + ": " + matrix::evaluation_report_json(report));
  expect(!report.committed && !report.value && report.added.empty(), "failed transaction leaked output");
  expect(report.executed == executed, "failed transaction execution flag mismatch");
  expect(session.bindings().size() == size && report.binding_count == size, "failure changed retained bindings");
}

void retained_values() {
  matrix::submission_session session;
  auto first = complete(session, "value Answer is 40 plus 2");
  expect(first.check.result_type == "void" && !first.value && first.added.size() == 1, "binding-only transaction");
  scalar(session, "ANSWER", "i32", "42");
  const auto restored = matrix::prepare_session_submission("answer", false, "next.mat", session.bindings());
  expect(restored.report.status == "complete" && restored.report.nil_text.find("ADD") == std::string::npos,
         "retained data must not replay its original addition");
  scalar(session, "value long-value as number is 7\nlong-value", "i64", "7");
  scalar(session, "LONG_VALUE", "i64", "7");
  scalar(session, "value byte-value is 255 becomes override byte\nbyte-value", "u8", "255");
  scalar(session, "byte-value", "u8", "255");
  scalar(session, "value flag is true\nflag", "bool", "true");
  scalar(session, "flag", "bool", "true");
  scalar(session, "value minimum is -9223372036854775808\nminimum", "i64", "-9223372036854775808");
  scalar(session, "minimum", "i64", "-9223372036854775808");
  complete(session, "value negative-zero is -0.0");
  scalar(session, "negative-zero", "f64", "-0.0");
  complete(session, "value message is \"hello\"");
  scalar(session, "message plus \" world\"", "text", "hello world");
  complete(session, "value numbers is [1, 2, 3]");
  complete(session, "value copied is numbers");
  scalar(session, "copied at 1", "i32", "2");
  complete(session, "value empty as array of number is []");
  const auto empty = complete(session, "empty");
  expect(empty.value && empty.value->type == "array<i64>" && empty.value->elements.empty(),
         "retained empty arrays keep their concrete element type");
  complete(session, "value (Frame-Count, name) is (9, \"renderer\")");
  scalar(session, "FRAME_COUNT", "i32", "9");
  scalar(session, "name", "text", "renderer");
  const auto tuple = complete(session, "(answer, name, numbers)");
  expect(tuple.value && tuple.value->elements.size() == 3 && tuple.value->elements[2].elements.size() == 3,
         "nested owned value projection");
  const auto json = nil::detail::json_parser(matrix::evaluation_report_json(tuple)).parse();
  expect(json.at("schema").text == "nil.matrix.submission-evaluation.v1", "evaluation schema");
  expect(json.object.count("nil") == 0, "runtime result must not expose internal binding envelope NIL");
  const auto before = session.bindings().size();
  complete(session, "when true then\nvalue hidden is 9\nend when");
  expect(session.bindings().size() == before, "nested bindings must not persist");
  rollback(session, "hidden", "invalid", false);
  session.reset();
  expect(session.bindings().empty(), "reset clears retained data");
  rollback(session, "answer", "invalid", false);
}

void transactions() {
  matrix::submission_session session;
  complete(session, "value seed is 7");
  rollback(session, "value temporary is 9\n1 over 0", "runtime-error", true);
  rollback(session, "temporary", "invalid", false);
  scalar(session, "seed", "i32", "7");
  const auto bad_path = session.evaluate("1 over 0", false, std::string(1, char(0xff)) + ".mat");
  const auto diagnostic = nil::detail::json_parser(matrix::evaluation_report_json(bad_path)).parse();
  expect(diagnostic.at("status").text == "runtime-error", "runtime diagnostics are valid UTF-8 for arbitrary file paths");
  scalar(session, "[1] at 2", "i32", "0");
  rollback(session, "value temporary is 9\n[(1, true)] at 2", "runtime-error", true);
  rollback(session, "value temporary is 9\n1 plus true", "invalid", false);
  rollback(session, "value temporary is 9\nsay \"effect\"", "invalid", false);
  rollback(session, "value temporary is 9\n1e308 times 1e308", "runtime-error", true);
  rollback(session, "value SEED is 8", "invalid", false);
  rollback(session, "value later is", "incomplete", false);
  scalar(session, "seed", "i32", "7");
  auto scope = complete(session, "when true then\nvalue local is 8\nend when\nvalue local is 4\nlocal");
  expect(scope.value && scope.value->scalar == "4" && scope.added.size() == 1, "export uses lexical slot resolution");
  scalar(session, "local", "i32", "4");
  const std::string request =
    "nildata 1\nobject\nschema is \"nil.matrix.submission-request.v1\"\n"
    "profile is \"matrix.pure.v1\"\nbindings is an array\nend array\n"
    "value is an object\nkind is \"name\"\nname is \"SEED\"\nend object\nend object\n";
  const auto machine = session.evaluate(request, true, "request.nildata");
  expect(machine.committed && machine.value && machine.value->scalar == "7", "request reads retained source value");
}

void limits() {
  matrix::session_limits limits;
  limits.execution.max_instructions = 1;
  matrix::submission_session short_run(limits);
  rollback(short_run, "value lost is 1", "budget-exceeded", true);
  limits = {};
  limits.max_state_bytes = 8;
  matrix::submission_session small_state(limits);
  complete(small_state, "value x is 1");
  rollback(small_state, "value y is 2", "budget-exceeded", true);
  scalar(small_state, "x", "i32", "1");
  limits = {};
  limits.max_report_bytes = 64;
  matrix::submission_session small_report(limits);
  rollback(small_report, "value x is 1", "budget-exceeded", true);
  limits = {};
  bool cancelled = false;
  limits.execution.cancelled = [&] { return cancelled; };
  matrix::submission_session cancel_run(limits);
  complete(cancel_run, "value retained is 3");
  cancelled = true;
  rollback(cancel_run, "value lost is 9", "cancelled", false);
  cancelled = false;
  scalar(cancel_run, "retained", "i32", "3");
  limits = {};
  limits.execution.max_work_bytes = 1024;
  matrix::submission_session limited_work(limits);
  rollback(limited_work, "value payload is \"" + std::string(2048, 'x') + "\"", "budget-exceeded", true);
}
} // namespace

int main() {
  try { retained_values(); transactions(); limits(); }
  catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
  std::cout << checks << " session checks passed\n";
}
