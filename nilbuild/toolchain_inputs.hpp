#pragma once

#include "process.hpp"
#include "windows_toolchain_inputs.hpp"
#include "darwin_toolchain_inputs.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#if defined(__APPLE__)
#include <crt_externs.h>
#endif

namespace nilbuild {
struct process_cancelled : std::exception {
  int status;
  explicit process_cancelled(int value) : status(value) {}
  const char *what() const noexcept override { return "toolchain inspection was cancelled"; }
};

#if defined(__linux__)
namespace toolchain_detail {
namespace fs = std::filesystem;
inline std::string trim(const std::string &text) {
  const auto first = text.find_first_not_of(" \r\n\t");
  return first == std::string::npos ? "" : text.substr(first, text.find_last_not_of(" \r\n\t") - first + 1);
}
inline bool regular(const fs::path &path) {
  std::error_code error;
  return fs::is_regular_file(path, error) && !error;
}

// This intentionally recognizes the file-list subset used by GNU system
// library scripts. General linker scripts can alter search paths, select files
// conditionally, or include other scripts; unknown syntax disables reuse.
class script_parser {
  std::vector<std::string> tokens;
  std::size_t position = 0;
  const std::function<bool(const std::string &)> &file;

  bool consume(const std::string &token) {
    if (position == tokens.size() || tokens[position] != token) return false;
    ++position;
    return true;
  }
  bool files(unsigned depth) {
    if (depth > 32 || !consume("(")) return false;
    bool any = false;
    while (position < tokens.size() && tokens[position] != ")") {
      if (consume(",")) continue;
      const auto token = tokens[position++];
      if (token == "AS_NEEDED") {
        if (!files(depth + 1)) return false;
      } else if (token == "(" || token == ";" || !file(token)) return false;
      any = true;
    }
    return any && consume(")");
  }
public:
  explicit script_parser(const std::function<bool(const std::string &)> &callback) : file(callback) {}
  bool parse(const std::string &text) {
    for (std::size_t i = 0; i < text.size();) {
      const auto c = static_cast<unsigned char>(text[i]);
      if (std::isspace(c)) { ++i; continue; }
      if (text.compare(i, 2, "/*") == 0) {
        const auto end = text.find("*/", i + 2);
        if (end == std::string::npos) return false;
        i = end + 2;
        continue;
      }
      if (text[i] == '(' || text[i] == ')' || text[i] == ',' || text[i] == ';') {
        tokens.push_back(text.substr(i++, 1));
      } else if (text[i] == '"') {
        const auto end = text.find('"', i + 1);
        if (end == std::string::npos) return false;
        auto token = text.substr(i + 1, end - i - 1);
        if (token.empty() || token.find('\\') != std::string::npos) return false;
        tokens.push_back(std::move(token));
        i = end + 1;
      } else {
        const auto begin = i;
        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])) &&
               text[i] != '(' && text[i] != ')' && text[i] != ',' && text[i] != ';') ++i;
        tokens.push_back(text.substr(begin, i - begin));
      }
    }
    bool inputs = false;
    while (position < tokens.size()) {
      if (consume(";")) continue;
      const auto command = tokens[position++];
      if (command == "INPUT" || command == "GROUP") {
        if (!files(0)) return false;
        inputs = true;
      } else if (command == "OUTPUT_FORMAT" || command == "OUTPUT_ARCH") {
        if (!consume("(")) return false;
        unsigned values = 0;
        while (position < tokens.size() && tokens[position] != ")") {
          const auto token = tokens[position++];
          if (token == ",") continue;
          if (token == "(" || token == ";" || ++values > 3) return false;
        }
        if (!consume(")") || (command == "OUTPUT_ARCH" ? values != 1 : values != 1 && values != 3)) return false;
      } else return false;
    }
    return inputs;
  }
};

class collector {
  fs::path driver, cwd;
  bool query_failed = false;
  std::set<fs::path> paths, visiting;
  std::map<std::string, std::optional<fs::path>> queried_files;

  fs::path absolute(const fs::path &path) const {
    // Do not canonicalize away a selected symlink. Repointing it is an input
    // change even if its old target remains available on disk.
    return path.is_absolute() ? path : cwd / path;
  }
  std::optional<std::string> query(const std::string &option) {
    const auto result = execute({driver.string(), "-no-pie", "-pthread", option}, cwd);
    if (result.cancelled) throw process_cancelled(result.status);
    if (result.status) { query_failed = true; return std::nullopt; }
    const auto value = trim(result.output);
    if (value.find_first_of("\r\n\0", 0, 3) != std::string::npos) { query_failed = true; return std::nullopt; }
    return value;
  }
  std::optional<fs::path> find_file(const std::string &name) {
    const auto cached = queried_files.find(name);
    if (cached != queried_files.end()) return cached->second;
    const auto answer = query("-print-file-name=" + name);
    std::optional<fs::path> path;
    if (answer && !answer->empty() && *answer != name && regular(absolute(*answer))) path = absolute(*answer);
    queried_files.emplace(name, path);
    return path;
  }
  std::optional<fs::path> library(const std::string &name) {
    if (name.empty() || name.find('/') != std::string::npos || name.find('\\') != std::string::npos) return std::nullopt;
    if (name.front() == ':') return find_file(name.substr(1));
    const auto shared = find_file("lib" + name + ".so");
    return shared ? shared : find_file("lib" + name + ".a");
  }
  bool input(const std::string &name, const fs::path &script, unsigned depth) {
    if (name.empty() || name.front() == '=' || name.find('$') != std::string::npos) return false;
    if (name.compare(0, 2, "-l") == 0) {
      const auto selected = library(name.substr(2));
      return selected && collect(*selected, depth);
    }
    if (name.front() == '-') return false;
    const fs::path path(name);
    if (path.is_absolute()) return collect(path, depth);
    // GNU INPUT/GROUP search the script directory before cwd and library paths.
    // https://sourceware.org/binutils/docs/ld/File-Commands.html
    for (const auto &candidate : {script.parent_path() / path, cwd / path})
      if (regular(candidate)) return collect(candidate, depth);
    const auto selected = find_file(name);
    return selected && collect(*selected, depth);
  }
  bool collect(const fs::path &selected, unsigned depth) {
    const auto path = absolute(selected);
    if (depth > 16 || paths.size() >= 128 || !regular(path)) return false;
    if (visiting.count(path)) return false;
    if (paths.count(path)) return true;
    std::ifstream stream(path, std::ios::binary);
    char header[8]{};
    stream.read(header, sizeof(header));
    const auto count = stream.gcount();
    if (count >= 4 && std::memcmp(header, "\177ELF", 4) == 0) {
      paths.insert(path); return true;
    }
    if (count == 8 && std::memcmp(header, "!<arch>\n", 8) == 0) {
      paths.insert(path); return true;
    }
    // Thin archives refer to external object files: treating their small
    // archive header as the complete input would permit stale cache hits.
    if (count == 8 && std::memcmp(header, "!<thin>\n", 8) == 0) return false;
    stream.clear();
    stream.seekg(0);
    std::string text;
    char buffer[8192];
    while (stream.read(buffer, sizeof(buffer)) || stream.gcount()) {
      text.append(buffer, static_cast<std::size_t>(stream.gcount()));
      if (text.size() > 1024 * 1024 || text.find('\0') != std::string::npos) return false;
    }
    if (stream.bad()) return false;
    visiting.insert(path);
    const std::function<bool(const std::string &)> file = [&](const auto &name) { return input(name, path, depth + 1); };
    script_parser parser(file);
    const bool supported = parser.parse(text);
    visiting.erase(path);
    if (supported) paths.insert(path);
    return supported;
  }
  std::optional<fs::path> program(const std::string &answer) const {
    if (answer.empty()) return std::nullopt;
    const fs::path path(answer);
    if (path.has_parent_path()) return regular(absolute(path)) ? std::optional<fs::path>(absolute(path)) : std::nullopt;
    const char *environment = std::getenv("PATH");
    if (!environment) return std::nullopt;
    std::istringstream directories(environment);
    std::string directory;
    while (std::getline(directories, directory, ':')) {
      const auto candidate = absolute(fs::path(directory) / path);
      if (regular(candidate) && access(candidate.c_str(), X_OK) == 0) return candidate;
    }
    return std::nullopt;
  }
public:
  collector(fs::path compiler_driver, fs::path directory) : driver(std::move(compiler_driver)), cwd(std::move(directory)) {}
  std::optional<std::vector<fs::path>> files() {
    // This models the framework's Linux non-PIE link invocation. Different
    // host formats, custom sysroots and external GCC specs need a richer graph.
    const auto sysroot = query("-print-sysroot");
    if (!sysroot || !sysroot->empty() || find_file("specs")) return std::nullopt;
    const auto linker_name = query("-print-prog-name=ld");
    if (!linker_name) return std::nullopt;
    const auto linker = program(*linker_name);
    if (!linker) return std::nullopt;
    paths.insert(absolute(driver));
    paths.insert(*linker);
    for (const auto *name : {"crt1.o", "crti.o", "crtbegin.o", "crtend.o", "crtn.o", "libc_nonshared.a"}) {
      const auto path = find_file(name);
      if (!path || !collect(*path, 0)) return std::nullopt;
    }
    for (const auto *name : {"stdc++", "m", "gcc_s", "gcc", "c", "dl", "pthread"}) {
      const auto path = library(name);
      if (!path || !collect(*path, 0)) return std::nullopt;
    }
    if (query_failed) return std::nullopt;
    return std::vector<fs::path>(paths.begin(), paths.end());
  }
};
} // namespace toolchain_detail
#endif

inline std::optional<std::vector<std::filesystem::path>> native_toolchain_files(
    const std::filesystem::path &driver, const std::filesystem::path &directory,
    const std::filesystem::path &runtime = {},
    const std::vector<std::filesystem::path> &library_paths = {}) {
#if defined(__linux__)
  (void)runtime; (void)library_paths;
  return toolchain_detail::collector(driver, directory).files();
#elif defined(_WIN32)
  if (library_paths.empty()) return std::nullopt;
  // These can inject arbitrary response files, objects, search paths or tools
  // that are outside the supported closure. Compile normally, without reuse.
  for (const auto *name : {"LINK", "_LINK_"}) {
    const char *value = std::getenv(name);
    if (value && *value) return std::nullopt;
  }
  const char *lib = std::getenv("LIB");
  const auto inspect = [&](const std::filesystem::path &archive) -> std::optional<std::string> {
    const auto result = execute({driver.string(), "/dump", "/nologo", "/directives", archive.string()}, directory);
    if (result.cancelled) throw process_cancelled(result.status);
    if (result.status || !result.error.empty()) return {};
    return result.output;
  };
  return windows_toolchain_detail::collector(driver, directory, runtime,
      library_paths, lib ? lib : "", inspect).files();
#elif defined(__APPLE__)
  // nilbuild currently forwards no -l/--link options. Expanding that command
  // surface requires expanding this closure before admitting those projects.
  if (!library_paths.empty() || runtime.empty()) return std::nullopt;
  bool local_library_environment = false;
  for (char **entry = *_NSGetEnviron(); entry && *entry; ++entry) {
    const std::string item(*entry);
    const auto equal = item.find('=');
    if (equal == std::string::npos || equal + 1 == item.size()) continue;
    const auto name = item.substr(0, equal);
    if (name == "LIBRARY_PATH") {
      // This exact search profile is modeled below and already appears in the
      // build key. Empty components/other directories are intentionally not
      // inferred or sanitized; they require a broader search closure.
      if (item.substr(equal + 1) != "/usr/local/lib") return std::nullopt;
      local_library_environment = true; continue;
    }
    if (name.rfind("DYLD_", 0) == 0 || name.rfind("LD_", 0) == 0 || name.rfind("CCC_", 0) == 0 ||
        name.rfind("CLANG_CONFIG_", 0) == 0 || name == "COMPILER_PATH" ||
        name == "GCC_EXEC_PREFIX" || name == "CPATH" || name == "CPLUS_INCLUDE_PATH" ||
        name == "C_INCLUDE_PATH" || name == "OBJC_INCLUDE_PATH" || name == "ARCHFLAGS" ||
        name == "IPHONEOS_DEPLOYMENT_TARGET" || name == "TVOS_DEPLOYMENT_TARGET" ||
        name == "WATCHOS_DEPLOYMENT_TARGET") return std::nullopt;
  }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
  unsigned queries = 0;
  const auto query = [&](const std::vector<std::string> &command, bool driver_plan = false)
      -> std::optional<std::string> {
    const auto now = std::chrono::steady_clock::now();
    if (++queries > 128 || now >= deadline) return {};
    process_options options;
    options.close_stdin = true; options.cleanup_descendants = true;
    options.time_limit = std::min(std::chrono::milliseconds(15000),
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now));
    options.output_limit = 4 * 1024 * 1024; options.total_output_limit = 4 * 1024 * 1024;
    const auto result = execute_many({{command, directory}}, 1, options).front();
    if (result.cancelled) throw process_cancelled(result.status);
    if (result.status || result.timed_out || result.output_limited || (!driver_plan && !result.error.empty())) return {};
    return driver_plan ? result.output + result.error : result.output;
  };
  const auto output = query({driver.string(), "-###", "-pthread", "-o", "/dev/null", runtime.string(),
                             "-framework", "Cocoa", "-framework", "CoreGraphics"}, true);
  if (!output) return {};
  const auto plan = darwin_toolchain_detail::parse_plan(*output, runtime);
  if (!plan || (local_library_environment && !plan->local_libraries)) return {};
#if defined(__aarch64__) || defined(__arm64__)
  if (plan->architecture != "arm64") return {};
#elif defined(__x86_64__)
  if (plan->architecture != "x86_64") return {};
#else
  return {};
#endif
  // Archives can carry hidden -l/-framework directives in object load commands.
  // Keep that unsupported until their names join the modeled search roots.
  for (const auto &archive : {runtime, plan->compiler_rt}) {
    const auto commands = query({"/usr/bin/otool", "-arch", plan->architecture, "-l", archive.string()});
    if (!commands || !darwin_toolchain_detail::supported_archive_commands(*commands)) return {};
  }
  const auto inspect = [&](const std::filesystem::path &image)
      -> std::optional<darwin_toolchain_detail::image_description> {
    const auto libraries = query({"/usr/bin/otool", "-arch", plan->architecture, "-L", image.string()});
    const auto commands = query({"/usr/bin/otool", "-arch", plan->architecture, "-l", image.string()});
    if (!libraries || !commands) return {};
    return darwin_toolchain_detail::parse_image(image, *libraries, *commands);
  };
  return darwin_toolchain_detail::collector(driver, directory, runtime, *plan, inspect).files();
#else
  (void)driver; (void)directory; (void)runtime; (void)library_paths;
  return std::nullopt;
#endif
}
} // namespace nilbuild
