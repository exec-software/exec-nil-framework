#include "json.hpp"
#include "process.hpp"
#include "nil_version.hpp"

#include <algorithm>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;
using nilbuild::json;
namespace {
constexpr std::size_t manifest_limit = 1024 * 1024;
struct options {
  std::string command;
  std::optional<std::string> project, sdk, compiler, interpreter;
  std::string target = "vm";
  bool json_output = false;
  std::vector<std::string> program_arguments;
};
struct project {
  fs::path file, directory, entry, output_directory;
  std::string name;
  std::vector<fs::path> include_paths;
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
  const auto data = nilbuild::json_parser(read_file(file, manifest_limit)).parse();
  object_fields(data, {"schema", "name", "entry", "includePaths", "sdkPath", "outputDirectory", "analysis"}, "project");
  if (string_value(data.at("schema"), "schema") != "nil.project.v1")
    throw std::runtime_error("project schema must be 'nil.project.v1'");
  project result;
  result.file = file; result.directory = file.parent_path();
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
  return result;
}
void usage() {
  std::cout << "NIL project driver\n\n"
      "Usage: nilbuild <describe|check|build|run> [options] [-- program arguments]\n"
      "  --project FILE       Manifest (default: nearest ancestor nil.project.json)\n"
      "  --target vm|native   Artifact route (default: vm)\n"
      "  --sdk PATH           Override manifest SDK, relative to caller directory\n"
      "  --compiler PATH      Override nilcompiler executable\n"
      "  --interpreter PATH   Override nilinterpreter executable\n"
      "  --format json        Machine output for describe/check\n"
      "  --help               Show help\n"
      "  --version            Show product version\n\n"
      "Build checks the import graph before compiling and atomically replaces the\n"
      "output only on success. Run always builds first. Programs inherit stdin;\n"
      "compiler children are limited to 300 seconds and 16 MiB of output.\n";
}
options parse_options(int argc, char **argv) {
  options result;
  if (argc < 2) throw std::runtime_error("expected describe, check, build or run; use --help");
  result.command = argv[1];
  if (result.command != "describe" && result.command != "check" && result.command != "build" && result.command != "run")
    throw std::runtime_error("unknown command: " + result.command);
  std::set<std::string> seen;
  for (int i = 2; i < argc; ++i) {
    const std::string option = argv[i];
    if (option == "--") {
      if (result.command != "run") throw std::runtime_error("program arguments require run");
      while (++i < argc) result.program_arguments.push_back(argv[i]);
      break;
    }
    if (!seen.insert(option).second) throw std::runtime_error("duplicate option: " + option);
    if (option != "--project" && option != "--sdk" && option != "--compiler" && option != "--interpreter" && option != "--target" && option != "--format")
      throw std::runtime_error("unknown option: " + option);
    if (++i >= argc || std::string(argv[i]).empty()) throw std::runtime_error("missing value for " + option);
    const std::string value = argv[i];
    if (option == "--project") result.project = value;
    else if (option == "--sdk") result.sdk = value;
    else if (option == "--compiler") result.compiler = value;
    else if (option == "--interpreter") result.interpreter = value;
    else if (option == "--target") {
      if (value != "vm" && value != "native") throw std::runtime_error("target must be vm or native");
      result.target = value;
    } else {
      if (value != "json") throw std::runtime_error("format must be json");
      result.json_output = true;
    }
  }
  if (result.json_output && result.command != "describe" && result.command != "check")
    throw std::runtime_error("--format json is supported only by describe and check");
  return result;
}
fs::path discover_project(const options &opts, const fs::path &caller) {
  if (opts.project) return absolute(*opts.project, caller);
  auto directory = caller;
  for (;;) {
    const auto candidate = directory / "nil.project.json";
    if (fs::exists(candidate)) return candidate;
    if (directory.parent_path() == directory) break;
    directory = directory.parent_path();
  }
  throw std::runtime_error("no nil.project.json found in this directory or its ancestors; use --project FILE");
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
int check(const project &value, const fs::path &compiler, bool json_output) {
  std::vector<std::string> args{compiler.string(), "--analyze"};
  add_project_arguments(args, value);
  for (const auto &code : value.disabled_rules) { args.push_back("--disable-rule"); args.push_back(code); }
  if (value.warnings_as_errors) args.push_back("--warnings-as-errors");
  args.push_back(value.entry.string());
  const auto result = nilbuild::execute(args, value.directory);
  if (!result.error.empty()) std::cerr << result.error;
  if (result.cancelled) return result.status;
  if (result.output.empty()) {
    if (result.status == 0) throw std::runtime_error("compiler returned no analysis report");
    return result.status;
  }
  const auto report = nilbuild::json_parser(result.output).parse();
  if (report.at("schema").type != json::kind::string || report.at("schema").text != "nil.analysis.v1" ||
      report.at("success").type != json::kind::boolean || report.at("diagnostics").type != json::kind::array)
    throw std::runtime_error("compiler returned an invalid nil.analysis.v1 report");
  if (json_output) std::cout << result.output;
  else for (const auto &diagnostic : report.at("diagnostics").array) {
    std::cerr << diagnostic_location(diagnostic, value.entry) << ": "
        << diagnostic.at("severity").text << ' ' << diagnostic.at("code").text << ": "
        << diagnostic.at("message").text << '\n';
    if (diagnostic.at("hint").type == json::kind::string && !diagnostic.at("hint").text.empty())
      std::cerr << "  " << diagnostic.at("hint").text << '\n';
  }
  return result.status != 0 ? result.status : report.at("success").boolean ? 0 : 1;
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
int build(const project &value, const fs::path &compiler, bool native) {
  const int analyzed = check(value, compiler, false);
  if (analyzed != 0) return analyzed;
  protect_inputs(value);
  const auto output = native ? value.native_output() : value.vm_output();
  staging_directory staging(value.output_directory);
  const auto temporary = staging.path / output.filename();
  std::vector<std::string> args{compiler.string(), native ? "--native" : "--no-bundle"};
  add_project_arguments(args, value);
  args.insert(args.end(), {"--output", temporary.string(), value.entry.string()});
  const auto compiled = nilbuild::execute(args, value.directory);
  // Compilation chatter belongs on stderr so `run` preserves program stdout.
  std::cerr << compiled.output << compiled.error;
  if (compiled.status != 0) return compiled.status;
  if (!fs::is_regular_file(fs::symlink_status(temporary)))
    throw std::runtime_error("compiler succeeded without creating a regular output artifact");
  if (native && !executable(temporary)) throw std::runtime_error("compiler native artifact is not executable");
  protect_inputs(value);
  fs::rename(temporary, output);
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
    const auto value = load_project(discover_project(opts, caller), opts, caller);
    if (opts.command == "describe") {
      if (opts.json_output) std::cout << nilbuild::dump(describe(value)) << '\n';
      else std::cout << value.name << '\n' << "Project: " << value.file.string() << '\n'
          << "Entry: " << value.entry.string() << '\n' << "VM output: " << value.vm_output().string() << '\n'
          << "Native output: " << value.native_output().string() << '\n';
      return 0;
    }
    const auto self = self_path(argv[0], caller);
    const auto compiler = tool_path("nilcompiler", opts.compiler, self, caller);
    if (opts.command == "check") return check(value, compiler, opts.json_output);
    const bool native = opts.target == "native";
    // Resolve the interpreter before compilation, but never run a stale output.
    std::optional<fs::path> interpreter;
    if (opts.command == "run" && !native)
      interpreter = tool_path("nilinterpreter", opts.interpreter, self, caller);
    const int status = build(value, compiler, native);
    if (status != 0) return status;
    const auto output = native ? value.native_output() : value.vm_output();
    if (opts.command == "build") { std::cout << output.string() << '\n'; return 0; }
    std::vector<std::string> command;
    if (native) command.push_back(output.string());
    else {
      command = {interpreter->string(), output.string()};
      if (!opts.program_arguments.empty()) command.push_back("--");
    }
    command.insert(command.end(), opts.program_arguments.begin(), opts.program_arguments.end());
    return nilbuild::run_program(command, value.directory);
  } catch (const std::exception &error) {
    std::cerr << "nilbuild: " << error.what() << '\n';
    return 2;
  }
}
