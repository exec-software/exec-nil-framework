#include "../nilbuild/process.hpp"
#include "../common/strict_json.hpp"
#include "../nilcompiler/src/nildata.hpp"
#include "nil_version.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <set>
#include <thread>

namespace fs = std::filesystem;
using nil::detail::json;
namespace {
struct options {
  std::string command, target = "vm", format = "human", filter;
  std::optional<std::string> compiler, interpreter;
  std::vector<std::string> sources, sdk_paths, includes, ids;
  std::size_t jobs = std::min(4u, std::max(1u, std::thread::hardware_concurrency()));
  std::chrono::milliseconds timeout{30000};
  bool no_stdlib = false;
};
json number(std::int64_t value) {
  json result; result.type = json::kind::number; result.text = std::to_string(value); return result;
}
std::string clean_text(const std::string &value) {
  std::string output;
  for (std::size_t i = 0; i < value.size();) {
    const auto first = static_cast<unsigned char>(value[i]);
    const std::size_t count = first < 0x80 ? 1 : first >= 0xc2 && first <= 0xdf ? 2 :
        first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
    bool valid = count && count <= value.size() - i;
    if (valid) try { nil::detail::valid_utf8(value.substr(i, count)); } catch (...) { valid = false; }
    if (valid) { output.append(value, i, count); i += count; }
    else { output += "\xef\xbf\xbd"; ++i; }
  }
  return output;
}
void usage() {
  std::cout << "Usage: niltest <list|run> [options] SOURCE...\n"
      "  --target vm|native|all       Execution routes (default vm)\n"
      "  --compiler PATH             Compiler override\n"
      "  --interpreter PATH          Interpreter override\n"
      "  --sdk-path PATH, -I PATH     SDK/import roots (repeatable)\n"
      "  --no-stdlib                 Disable implicit standard-library lookup\n"
      "  --filter TEXT               Match a substring of test id, name, or suite\n"
      "  --test-id ID                Select an exact test id (repeatable)\n"
      "  --jobs N                    Concurrent cases, 1..64\n"
      "  --timeout SECONDS           Per-case deadline, 0.001..3600 (default 30)\n"
      "  --format human|json|nildata  Complete discovery/result document\n"
      "  --help, --version\n\n"
      "Tests run in separate processes; successful completion exits 77.\n"
      "Capture is bounded to 16 MiB per child and 64 MiB per run; compilation allows 300 seconds.\n"
      "NILData reports use the 1 MiB document limit; use JSON for larger reports.\n"
      "Execution currently supports POSIX hosts; Windows is not supported.\n";
}
options parse(int argc, char **argv) {
  if (argc < 2) throw std::runtime_error("expected list or run; use --help");
  options result; result.command = argv[1];
  if (result.command != "list" && result.command != "run") throw std::runtime_error("expected list or run");
  std::set<std::string> seen;
  for (int i = 2; i < argc; ++i) {
    std::string key = argv[i];
    if (key == "--") { while (++i < argc) result.sources.push_back(argv[i]); break; }
    if (key.empty() || key.front() != '-') { result.sources.push_back(key); continue; }
    if (key == "-j") key = "--jobs";
    const bool repeat = key == "--sdk-path" || key == "-I" || key == "--test-id";
    if (!repeat && !seen.insert(key).second) throw std::runtime_error("duplicate option: " + key);
    if (key == "--no-stdlib") { result.no_stdlib = true; continue; }
    if (key != "--target" && key != "--compiler" && key != "--interpreter" &&
        key != "--sdk-path" && key != "-I" && key != "--filter" && key != "--test-id" &&
        key != "--jobs" && key != "--timeout" && key != "--format")
      throw std::runtime_error("unknown option: " + key);
    if (++i == argc || !*argv[i]) throw std::runtime_error("missing value for " + key);
    const std::string value = argv[i];
    if (key == "--compiler") result.compiler = value;
    else if (key == "--interpreter") result.interpreter = value;
    else if (key == "--sdk-path") result.sdk_paths.push_back(value);
    else if (key == "-I") result.includes.push_back(value);
    else if (key == "--test-id") result.ids.push_back(value);
    else if (key == "--filter") result.filter = value;
    else if (key == "--target") {
      if (value != "vm" && value != "native" && value != "all") throw std::runtime_error("target must be vm, native, or all");
      result.target = value;
    } else if (key == "--format") {
      if (value != "human" && value != "json" && value != "nildata") throw std::runtime_error("format must be human, json, or nildata");
      result.format = value;
    } else if (key == "--jobs") {
      if (value.size() > 2 || value.find_first_not_of("0123456789") != std::string::npos ||
          std::stoi(value) < 1 || std::stoi(value) > 64) throw std::runtime_error("jobs must be from 1 to 64");
      result.jobs = std::stoul(value);
    } else {
      std::size_t used = 0;
      double seconds;
      try { seconds = std::stod(value, &used); } catch (...) { throw std::runtime_error("invalid timeout"); }
      if (used != value.size() || !std::isfinite(seconds) || seconds < .001 || seconds > 3600)
        throw std::runtime_error("timeout must be from 0.001 to 3600 seconds");
      result.timeout = std::chrono::milliseconds(static_cast<std::int64_t>(std::ceil(seconds * 1000)));
    }
  }
  if (result.sources.empty()) throw std::runtime_error("at least one Ny or Matrix source is required");
  for (auto &source : result.sources) {
    source = fs::absolute(source).lexically_normal().string();
    const auto extension = fs::path(source).extension().string();
    if (extension != ".ny" && extension != ".matrix" && extension != ".mat")
      throw std::runtime_error("test source must use .ny, .matrix, or .mat: " + source);
    if (!fs::is_regular_file(source)) throw std::runtime_error("test source is not a regular file: " + source);
  }
  return result;
}
bool executable(const fs::path &path) {
  std::error_code error;
  if (!fs::is_regular_file(path, error)) return false;
#ifdef _WIN32
  return true;
#else
  return access(path.c_str(), X_OK) == 0;
#endif
}
std::optional<fs::path> on_path(const std::string &name) {
  const char *raw = std::getenv("PATH");
  if (!raw) return {};
  std::string paths(raw);
  std::size_t start = 0;
  while (start <= paths.size()) {
    const auto end = paths.find(':', start);
    const auto part = paths.substr(start, end == std::string::npos ? end : end - start);
    if (!part.empty()) {
      const auto candidate = fs::absolute(fs::path(part) / name);
      if (executable(candidate)) return candidate;
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return {};
}
fs::path tool(const std::string &name, const std::optional<std::string> &override,
              const fs::path &self) {
  if (override) {
    if (fs::path(*override).has_parent_path()) {
      const auto candidate = fs::absolute(*override);
      if (executable(candidate)) return candidate;
    } else if (const auto candidate = on_path(*override)) return *candidate;
    throw std::runtime_error("cannot execute specified " + name + ": " + *override);
  }
  const auto sibling = self.parent_path() / name;
  if (executable(sibling)) return sibling;
  if (const auto found = on_path(name)) return *found;
  throw std::runtime_error("cannot find " + name + "; install it beside niltest or configure its path");
}
fs::path self_path(const char *argv0) {
#ifdef __linux__
  std::error_code error;
  auto path = fs::read_symlink("/proc/self/exe", error);
  if (!error) return path;
#endif
  if (fs::path(argv0).has_parent_path()) return fs::absolute(argv0);
  if (auto path = on_path(argv0)) return *path;
  return fs::absolute(argv0);
}
struct temporary_directory {
  fs::path path;
  temporary_directory() {
#ifdef _WIN32
    throw std::runtime_error("niltest execution currently requires a POSIX host; Windows is not supported");
#else
    std::string pattern = (fs::temp_directory_path() / "niltest-XXXXXX").string();
    const char *created = mkdtemp(pattern.data());
    if (!created) throw std::runtime_error("could not create private test artifact directory");
    path = created;
#endif
  }
  ~temporary_directory() { std::error_code error; if (!path.empty()) fs::remove_all(path, error); }
};
std::string text(const json &value, const std::string &key) {
  const auto &field = value.at(key);
  if (field.type != json::kind::string) throw std::runtime_error("invalid compiler test metadata field: " + key);
  return field.text;
}
std::string metadata_text(const json &value, const std::string &key, bool nonempty = false) {
  const auto result = text(value, key);
  if ((nonempty && result.empty()) || result.find('\0') != std::string::npos)
    throw std::runtime_error("invalid compiler test metadata field: " + key);
  return result;
}
void validate_test_line(const json &value) {
  if (value.type != json::kind::number) throw std::runtime_error("compiler returned an invalid test line");
  try {
    std::size_t used = 0;
    const auto line = std::stold(value.text, &used);
    // Match integer locations consumable by JSON editor clients, including
    // zero for a compiler location that has no source line.
    if (used != value.text.size() || !std::isfinite(line) || line < 0 ||
        line > 9007199254740991.0L || std::floor(line) != line)
      throw std::runtime_error("invalid line");
  } catch (const std::exception &) {
    throw std::runtime_error("compiler returned an invalid test line; expected a nonnegative safe integer");
  }
}
std::vector<std::string> compiler_arguments(const options &opts, const fs::path &compiler) {
  std::vector<std::string> result{compiler.string(), "--quiet"};
  if (opts.no_stdlib) result.push_back("--no-stdlib");
  for (const auto &path : opts.sdk_paths) { result.push_back("--sdk-path"); result.push_back(fs::absolute(path).string()); }
  for (const auto &path : opts.includes) { result.push_back("-I"); result.push_back(fs::absolute(path).string()); }
  return result;
}
void emit(const json &report, const std::string &format, bool list) {
  if (format == "json") { std::cout << nil::detail::dump(report) << '\n'; return; }
  if (format == "nildata") { std::cout << nil::data::from_json(nil::detail::dump(report)); return; }
  for (const auto &test : report.at("tests").array) {
    if (list) std::cout << text(test, "id") << "  " << text(test, "name") << "\n";
    else {
      std::cout << text(test, "status") << " [" << text(test, "route") << "] " << text(test, "name")
                << " (" << test.at("durationMs").text << " ms)\n";
      if (text(test, "status") != "passed") std::cout << text(test, "message") << '\n';
      std::cout << text(test, "stdout");
      std::cerr << text(test, "stderr");
    }
  }
  for (const auto &error : report.at("errors").array) std::cerr << "niltest: " << error.text << '\n';
  if (!list) std::cout << report.at("summary").at("passed").text << " passed, "
      << report.at("summary").at("failed").text << " failed, "
      << report.at("summary").at("errors").text << " errors\n";
}
json empty_report(bool list) {
  auto result = json::map();
  result.object["schema"] = json::str(list ? "nil.tests.v1" : "nil.test.results.v1");
  result.object["tests"] = json::list();
  result.object["errors"] = json::list();
  auto summary = json::map();
  for (const auto *key : {"total", "passed", "failed", "errors", "cancelled"}) summary.object[key] = number(0);
  result.object["summary"] = std::move(summary);
  return result;
}
void summarize(json &report) {
  auto &summary = report.object["summary"];
  std::int64_t passed = 0, failed = 0, errors = 0, cancelled = 0;
  for (const auto &item : report.at("tests").array) {
    const auto state = text(item, "status");
    if (state == "passed") ++passed;
    else if (state == "failed" || state == "timeout") ++failed;
    else if (state == "cancelled") ++cancelled;
    else ++errors;
  }
  summary.object["total"] = number(report.at("tests").array.size());
  summary.object["passed"] = number(passed); summary.object["failed"] = number(failed);
  summary.object["errors"] = number(errors + report.at("errors").array.size());
  summary.object["cancelled"] = number(cancelled);
}
json case_result(const json &metadata, const std::string &route,
                 const nilbuild::process_result &result, const std::string &build_error = {}) {
  auto item = metadata;
  std::string state, message;
  if (!build_error.empty()) { state = "error"; message = build_error; }
  else if (result.cancelled || !result.started) { state = "cancelled"; message = "test execution cancelled"; }
  else if (result.timed_out) { state = "timeout"; message = "test exceeded its deadline"; }
  else if (result.output_limited) { state = "error"; message = "test output exceeded the capture limit (16 MiB per child, 64 MiB per run)"; }
  else if (result.signal) { state = "failed"; message = "test terminated by signal " + std::to_string(result.signal); }
  else if (result.status == 77) { state = "passed"; }
  else { state = "failed"; message = result.status == 0 ? "test returned without completing the harness" : "test exited " + std::to_string(result.status); }
  item.object["route"] = json::str(route); item.object["status"] = json::str(state);
  item.object["message"] = json::str(message); item.object["exitCode"] = number(result.status);
  item.object["signal"] = number(result.signal); item.object["timedOut"] = json::flag(result.timed_out);
  item.object["cancelled"] = json::flag(result.cancelled || !result.started);
  item.object["durationMs"] = number(result.duration_ms);
  item.object["stdout"] = json::str(clean_text(result.output)); item.object["stderr"] = json::str(clean_text(result.error));
  return item;
}
int execute(const options &opts, const fs::path &self, json &report) {
#ifdef _WIN32
  throw std::runtime_error("niltest execution currently requires a POSIX host; Windows is not supported");
#else
  nilbuild::cancellation_scope cancellation;
  const auto compiler = tool("nilcompiler", opts.compiler, self);
  auto discovery = compiler_arguments(opts, compiler);
  discovery.push_back("--test-list"); discovery.insert(discovery.end(), opts.sources.begin(), opts.sources.end());
  nilbuild::process_options compile_options;
  compile_options.close_stdin = true; compile_options.report_limits = true;
  compile_options.cleanup_descendants = true;
  const auto discovered = nilbuild::execute_many({{discovery, fs::current_path()}}, 1, compile_options).front();
  if (discovered.status || discovered.timed_out || discovered.output_limited || discovered.cancelled) {
    std::string reason = discovered.timed_out ? "test discovery timed out" : discovered.output_limited ? "test discovery exceeded output limit" : "test discovery failed";
    throw std::runtime_error(reason + ": " + clean_text(discovered.error));
  }
  auto manifest = nil::detail::json_parser(discovered.output).parse();
  if (text(manifest, "schema") != "nil.tests.v1" || manifest.at("tests").type != json::kind::array)
    throw std::runtime_error("compiler returned an invalid nil.tests.v1 document");
  std::set<std::string> all_ids, selected_ids(opts.ids.begin(), opts.ids.end());
  std::vector<json> selected;
  for (const auto &item : manifest.at("tests").array) {
    if (all_ids.size() >= 10000) throw std::runtime_error("test discovery exceeds 10000 cases");
    const auto id = metadata_text(item, "id", true);
    if (!all_ids.insert(id).second) throw std::runtime_error("compiler returned empty or duplicate test identifiers");
    const auto name = metadata_text(item, "name"), suite = metadata_text(item, "suite");
    const auto file = metadata_text(item, "file");
    if (!file.empty() && !fs::path(file).is_absolute())
      throw std::runtime_error("compiler returned a relative test source path");
    (void)metadata_text(item, "kind"); (void)metadata_text(item, "function", true);
    validate_test_line(item.at("line"));
    if (!selected_ids.empty() && !selected_ids.count(id)) continue;
    if (!opts.filter.empty() && id.find(opts.filter) == std::string::npos && name.find(opts.filter) == std::string::npos && suite.find(opts.filter) == std::string::npos) continue;
    selected.push_back(item);
  }
  for (const auto &id : selected_ids) if (!all_ids.count(id)) throw std::runtime_error("unknown test id: " + id);
  if (opts.command == "list") { report.object["tests"].array = std::move(selected); return 0; }
  if (selected.empty()) throw std::runtime_error("no tests matched the selection");
  const auto interpreter = opts.target != "native" ? tool("nilinterpreter", opts.interpreter, self) : fs::path{};
  temporary_directory artifacts;
  std::vector<nilbuild::process_command> commands;
  std::vector<std::pair<json, std::string>> cases;
  for (const std::string route : {"vm", "native"}) {
    if (opts.target != "all" && opts.target != route) continue;
    auto compile = compiler_arguments(opts, compiler);
    const auto artifact = artifacts.path / (route == "vm" ? "tests.nil" : "tests.native");
    compile.push_back("--test"); compile.push_back(route == "vm" ? "--no-bundle" : "--native");
    compile.insert(compile.end(), {"--output", artifact.string()});
    compile.insert(compile.end(), opts.sources.begin(), opts.sources.end());
    const auto built = nilbuild::execute_many({{compile, fs::current_path()}}, 1, compile_options).front();
    if (built.status || built.timed_out || built.output_limited || built.cancelled || !fs::is_regular_file(artifact)) {
      const std::string error = route + " test compilation " +
          (built.cancelled ? "cancelled" : built.timed_out ? "timed out" : built.output_limited ? "exceeded output limit" :
          built.status == 0 ? "did not create an artifact" : "failed") + ": " + clean_text(built.error);
      report.object["errors"].array.push_back(json::str(error));
      for (const auto &test : selected) report.object["tests"].array.push_back(case_result(test, route, built, error));
      continue;
    }
    for (const auto &test : selected) {
      const auto id = text(test, "id");
      std::vector<std::string> command = route == "vm" ?
          std::vector<std::string>{interpreter.string(), "--no-ai", artifact.string(), "--", id} :
          std::vector<std::string>{artifact.string(), id};
      commands.push_back({std::move(command), fs::current_path()}); cases.emplace_back(test, route);
    }
  }
  nilbuild::process_options run_options;
  run_options.fail_fast = false; run_options.report_limits = true; run_options.close_stdin = true;
  run_options.cleanup_descendants = true;
  run_options.success_exit = 77; run_options.time_limit = opts.timeout;
  run_options.total_output_limit = 64 * 1024 * 1024;
  const auto results = nilbuild::execute_many(commands, opts.jobs, run_options);
  for (std::size_t index = 0; index < results.size(); ++index)
    report.object["tests"].array.push_back(case_result(cases[index].first, cases[index].second, results[index]));
  summarize(report);
  if (nilbuild::pending_signal) return 128 + nilbuild::pending_signal;
  if (report.at("summary").at("errors").text != "0") return 2;
  return report.at("summary").at("failed").text == "0" ? 0 : 1;
#endif
}
} // namespace
int main(int argc, char **argv) {
  if (argc == 2 && std::string(argv[1]) == "--version") { std::cout << "niltest " << NIL_VERSION << '\n'; return 0; }
  if (argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) { usage(); return 0; }
  std::string format = "human";
  for (int i = 1; i + 1 < argc; ++i) if (std::string(argv[i]) == "--format") format = argv[i + 1];
  const bool list = argc > 1 && std::string(argv[1]) == "list";
  auto report = empty_report(list);
  int status = 2;
  try { const auto opts = parse(argc, argv); format = opts.format; status = execute(opts, self_path(argv[0]), report); }
  catch (const std::exception &error) {
    report.object["errors"].array.push_back(json::str(clean_text(error.what())));
#ifndef _WIN32
    if (nilbuild::pending_signal) status = 128 + nilbuild::pending_signal;
#endif
  }
  if (!list) summarize(report);
  try { emit(report, format, list); }
  catch (const std::exception &error) { std::cerr << "niltest: could not serialize report: " << error.what() << '\n'; return 2; }
  return status;
}
