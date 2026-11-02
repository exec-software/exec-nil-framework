#pragma once

// Conservative file closure for the compiler's explicitly selected MSVC
// link.exe route. Unsupported directives/toolchains disable reuse, not builds.
#include "process.hpp"

#include <cctype>
#include <functional>
#include <optional>
#include <set>
#include <sstream>

namespace nilbuild::windows_toolchain_detail {
namespace fs = std::filesystem;

inline std::string lowercase(std::string value) {
  for (auto &byte : value)
    byte = static_cast<char>(std::tolower(static_cast<unsigned char>(byte)));
  return value;
}
inline std::string trim(const std::string &value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  return first == std::string::npos ? std::string{} :
      value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}

// DUMPBIN prints one or more quoted linker options per directive line. Parse
// the complete line; recognizing just DEFAULTLIB and ignoring the remainder
// could miss @response files, /LIBPATH changes or another file-reading option.
inline bool directive_line(const std::string &line,
                           std::vector<std::string> &libraries) {
  std::size_t offset = 0;
  while (offset < line.size()) {
    while (offset < line.size() && std::isspace(static_cast<unsigned char>(line[offset]))) ++offset;
    if (offset == line.size()) return true;
    std::string token;
    bool quoted = false;
    while (offset < line.size()) {
      const char byte = line[offset++];
      if (byte == '"') { quoted = !quoted; continue; }
      if (!quoted && std::isspace(static_cast<unsigned char>(byte))) break;
      if (byte == '\0') return false;
      token += byte;
    }
    if (quoted || token.empty() || (token.front() != '/' && token.front() != '-')) return false;
    const auto colon = token.find(':');
    const auto option = lowercase(token.substr(1, colon == std::string::npos ? colon : colon - 1));
    const auto argument = colon == std::string::npos ? std::string{} : token.substr(colon + 1);
    if (option == "defaultlib") {
      if (argument.empty() || argument.find_first_of("*?@") != std::string::npos) return false;
      libraries.push_back(argument);
    } else if (option == "throwingnew") {
      // MSVC's own operator-new object marks its C++ allocation semantics.
      // This is a bare flag, not a filename or a library search instruction.
      if (colon != std::string::npos) return false;
    } else if (option == "failifmismatch" || option == "alternatename" ||
               option == "include" || option == "export" || option == "merge" ||
               option == "section" || option == "manifestdependency" ||
               option == "guard" || option == "guardsym" || option == "editandcontinue" ||
               option == "disallowlib") {
      // These affect symbols, validation or output sections, not input-file
      // selection. DISALLOWLIB names an incompatible CRT, never an additional
      // input. Their exact bytes are included in their archive's digest.
      if (argument.empty() && option != "editandcontinue") return false;
    } else {
      return false;
    }
  }
  return true;
}

inline std::optional<std::vector<std::string>> directive_libraries(const std::string &dump) {
  std::vector<std::string> libraries;
  std::istringstream lines(dump);
  std::string line;
  bool directives = false, library = false, saw_directives = false;
  while (std::getline(lines, line)) {
    line = trim(line);
    if (line == "File Type: LIBRARY") { library = true; continue; }
    if (line == "Linker Directives") { directives = saw_directives = true; continue; }
    if (line.empty()) { directives = false; continue; }
    if (directives) {
      if (line == "-----------------") continue;
      if (!directive_line(line, libraries)) return {};
    } else if (line.front() == '/' || line.front() == '-') {
      // Do not silently ignore options if a tool changes its heading format.
      return {};
    }
    if (line.find(".drectve") != std::string::npos && !saw_directives) return {};
  }
  return library ? std::optional<std::vector<std::string>>(libraries) : std::nullopt;
}

inline std::vector<fs::path> library_search_paths(
    const fs::path &cwd, const std::vector<fs::path> &explicit_paths,
    const std::string &environment) {
  // Microsoft documents cwd first for an unqualified .lib, /LIBPATH in
  // command-line order next, and LIB last. Do not canonicalize selected
  // aliases: repointing a path is an input change even if the old file exists.
  std::vector<fs::path> paths{cwd};
  for (const auto &path : explicit_paths)
    paths.push_back((path.is_absolute() ? path : cwd / path).lexically_normal());
  std::istringstream entries(environment);
  std::string entry;
  while (std::getline(entries, entry, ';')) {
    if (entry.size() >= 2 && entry.front() == '"' && entry.back() == '"')
      entry = entry.substr(1, entry.size() - 2);
    const fs::path path(entry);
    paths.push_back((path.is_absolute() ? path : cwd / path).lexically_normal());
  }
  return paths;
}

class collector {
public:
  using probe = std::function<std::optional<std::string>(const fs::path &)>;
private:
  fs::path driver, cwd, runtime;
  std::vector<fs::path> search;
  probe inspect;
  std::set<fs::path> files_seen;
  std::set<std::string> libraries_seen;

  static bool regular(const fs::path &path) {
    std::error_code error;
    return fs::is_regular_file(path, error) && !error;
  }
  bool add(const fs::path &path) {
    if (!regular(path) || files_seen.size() >= 1024) return false;
    files_seen.insert(path.lexically_normal());
    return true;
  }
  std::optional<fs::path> resolve(std::string name) const {
    fs::path path(name);
    if (!path.has_extension()) path += ".lib";
    if (path.has_parent_path()) {
      const auto selected = (path.is_absolute() ? path : cwd / path).lexically_normal();
      return regular(selected) ? std::optional<fs::path>(selected) : std::nullopt;
    }
    for (const auto &directory : search) {
      const auto selected = (directory / path).lexically_normal();
      if (regular(selected)) return selected;
    }
    return {};
  }
  bool library(const fs::path &path, unsigned depth) {
    if (depth > 64 || !add(path)) return false;
    // Default library graphs may contain cycles. The archive has already
    // been recorded, so revisiting it does not need another subprocess.
    if (!libraries_seen.insert(lowercase(path.lexically_normal().string())).second) return true;
    const auto output = inspect(path);
    if (!output) return false;
    const auto dependencies = directive_libraries(*output);
    if (!dependencies) return false;
    for (const auto &name : *dependencies) {
      const auto selected = resolve(name);
      if (!selected || !library(*selected, depth + 1)) return false;
    }
    return true;
  }
public:
  collector(fs::path linker, fs::path directory, fs::path native_runtime,
            const std::vector<fs::path> &explicit_paths,
            const std::string &lib_environment, probe inspect_archive)
      : driver(std::move(linker)), cwd(std::move(directory)),
        runtime(std::move(native_runtime)),
        search(library_search_paths(cwd, explicit_paths, lib_environment)),
        inspect(std::move(inspect_archive)) {}

  std::optional<std::vector<fs::path>> files() {
    if (!driver.is_absolute() || lowercase(driver.filename().string()) != "link.exe" ||
        !runtime.is_absolute() || !add(driver)) return {};
    // /MANIFEST:EMBED needs the resource converter. LINK searches PATH if its
    // adjacent converter is absent; that fallback is outside this closure.
    if (!add(driver.parent_path() / "cvtres.exe")) return {};
    // MSVC can invoke cvtres.exe and load c2/mspdb DLLs while linking. Include
    // its adjacent helper installation (including localized subdirectories),
    // not only link.exe. A changed/new helper changes the closure on recheck.
    std::error_code error;
    fs::recursive_directory_iterator iterator(driver.parent_path(), error), end;
    if (error) return {};
    for (; iterator != end; iterator.increment(error)) {
      if (error || iterator.depth() > 4) return {};
      const auto path = iterator->path();
      const auto extension = lowercase(path.extension().string());
      if (extension == ".exe" || extension == ".dll" || extension == ".config" ||
          extension == ".manifest") {
        if (!add(path)) return {};
      }
    }
    if (error || !library(runtime, 0)) return {};
    for (const auto *name : {"kernel32.lib", "ws2_32.lib", "msvcrt.lib", "ucrt.lib",
                             "vcruntime.lib", "legacy_stdio_definitions.lib"}) {
      const auto selected = resolve(name);
      if (!selected || !library(*selected, 0)) return {};
    }
    return std::vector<fs::path>(files_seen.begin(), files_seen.end());
  }
};

} // namespace nilbuild::windows_toolchain_detail
