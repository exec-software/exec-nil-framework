#include "project_format.hpp"
#include "../common/content_digest.hpp"
#include "process.hpp"
#include "toolchain_inputs.hpp"
#include "nil_version.hpp"

#include <algorithm>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <system_error>
#include <functional>
#include <memory>
#include <thread>
#ifndef _WIN32
#include <sys/file.h>
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;
using nilbuild::json;
namespace {
constexpr std::size_t manifest_limit = 1024 * 1024;
struct options {
  std::string command;
  std::optional<std::string> project, sdk, compiler, interpreter;
  std::string target = "vm";
  std::optional<std::string> format, output, name, entry;
  std::size_t jobs = std::min(4u, std::max(1u, std::thread::hardware_concurrency()));
  bool rebuild = false;
  std::vector<std::string> program_arguments;
};
struct project {
  fs::path file, directory, entry, output_directory;
  std::string name;
  std::vector<fs::path> include_paths, references;
  std::string manifest_digest;
  std::optional<fs::path> sdk;
  std::vector<std::string> disabled_rules;
  bool warnings_as_errors = false;
  fs::path vm_output() const { return output_directory / (name + ".nil"); }
  fs::path native_output() const {
#ifdef _WIN32
    return output_directory / (name + ".exe");
#else
    return output_directory / name;
#endif
  }
};
std::string lower(std::string value) {
  for (auto &c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return value;
}
bool source_extension(const fs::path &path) {
  const auto ext = lower(path.extension().string());
  return ext == ".ny" || ext == ".matrix" || ext == ".mat";
}
fs::path absolute(const fs::path &path, const fs::path &base) {
  auto resolved = (path.is_absolute() ? path : base / path).lexically_normal();
  // Match path.resolve semantics, including directory aliases such as ".".
  if (resolved.filename().empty()) resolved = resolved.parent_path();
  return resolved;
}
std::string read_file(const fs::path &file, std::size_t limit) {
  if (!fs::is_regular_file(file)) throw std::runtime_error("expected a regular file: " + file.string());
  std::ifstream input(file, std::ios::binary);
  if (!input) throw std::runtime_error("cannot read file: " + file.string());
  std::string text;
  char buffer[8192];
  while (input.read(buffer, sizeof(buffer)) || input.gcount()) {
    text.append(buffer, static_cast<std::size_t>(input.gcount()));
    if (text.size() > limit) throw std::runtime_error("file exceeds " + std::to_string(limit) + " bytes: " + file.string());
  }
  if (input.bad()) throw std::runtime_error("cannot read file: " + file.string());
  return text;
}
void object_fields(const json &value, std::initializer_list<const char *> keys, const std::string &label) {
  if (value.type != json::kind::object) throw std::runtime_error(label + " must be an object");
  for (const auto &field : value.object) {
    if (std::none_of(keys.begin(), keys.end(), [&](const char *key) { return field.first == key; }))
      throw std::runtime_error("unknown " + label + " field: " + field.first);
  }
}
std::string string_value(const json &value, const std::string &label) {
  if (value.type != json::kind::string || value.text.empty() || value.text.find('\0') != std::string::npos)
    throw std::runtime_error(label + " must be a nonempty string without NUL bytes");
  return value.text;
}
std::vector<std::string> string_list(const json &value, const std::string &label) {
  if (value.type != json::kind::array) throw std::runtime_error(label + " must be an array of strings");
  std::vector<std::string> result;
  for (const auto &item : value.array) result.push_back(string_value(item, label));
  return result;
}
bool same_path(const fs::path &a, const fs::path &b) {
  if (a == b) return true;
  std::error_code error;
  if (fs::equivalent(a, b, error) && !error) return true;
  error.clear();
  const auto resolved_a = fs::weakly_canonical(a, error);
  if (error) return false;
  const auto resolved_b = fs::weakly_canonical(b, error);
  return !error && resolved_a == resolved_b;
}
void protect_inputs(const project &value) {
  for (const auto &output : {value.vm_output(), value.native_output()}) {
    if (same_path(output, value.file) || same_path(output, value.entry))
      throw std::runtime_error("output would overwrite the project manifest or entry source: " + output.string());
    if (fs::exists(output) && !fs::is_regular_file(fs::symlink_status(output)))
      throw std::runtime_error("output must be a regular file, not a directory or symlink: " + output.string());
  }
}
project load_project(const fs::path &file, const options &opts, const fs::path &caller) {
  const auto contents = read_file(file, manifest_limit);
  const auto data = nilbuild::parse_project_document(contents, file);
  object_fields(data, {"schema", "name", "entry", "includePaths", "sdkPath", "outputDirectory", "analysis", "projectReferences"}, "project");
  if (string_value(data.at("schema"), "schema") != "nil.project.v1")
    throw std::runtime_error("project schema must be 'nil.project.v1'");
  project result;
  result.file = file; result.directory = file.parent_path();
  result.manifest_digest = nil::detail::content_digest(contents);
  if (data.object.count("projectReferences")) {
    for (const auto &reference : string_list(data.at("projectReferences"), "projectReferences")) {
      const auto resolved = absolute(reference, result.directory);
      if (std::any_of(result.references.begin(), result.references.end(), [&](const fs::path &p) { return same_path(p, resolved); }))
        throw std::runtime_error("projectReferences must contain distinct manifest paths");
      result.references.push_back(resolved);
    }
  }
  result.name = string_value(data.at("name"), "name");
  const auto letter_or_digit = [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
  };
  if (result.name.size() > 128 || !letter_or_digit(result.name.front()) ||
      std::any_of(result.name.begin(), result.name.end(), [&](unsigned char c) {
        return !letter_or_digit(c) && c != '.' && c != '_' && c != '-';
      }) || source_extension(result.name))
    throw std::runtime_error("project name must start with an ASCII letter or digit and contain only letters, digits, '.', '_' or '-' (maximum 128 bytes, no source extension)");
  result.entry = absolute(string_value(data.at("entry"), "entry"), result.directory);
  if (!source_extension(result.entry)) throw std::runtime_error("entry must be a Ny (.ny) or Matrix (.matrix, .mat) source file");
  result.include_paths.push_back(result.directory);
  if (data.object.count("includePaths")) {
    for (const auto &path : string_list(data.at("includePaths"), "includePaths")) {
      const auto resolved = absolute(path, result.directory);
      if (std::find(result.include_paths.begin(), result.include_paths.end(), resolved) == result.include_paths.end())
        result.include_paths.push_back(resolved);
    }
  }
  if (data.object.count("sdkPath")) result.sdk = absolute(string_value(data.at("sdkPath"), "sdkPath"), result.directory);
  if (opts.sdk) result.sdk = absolute(*opts.sdk, caller);
  result.output_directory = absolute(data.object.count("outputDirectory") ?
      string_value(data.at("outputDirectory"), "outputDirectory") : "build", result.directory);
  if (data.object.count("analysis")) {
    const auto &analysis = data.at("analysis");
    object_fields(analysis, {"disabledRules", "warningsAsErrors"}, "analysis");
    if (analysis.object.count("disabledRules")) {
      result.disabled_rules = string_list(analysis.at("disabledRules"), "analysis.disabledRules");
      std::set<std::string> seen;
      for (const auto &code : result.disabled_rules) {
        if (code.size() != 7 || code.substr(0, 3) != "NIL" ||
            std::any_of(code.begin() + 3, code.end(), [](char c) { return c < '0' || c > '9'; }) ||
            !seen.insert(code).second)
          throw std::runtime_error("analysis.disabledRules must contain distinct NIL rule codes (for example NIL1001)");
      }
    }
    if (analysis.object.count("warningsAsErrors")) {
      const auto &flag = analysis.at("warningsAsErrors");
      if (flag.type != json::kind::boolean) throw std::runtime_error("analysis.warningsAsErrors must be boolean");
      result.warnings_as_errors = flag.boolean;
    }
  }
  protect_inputs(result);
  return result;
}
json describe(const project &value) {
  auto result = json::map();
  result.object["schema"] = json::str("nil.project.description.v1");
  for (const auto &field : std::vector<std::pair<std::string, std::string>>{
      {"projectFile", value.file.string()}, {"projectDirectory", value.directory.string()},
      {"name", value.name}, {"entry", value.entry.string()}, {"outputDirectory", value.output_directory.string()},
      {"vmOutput", value.vm_output().string()}, {"nativeOutput", value.native_output().string()}})
    result.object[field.first] = json::str(field.second);
  result.object["includePaths"] = json::list();
  for (const auto &path : value.include_paths) result.object["includePaths"].array.push_back(json::str(path.string()));
  result.object["sdkPath"] = value.sdk ? json::str(value.sdk->string()) : json{};
  auto analysis = json::map();
  analysis.object["disabledRules"] = json::list();
  for (const auto &code : value.disabled_rules) analysis.object["disabledRules"].array.push_back(json::str(code));
  analysis.object["warningsAsErrors"] = json::flag(value.warnings_as_errors);
  result.object["analysis"] = std::move(analysis);
  if (!value.references.empty()) {
    result.object["projectReferences"] = json::list();
    for (const auto &reference : value.references) result.object["projectReferences"].array.push_back(json::str(reference.string()));
  }
  return result;
}
void usage() {
  std::cout << "NIL project driver\n\n"
      "Usage: nilbuild <init|convert|describe|check|build|run> [options] [-- program arguments]\n"
      "  --project FILE       Manifest (nearest nil.project.nildata, .json, .yaml or .yml)\n"
      "  --target vm|native|all  Artifact route (default: vm; all requires build)\n"
      "  --jobs N, -j N       Concurrent compiler processes, 1..64 (default: up to 4)\n"
      "  --rebuild            Ignore cached artifacts for this build/run\n"
      "  --sdk PATH           Override manifest SDK, relative to caller directory\n"
      "  --compiler PATH      Override nilcompiler executable\n"
      "  --interpreter PATH   Override nilinterpreter executable\n"
      "  --format nildata|json|yaml  init/convert/describe/check serialization\n"
      "  --output FILE        Destination for convert (default: nil.project.nildata)\n"
      "  --name NAME          init project name (default: hello)\n"
      "  --entry FILE         init entrypoint (default: src/main.matrix)\n"
      "  --help               Show help\n"
      "  --version            Show product version\n\n"
      "New project files and build state use NILData. JSON/YAML project formats\n"
      "remain available for compatibility. Builds always analyze current inputs;\n"
      "unchanged artifacts skip compilation. Project references build first.\n"
      "Compiler children are limited to 300 seconds and 16 MiB of output each.\n";
}
options parse_options(int argc, char **argv) {
  options result;
  if (argc < 2) throw std::runtime_error("expected a command; use --help");
  result.command = argv[1];
  const std::set<std::string> commands{"init", "convert", "describe", "check", "build", "run"};
  if (!commands.count(result.command)) throw std::runtime_error("unknown command: " + result.command);
  std::set<std::string> seen;
  for (int i = 2; i < argc; ++i) {
    std::string option = argv[i];
    if (option == "--") {
      if (result.command != "run") throw std::runtime_error("program arguments require run");
      while (++i < argc) result.program_arguments.push_back(argv[i]);
      break;
    }
    if (option == "-j") option = "--jobs";
    if (!seen.insert(option).second) throw std::runtime_error("duplicate option: " + option);
    if (option == "--rebuild") { result.rebuild = true; continue; }
    const std::set<std::string> flags{"--project", "--sdk", "--compiler", "--interpreter", "--target", "--format", "--jobs", "--output", "--name", "--entry"};
    if (!flags.count(option)) throw std::runtime_error("unknown option: " + option);
    if (++i >= argc || std::string(argv[i]).empty()) throw std::runtime_error("missing value for " + option);
    const std::string value = argv[i];
    if (option == "--project") result.project = value;
    else if (option == "--sdk") result.sdk = value;
    else if (option == "--compiler") result.compiler = value;
    else if (option == "--interpreter") result.interpreter = value;
    else if (option == "--output") result.output = value;
    else if (option == "--name") result.name = value;
    else if (option == "--entry") result.entry = value;
    else if (option == "--target") {
      if (value != "vm" && value != "native" && value != "all") throw std::runtime_error("target must be vm, native or all");
      result.target = value;
    } else if (option == "--jobs") {
      if (value.size() > 2 || value.find_first_not_of("0123456789") != std::string::npos || std::stoi(value) < 1 || std::stoi(value) > 64)
        throw std::runtime_error("jobs must be an integer from 1 to 64");
      result.jobs = std::stoul(value);
    } else {
      if (value != "nildata" && value != "json" && value != "yaml") throw std::runtime_error("format must be nildata, json or yaml");
      result.format = value;
    }
  }
  const bool building = result.command == "build" || result.command == "run";
  if (result.format && building) throw std::runtime_error("--format requires init, convert, describe or check");
  if ((result.rebuild || seen.count("--jobs")) && !building) throw std::runtime_error("--jobs and --rebuild require build or run");
  if (result.target == "all" && result.command != "build") throw std::runtime_error("--target all requires build");
  if ((result.name || result.entry) && result.command != "init") throw std::runtime_error("--name and --entry require init");
  if (result.output && result.command != "convert") throw std::runtime_error("--output requires convert");
  return result;
}
fs::path discover_project(const options &opts, const fs::path &caller) {
  if (opts.project) return absolute(*opts.project, caller);
  auto directory = caller;
  for (;;) {
    for (const auto *name : {"nil.project.nildata", "nil.project.json", "nil.project.yaml", "nil.project.yml"}) {
      const auto candidate = directory / name;
      if (fs::exists(candidate)) return candidate;
    }
    if (directory.parent_path() == directory) break;
    directory = directory.parent_path();
  }
  throw std::runtime_error("no nil.project.nildata (or legacy JSON/YAML project) found; use --project FILE");
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
std::optional<fs::path> search_path(const std::string &name, const fs::path &caller) {
  const char *environment = std::getenv("PATH");
  if (!environment) return std::nullopt;
  std::istringstream paths(environment);
  std::string item;
#ifdef _WIN32
  constexpr char separator = ';';
#else
  constexpr char separator = ':';
#endif
  while (std::getline(paths, item, separator)) {
    const auto candidate = absolute(fs::path(item) / name, caller);
    if (executable(candidate)) return candidate;
  }
  return std::nullopt;
}
fs::path tool_path(const std::string &name, const std::optional<std::string> &override,
                   const fs::path &self, const fs::path &caller) {
  if (override) {
    if (fs::path(*override).has_parent_path()) {
      const auto candidate = absolute(*override, caller);
      if (executable(candidate)) return candidate;
    } else if (const auto found = search_path(*override, caller)) return *found;
    throw std::runtime_error("cannot execute specified " + name + ": " + *override);
  }
  const auto sibling = self.parent_path() / name;
  if (executable(sibling)) return sibling;
  if (const auto found = search_path(name, caller)) return *found;
  throw std::runtime_error("cannot find " + name + "; install it beside nilbuild or use --" +
      (name == "nilcompiler" ? "compiler" : "interpreter") + " PATH");
}
fs::path self_path(const char *argument, const fs::path &caller) {
#ifdef __linux__
  std::error_code error;
  const auto executable = fs::read_symlink("/proc/self/exe", error);
  if (!error) return executable;
#endif
  if (fs::path(argument).has_parent_path()) return absolute(argument, caller);
  if (const auto found = search_path(argument, caller)) return *found;
  return caller / argument;
}
void add_project_arguments(std::vector<std::string> &args, const project &value) {
  if (value.sdk) { args.push_back("--sdk-path"); args.push_back(value.sdk->string()); }
  for (const auto &root : value.include_paths) { args.push_back("-I"); args.push_back(root.string()); }
}
std::string diagnostic_location(const json &diagnostic, const fs::path &fallback) {
  const auto &path = diagnostic.at("path");
  const std::string file = path.type == json::kind::string ? path.text : fallback.string();
  const auto &offset = diagnostic.at("start");
  if (offset.type != json::kind::number) return file;
  try {
    const auto contents = read_file(file, 16 * 1024 * 1024);
    const auto start = std::min<std::size_t>(std::stoull(offset.text), contents.size());
    std::size_t line = 1, column = 1;
    for (std::size_t i = 0; i < start; ++i) {
      if (contents[i] == '\n') { ++line; column = 1; }
      else {
        const auto byte = static_cast<unsigned char>(contents[i]);
        if ((byte & 0xc0) != 0x80) column += byte >= 0xf0 ? 2 : 1;
      }
    }
    return file + ":" + std::to_string(line) + ":" + std::to_string(column);
  } catch (...) { return file; }
}
std::vector<std::string> analysis_arguments(const project &value, const fs::path &compiler) {
  std::vector<std::string> args{compiler.string(), "--analyze"};
  add_project_arguments(args, value);
  for (const auto &code : value.disabled_rules) { args.push_back("--disable-rule"); args.push_back(code); }
  if (value.warnings_as_errors) args.push_back("--warnings-as-errors");
  args.push_back(value.entry.string());
  return args;
}
int analysis_result(const project &value, const nilbuild::process_result &result,
                    const std::optional<std::string> &format, json &report) {
  if (!result.error.empty()) std::cerr << result.error;
  if (result.cancelled) return result.status;
  if (result.output.empty()) {
    if (result.status == 0) throw std::runtime_error("compiler returned no analysis report");
    return result.status;
  }
  report = nilbuild::json_parser(result.output).parse();
  if (report.at("schema").type != json::kind::string || report.at("schema").text != "nil.analysis.v1" ||
      report.at("success").type != json::kind::boolean || report.at("diagnostics").type != json::kind::array)
    throw std::runtime_error("compiler returned an invalid nil.analysis.v1 report");
  if (format) std::cout << (*format == "json" ? result.output : nilbuild::serialize_project_document(report, *format));
  else for (const auto &diagnostic : report.at("diagnostics").array) {
    std::cerr << diagnostic_location(diagnostic, value.entry) << ": "
        << diagnostic.at("severity").text << ' ' << diagnostic.at("code").text << ": "
        << diagnostic.at("message").text << '\n';
    if (diagnostic.at("hint").type == json::kind::string && !diagnostic.at("hint").text.empty())
      std::cerr << "  " << diagnostic.at("hint").text << '\n';
  }
  return result.status != 0 ? result.status : report.at("success").boolean ? 0 : 1;
}
int check(const project &value, const fs::path &compiler, const std::optional<std::string> &format) {
  json report;
  return analysis_result(value, nilbuild::execute(analysis_arguments(value, compiler), value.directory), format, report);
}
class staging_directory {
public:
  fs::path path;
  explicit staging_directory(const fs::path &parent) {
#ifdef _WIN32
    (void)parent;
    throw std::runtime_error("nilbuild execution currently requires a POSIX host; Windows execution is not supported");
#else
    fs::create_directories(parent);
    std::string pattern = (parent / ".nilbuild-XXXXXX").string();
    const char *created = mkdtemp(pattern.data());
    if (!created) throw std::runtime_error("cannot create staging directory in " + parent.string());
    path = created;
#endif
  }
  ~staging_directory() { std::error_code error; fs::remove_all(path, error); }
};
// Build graph nodes are whole programs: a reference establishes build order,
// while normal include paths continue to define language import resolution.
struct project_graph {
  std::vector<project> projects;
  std::vector<std::vector<std::size_t>> dependencies;
};
project_graph load_graph(const project &root, const options &opts, const fs::path &caller) {
  project_graph graph;
  std::map<std::string, std::size_t> indices;
  std::set<std::string> visiting;
  std::function<std::size_t(const project &)> visit = [&](const project &value) {
    const auto identity = fs::canonical(value.file).string();
    if (visiting.count(identity)) throw std::runtime_error("project reference cycle at " + value.file.string());
    if (indices.count(identity)) return indices.at(identity);
    if (indices.size() >= 256) throw std::runtime_error("project graph exceeds 256 projects");
    visiting.insert(identity);
    const auto index = graph.projects.size();
    indices[identity] = index;
    graph.projects.push_back(value);
    graph.dependencies.emplace_back();
    std::vector<std::size_t> dependencies;
    for (const auto &reference : value.references) dependencies.push_back(visit(load_project(reference, opts, caller)));
    graph.dependencies[index] = std::move(dependencies);
    visiting.erase(identity);
    return index;
  };
  visit(root);
  std::vector<fs::path> outputs;
  for (const auto &value : graph.projects) {
    for (const auto &output : {value.vm_output(), value.native_output()}) {
      for (const auto &previous : outputs) if (same_path(previous, output))
        throw std::runtime_error("projects share an output: " + output.string());
      for (const auto &other : graph.projects)
        if (same_path(output, other.file) || same_path(output, other.entry))
          throw std::runtime_error("output would overwrite a project input: " + output.string());
      outputs.push_back(output);
    }
  }
  return graph;
}
void write_new(const fs::path &path, const std::string &text) {
#ifndef _WIN32
  const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
  if (fd < 0) throw std::runtime_error("cannot create new file (destination must not exist): " + path.string());
  std::size_t position = 0;
  while (position < text.size()) {
    const auto written = write(fd, text.data() + position, text.size() - position);
    if (written < 0 && errno == EINTR) continue;
    if (written <= 0) { close(fd); fs::remove(path); throw std::runtime_error("cannot write " + path.string()); }
    position += static_cast<std::size_t>(written);
  }
  if (close(fd) != 0) { fs::remove(path); throw std::runtime_error("cannot close " + path.string()); }
#else
  throw std::runtime_error("nilbuild file creation currently requires a POSIX host");
#endif
}
std::string destination_format(const options &opts, const fs::path &path) {
  const auto format = nilbuild::project_document_format(path);
  if (opts.format && *opts.format != format) throw std::runtime_error("destination suffix must match --format");
  return format;
}
int create_project(const options &opts, const fs::path &caller) {
  const auto format = opts.format.value_or("nildata");
  const auto path = absolute(opts.project.value_or("nil.project." + format), caller);
  const auto selected = destination_format(opts, path);
  auto data = json::map();
  data.object["schema"] = json::str("nil.project.v1");
  data.object["name"] = json::str(opts.name.value_or("hello"));
  data.object["entry"] = json::str(opts.entry.value_or("src/main.matrix"));
  data.object["includePaths"] = json::list();
  data.object["includePaths"].array.push_back(json::str("src"));
  data.object["outputDirectory"] = json::str("build");
  auto policy = json::map();
  policy.object["disabledRules"] = json::list();
  policy.object["warningsAsErrors"] = json::flag(false);
  data.object["analysis"] = std::move(policy);
  if (opts.sdk) data.object["sdkPath"] = json::str(absolute(*opts.sdk, caller).lexically_relative(path.parent_path()).generic_string());
  // Validate in a private sibling directory before creating the destination.
  staging_directory stage(path.parent_path());
  const auto temporary = stage.path / path.filename();
  write_new(temporary, nilbuild::serialize_project_document(data, selected));
  load_project(temporary, options{}, caller);
  write_new(path, nilbuild::serialize_project_document(data, selected));
  if (selected != "nildata") std::cerr << "nilbuild: legacy project format; NILData is preferred for new projects\n";
  std::cout << path.string() << '\n';
  return 0;
}
int convert_project(const project &value, const options &opts, const fs::path &caller) {
  const auto format = opts.format.value_or("nildata");
  const auto output = opts.output ? absolute(*opts.output, caller) : value.directory / ("nil.project." + format);
  const auto selected = destination_format(opts, output);
  if (same_path(output, value.vm_output()) || same_path(output, value.native_output()) || same_path(output, value.entry))
    throw std::runtime_error("converted manifest would overwrite a project source or artifact: " + output.string());
  if (output.parent_path() != value.directory)
    throw std::runtime_error("format conversion must stay in the project directory to preserve import lookup and working directory");
  const auto contents = read_file(value.file, manifest_limit);
  if (nil::detail::content_digest(contents) != value.manifest_digest) throw std::runtime_error("project changed during conversion; retry");
  auto data = nilbuild::parse_project_document(contents, value.file);
  write_new(output, nilbuild::serialize_project_document(data, selected));
  if (selected != "nildata") std::cerr << "nilbuild: legacy project format; NILData is preferred for new projects\n";
  std::cout << output.string() << '\n';
  return 0;
}
class output_locks {
#ifndef _WIN32
  std::vector<int> descriptors;
#endif
public:
  ~output_locks() {
#ifndef _WIN32
    for (int fd : descriptors) close(fd);
#endif
  }
  void acquire(const fs::path &output) {
#ifndef _WIN32
    const auto directory = output.parent_path() / ".nilbuild";
    fs::create_directories(output.parent_path());
    if (fs::exists(directory) && !fs::is_directory(fs::symlink_status(directory)))
      throw std::runtime_error("build state directory must not be a symlink: " + directory.string());
    fs::create_directory(directory);
    const auto lock = directory / (nil::detail::content_digest(fs::weakly_canonical(output).string()) + ".lock");
    const int fd = open(lock.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0666);
    if (fd < 0) throw std::runtime_error("cannot open build lock: " + lock.string());
    struct stat metadata{};
    if (fstat(fd, &metadata) || !S_ISREG(metadata.st_mode) || flock(fd, LOCK_EX | LOCK_NB)) {
      close(fd); throw std::runtime_error("output is in use by another build or has an invalid lock: " + output.string());
    }
    descriptors.push_back(fd);
#else
    throw std::runtime_error("nilbuild execution currently requires a POSIX host");
#endif
  }
};
fs::path state_path(const fs::path &output) {
  return output.parent_path() / ".nilbuild" / (output.filename().string() + ".nildata");
}
struct build_cancelled { int status; };
void check_cancelled() {
#ifndef _WIN32
  if (nilbuild::pending_signal) throw build_cancelled{128 + nilbuild::pending_signal};
#endif
}
using input_map = std::map<std::string, std::string>;
void add_input(input_map &inputs, const fs::path &path) {
  inputs[fs::absolute(path).lexically_normal().string()] = nil::detail::file_digest(path);
  check_cancelled();
}
void verify_inputs(const input_map &inputs) {
  for (const auto &[path, digest] : inputs) {
    check_cancelled();
    if (nil::detail::file_digest(path) != digest)
      throw std::runtime_error("build input changed during the build; retry: " + path);
  }
}
std::optional<input_map> reported_inputs(const json &report) {
  const auto complete = report.object.find("dependenciesComplete");
  if (complete == report.object.end() || complete->second.type != json::kind::boolean || !complete->second.boolean) return std::nullopt;
  const auto &dependencies = report.at("dependencies");
  if (dependencies.type != json::kind::array || dependencies.array.empty()) return std::nullopt;
  input_map inputs;
  for (const auto &dependency : dependencies.array) {
    const auto path = string_value(dependency.at("path"), "dependency path");
    const auto digest = string_value(dependency.at("digest"), "dependency digest");
    if (!fs::path(path).is_absolute() || digest.size() != 64 || digest.find_first_not_of("0123456789abcdef") != std::string::npos)
      throw std::runtime_error("compiler returned an invalid dependency identity");
    if (!inputs.emplace(path, digest).second) throw std::runtime_error("compiler returned duplicate dependency paths");
  }
  verify_inputs(inputs);
  return inputs;
}
// The compiler tells us which runtime and linker it would select. Query that
// driver for startup objects, libraries and its linker rather than guessing a
// host installation layout. Missing identities disable reuse, not compilation.
bool native_inputs(input_map &inputs, const json &report, const project &value) {
  const auto runtime = report.object.find("nativeRuntime"), linker = report.object.find("nativeLinker");
  if (runtime == report.object.end() || linker == report.object.end() ||
      runtime->second.type != json::kind::string || linker->second.type != json::kind::string) return false;
  add_input(inputs, runtime->second.text);
  const auto files = nilbuild::native_toolchain_files(linker->second.text, value.directory);
  if (!files) return false;
  for (const auto &file : *files) add_input(inputs, file);
  return true;
}
struct build_task {
  std::size_t project_index;
  bool native;
  fs::path output;
  std::optional<input_map> inputs;
  std::string key;
  std::unique_ptr<staging_directory> staging;
};
std::string build_key(const project &value, const build_task &task, const std::vector<std::string> &references) {
  auto description = describe(value);
  description.object["schema"] = json::str("nil.build.inputs.v1");
  description.object["target"] = json::str(task.native ? "native" : "vm");
  description.object["inputs"] = json::map();
  for (const auto &[path, digest] : *task.inputs) description.object["inputs"].object[path] = json::str(digest);
  auto environment = json::map();
  for (const auto *name : {"PATH", "NIL_STDLIB_PATH", "NY_STDLIB_PATH", "NIL_LIBRARY_PATH", "NY_LIBRARY_PATH", "LIBRARY_PATH", "LD_LIBRARY_PATH", "LD_PRELOAD", "LD_RUN_PATH", "LDEMULATION", "GNUTARGET", "COMPILER_PATH", "GCC_EXEC_PREFIX", "SDKROOT", "MACOSX_DEPLOYMENT_TARGET", "CPATH", "CPLUS_INCLUDE_PATH", "LANG", "LC_ALL"}) {
    const char *value = std::getenv(name);
    environment.object[name] = value ? json::str(value) : json{};
  }
  description.object["environment"] = std::move(environment);
  description.object["references"] = json::list();
  for (const auto &reference : references) description.object["references"].array.push_back(json::str(reference));
  return nil::detail::content_digest(nilbuild::dump(description));
}
bool reusable(const build_task &task) {
  if (!task.inputs || !fs::is_regular_file(fs::symlink_status(task.output)) || (task.native && !executable(task.output))) return false;
  try {
    const auto state = state_path(task.output);
    if (!fs::is_regular_file(fs::symlink_status(state))) return false;
    const auto data = nilbuild::parse_project_document(read_file(state, manifest_limit), state);
    return data.at("schema").text == "nil.build.state.v1" && data.at("key").text == task.key &&
        data.at("artifact").text == nil::detail::file_digest(task.output);
  } catch (const std::exception &) { return false; }
}
void publish_state(const build_task &task) {
  const auto state = state_path(task.output);
  if (!task.inputs) {
    std::error_code ignored; fs::remove(state, ignored); return;
  }
  auto data = json::map();
  data.object["schema"] = json::str("nil.build.state.v1");
  data.object["key"] = json::str(task.key);
  data.object["artifact"] = json::str(nil::detail::file_digest(task.output));
  const auto temporary = task.staging->path / "state.nildata";
  write_new(temporary, nilbuild::serialize_project_document(data, "nildata"));
  fs::rename(temporary, state);
}
int failed_batch(const std::vector<nilbuild::process_result> &results) {
  for (const auto &result : results) if (result.started && result.status && !result.cancelled) return result.status;
  for (const auto &result : results) if (result.status) return result.status;
  return 0;
}
int build(const project &root, const options &opts, const fs::path &compiler, const fs::path &self, const fs::path &caller) {
#ifndef _WIN32
  nilbuild::cancellation_scope cancellation;
#endif
  const auto graph = load_graph(root, opts, caller);
  output_locks locks;
  for (const auto &value : graph.projects) {
    if (opts.target != "native") locks.acquire(value.vm_output());
    if (opts.target != "vm") locks.acquire(value.native_output());
  }
  std::set<std::size_t> complete;
  while (complete.size() < graph.projects.size()) {
    check_cancelled();
    std::vector<std::size_t> ready;
    std::vector<nilbuild::process_command> analysis_commands;
    for (std::size_t i = 0; i < graph.projects.size(); ++i) {
      if (complete.count(i) || !std::all_of(graph.dependencies[i].begin(), graph.dependencies[i].end(), [&](auto dependency) { return complete.count(dependency); })) continue;
      ready.push_back(i);
      analysis_commands.push_back({analysis_arguments(graph.projects[i], compiler), graph.projects[i].directory});
    }
    if (ready.empty()) throw std::runtime_error("project graph has no ready work");
    const auto analyses = nilbuild::execute_many(analysis_commands, opts.jobs);
    std::vector<json> reports(ready.size());
    int status = failed_batch(analyses);
    for (std::size_t i = 0; i < ready.size(); ++i) if (analyses[i].started) {
      const auto analyzed = analysis_result(graph.projects[ready[i]], analyses[i], std::nullopt, reports[i]);
      if (!status) status = analyzed;
    }
    if (status) return status;
    std::vector<build_task> tasks;
    std::vector<nilbuild::process_command> commands;
    for (std::size_t i = 0; i < ready.size(); ++i) {
      const auto index = ready[i];
      const auto &value = graph.projects[index];
      const auto sources = reported_inputs(reports[i]);
      if (nil::detail::file_digest(value.file) != value.manifest_digest) throw std::runtime_error("project manifest changed during the build; retry");
      std::vector<std::string> references;
      for (const auto dependency : graph.dependencies[index]) {
        const auto &project = graph.projects[dependency];
        if (opts.target != "native") references.push_back(nil::detail::file_digest(project.vm_output()));
        if (opts.target != "vm") references.push_back(nil::detail::file_digest(project.native_output()));
      }
      for (const bool native : {false, true}) {
        if ((native && opts.target == "vm") || (!native && opts.target == "native")) continue;
        build_task task{index, native, native ? value.native_output() : value.vm_output(), sources, {}, {}};
        if (task.inputs) {
          add_input(*task.inputs, value.file); add_input(*task.inputs, compiler); add_input(*task.inputs, self);
          if (reports[i].object.count("compilerFiles")) {
            for (const auto &file : string_list(reports[i].at("compilerFiles"), "compilerFiles")) add_input(*task.inputs, file);
          }
          if (native && !native_inputs(*task.inputs, reports[i], value)) task.inputs.reset();
        }
        if (task.inputs) {
          for (const auto &[input, digest] : *task.inputs) {
            (void)digest;
            for (std::size_t other = 0; other < graph.projects.size(); ++other) {
              const auto &project = graph.projects[other];
              if (!complete.count(other) && ((opts.target != "native" && same_path(input, project.vm_output())) || (opts.target != "vm" && same_path(input, project.native_output()))))
                throw std::runtime_error("output would overwrite a consumed build input; declare projectReferences to order generated imports: " + input);
            }
          }
          task.key = build_key(value, task, references);
        }
        protect_inputs(value);
        if (!opts.rebuild && reusable(task)) {
          std::cerr << "nilbuild: up to date: " << task.output.string() << '\n';
          continue;
        }
        std::cerr << "nilbuild: building " << value.name << " (" << (native ? "native" : "vm") << ")\n";
        task.staging = std::make_unique<staging_directory>(value.output_directory);
        std::vector<std::string> arguments{compiler.string(), native ? "--native" : "--no-bundle"};
        add_project_arguments(arguments, value);
        arguments.insert(arguments.end(), {"--output", (task.staging->path / task.output.filename()).string(), value.entry.string()});
        commands.push_back({std::move(arguments), value.directory});
        tasks.push_back(std::move(task));
      }
    }
    if (!commands.empty()) {
      const auto compiled = nilbuild::execute_many(commands, opts.jobs);
      for (const auto &result : compiled) std::cerr << result.output << result.error;
      if (const auto failed = failed_batch(compiled)) return failed;
      // Re-resolve the graph after a compilation before publishing cacheable
      // artifacts. This catches new shadowing files as well as modified inputs.
      for (std::size_t i = 0; i < ready.size(); ++i) {
        const auto index = ready[i];
        if (std::none_of(tasks.begin(), tasks.end(), [&](const auto &task) { return task.project_index == index && task.inputs.has_value(); })) continue;
        json current;
        const auto &value = graph.projects[index];
        const auto checked = analysis_result(value, nilbuild::execute(analysis_arguments(value, compiler), value.directory), std::nullopt, current);
        if (checked) return checked;
        if (nilbuild::dump(current.at("dependencies")) != nilbuild::dump(reports[i].at("dependencies")) || !reported_inputs(current))
          throw std::runtime_error("dependency graph changed during the build; retry: " + value.file.string());
      }
      for (const auto &task : tasks) {
        const auto temporary = task.staging->path / task.output.filename();
        if (!fs::is_regular_file(fs::symlink_status(temporary))) throw std::runtime_error("compiler succeeded without creating a regular output artifact");
        if (task.native && !executable(temporary)) throw std::runtime_error("compiler native artifact is not executable");
        if (task.inputs) verify_inputs(*task.inputs);
        protect_inputs(graph.projects[task.project_index]);
      }
      check_cancelled();
      for (const auto &task : tasks) {
        fs::rename(task.staging->path / task.output.filename(), task.output);
        publish_state(task);
      }
    }
    check_cancelled();
    complete.insert(ready.begin(), ready.end());
  }
  return 0;
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc == 2 && std::string(argv[1]) == "--version") {
      std::cout << "nilbuild " << NIL_VERSION << '\n'; return 0;
    }
    if (argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
      usage(); return 0;
    }
    const auto opts = parse_options(argc, argv);
    const auto caller = fs::current_path();
    if (opts.command == "init") return create_project(opts, caller);
    const auto value = load_project(discover_project(opts, caller), opts, caller);
    if (opts.command == "convert") return convert_project(value, opts, caller);
    if (opts.command == "describe") {
      std::cout << nilbuild::serialize_project_document(describe(value), opts.format.value_or("nildata"));
      return 0;
    }
    const auto self = self_path(argv[0], caller);
    const auto compiler = tool_path("nilcompiler", opts.compiler, self, caller);
    if (opts.command == "check") return check(value, compiler, opts.format);
    const bool native = opts.target == "native";
    // Resolve the interpreter before compilation, but never run a stale output.
    std::optional<fs::path> interpreter;
    if (opts.command == "run" && !native)
      interpreter = tool_path("nilinterpreter", opts.interpreter, self, caller);
    const int status = build(value, opts, compiler, self, caller);
    if (status != 0) return status;
    const auto output = native ? value.native_output() : value.vm_output();
    if (opts.command == "build") {
      if (opts.target != "native") std::cout << value.vm_output().string() << '\n';
      if (opts.target != "vm") std::cout << value.native_output().string() << '\n';
      return 0;
    }
    std::vector<std::string> command;
    if (native) command.push_back(output.string());
    else {
      command = {interpreter->string(), output.string()};
      if (!opts.program_arguments.empty()) command.push_back("--");
    }
    command.insert(command.end(), opts.program_arguments.begin(), opts.program_arguments.end());
    return nilbuild::run_program(command, value.directory);
  } catch (const nilbuild::process_cancelled &cancelled) {
    return cancelled.status;
  } catch (const build_cancelled &cancelled) {
    return cancelled.status;
  } catch (const std::exception &error) {
    std::cerr << "nilbuild: " << error.what() << '\n';
    return 2;
  }
}
