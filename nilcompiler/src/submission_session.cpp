#include "submission_session.hpp"
#include "nil_loader.hpp"
#include "submission_profile.hpp"
#include "vm.hpp"
#include "../../common/strict_json.hpp"

#include <chrono>
#include <sstream>
#include <stdexcept>

namespace matrix {
namespace {
using nil::detail::json;

json count(std::size_t value) {
  json result;
  result.type = json::kind::number;
  result.text = std::to_string(value);
  return result;
}

json value_json(const ny::submission_value &value) {
  auto result = json::map();
  result.object["type"] = json::str(value.type);
  if (value.type.rfind("array<", 0) == 0 || value.type.rfind("tuple<", 0) == 0) {
    auto items = json::list();
    for (const auto &child : value.elements) items.array.push_back(value_json(child));
    result.object["elements"] = std::move(items);
  } else result.object["scalar"] = json::str(value.scalar);
  return result;
}

[[noreturn]] void budget(const std::string &message) {
  throw ny::pure_execution_error(ny::pure_execution_error::kind::budget, message);
}

void validate_state(const std::vector<submission_binding> &bindings,
                    const session_limits &limits) {
  std::size_t nodes = 0, bytes = 0;
  const auto charge = [&](std::size_t amount) {
    if (amount > limits.max_state_bytes - bytes) budget("retained session byte budget exceeded");
    bytes += amount;
  };
  const auto visit = [&](const auto &self, const ny::submission_value &value, unsigned depth) -> void {
    if (++nodes > limits.max_state_nodes || depth > 32) budget("retained session value budget exceeded");
    charge(value.type.size()); charge(value.scalar.size());
    for (const auto &child : value.elements) self(self, child, depth + 1);
  };
  for (const auto &binding : bindings) {
    charge(binding.name.size());
    visit(visit, binding.value, 0);
  }
}

void failure(evaluation_report &report, const std::string &status,
             const std::string &message, const std::string &filename, int line) {
  report.check.status = status;
  report.check.nil_text.clear();
  report.value.reset();
  report.added.clear();
  report.committed = false;
  auto bounded = message.substr(0, 4096);
  if (message.size() > bounded.size()) {
    while (!bounded.empty() && (static_cast<unsigned char>(message[bounded.size()]) & 0xc0) == 0x80)
      bounded.pop_back();
    bounded += "...";
  }
  report.check.diagnostics.push_back({"runtime", status == "budget-exceeded" ? "MS6002" :
      status == "cancelled" ? "MS6003" : "MS6001", std::move(bounded), filename, line, 0, 0});
}
} // namespace

submission_session::submission_session(session_limits limits) : limits_(std::move(limits)) {
  if (!limits_.max_state_bytes || limits_.max_state_bytes > 1024 * 1024 ||
      !limits_.max_state_nodes || limits_.max_state_nodes > 1024 ||
      !limits_.max_report_bytes || limits_.max_report_bytes > 1024 * 1024 ||
      !limits_.max_execution_ms || limits_.max_execution_ms > 10000)
    throw std::invalid_argument("session limits must be positive and within the hosted profile ceilings");
  ny::VM validation;
  validation.configurePureExecution(limits_.execution);
}

evaluation_report submission_session::evaluate(const std::string &source, bool request,
                                               const std::string &filename) {
  evaluation_report report;
  report.binding_count = bindings_.size();
  auto prepared = prepare_session_submission(source, request, filename, bindings_);
  report.check = std::move(prepared.report);
  if (report.check.status != "complete") return report;
  ny::VM vm;
  int line = 0;
  try {
    if (!prepared.module) throw std::runtime_error("checked submission has no executable module");
    // Recheck the actual artifact at the execution boundary. The public checker
    // report and its text cannot be presented as proof of admission.
    ny::validate_nil_submission_profile(*prepared.module);
    ny::codegen_options options;
    options.include_debug_info = false;
    options.include_source_debug = false;
    std::ostringstream bytes(std::ios::out | std::ios::binary);
    ny::nil_binary_writer writer(bytes, options);
    writer.emit_binary_module(*prepared.module);
    const auto binary = bytes.str();
    if (binary.size() > 2 * 1024 * 1024) budget("compiled submission binary budget exceeded");
    std::istringstream input(binary, std::ios::in | std::ios::binary);
    std::unordered_map<std::string, ny::Value> constants;
    const auto program = ny::parseBinaryProgram(input, constants);
    vm.configurePureExecution(limits_.execution);
    vm.loadProgram(program, constants);
    vm.resetExecution("__nil_submission");
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(limits_.max_execution_ms);
    do {
      if (std::chrono::steady_clock::now() >= deadline) budget("submission execution deadline exceeded");
      if (vm.getPC() < vm.getProgram().size()) line = vm.getProgram()[vm.getPC()].line;
    } while (vm.stepInstruction());
    report.instructions_executed = vm.pureInstructionsExecuted();
    report.executed = report.instructions_executed != 0;
    const auto expected = prepared.binding_names.size() + (prepared.has_value ? 1 : 0);
    if (!expected) {
      if (!vm.getStack().empty()) throw std::runtime_error("void submission left an unexpected value");
    } else {
      if (vm.getStack().size() != 1) throw std::runtime_error("submission returned an invalid value count");
      auto result = vm.snapshotPureValue(vm.getStack().back(), prepared.module->functions.front().return_type);
      if (result.type.rfind("tuple<", 0) != 0 || result.elements.size() != expected)
        throw std::runtime_error("submission returned an invalid result envelope");
      std::size_t index = 0;
      if (prepared.has_value) {
        report.value = std::move(result.elements[index++]);
        if (report.value->type != report.check.result_type)
          throw std::runtime_error("submission result does not match the checked type");
      }
      for (const auto &name : prepared.binding_names)
        report.added.push_back({name, std::move(result.elements[index++])});
    }
    auto candidate = bindings_;
    candidate.insert(candidate.end(), report.added.begin(), report.added.end());
    validate_state(candidate, limits_);
    // A retained value must remain representable through the admitted shared
    // compiler pipeline. This compiles data only, never previous computations.
    if (!report.added.empty()) {
      const auto restorable = prepare_session_submission("", false, "<retained-values>", candidate);
      if (restorable.report.status != "complete")
        budget("retained values exceed the compiler restoration profile");
    }
    if (limits_.execution.cancelled && limits_.execution.cancelled())
      throw ny::pure_execution_error(ny::pure_execution_error::kind::cancelled, "submission cancelled before publication");
    if (std::chrono::steady_clock::now() >= deadline)
      budget("submission execution/publication deadline exceeded");
    report.binding_count = candidate.size();
    report.committed = true;
    // Validate projection before publication: failure cannot partially publish
    // bindings that a client never received successfully.
    if (evaluation_report_json(report).size() > limits_.max_report_bytes)
      budget("submission result report budget exceeded");
    if (std::chrono::steady_clock::now() >= deadline)
      budget("submission result projection deadline exceeded");
    bindings_.swap(candidate);
    return report;
  } catch (const ny::pure_execution_error &error) {
    failure(report, error.reason == ny::pure_execution_error::kind::cancelled ?
        "cancelled" : "budget-exceeded", error.what(), filename, line);
  } catch (const std::exception &error) {
    failure(report, "runtime-error", error.what(), filename, line);
  }
  report.instructions_executed = vm.pureInstructionsExecuted();
  report.executed = report.instructions_executed != 0;
  report.binding_count = bindings_.size();
  return report;
}

std::string submission_value_json(const ny::submission_value &value) {
  return nil::detail::dump(value_json(value));
}

std::string evaluation_report_json(const evaluation_report &report) {
  // Reuse the existing bounded UTF-8 diagnostic projection and field contract.
  auto checked = report.check;
  checked.nil_text.clear();
  auto out = nil::detail::json_parser(submission_report_json(checked)).parse();
  out.object.erase("nil");
  out.object["schema"] = json::str("nil.matrix.submission-evaluation.v1");
  out.object["executed"] = json::flag(report.executed);
  out.object["committed"] = json::flag(report.committed);
  out.object["value"] = report.value ? value_json(*report.value) : json{};
  out.object["binding_count"] = count(report.binding_count);
  out.object["instructions_executed"] = count(report.instructions_executed);
  auto added = json::list();
  for (const auto &binding : report.added) {
    auto item = json::map();
    item.object["name"] = json::str(binding.name);
    item.object["value"] = value_json(binding.value);
    added.array.push_back(std::move(item));
  }
  out.object["added"] = std::move(added);
  return nil::detail::dump(out);
}
} // namespace matrix
