#pragma once

// Conservative closure for nilbuild's ordinary Apple-clang native invocation.
// This deliberately inventories a superset of SDK linkable inputs; unsupported
// drivers, search profiles, stub formats or escaping dependencies disable reuse.
// Every returned file is content-hashed by nilbuild, and the complete namespace
// is collected again before publication. No timestamps certify cache identity.
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace nilbuild::darwin_toolchain_detail {
namespace fs = std::filesystem;

inline std::string trim(const std::string &text) {
  const auto first = text.find_first_not_of(" \r\n\t");
  return first == std::string::npos ? "" : text.substr(first, text.find_last_not_of(" \r\n\t") - first + 1);
}
inline bool regular(const fs::path &path) {
  std::error_code error;
  return fs::is_regular_file(path, error) && !error;
}
inline bool inside(const fs::path &path, const fs::path &root) {
  const auto value = path.lexically_normal(), base = root.lexically_normal();
  auto at = value.begin();
  for (auto part = base.begin(); part != base.end(); ++part, ++at)
    if (at == value.end() || *at != *part) return false;
  return true;
}
inline bool clean_path(const fs::path &path) {
  const auto text = path.string();
  return path.is_absolute() && text.find_first_of("\r\n\0", 0, 3) == std::string::npos &&
         path == path.lexically_normal();
}

// clang -### quotes every command argument. Interpret only its double-quoted
// command syntax; never execute text through a shell or accept response files.
inline std::optional<std::vector<std::string>> quoted_arguments(const std::string &line) {
  std::vector<std::string> result;
  std::size_t at = 0;
  while (at < line.size()) {
    while (at < line.size() && std::isspace(static_cast<unsigned char>(line[at]))) ++at;
    if (at == line.size()) break;
    if (line[at++] != '"') return {};
    std::string value;
    bool closed = false;
    while (at < line.size()) {
      char byte = line[at++];
      if (byte == '"') { closed = true; break; }
      if (byte == '\\') {
        if (at == line.size() || (line[at] != '\\' && line[at] != '"')) return {};
        byte = line[at++];
      }
      if (byte == '\0' || byte == '\n' || byte == '\r') return {};
      value += byte;
    }
    if (!closed || value.empty()) return {};
    result.push_back(std::move(value));
    if (at < line.size() && !std::isspace(static_cast<unsigned char>(line[at]))) return {};
  }
  return result.empty() ? std::nullopt : std::optional<std::vector<std::string>>(std::move(result));
}

struct link_plan {
  fs::path installed, linker, sdk, lto, compiler_rt;
  std::string architecture;
  bool local_libraries = false;
};
inline std::optional<link_plan> parse_plan(const std::string &text, const fs::path &runtime) {
  if (text.size() > 1024 * 1024 || text.find('\0') != std::string::npos) return {};
  link_plan plan;
  std::vector<std::string> command;
  std::istringstream lines(text);
  std::string line;
  bool apple = false;
  while (std::getline(lines, line)) {
    line = trim(line);
    if (line.empty()) continue;
    if (line.rfind("Apple clang version ", 0) == 0 && !apple) { apple = true; continue; }
    if (line.rfind("Target: ", 0) == 0 || line == "Thread model: posix") continue;
    if (line.rfind("InstalledDir: ", 0) == 0 && plan.installed.empty()) {
      plan.installed = line.substr(14); continue;
    }
    if (line.front() == '"' && command.empty()) {
      const auto parsed = quoted_arguments(line);
      if (!parsed) return {};
      command = *parsed; continue;
    }
    // Includes default configuration-file diagnostics and extra compiler jobs.
    return {};
  }
  if (!apple || !clean_path(plan.installed) || command.empty() ||
      plan.installed.filename() != "bin" || plan.installed.parent_path().filename() != "usr") return {};
  const auto installation = plan.installed.parent_path().parent_path().filename();
  if (installation != "XcodeDefault.xctoolchain" && installation != "CommandLineTools") return {};
  plan.linker = command.front();
  if (plan.linker != plan.installed / "ld") return {};
  bool dynamic = false, platform = false, output = false, input = false, cxx = false, system = false;
  std::set<std::string> frameworks;
  for (std::size_t index = 1; index < command.size(); ++index) {
    const auto &option = command[index];
    const auto next = [&]() -> std::optional<std::string> {
      if (index >= command.size() - 1) { index = command.size(); return {}; }
      return command[++index];
    };
    if (option == "-demangle" || option == "-no_deduplicate" || option == "-adhoc_codesign") continue;
    if (option == "-dynamic" && !dynamic) { dynamic = true; continue; }
    if (option == "-lc++" && !cxx) { cxx = true; continue; }
    if (option == "-lSystem" && !system) { system = true; continue; }
    // Apple's selected driver emits this explicit search path on ordinary
    // hosted installations. Unlike ld's implicit default it can fall back to
    // the host when the selected SDK has no usr/local/lib directory.
    if (option == "-L/usr/local/lib") { plan.local_libraries = true; continue; }
    if (option == "-L") {
      const auto value = next();
      if (!value || *value != "/usr/local/lib") return {};
      plan.local_libraries = true; continue;
    }
    if (option == "-arch" && plan.architecture.empty()) {
      const auto value = next(); if (!value || (*value != "arm64" && *value != "x86_64")) return {};
      plan.architecture = *value; continue;
    }
    if (option == "-platform_version" && !platform) {
      const auto name = next(), minimum = next(), sdk_version = next();
      if (!name || *name != "macos" || !minimum || !sdk_version || minimum->empty() || sdk_version->empty() ||
          minimum->find_first_not_of("0123456789.") != std::string::npos ||
          sdk_version->find_first_not_of("0123456789.") != std::string::npos) return {};
      platform = true; continue;
    }
    if (option == "-syslibroot" && plan.sdk.empty()) {
      const auto value = next(); if (!value || !clean_path(*value)) return {};
      plan.sdk = *value; continue;
    }
    if (option == "-lto_library" && plan.lto.empty()) {
      const auto value = next(); if (!value || !clean_path(*value)) return {};
      plan.lto = *value; continue;
    }
    if (option == "-mllvm") {
      const auto value = next(); if (!value || *value != "-enable-linkonceodr-outlining") return {};
      continue;
    }
    if (option == "-o" && !output) {
      const auto value = next(); if (!value || *value != "/dev/null") return {};
      output = true; continue;
    }
    if (option == "-framework") {
      const auto value = next();
      if (!value || (*value != "Cocoa" && *value != "CoreGraphics") || !frameworks.insert(*value).second) return {};
      continue;
    }
    if (option == runtime.string() && !input) { input = true; continue; }
    const fs::path path(option);
    if (plan.compiler_rt.empty() && clean_path(path) && path.filename() == "libclang_rt.osx.a" &&
        inside(path, plan.installed.parent_path() / "lib" / "clang")) {
      plan.compiler_rt = path; continue;
    }
    return {}; // Includes custom -L/-F, -force_load, plugins, and response files.
  }
  if (!dynamic || !platform || !output || !input || !cxx || !system || frameworks.size() != 2 ||
      plan.architecture.empty() || plan.sdk.empty() || plan.lto.empty() || plan.compiler_rt.empty() ||
      plan.lto != plan.installed.parent_path() / "lib" / "libLTO.dylib" ||
      plan.sdk.filename().string().rfind("MacOSX", 0) != 0 || plan.sdk.extension() != ".sdk") return {};
  return plan;
}

// The two explicit archives are the only link inputs that can carry object
// autolinks in this profile. An unmodeled LC_LINKER_OPTION can request libraries
// outside the command's fixed c++/System/framework roots. otool must successfully
// inspect the selected architecture before the caller invokes the collector.
inline bool supported_archive_commands(const std::string &text) {
  return !text.empty() && text.size() <= 4 * 1024 * 1024 && text.find('\0') == std::string::npos &&
         text.find("LC_LINKER_OPTION") == std::string::npos;
}

inline bool system_install_name(const std::string &name) {
  const fs::path path(name);
  // Install names are always POSIX paths, including in portable parser tests.
  return !name.empty() && name.front() == '/' && name.find_first_of("\\\r\n\0 \t", 0, 6) == std::string::npos &&
         path == path.lexically_normal() && (inside(path, "/usr/lib") || inside(path, "/System/Library/Frameworks") ||
                              inside(path, "/System/Library/PrivateFrameworks"));
}

// A conservative lexical admission check for Apple's generated YAML TBD v4.
// Re-export paths must be explicit system install names. Escapes, aliases, custom tags,
// relative re-exports and rpath names require a richer parser and disable reuse.
// Inventorying every SDK stub covers all architecture/OS re-export choices.
inline bool supported_stub(const std::string &text, std::set<std::string> *references = nullptr) {
  if (text.size() > 32 * 1024 * 1024 || text.find('\0') != std::string::npos ||
      text.find('\\') != std::string::npos || text.rfind("--- !tapi-tbd", 0) != 0) return false;
  std::istringstream lines(text); std::string line;
  bool version = false, install = false, in_libraries = false;
  std::string libraries;
  auto scalar = [](std::string value) {
    value = trim(value);
    if (value.size() >= 2 && (value.front() == '\'' || value.front() == '"') && value.back() == value.front())
      value = value.substr(1, value.size() - 2);
    return value;
  };
  const auto admit_name = [&](const std::string &name) {
    if (!system_install_name(name)) return false;
    const fs::path path(name);
    if (!path.extension().empty() && path.extension() != ".dylib" && path.extension() != ".tbd") return false;
    for (const auto &part : path)
      if (part == "Headers" || part == "PrivateHeaders" || part == "Modules") return false;
    if (references) { references->insert(name); if (references->size() > 65536) return false; }
    return true;
  };
  while (std::getline(lines, line)) {
    line = trim(line);
    if (line.empty() || line.front() == '#') continue;
    if (line.rfind("---", 0) == 0) { if (line != "--- !tapi-tbd") return false; continue; }
    if (line.rfind("tbd-version:", 0) == 0) {
      if (trim(line.substr(12)) != "4") return false;
      version = true; continue;
    }
    if (line.rfind("install-name:", 0) == 0) {
      // LC_ID_DYLIB and $ld$install_name export strings describe output/interface
      // identity; they do not name additional files read by the linker.
      // Generated SDKs contain @rpath and historical AppleInternal IDs here.
      if (scalar(line.substr(13)).empty()) return false;
      install = true;
    }
    const auto split = line.find(':');
    const auto key = split == std::string::npos ? std::string{} : trim(line.substr(0, split));
    if (key == "reexported-libraries" && !trim(line.substr(split + 1)).empty()) return false;
    if (!line.empty() && (line.front() == '"' || line.front() == '\'')) {
      const auto end_quote = line.find(line.front(), 1);
      if (end_quote != std::string::npos && trim(line.substr(end_quote + 1)).rfind(':', 0) == 0) return false;
    }
    if (key == "libraries" || key == "library") {
      if (in_libraries) return false;
      libraries = trim(line.substr(split + 1));
      if (libraries.empty() || libraries.front() != '[') return false;
      in_libraries = true;
    } else if (in_libraries) libraries += ' ' + line;
    if (in_libraries && libraries.find(']') != std::string::npos) {
      if (libraries.back() != ']') return false;
      std::istringstream values(libraries.substr(1, libraries.size() - 2)); std::string value;
      while (std::getline(values, value, ',')) if (!admit_name(scalar(value))) return false;
      in_libraries = false;
    }
    // Reject YAML indirection outside quoted strings, so it cannot manufacture
    // an unobserved re-export value from an anchor or tagged scalar.
    bool quoted = false; char quote = 0;
    for (std::size_t at = 0; at < line.size(); ++at) {
      const char byte = line[at];
      if ((byte == '\'' || byte == '"') && (!quoted || quote == byte)) {
        quoted = !quoted; quote = byte; continue;
      }
      if (!quoted && (byte == '&' || byte == '*' || byte == '!' || byte == '|' || byte == '>' ||
                      byte == '{' || byte == '}')) return false;
    }
  }
  return version && install && !in_libraries;
}

struct image_description {
  std::vector<std::string> libraries, rpaths;
  bool system_loader = false;
};
inline std::optional<image_description> parse_image(const fs::path &path,
                                                    const std::string &listing,
                                                    const std::string &commands) {
  if (listing.size() > 1024 * 1024 || commands.size() > 4 * 1024 * 1024 ||
      listing.find('\0') != std::string::npos || commands.find('\0') != std::string::npos ||
      commands.find("LC_DYLD_ENVIRONMENT") != std::string::npos) return {};
  image_description result;
  std::istringstream lines(listing); std::string line; bool heading = false;
  while (std::getline(lines, line)) {
    if (line.empty()) continue;
    if (line.front() != '\t' && line.front() != ' ') {
      if (line.rfind(path.string(), 0) != 0 || line.back() != ':') return {};
      heading = true; continue;
    }
    line = trim(line); const auto marker = line.find(" (compatibility version ");
    if (marker == std::string::npos) return {};
    result.libraries.push_back(line.substr(0, marker));
  }
  if (!heading) return {};
  std::istringstream load_commands(commands); bool rpath = false, loader = false;
  while (std::getline(load_commands, line)) {
    line = trim(line);
    if (line == "cmd LC_RPATH") { if (rpath || loader) return {}; rpath = true; }
    else if (line == "cmd LC_LOAD_DYLINKER") { if (rpath || loader) return {}; loader = true; }
    else if (rpath && line.rfind("path ", 0) == 0) {
      const auto marker = line.rfind(" (offset ");
      if (marker == std::string::npos || marker <= 5) return {};
      result.rpaths.push_back(line.substr(5, marker - 5)); rpath = false;
    } else if (loader && line.rfind("name ", 0) == 0) {
      const auto marker = line.rfind(" (offset ");
      if (marker == std::string::npos || line.substr(5, marker - 5) != "/usr/lib/dyld") return {};
      result.system_loader = true; loader = false;
    } else if ((rpath || loader) && line.rfind("cmdsize ", 0) != 0) return {};
  }
  return rpath || loader ? std::nullopt : std::optional<image_description>(std::move(result));
}

class collector {
public:
  using probe = std::function<std::optional<image_description>(const fs::path &)>;
private:
  fs::path driver, cwd, runtime, host;
  link_plan plan;
  probe dependencies;
  std::set<fs::path> files_seen, active_directories;
  std::set<std::string> stub_references;
  std::set<std::string> inspected_contexts;
  std::map<fs::path, image_description> image_descriptions;
  std::size_t entries = 0;

  fs::path host_path(const fs::path &path) const { return host / path.relative_path(); }
  bool add(const fs::path &path) {
    if (!clean_path(path) || !regular(path) || files_seen.size() >= 65536) return false;
    if (path.extension() == ".a") {
      std::ifstream archive(path, std::ios::binary); char magic[8]{};
      archive.read(magic, sizeof(magic));
      if (archive.gcount() == 8 && std::string(magic, sizeof(magic)) == "!<thin>\n") return false;
    }
    files_seen.insert(path); return true;
  }
  bool read_stub(const fs::path &path) {
    std::ifstream stream(path, std::ios::binary); std::string text; char block[8192];
    while (stream.read(block, sizeof(block)) || stream.gcount()) {
      text.append(block, static_cast<std::size_t>(stream.gcount()));
      if (text.size() > 32 * 1024 * 1024) return false;
    }
    return !stream.bad() && supported_stub(text, &stub_references);
  }
  bool tree(const fs::path &path, bool sdk, unsigned depth = 0) {
    std::error_code error;
    if (depth > 32 || ++entries > 1000000) return false;
    const auto canonical = fs::canonical(path, error);
    if (error || !active_directories.insert(canonical).second) return false;
    fs::directory_iterator iterator(path, error), end;
    if (error) return false;
    for (; iterator != end; iterator.increment(error)) {
      if (error || ++entries > 1000000) return false;
      const auto entry = iterator->path();
      const auto status = fs::status(entry, error);
      if (error) return false;
      if (fs::is_directory(status)) {
        // Header/resource trees cannot be selected as linked library files.
        const auto name = entry.filename();
        if (name == "Headers" || name == "PrivateHeaders" || name == "Modules") continue;
        if (!tree(entry, sdk, depth + 1)) return false;
      } else if (fs::is_regular_file(status)) {
        const auto extension = entry.extension();
        if (extension == ".tbd" || extension == ".dylib" || extension == ".so" || extension == ".a" || extension == ".o" || extension.empty()) {
          if (sdk && extension.empty()) {
            std::ifstream stream(entry, std::ios::binary); char prefix[16]{};
            stream.read(prefix, sizeof(prefix));
            const auto size = static_cast<std::size_t>(stream.gcount());
            if (stream.bad()) return false;
            const std::string bytes(prefix, size);
            if (bytes.rfind("--- !tapi-tbd", 0) == 0) {
              if (!add(entry) || !read_stub(entry)) return false;
            } else if (bytes.rfind("!<arch>\n", 0) == 0 || bytes.rfind("!<thin>\n", 0) == 0 ||
                (size >= 4 && (bytes.compare(0, 4, "\xfe\xed\xfa\xce", 4) == 0 || bytes.compare(0, 4, "\xce\xfa\xed\xfe", 4) == 0 ||
                               bytes.compare(0, 4, "\xfe\xed\xfa\xcf", 4) == 0 || bytes.compare(0, 4, "\xcf\xfa\xed\xfe", 4) == 0 ||
                               bytes.compare(0, 4, "\xca\xfe\xba\xbe", 4) == 0 || bytes.compare(0, 4, "\xbe\xba\xfe\xca", 4) == 0 ||
                               bytes.compare(0, 4, "\xca\xfe\xba\xbf", 4) == 0 || bytes.compare(0, 4, "\xbf\xba\xfe\xca", 4) == 0))) return false;
            continue; // Non-linkable metadata such as framework PkgInfo.
          }
          if (!add(entry) || (extension == ".tbd" && !read_stub(entry))) return false;
          if (sdk && (extension == ".dylib" || extension == ".so")) return false; // binary SDK stubs need load-command analysis
        }
      } else return false;
    }
    active_directories.erase(canonical);
    return !error;
  }
  bool image(const fs::path &path, const fs::path &executable_directory, unsigned depth = 0,
             const std::vector<fs::path> &inherited_rpaths = {}, std::vector<fs::path> *effective_rpaths = nullptr) {
    if (depth > 32 || !add(path)) return false;
    std::error_code error;
    const auto physical = fs::canonical(path, error);
    if (error || !add(physical)) return false;
    auto cached = image_descriptions.find(path);
    if (cached == image_descriptions.end()) {
      const auto description = dependencies(path);
      if (!description) return false;
      cached = image_descriptions.emplace(path, *description).first;
    }
    const auto &description = cached->second;
    if (physical.parent_path() != path.parent_path()) {
      const auto loader_relative = [](const std::string &name) { return name.rfind("@loader_path", 0) == 0; };
      if (std::any_of(description.rpaths.begin(), description.rpaths.end(), loader_relative) ||
          std::any_of(description.libraries.begin(), description.libraries.end(), loader_relative)) return false;
    }
    // Compiler identity separately proves the active system dyld image and all
    // shared-cache backing bytes. Include an ordinary loader path as well when
    // the host exposes one; an alternative LC_LOAD_DYLINKER is never admitted.
    if (description.system_loader && regular(host_path("/usr/lib/dyld")) && !add(host_path("/usr/lib/dyld"))) return false;
    std::vector<fs::path> rpaths;
    for (const auto &name : description.rpaths) {
      fs::path selected;
      if (name.rfind("@loader_path/", 0) == 0) selected = (physical.parent_path() / name.substr(13)).lexically_normal();
      else if (name.rfind("@executable_path/", 0) == 0) selected = (executable_directory / name.substr(17)).lexically_normal();
      else selected = name;
      if (!clean_path(selected) || (!inside(selected, plan.installed.parent_path()) &&
                                    !system_install_name(selected.string()))) return false;
      rpaths.push_back(selected);
    }
    rpaths.insert(rpaths.end(), inherited_rpaths.begin(), inherited_rpaths.end());
    std::vector<fs::path> unique_rpaths;
    for (const auto &root : rpaths)
      if (std::find(unique_rpaths.begin(), unique_rpaths.end(), root) == unique_rpaths.end()) unique_rpaths.push_back(root);
    rpaths = std::move(unique_rpaths);
    if (effective_rpaths) *effective_rpaths = rpaths;
    std::string context = path.string() + '\0' + executable_directory.string();
    for (const auto &root : rpaths) context += '\0' + root.string();
    if (!inspected_contexts.insert(context).second) return true;
    if (inspected_contexts.size() > 1024) return false;
    for (const auto &name : description.libraries) {
      fs::path selected;
      if (name.rfind("@rpath/", 0) == 0) {
        const auto tail = fs::path(name.substr(7));
        if (tail.has_parent_path()) return false;
        for (const auto &root : rpaths) {
          const auto candidate = root / tail;
          if (regular(candidate)) { selected = candidate; break; }
        }
        // A dylib's install ID may use its parent's rpath and appear in otool
        // -L. It names this file, not an additional dependency.
        if (selected.empty() && path.filename() == tail && path.extension() == ".dylib") selected = path;
        if (selected.empty()) return false;
      } else if (name.rfind("@loader_path/", 0) == 0) {
        selected = (physical.parent_path() / name.substr(13)).lexically_normal();
        if (!inside(selected, plan.installed.parent_path())) return false;
      } else if (system_install_name(name)) {
        selected = host_path(name);
        // Non-file system images are represented by the compiler's complete
        // shared-cache backing-file superset. Ordinary overrides are included.
        if (!regular(selected)) continue;
      } else {
        selected = name;
        if (!clean_path(selected) || !inside(selected, plan.installed.parent_path())) return false;
      }
      if (selected != path && !image(selected, executable_directory, depth + 1, rpaths)) return false;
    }
    return true;
  }
  bool adjacent_helpers() {
    const auto directory = plan.installed.parent_path() / "lib";
    std::error_code error;
    fs::directory_iterator iterator(directory, error), end;
    if (error) return false;
    for (; iterator != end; iterator.increment(error)) {
      if (error || ++entries > 1000000) return false;
      if (iterator->path().extension() == ".dylib" && !add(iterator->path())) return false;
    }
    return !error;
  }
  bool local_library_inputs() {
    if (!plan.local_libraries) return true;
    std::error_code error;
    // Options.cpp:4380-4429: an explicit absolute -L uses the SDK directory
    // when it exists; unlike a standard search directory it otherwise falls
    // back to the host. Inspect this choice anew on every collection.
    const auto sdk_directory = plan.sdk / "usr/local/lib";
    const bool sdk_directory_exists = fs::is_directory(sdk_directory, error);
    if (error && error != std::errc::no_such_file_or_directory) return false;
    if (sdk_directory_exists) return true; // Complete SDK tree was collected.
    error.clear();
    const auto directory = host_path("/usr/local/lib");
    const bool host_directory_exists = fs::is_directory(directory, error);
    if (error && error != std::errc::no_such_file_or_directory) return false;
    if (!host_directory_exists) return true;

    // The default command's two -l roots and every architecture/OS SDK
    // re-export form a conservative set of names the linker can look up here.
    // This is a fresh namespace lookup, not a list cached from the last build:
    // additions, removals, aliases and new transitive TBD names are observed.
    // Unrelated Homebrew libraries cannot be chosen by these roots; inspecting
    // their dyld dependencies would incorrectly model them as executed tools.
    std::set<std::string> candidates = {"libc++.tbd", "libc++.dylib", "libc++.so", "libc++.a",
                                         "libSystem.tbd", "libSystem.dylib", "libSystem.so", "libSystem.a"};
    std::set<std::string> checked;
    for (;;) {
      for (const auto &reference : stub_references) {
        const auto path = fs::path(reference), leaf = path.filename();
        // Options.cpp:1153-1199 searches frameworks via -F, and never searches
        // -L for a .dylib embedded inside a framework. Other indirect library
        // lookups use the literal leaf and its .tbd replacement (no lib prefix).
        if (reference.find('/' + leaf.string() + ".framework/") != std::string::npos ||
            (path.extension() == ".dylib" && reference.find(".framework/") != std::string::npos)) continue;
        candidates.insert(leaf.string());
        auto stub = leaf; stub.replace_extension(".tbd"); candidates.insert(stub.string());
      }
      if (candidates.size() > 131072) return false;
      bool changed = false;
      for (const auto &name : candidates) {
        if (!checked.insert(name).second) continue;
        changed = true;
        const auto path = directory / name;
        error.clear(); const auto status = fs::status(path, error);
        if (error == std::errc::no_such_file_or_directory || status.type() == fs::file_type::not_found) continue;
        if (error || !fs::is_regular_file(status)) return false;
        // A selected ordinary binary/archive requires its own link-input
        // dependency closure; don't confuse that with the tools' dyld graph.
        // Admit only supported text stubs, including a stub behind an alias
        // with a .dylib/extensionless name, and collect its re-exports as well.
        if (!add(path) || !read_stub(path)) return false;
      }
      if (!changed) break;
    }
    return true;
  }
public:
  collector(fs::path selected_driver, fs::path directory, fs::path selected_runtime,
            link_plan selected_plan, probe inspect_image, fs::path host_root = "/")
      : driver(std::move(selected_driver)), cwd(std::move(directory)), runtime(std::move(selected_runtime)),
        host(std::move(host_root)), plan(std::move(selected_plan)), dependencies(std::move(inspect_image)) {}

  std::optional<std::vector<fs::path>> files() {
    std::error_code error;
    const auto actual_driver = fs::canonical(driver, error);
    if (error) return {};
    const auto actual_compiler = fs::canonical(plan.installed / "clang++", error);
    if (error) return {};
    const auto actual_linker = fs::canonical(plan.linker, error);
    if (error) return {};
    std::vector<fs::path> linker_rpaths;
    if (!clean_path(driver) || !clean_path(cwd) || !add(runtime) || !image(driver, actual_driver.parent_path()) ||
        !image(plan.installed / "clang++", actual_compiler.parent_path()) ||
        !image(plan.linker, actual_linker.parent_path(), 0, {}, &linker_rpaths) ||
        !image(plan.lto, actual_linker.parent_path(), 0, linker_rpaths) || !add(plan.compiler_rt)) return {};
    if (!add(plan.sdk / "SDKSettings.json") || !add(host_path("/System/Library/CoreServices/SystemVersion.plist"))) return {};
    if (fs::exists(plan.sdk / "AppleInternal", error) || error) return {};
    for (const auto &directory : {plan.sdk / "usr/lib", plan.sdk / "System/Library/Frameworks"})
      if (!tree(directory, true)) return {};
    if (!adjacent_helpers()) return {};
    // With exactly one syslibroot, Apple's linker omits absent standard search
    // directories instead of falling back to host /usr/local or /Library.
    // https://github.com/apple-oss-distributions/ld64/blob/main/src/ld/Options.cpp
    // Keep optional SDK search namespaces in the fresh inventory when present.
    for (const auto &relative : {"usr/local/lib", "Library/Frameworks", "System/Library/PrivateFrameworks"}) {
      const auto directory = plan.sdk / relative;
      const bool present = fs::exists(directory, error);
      if (error || (present && !tree(directory, true))) return {};
    }
    if (!local_library_inputs()) return {};
    // Absolute re-export lookup differs from standard -L/-F lookup: when the
    // SDK/inlined stub cannot satisfy it, ld can try the raw host pathname.
    // Until ordinary host link-input closure is modeled, refuse that profile.
    // Re-checking these absences catches a newly installed fallback as well.
    for (const auto &name : stub_references) {
      const auto relative = fs::path(name).relative_path();
      auto sdk_stub = plan.sdk / relative; sdk_stub.replace_extension(".tbd");
      if (regular(plan.sdk / relative)) {
        if (!files_seen.count(plan.sdk / relative)) return {};
        continue;
      }
      if (regular(sdk_stub)) {
        if (!files_seen.count(sdk_stub)) return {};
        continue;
      }
      auto host_stub = host_path(name); host_stub.replace_extension(".tbd");
      if (regular(host_path(name)) || regular(host_stub)) return {};
    }
    return std::vector<fs::path>(files_seen.begin(), files_seen.end());
  }
};
} // namespace nilbuild::darwin_toolchain_detail
