#pragma once

// Conservative closure for nilbuild's ordinary Apple-clang native invocation.
// This deliberately inventories a superset of SDK linkable inputs; unsupported
// drivers, search profiles, stub formats or escaping dependencies disable reuse.
// Every returned file is content-hashed by nilbuild, and the complete namespace
// is collected again before publication. No timestamps certify cache identity.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace nilbuild::darwin_toolchain_detail {
namespace fs = std::filesystem;

inline void explain(std::string *reason, std::string message) {
  if (!reason || !reason->empty()) return;
  if (message.size() > 1024) message.resize(1024);
  for (char &byte : message)
    if (static_cast<unsigned char>(byte) < 32 || byte == 127) byte = '?';
  *reason = std::move(message);
}

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

inline bool macho_magic(const std::string &bytes) {
  if (bytes.size() < 4) return false;
  const auto prefix = bytes.substr(0, 4);
  return prefix == std::string("\xfe\xed\xfa\xce", 4) || prefix == std::string("\xce\xfa\xed\xfe", 4) ||
         prefix == std::string("\xfe\xed\xfa\xcf", 4) || prefix == std::string("\xcf\xfa\xed\xfe", 4) ||
         prefix == std::string("\xca\xfe\xba\xbe", 4) || prefix == std::string("\xbe\xba\xfe\xca", 4) ||
         prefix == std::string("\xca\xfe\xba\xbf", 4) || prefix == std::string("\xbf\xba\xfe\xca", 4);
}

// An SDK may ship runnable plug-ins alongside its linkable interfaces. Apple
// ld's ordinary dylib parser rejects MH_EXECUTE and MH_BUNDLE; this profile
// never passes -bundle_loader. Ignore only positively identified such images,
// including every universal slice. A dylib/object, malformed header or unknown
// file remains unmodeled. Sources: cctools mach-o/{loader,fat}.h and ld64's
// src/ld/parsers/macho_dylib_file.cpp, Parser::validFile.
inline std::optional<std::vector<std::uint32_t>> macho_file_types(const fs::path &path) {
  std::vector<std::uint32_t> kinds;
  std::error_code error;
  const auto size = fs::file_size(path, error);
  if (error || size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamoff>::max())) return {};
  std::ifstream file(path, std::ios::binary);
  const auto read = [&](std::uint64_t offset, std::size_t count) -> std::optional<std::string> {
    if (offset > size || count > size - offset || count > 4 * 1024 * 1024) return {};
    std::string bytes(count, '\0'); file.clear(); file.seekg(static_cast<std::streamoff>(offset));
    if (!file.read(bytes.data(), static_cast<std::streamsize>(count))) return {};
    return bytes;
  };
  const auto number = [](const std::string &bytes, std::size_t at, unsigned count, bool big) {
    std::uint64_t value = 0;
    for (unsigned index = 0; index < count; ++index)
      value |= std::uint64_t(static_cast<unsigned char>(bytes[at + index])) <<
               (8 * (big ? count - index - 1 : index));
    return value;
  };
  const auto slice = [&](std::uint64_t offset, std::uint64_t extent,
                         std::optional<std::pair<std::uint64_t, std::uint64_t>> cpu) {
    const auto header = read(offset, 28);
    if (!header || extent < 28) return false;
    const auto magic = header->substr(0, 4);
    const bool big = magic == std::string("\xfe\xed\xfa\xce", 4) || magic == std::string("\xfe\xed\xfa\xcf", 4);
    const bool wide = magic == std::string("\xfe\xed\xfa\xcf", 4) || magic == std::string("\xcf\xfa\xed\xfe", 4);
    if (!big && magic != std::string("\xce\xfa\xed\xfe", 4) && magic != std::string("\xcf\xfa\xed\xfe", 4)) return false;
    const auto header_size = wide ? 32U : 28U;
    if (extent < header_size || !read(offset, header_size)) return false;
    if (cpu && (number(*header, 4, 4, big) != cpu->first || (number(*header, 8, 4, big) & 0x00ffffffU) != (cpu->second & 0x00ffffffU))) return false;
    const auto kind = number(*header, 12, 4, big);
    const auto count = number(*header, 16, 4, big), bytes = number(*header, 20, 4, big);
    if (count > 65536 || bytes > 4 * 1024 * 1024 || bytes > extent - header_size) return false;
    const auto commands = read(offset + header_size, static_cast<std::size_t>(bytes));
    if (!commands) return false;
    std::size_t at = 0;
    for (std::uint64_t index = 0; index < count; ++index) {
      if (at > commands->size() || commands->size() - at < 8) return false;
      const auto length = number(*commands, at + 4, 4, big);
      if (length < 8 || length % 4 || length > commands->size() - at) return false;
      at += static_cast<std::size_t>(length);
    }
    if (at != commands->size()) return false;
    kinds.push_back(static_cast<std::uint32_t>(kind));
    return true;
  };
  const auto header = read(0, 8);
  if (!header || !macho_magic(*header)) return {};
  const auto magic = header->substr(0, 4);
  const bool fat_big = magic == std::string("\xca\xfe\xba\xbe", 4) || magic == std::string("\xca\xfe\xba\xbf", 4);
  const bool fat_little = magic == std::string("\xbe\xba\xfe\xca", 4) || magic == std::string("\xbf\xba\xfe\xca", 4);
  if (!fat_big && !fat_little) {
    if (!slice(0, size, {})) return {};
    return kinds;
  }
  const bool wide = magic == std::string("\xca\xfe\xba\xbf", 4) || magic == std::string("\xbf\xba\xfe\xca", 4);
  const auto count = number(*header, 4, 4, fat_big);
  const auto stride = wide ? 32U : 20U;
  if (!count || count > 64 || size < 8 + count * stride) return {};
  const auto table = read(8, static_cast<std::size_t>(count * stride));
  if (!table) return {};
  // ld64 also recognizes one legacy ARM64 descriptor beyond nfat_arch in
  // FAT_MAGIC files. Do not silently omit a hidden potentially-linkable slice.
  if (!wide && size >= 8 + (count + 1) * stride) {
    const auto extra = read(8 + count * stride, stride);
    if (!extra) return {};
    if (number(*extra, 0, 4, fat_big) == 0x0100000c && number(*extra, 4, 4, fat_big) <= 1) return {};
  }
  std::vector<std::pair<std::uint64_t, std::uint64_t>> extents;
  for (std::uint64_t index = 0; index < count; ++index) {
    const auto at = static_cast<std::size_t>(index * stride);
    const auto offset = number(*table, at + 8, wide ? 8 : 4, fat_big);
    const auto extent = number(*table, at + (wide ? 16 : 12), wide ? 8 : 4, fat_big);
    const auto alignment = number(*table, at + (wide ? 24 : 16), 4, fat_big);
    if (alignment > 63 || offset < 8 + count * stride || offset > size || extent > size - offset ||
        offset % (std::uint64_t(1) << alignment) || (wide && number(*table, at + 28, 4, fat_big))) return {};
    for (const auto &previous : extents)
      if (offset < previous.second && previous.first < offset + extent) return {};
    extents.emplace_back(offset, offset + extent);
    if (!slice(offset, extent, std::make_pair(number(*table, at, 4, fat_big), number(*table, at + 4, 4, fat_big)))) return {};
  }
  return kinds;
}

inline bool non_linkable_macho(const fs::path &path) {
  const auto kinds = macho_file_types(path);
  return kinds && !kinds->empty() && std::all_of(kinds->begin(), kinds->end(),
      [](std::uint32_t kind) { return kind == 2 || kind == 8; }); // MH_EXECUTE / MH_BUNDLE
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
  std::string *reason;

  bool fail(const char *message, const fs::path &path = {}) {
    if (reason) explain(reason, std::string(message) + (path.empty() ? "" : ": " + path.string()));
    return false;
  }
  std::optional<std::vector<fs::path>> refuse(const char *message, const fs::path &path = {}) {
    fail(message, path); return {};
  }

  fs::path host_path(const fs::path &path) const { return host / path.relative_path(); }
  bool add(const fs::path &path) {
    if (!clean_path(path) || !regular(path)) return fail("required input is not a normalized regular file", path);
    if (files_seen.size() >= 65536) return fail("input file inventory exceeded 65536 entries", path);
    if (path.extension() == ".a") {
      std::ifstream archive(path, std::ios::binary); char magic[8]{};
      archive.read(magic, sizeof(magic));
      if (archive.gcount() == 8 && std::string(magic, sizeof(magic)) == "!<thin>\n") return fail("thin archive has unmodeled external members", path);
    }
    files_seen.insert(path); return true;
  }
  bool read_stub(const fs::path &path) {
    std::ifstream stream(path, std::ios::binary); std::string text; char block[8192];
    while (stream.read(block, sizeof(block)) || stream.gcount()) {
      text.append(block, static_cast<std::size_t>(stream.gcount()));
      if (text.size() > 32 * 1024 * 1024) return fail("TBD input exceeds 32 MiB", path);
    }
    if (stream.bad()) return fail("could not read TBD input", path);
    return supported_stub(text, &stub_references) || fail("unsupported TBD dependency profile", path);
  }
  bool tree(const fs::path &path, bool sdk, unsigned depth = 0) {
    std::error_code error;
    if (depth > 32 || ++entries > 1000000) return fail("SDK traversal exceeded depth or entry bound", path);
    const auto canonical = fs::canonical(path, error);
    if (error || !active_directories.insert(canonical).second) return fail("unreadable or cyclic SDK directory", path);
    fs::directory_iterator iterator(path, error), end;
    if (error) return fail("could not enumerate SDK directory", path);
    for (; iterator != end; iterator.increment(error)) {
      if (error || ++entries > 1000000) return fail("SDK enumeration failed or exceeded entry bound", path);
      const auto entry = iterator->path();
      const auto status = fs::status(entry, error);
      if (error) return fail("could not stat SDK entry", entry);
      if (fs::is_directory(status)) {
        // Header/module trees cannot be selected as linked library files.
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
            if (stream.bad()) return fail("could not inspect extensionless SDK entry", entry);
            const std::string bytes(prefix, size);
            if (bytes.rfind("--- !tapi-tbd", 0) == 0) {
              if (!add(entry) || !read_stub(entry)) return false;
            } else if (bytes.rfind("!<arch>\n", 0) == 0 || bytes.rfind("!<thin>\n", 0) == 0) {
              return fail("unmodeled extensionless archive SDK input", entry);
            } else if (macho_magic(bytes) && !non_linkable_macho(entry)) {
              return fail("unmodeled or malformed extensionless Mach-O SDK input", entry);
            }
            continue; // Non-linkable metadata such as framework PkgInfo.
          }
          if (!add(entry) || (extension == ".tbd" && !read_stub(entry))) return false;
          if (sdk && (extension == ".dylib" || extension == ".so")) return fail("unmodeled binary SDK input", entry);
        }
      } else return fail("SDK entry is not a regular file or directory", entry);
    }
    active_directories.erase(canonical);
    return !error || fail("SDK enumeration failed", path);
  }
  bool image(const fs::path &path, const fs::path &executable_directory, unsigned depth = 0,
             const std::vector<fs::path> &inherited_rpaths = {}, std::vector<fs::path> *effective_rpaths = nullptr) {
    if (depth > 32) return fail("tool image dependency depth exceeded 32", path);
    if (!add(path)) return false;
    std::error_code error;
    const auto physical = fs::canonical(path, error);
    if (error) return fail("could not resolve tool image", path);
    if (!add(physical)) return false;
    auto cached = image_descriptions.find(path);
    if (cached == image_descriptions.end()) {
      const auto description = dependencies(path);
      if (!description) return fail("tool image inspection failed", path);
      cached = image_descriptions.emplace(path, *description).first;
    }
    const auto &description = cached->second;
    if (physical.parent_path() != path.parent_path()) {
      const auto loader_relative = [](const std::string &name) { return name.rfind("@loader_path", 0) == 0; };
      if (std::any_of(description.rpaths.begin(), description.rpaths.end(), loader_relative) ||
          std::any_of(description.libraries.begin(), description.libraries.end(), loader_relative)) return fail("cross-directory image alias uses loader-relative paths", path);
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
                                    !system_install_name(selected.string()))) return fail("tool rpath escapes modeled roots", selected);
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
    if (inspected_contexts.size() > 1024) return fail("tool image contexts exceeded 1024", path);
    for (const auto &name : description.libraries) {
      std::vector<fs::path> selected;
      if (name.rfind("@rpath/", 0) == 0) {
        const auto tail = fs::path(name.substr(7));
        if (tail.has_parent_path()) return fail("tool rpath dependency has an unmodeled suffix", name);
        // Tool inspection inventories every universal slice (the OS shim may
        // be arm64e while the emitted link target is arm64). Its merged rpaths
        // cannot safely pick only one winner: include every existing candidate
        // so a different architecture/context's first match cannot be hidden.
        for (const auto &root : rpaths) {
          const auto candidate = root / tail;
          if (regular(candidate)) selected.push_back(candidate);
        }
        // A dylib's install ID is self metadata, not an extra file dependency.
        if (selected.empty() && path.filename() == tail && path.extension() == ".dylib") selected.push_back(path);
        if (selected.empty()) return fail("tool rpath dependency could not be resolved", name);
      } else if (name.rfind("@loader_path/", 0) == 0) {
        const auto candidate = (physical.parent_path() / name.substr(13)).lexically_normal();
        if (!inside(candidate, plan.installed.parent_path())) return fail("loader-relative dependency escapes toolchain", candidate);
        selected.push_back(candidate);
      } else if (system_install_name(name)) {
        const auto candidate = host_path(name);
        // Actual system images without ordinary files are covered by the
        // compiler's complete active shared-cache backing-file superset.
        if (regular(candidate)) selected.push_back(candidate);
      } else {
        const fs::path candidate = name;
        if (!clean_path(candidate) || !inside(candidate, plan.installed.parent_path())) return fail("tool dependency escapes modeled roots", candidate);
        selected.push_back(candidate);
      }
      for (const auto &candidate : selected)
        if (candidate != path && !image(candidate, executable_directory, depth + 1, rpaths)) return false;
    }
    return true;
  }
  bool adjacent_helpers() {
    const auto directory = plan.installed.parent_path() / "lib";
    std::error_code error;
    fs::directory_iterator iterator(directory, error), end;
    if (error) return fail("could not enumerate adjacent toolchain helpers", directory);
    for (; iterator != end; iterator.increment(error)) {
      if (error || ++entries > 1000000) return fail("adjacent helper enumeration failed or exceeded entry bound", directory);
      if (iterator->path().extension() == ".dylib" && !add(iterator->path())) return false;
    }
    return !error || fail("adjacent helper enumeration failed", directory);
  }
  bool local_library_inputs() {
    if (!plan.local_libraries) return true;
    std::error_code error;
    // Options.cpp:4380-4429: an explicit absolute -L uses the SDK directory
    // when it exists; unlike a standard search directory it otherwise falls
    // back to the host. Inspect this choice anew on every collection.
    const auto sdk_directory = plan.sdk / "usr/local/lib";
    const bool sdk_directory_exists = fs::is_directory(sdk_directory, error);
    if (error && error != std::errc::no_such_file_or_directory) return fail("could not inspect SDK local library directory", sdk_directory);
    if (sdk_directory_exists) return true; // Complete SDK tree was collected.
    error.clear();
    const auto directory = host_path("/usr/local/lib");
    const bool host_directory_exists = fs::is_directory(directory, error);
    if (error && error != std::errc::no_such_file_or_directory) return fail("could not inspect host local library directory", directory);
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
      if (candidates.size() > 131072) return fail("local-library candidate bound exceeded", directory);
      bool changed = false;
      for (const auto &name : candidates) {
        if (!checked.insert(name).second) continue;
        changed = true;
        const auto path = directory / name;
        error.clear(); const auto status = fs::status(path, error);
        if (error == std::errc::no_such_file_or_directory || status.type() == fs::file_type::not_found) continue;
        if (error || !fs::is_regular_file(status)) return fail("local-library candidate is unreadable or not regular", path);
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
            link_plan selected_plan, probe inspect_image, fs::path host_root = "/", std::string *failure_reason = nullptr)
      : driver(std::move(selected_driver)), cwd(std::move(directory)), runtime(std::move(selected_runtime)),
        host(std::move(host_root)), plan(std::move(selected_plan)), dependencies(std::move(inspect_image)), reason(failure_reason) {}

  std::optional<std::vector<fs::path>> files() {
    std::error_code error;
    const auto actual_driver = fs::canonical(driver, error);
    if (error) return refuse("could not resolve selected driver", driver);
    const auto actual_compiler = fs::canonical(plan.installed / "clang++", error);
    if (error) return refuse("could not resolve selected compiler", plan.installed / "clang++");
    const auto actual_linker = fs::canonical(plan.linker, error);
    if (error) return refuse("could not resolve selected linker", plan.linker);
    std::vector<fs::path> linker_rpaths;
    if (!clean_path(driver) || !clean_path(cwd)) return refuse("driver or working directory is not normalized");
    if (!add(runtime) || !image(driver, actual_driver.parent_path()) ||
        !image(plan.installed / "clang++", actual_compiler.parent_path()) ||
        !image(plan.linker, actual_linker.parent_path(), 0, {}, &linker_rpaths) ||
        !image(plan.lto, actual_linker.parent_path(), 0, linker_rpaths) || !add(plan.compiler_rt)) return {};
    if (!add(plan.sdk / "SDKSettings.json") || !add(host_path("/System/Library/CoreServices/SystemVersion.plist"))) return {};
    if (fs::exists(plan.sdk / "AppleInternal", error) || error) return refuse("AppleInternal SDK profile or lookup failure", plan.sdk);
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
      if (error) return refuse("could not inspect optional SDK directory", directory);
      if (present && !tree(directory, true)) return {};
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
        if (!files_seen.count(plan.sdk / relative)) return refuse("SDK re-export missing from inventory", plan.sdk / relative);
        continue;
      }
      if (regular(sdk_stub)) {
        if (!files_seen.count(sdk_stub)) return refuse("SDK re-export stub missing from inventory", sdk_stub);
        continue;
      }
      auto host_stub = host_path(name); host_stub.replace_extension(".tbd");
      if (regular(host_path(name)) || regular(host_stub)) return refuse("unmodeled ordinary host re-export fallback", name);
    }
    return std::vector<fs::path>(files_seen.begin(), files_seen.end());
  }
};
} // namespace nilbuild::darwin_toolchain_detail
