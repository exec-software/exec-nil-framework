/*
 * Copyright (C) 2008 - 2026 Execute Software.  All rights reserved.
 * SPDX-License-Identifier: MIT
 * See the LICENSE file in the project root for license terms.
 */
#include "codegen.hpp"
#include "analysis.hpp"
#include "standard_namespace.hpp"
#include "nil_version.hpp"
#include "diagnostics.hpp"
#include "nil_loader.hpp"
#if !defined(_WIN32)
#include <unistd.h>
#endif

#include "matrix_diagnostics.hpp"
#include "matrix_parser.hpp"
#include "parser.hpp"
#include "type_checker.hpp"
#include "executable_context.hpp"
#include "secure_file_io.hpp"
#if defined(NY_HAS_LLVM)
#include "llvm_backend.hpp"
#endif
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib> // for std::system
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <queue>
#include <random>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif

// @ai_context(
//   intent="Control compiler diagnostic verbosity",
//   assumptions=["Global state is acceptable for command-line tool"],
//   constraints=["0=quiet, 1=normal, 2=verbose, 3=debug"]
// )
// Global verbosity level is now defined in diagnostics.cpp
// int g_verbosity_level = 1;

#if !defined(NIL_NATIVE_LINKER_DRIVER)
#define NIL_NATIVE_LINKER_DRIVER "clang"
#endif
#if !defined(NY_NATIVE_INSTALL_LIBDIR)
#define NY_NATIVE_INSTALL_LIBDIR "lib"
#endif

static std::optional<std::filesystem::path>
existing_executable_path(const std::filesystem::path &path) {
  std::error_code error;
  const auto status = std::filesystem::status(path, error);
  if (error || !std::filesystem::is_regular_file(status))
    return std::nullopt;
#ifndef _WIN32
  constexpr auto executable_bits = std::filesystem::perms::owner_exec |
                                   std::filesystem::perms::group_exec |
                                   std::filesystem::perms::others_exec;
  if ((status.permissions() & executable_bits) == std::filesystem::perms::none)
    return std::nullopt;
#endif

  auto canonical = std::filesystem::weakly_canonical(path, error);
  if (!error)
    return canonical;

  error.clear();
  auto absolute = std::filesystem::absolute(path, error);
  if (!error)
    return absolute;
  return path;
}

static std::optional<std::filesystem::path>
find_executable_on_path(const std::filesystem::path &name) {
  const char *search_path = std::getenv("PATH");
  if (!search_path || name.empty() || name.has_parent_path())
    return std::nullopt;

#ifdef _WIN32
  constexpr char separator = ';';
#else
  constexpr char separator = ':';
#endif
  std::istringstream entries(search_path);
  std::string entry;
  while (std::getline(entries, entry, separator)) {
    const auto directory = entry.empty() ? std::filesystem::path(".")
                                         : std::filesystem::path(entry);
    auto candidate = directory / name;
    if (auto resolved = existing_executable_path(candidate))
      return resolved;
#ifdef _WIN32
    if (!candidate.has_extension()) {
      candidate += ".exe";
      if (auto resolved = existing_executable_path(candidate))
        return resolved;
    }
#endif
  }
  return std::nullopt;
}

static std::optional<std::filesystem::path>
resolve_executable_path(const char *invocation) {
#if defined(__linux__)
  std::error_code proc_error;
  auto proc_path = std::filesystem::read_symlink("/proc/self/exe", proc_error);
  if (!proc_error && !proc_path.empty())
    return existing_executable_path(proc_path);
#elif defined(__APPLE__)
  uint32_t path_size = 0;
  if (_NSGetExecutablePath(nullptr, &path_size) == -1 && path_size > 0) {
    std::vector<char> path_buffer(path_size);
    if (_NSGetExecutablePath(path_buffer.data(), &path_size) == 0) {
      if (auto resolved = existing_executable_path(path_buffer.data()))
        return resolved;
    }
  }
#endif

  std::filesystem::path invoked(invocation ? invocation : "");
  if (invoked.has_parent_path()) {
    if (auto resolved = existing_executable_path(invoked))
      return resolved;
    return std::nullopt;
  }
  return find_executable_on_path(invoked);
}

static std::vector<std::filesystem::path>
native_runtime_archive_candidates(const char *compiler_invocation) {
#ifdef _WIN32
  const std::vector<const char *> archive_names = {"nil.lib",
                                                   "libnil.a"};
#else
  const std::vector<const char *> archive_names = {"libnil.a",
                                                   "nil.lib"};
#endif
  std::vector<std::filesystem::path> candidates;
  const auto compiler_path = resolve_executable_path(compiler_invocation);
  if (!compiler_path)
    return candidates;

  const auto compiler_directory = compiler_path->parent_path();
  // Packaged SDK tools live at the SDK root. For a bin-based install/build,
  // also accept the prefix's sealed nillib path and CMake's sibling lib output,
  // including the configuration subdirectory used by multi-config builds.
  for (const char *archive_name : archive_names) {
    candidates.push_back(compiler_directory / archive_name);
    candidates.push_back(compiler_directory / "nillib" / "native" /
                         archive_name);
    if (compiler_directory.filename() == "bin") {
      const std::filesystem::path configured_libdir = NY_NATIVE_INSTALL_LIBDIR;
      candidates.push_back(compiler_directory.parent_path() / "nillib" /
                           "native" / archive_name);
      candidates.push_back(configured_libdir.is_absolute()
                               ? configured_libdir / archive_name
                               : compiler_directory.parent_path() /
                                     configured_libdir / archive_name);
      candidates.push_back(compiler_directory.parent_path() / "lib" /
                           archive_name);
    } else if (compiler_directory.parent_path().filename() == "bin") {
      const auto build_root = compiler_directory.parent_path().parent_path();
      candidates.push_back(build_root / "nillib" / "native" / archive_name);
      candidates.push_back(build_root / "lib" / compiler_directory.filename() /
                           archive_name);
    }
  }
  return candidates;
}

static std::optional<std::filesystem::path>
find_native_runtime_archive(const char *compiler_invocation) {
  for (const auto &candidate :
       native_runtime_archive_candidates(compiler_invocation)) {
    std::error_code error;
    if (std::filesystem::is_regular_file(candidate, error) && !error) {
      const auto canonical =
          std::filesystem::weakly_canonical(candidate, error);
      return error ? candidate : canonical;
    }
  }
  return std::nullopt;
}

class remove_directory_on_exit {
public:
  explicit remove_directory_on_exit(std::filesystem::path path)
      : path_(std::move(path)) {}
  remove_directory_on_exit(const remove_directory_on_exit &) = delete;
  remove_directory_on_exit &
  operator=(const remove_directory_on_exit &) = delete;
  ~remove_directory_on_exit() {
    if (!path_.empty()) {
      std::error_code ignored;
      std::filesystem::remove_all(path_, ignored);
    }
  }

private:
  std::filesystem::path path_;
};

static std::optional<std::filesystem::path>
create_private_staging_directory(const std::filesystem::path &output,
                                 const std::string &purpose,
                                 std::error_code &error) {
  auto parent = output.parent_path();
  if (parent.empty())
    parent = ".";

  try {
    std::random_device random_source;
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
      std::ostringstream suffix;
      suffix << '.' << purpose << '-';
      for (unsigned word = 0; word < 4; ++word) {
        suffix << std::hex << std::setfill('0') << std::setw(8)
               << static_cast<uint32_t>(random_source());
      }
      const auto candidate = parent / suffix.str();

#ifdef _WIN32
      if (CreateDirectoryW(candidate.c_str(), nullptr)) {
        error.clear();
        return candidate;
      }
      const auto native_error = GetLastError();
      if (native_error == ERROR_ALREADY_EXISTS ||
          native_error == ERROR_FILE_EXISTS)
        continue;
      error = std::error_code(static_cast<int>(native_error),
                              std::system_category());
      return std::nullopt;
#else
      // mkdir is the reservation boundary: it cannot follow or replace a
      // pre-planted symlink. The requested mode can only be narrowed by umask,
      // so no broader-access window exists; replace the permissions immediately
      // afterward to keep the workspace usable and exactly 0700 even under a
      // restrictive caller umask.
      if (::mkdir(candidate.c_str(), S_IRWXU) == 0) {
        std::error_code permission_error;
        std::filesystem::permissions(
            candidate, std::filesystem::perms::owner_all,
            std::filesystem::perm_options::replace, permission_error);
        std::error_code status_error;
        const auto status =
            std::filesystem::symlink_status(candidate, status_error);
        constexpr auto permission_mask = std::filesystem::perms::all;
        if (permission_error || status_error ||
            !std::filesystem::is_directory(status) ||
            (status.permissions() & permission_mask) !=
                std::filesystem::perms::owner_all) {
          error =
              permission_error
                  ? permission_error
                  : (status_error
                         ? status_error
                         : std::make_error_code(std::errc::permission_denied));
          std::error_code cleanup_error;
          std::filesystem::remove(candidate, cleanup_error);
          return std::nullopt;
        }
        error.clear();
        return candidate;
      }
      if (errno == EEXIST)
        continue;
      error = std::error_code(errno, std::generic_category());
      return std::nullopt;
#endif
    }
  } catch (const std::exception &) {
    error = std::make_error_code(std::errc::io_error);
    return std::nullopt;
  }

  error = std::make_error_code(std::errc::file_exists);
  return std::nullopt;
}

static bool replace_output_file(const std::filesystem::path &staging,
                                const std::filesystem::path &output,
                                std::error_code &error) {
#ifdef _WIN32
  if (MoveFileExW(staging.c_str(), output.c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    return true;
  error =
      std::error_code(static_cast<int>(GetLastError()), std::system_category());
  return false;
#else
  std::filesystem::rename(staging, output, error);
  return !error;
#endif
}

static bool ensure_staged_regular_file_permissions(
    const std::filesystem::path &path,
    std::filesystem::perms required_owner_permissions, std::error_code &error) {
  const auto before = std::filesystem::symlink_status(path, error);
  if (error || !std::filesystem::is_regular_file(before)) {
    if (!error)
      error = std::make_error_code(std::errc::invalid_argument);
    return false;
  }
#ifndef _WIN32
  // File creation is subject to the caller's umask. Add back only the owner
  // permissions required by the next private-staging operation; never broaden
  // group or other access beyond what the creator selected.
  std::filesystem::permissions(path, required_owner_permissions,
                               std::filesystem::perm_options::add, error);
  if (error)
    return false;
  const auto after = std::filesystem::symlink_status(path, error);
  if (error || !std::filesystem::is_regular_file(after) ||
      (after.permissions() & required_owner_permissions) !=
          required_owner_permissions) {
    if (!error)
      error = std::make_error_code(std::errc::permission_denied);
    return false;
  }
#else
  (void)required_owner_permissions;
#endif
  error.clear();
  return true;
}

static void write_u64_little_endian(std::ostream &out, uint64_t value) {
  char bytes[8];
  for (unsigned index = 0; index < 8; ++index)
    bytes[index] = static_cast<char>((value >> (index * 8)) & 0xffu);
  out.write(bytes, sizeof(bytes));
}

#ifndef _WIN32
static std::string quote_posix_shell_argument(const std::string &argument) {
  std::string quoted("'");
  for (const char character : argument) {
    if (character == '\'')
      quoted += "'\\''";
    else
      quoted += character;
  }
  quoted += '\'';
  return quoted;
}
#endif

static void print_usage(std::ostream &out = std::cerr) {
  out << "Usage:\n"
      << "  nilcompiler [options] -o <output> <input1.ny> [<input2.ny> ...]\n"
      << "  nilcompiler [options] <input1.ny> [<input2.ny> ...] out.nil\n"
      << "  nilcompiler --analyze [--source-stdin] [-I <root>] <input.ny|matrix>\n"
      << "              [--emit-reflection <analysis_snapshot.ny>]\n"
      << "  nilcompiler --to-nydata <input.json>\n"
      << "  nilcompiler --to-json <input.nydata>\n"
      << "\nOptions:\n"
      << "  -o, --out, --output <path>     Set the output path\n"
      << "  --bundle[=true]                Bundle NIL with nilinterpreter "
         "(default)\n"
      << "  --no-bundle, --bundle=false    Emit a standalone NIL file\n"
      << "  --emit-binary-nil              Emit binary rather than textual "
         "NIL\n"
      << "  --emit-library                 Emit a self-contained NIL library "
         "(v2, or v4 with classes)\n"
      << "  --native                       Compile through the optional LLVM "
         "backend\n"
      << "  --embed-context                Embed annotated context in native ELF output\n"
      << "  --debug-info                   Retain source paths and lexical locals in NIL\n"
      << "  --inspect-context <artifact>   Print context JSON from NIL or native ELF; no execution\n"
      << "  --jit                          Execute through LLVM JIT (requires "
         "--native)\n"
      << "  -- [args...]                   Pass remaining arguments to a "
         "native JIT program\n"
      << "  --unmanaged                    Use manual memory mode for native "
         "codegen\n"
      << "  -l, --link <path>              Link/load a native library "
         "(repeatable)\n"
      << "  --no-stdlib                    Compile without standard-library "
         "lookup\n"
      << "  -I, --sdk, --sdk-path <path>   Add an SDK/standard-library search "
         "root\n"
      << "  --no-typecheck                 Continue after type-checking "
         "errors\n"
      << "  --to-nydata <path>             Convert JSON to NyData on stdout\n"
      << "  --to-json <path>               Convert NyData to JSON on stdout\n"
      << "  -v, -vv, -vvv                  Increase diagnostic verbosity\n"
      << "  -q, --quiet                    Show errors only\n"
      << "  --version                      Show the compiler version\n"
      << "  -h, --help                     Show this help\n"
      << "\nBy default, a non-native build creates a bundled executable; an "
         "output\n"
      << "ending in .nil loses that extension. Use --no-bundle to retain "
         "NIL.\n";
}

// Find Visual Studio link.exe and library paths
static std::pair<std::string, std::string> find_vs_linker() {
  // Common Visual Studio installation paths
  std::vector<std::string> search_paths = {
      "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC",
      "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC",
      "C:/Program Files/Microsoft Visual "
      "Studio/2022/Professional/VC/Tools/MSVC",
      "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Tools/MSVC",
      "C:/Program Files (x86)/Microsoft Visual "
      "Studio/2019/Community/VC/Tools/MSVC",
      "C:/Program Files (x86)/Microsoft Visual "
      "Studio/2019/Professional/VC/Tools/MSVC",
      "C:/Program Files (x86)/Microsoft Visual "
      "Studio/2019/Enterprise/VC/Tools/MSVC",
  };

  for (const auto &base_path : search_paths) {
    try {
      if (std::filesystem::exists(base_path)) {
        // Find the highest version number subdirectory
        std::string best_version;
        std::string best_linker_path;
        std::string best_lib_path;
        for (const auto &entry :
             std::filesystem::directory_iterator(base_path)) {
          if (entry.is_directory()) {
            std::string version = entry.path().filename().string();
            std::string linker_candidate =
                entry.path().string() + "/bin/Hostx64/x64/link.exe";
            std::string lib_candidate = entry.path().string() + "/lib/x64";
            if (std::filesystem::exists(linker_candidate) &&
                std::filesystem::exists(lib_candidate)) {
              if (best_version.empty() || version > best_version) {
                best_version = version;
                best_linker_path = linker_candidate;
                best_lib_path = lib_candidate;
              }
            }
          }
        }
        if (!best_linker_path.empty()) {
          return {best_linker_path, best_lib_path};
        }
      }
    } catch (...) {
      continue;
    }
  }
  return {"", ""};
}

// Find Windows SDK library paths (um and ucrt)
static std::pair<std::string, std::string> find_windows_sdk_lib() {
  std::vector<std::string> sdk_paths = {
      "C:/Program Files (x86)/Windows Kits/10/Lib",
  };

  for (const auto &sdk_base : sdk_paths) {
    try {
      if (std::filesystem::exists(sdk_base)) {
        // Find the highest version
        std::string best_version;
        std::string best_um_path;
        std::string best_ucrt_path;
        for (const auto &entry :
             std::filesystem::directory_iterator(sdk_base)) {
          if (entry.is_directory()) {
            std::string version = entry.path().filename().string();
            std::string um_path = entry.path().string() + "/um/x64";
            std::string ucrt_path = entry.path().string() + "/ucrt/x64";
            if (std::filesystem::exists(um_path) &&
                std::filesystem::exists(ucrt_path)) {
              if (best_version.empty() || version > best_version) {
                best_version = version;
                best_um_path = um_path;
                best_ucrt_path = ucrt_path;
              }
            }
          }
        }
        if (!best_um_path.empty()) {
          return {best_um_path, best_ucrt_path};
        }
      }
    } catch (...) {
      continue;
    }
  }
  return {"", ""};
}

static std::string find_module_name(const std::shared_ptr<ny::ast_program> &p,
                                    const std::string &fallback_path) {
  // Search AST for a module declaration
  for (const auto &d : p->declarations) {
    if (!d)
      continue;
    if (d->kind == ny::ast_node_kind::module_decl_node) {
      auto mod = std::dynamic_pointer_cast<ny::ast_module_decl>(d);
      if (mod && !mod->name.empty()) {
        return mod->name;
      }
    }
  }
  // Derive a fallback module name from the filename
  try {
    std::filesystem::path fp(fallback_path);
    return fp.stem().string();
  } catch (...) {
    // As a last resort, return the whole fallback path
    return fallback_path;
  }
}

static std::vector<std::string>
collect_import_paths(const std::shared_ptr<ny::ast_program> &p) {
  std::vector<std::string> out;
  for (const auto &d : p->declarations) {
    if (!d)
      continue;
    if (d->kind == ny::ast_node_kind::import_decl_node) {
      auto id = std::dynamic_pointer_cast<ny::ast_import_decl>(d);
      if (id && !id->module_path.empty())
        out.push_back(id->module_path);
    }
  }
  return out;
}

// Given a candidate import string like 'std::io::println', attempt to find the
// best matching module name in `modules`. Checks the full candidate and
// progressively strips trailing '::name' pieces until none remain.
// Short import aliases for common modules used by Gehenna
static const std::unordered_map<std::string, std::string> import_aliases = {
    {"console", "io::console"},
    {"mimic", "mimic"},
    {"engine_vulkan", "engine_vulkan"},
    {"engine_window", "engine_window"},
    {"engine_native", "engine_native"},
    {"math", "math"},
    {"voxel", "voxel"},
    {"agent", "agent"},
};

static std::optional<std::string>
resolve_import_to_known_module(const std::string &importpath,
                               const std::unordered_set<std::string> &modules) {

  // Check if this is a short alias first
  std::string resolved_path = importpath;
  auto alias_it = import_aliases.find(importpath);
  if (alias_it != import_aliases.end()) {
    resolved_path = alias_it->second;
  }

  // First: exact match
  if (modules.count(resolved_path))
    return resolved_path;

  // NOTE: Do NOT fall back to parent module names!
  // "aether::db" is NOT the same as "aether"
  // This was causing false dependency cycles
  return std::nullopt;
}

static std::string json_escape(const std::string &s) {
  std::string r;
  for (char c : s) {
    if (c == '"')
      r += "\\\"";
    else if (c == '\\')
      r += "\\\\";
    else if (c == '\n')
      r += "\\n";
    else if (c == '\t')
      r += "\\t";
    else if (c == '\r')
      r += "\\r";
    else
      r += c;
  }
  return r;
}

struct json_val {
  enum class T { null_t, bool_t, num_t, str_t, arr_t, obj_t };
  T t = T::null_t;
  bool b = false;
  double n = 0;
  std::string s;
  std::shared_ptr<std::vector<json_val>> a;
  std::shared_ptr<std::vector<std::pair<std::string, json_val>>> o;
};

// A page whose name ends in a Matrix extension is Matrix source. The
// extension is the only selector: both front ends emit the same AST, so
// nothing downstream needs to know which surface produced it.
//
// '.mat' is accepted alongside '.matrix' as the short form. Getting this
// wrong is quietly confusing rather than loudly wrong -- an unrecognised
// extension sends the page to the Ny parser, which reports
// "Undefined identifier 'book'" and says nothing about the real problem.
// Colour is only emitted when stderr is a terminal, so redirected output and
// test fixtures stay plain.
static bool ny_stderr_is_terminal() {
#if defined(_WIN32)
  return false;
#else
  return isatty(fileno(stderr)) != 0;
#endif
}

static bool is_matrix_source(const std::string &path) {
  const auto dot = path.find_last_of('.');
  if (dot == std::string::npos)
    return false;
  std::string extension = path.substr(dot);
  // Match editor source routing on every host, including case-sensitive
  // filesystems. Only the suffix is folded; the actual path stays unchanged.
  for (char &ch : extension)
    if (ch >= 'A' && ch <= 'Z')
      ch = static_cast<char>(ch - 'A' + 'a');
  return extension == ".matrix" || extension == ".mat";
}

struct json_parser {
  const std::string &src;
  size_t pos = 0;
  void skip_ws() {
    while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t' ||
                                src[pos] == '\n' || src[pos] == '\r'))
      ++pos;
  }
  std::string parse_str() {
    ++pos;
    std::string r;
    while (pos < src.size() && src[pos] != '"') {
      if (src[pos] == '\\') {
        ++pos;
        if (pos < src.size()) {
          switch (src[pos]) {
          case '"':
            r += '"';
            break;
          case '\\':
            r += '\\';
            break;
          case '/':
            r += '/';
            break;
          case 'n':
            r += '\n';
            break;
          case 't':
            r += '\t';
            break;
          case 'r':
            r += '\r';
            break;
          default:
            r += src[pos];
            break;
          }
        }
      } else {
        r += src[pos];
      }
      ++pos;
    }
    ++pos;
    return r;
  }
  json_val parse_val() {
    skip_ws();
    if (pos >= src.size())
      return {};
    char c = src[pos];
    if (c == '"') {
      json_val v;
      v.t = json_val::T::str_t;
      v.s = parse_str();
      return v;
    }
    if (c == '{') {
      ++pos;
      json_val v;
      v.t = json_val::T::obj_t;
      v.o = std::make_shared<std::vector<std::pair<std::string, json_val>>>();
      skip_ws();
      while (pos < src.size() && src[pos] != '}') {
        skip_ws();
        std::string key = parse_str();
        skip_ws();
        ++pos;
        v.o->push_back({key, parse_val()});
        skip_ws();
        if (pos < src.size() && src[pos] == ',')
          ++pos;
        skip_ws();
      }
      ++pos;
      return v;
    }
    if (c == '[') {
      ++pos;
      json_val v;
      v.t = json_val::T::arr_t;
      v.a = std::make_shared<std::vector<json_val>>();
      skip_ws();
      while (pos < src.size() && src[pos] != ']') {
        v.a->push_back(parse_val());
        skip_ws();
        if (pos < src.size() && src[pos] == ',')
          ++pos;
        skip_ws();
      }
      ++pos;
      return v;
    }
    if (src.size() - pos >= 4 && src.substr(pos, 4) == "null") {
      pos += 4;
      return {};
    }
    if (src.size() - pos >= 4 && src.substr(pos, 4) == "true") {
      pos += 4;
      json_val v;
      v.t = json_val::T::bool_t;
      v.b = true;
      return v;
    }
    if (src.size() - pos >= 5 && src.substr(pos, 5) == "false") {
      pos += 5;
      json_val v;
      v.t = json_val::T::bool_t;
      v.b = false;
      return v;
    }
    json_val v;
    v.t = json_val::T::num_t;
    size_t start = pos;
    if (src[pos] == '-')
      ++pos;
    while (pos < src.size() &&
           (std::isdigit((unsigned char)src[pos]) || src[pos] == '.' ||
            src[pos] == 'e' || src[pos] == 'E' || src[pos] == '+' ||
            src[pos] == '-'))
      ++pos;
    v.n = std::stod(src.substr(start, pos - start));
    return v;
  }
};

static void emit_nydata(std::ostream &os, const std::string &name,
                        const json_val &v, int depth = 0) {
  std::string ind(depth * 2, ' ');
  switch (v.t) {
  case json_val::T::obj_t:
    os << ind << "object " << name << "\n";
    for (const auto &[k, child] : *v.o)
      emit_nydata(os, k, child, depth + 1);
    os << ind << "end " << name << "\n";
    break;
  case json_val::T::arr_t:
    os << ind << "object " << name << "\n";
    for (size_t i = 0; i < v.a->size(); ++i)
      emit_nydata(os, std::to_string(i), (*v.a)[i], depth + 1);
    os << ind << "end " << name << "\n";
    break;
  case json_val::T::null_t:
    os << ind << "field " << name << "\n  null\n"
       << ind << "end " << name << "\n";
    break;
  case json_val::T::bool_t:
    os << ind << "field " << name << "\n  " << (v.b ? "true" : "false") << "\n"
       << ind << "end " << name << "\n";
    break;
  case json_val::T::num_t: {
    os << ind << "field " << name << "\n  ";
    if (v.n == static_cast<long long>(v.n))
      os << static_cast<long long>(v.n);
    else
      os << v.n;
    os << "\n" << ind << "end " << name << "\n";
    break;
  }
  case json_val::T::str_t:
    os << ind << "field " << name << "\n  " << v.s << "\n"
       << ind << "end " << name << "\n";
    break;
  }
}

static int cmd_to_nydata(const std::string &path) {
  std::ifstream f(path);
  if (!f.is_open()) {
    std::cerr << "Cannot open: " << path << "\n";
    return 1;
  }
  std::string src((std::istreambuf_iterator<char>(f)), {});
  json_parser p{src};
  json_val root = p.parse_val();
  if (root.t == json_val::T::obj_t) {
    for (const auto &[k, v] : *root.o)
      emit_nydata(std::cout, k, v, 0);
  } else {
    emit_nydata(std::cout, "root", root, 0);
  }
  return 0;
}

struct nydata_node {
  std::string name;
  bool is_field = false;
  std::string value;
  std::vector<nydata_node> children;
};

static std::string nydata_trim(const std::string &s) {
  const char *ws = " \t\r\n";
  size_t b = s.find_first_not_of(ws);
  if (b == std::string::npos)
    return "";
  return s.substr(b, s.find_last_not_of(ws) - b + 1);
}

static void parse_nydata_children(const std::vector<std::string> &lines,
                                  size_t &i, std::vector<nydata_node> &out) {
  while (i < lines.size()) {
    std::string ln = nydata_trim(lines[i]);
    if (ln.empty()) {
      ++i;
      continue;
    }
    if (ln.rfind("end ", 0) == 0) {
      ++i;
      return;
    }
    if (ln.rfind("object ", 0) == 0) {
      nydata_node node;
      node.name = ln.substr(7);
      node.is_field = false;
      ++i;
      parse_nydata_children(lines, i, node.children);
      out.push_back(std::move(node));
    } else if (ln.rfind("field ", 0) == 0) {
      nydata_node node;
      node.name = ln.substr(6);
      node.is_field = true;
      ++i;
      while (i < lines.size()) {
        std::string vl = nydata_trim(lines[i]);
        if (vl.empty()) {
          ++i;
          continue;
        }
        if (vl.rfind("end ", 0) == 0) {
          ++i;
          break;
        }
        node.value = vl;
        ++i;
      }
      out.push_back(std::move(node));
    } else {
      ++i;
    }
  }
}

static void emit_json_node(std::ostream &os, const nydata_node &node,
                           int depth) {
  std::string ind2((depth + 1) * 2, ' ');
  std::string ind(depth * 2, ' ');
  if (node.is_field) {
    const std::string &v = node.value;
    if (v == "true" || v == "false") {
      os << v;
      return;
    }
    if (v == "null") {
      os << "null";
      return;
    }
    bool is_num = !v.empty();
    size_t start_c = (v[0] == '-') ? 1 : 0;
    bool has_dot = false;
    for (size_t ci = start_c; ci < v.size() && is_num; ++ci) {
      char c = v[ci];
      if (c == '.' && !has_dot)
        has_dot = true;
      else if (!std::isdigit((unsigned char)c))
        is_num = false;
    }
    if (is_num && v.size() > start_c) {
      os << v;
      return;
    }
    os << '"' << json_escape(v) << '"';
  } else {
    bool is_array = !node.children.empty();
    for (size_t ci = 0; ci < node.children.size() && is_array; ++ci)
      if (node.children[ci].name != std::to_string(ci))
        is_array = false;
    if (is_array) {
      os << "[\n";
      for (size_t ci = 0; ci < node.children.size(); ++ci) {
        os << ind2;
        emit_json_node(os, node.children[ci], depth + 1);
        if (ci + 1 < node.children.size())
          os << ",";
        os << "\n";
      }
      os << ind << "]";
    } else {
      os << "{\n";
      for (size_t ci = 0; ci < node.children.size(); ++ci) {
        os << ind2 << '"' << json_escape(node.children[ci].name) << "\": ";
        emit_json_node(os, node.children[ci], depth + 1);
        if (ci + 1 < node.children.size())
          os << ",";
        os << "\n";
      }
      os << ind << "}";
    }
  }
}

static int cmd_to_json(const std::string &path) {
  std::ifstream f(path);
  if (!f.is_open()) {
    std::cerr << "Cannot open: " << path << "\n";
    return 1;
  }
  std::string src((std::istreambuf_iterator<char>(f)), {});
  std::vector<std::string> lines;
  std::string line;
  for (char c : src) {
    if (c == '\n') {
      lines.push_back(line);
      line.clear();
    } else
      line += c;
  }
  if (!line.empty())
    lines.push_back(line);
  size_t i = 0;
  std::vector<nydata_node> roots;
  parse_nydata_children(lines, i, roots);
  if (roots.size() == 1) {
    emit_json_node(std::cout, roots[0], 0);
  } else {
    std::cout << "{\n";
    for (size_t ri = 0; ri < roots.size(); ++ri) {
      std::cout << "  \"" << json_escape(roots[ri].name) << "\": ";
      emit_json_node(std::cout, roots[ri], 1);
      if (ri + 1 < roots.size())
        std::cout << ",";
      std::cout << "\n";
    }
    std::cout << "}";
  }
  std::cout << "\n";
  return 0;
}

static int compile_main(int argc, char *argv[], ny::analysis_session *analysis) {
  // Inspection is a separate, read-only command. Do not partially process a
  // compilation or accept extra output/execute options around its input.
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--") break;
    if (std::string(argv[i]) != "--inspect-context") continue;
    if (argc != 3 || i != 1) {
      std::cerr << "Error: use nilcompiler --inspect-context <artifact> alone\n";
      return 1;
    }
    try {
      const auto input = ny::detail::read_regular_file_bounded(
          argv[2], 512ULL * 1024ULL * 1024ULL);
      if (!input.ok) {
        std::cerr << "Error: cannot inspect context: " << input.error << '\n';
        return 1;
      }
      const auto context = ny::inspect_context_artifact(input.bytes);
      std::cout << context;
      return 0;
    } catch (const std::exception &error) {
      std::cerr << "Error: cannot inspect context: " << error.what() << '\n';
      return 1;
    }
  }
  // CLI arg parsing
  std::vector<std::string> input_paths;
  std::string output_path;
  std::vector<std::string> sdk_include_paths; // User-specified SDK paths
  bool emit_binary_nil = false;
  bool emit_library = false;
  bool use_native = false;
  bool use_jit = false;
  bool no_stdlib = false;
  bool skip_typecheck = false;
  bool unmanaged = false;
  bool embed_context = false;
  bool source_debug = false;
  bool bundle_output = true;
  bool has_program_argument_boundary = false;
  std::vector<std::string> program_args;
  std::vector<std::string> link_libraries;
  g_verbosity_level = 1; // Default: normal (errors + warnings + success)

  for (int i = 1; i < argc; ++i) {
    std::string a(argv[i]);
    if (a == "--") {
      has_program_argument_boundary = true;
      while (++i < argc)
        program_args.emplace_back(argv[i]);
      break;
    } else if (a == "-h" || a == "--help") {
      print_usage(std::cout);
      return 0;
    } else if (a == "--version") {
      std::cout << "nilcompiler " << NIL_VERSION << "\n";
      return 0;
    } else if (a == "--to-nydata" || a == "--to-json") {
      if (i + 1 >= argc) {
        std::cerr << "Missing input path after " << a << "\n";
        return 1;
      }
      return a == "--to-nydata" ? cmd_to_nydata(argv[++i])
                                : cmd_to_json(argv[++i]);
    } else if (a == "-o" || a == "--out" || a == "--output") {
      if (i + 1 >= argc) {
        std::cerr << "Missing output path after " << a << "\n";
        print_usage();
        return 1;
      }
      output_path = argv[++i];
    } else if (a == "-I" || a == "--sdk" || a == "--sdk-path") {
      if (i + 1 >= argc) {
        std::cerr << "Missing SDK path after " << a << "\n";
        print_usage();
        return 1;
      }
      sdk_include_paths.push_back(argv[++i]);
    } else if (a == "--emit-binary-nil" || a == "--emit-binary-mil") {
      // --emit-binary-mil is retained as a compatibility spelling.
      emit_binary_nil = true;
    } else if (a == "--emit-library") {
      emit_library = true;
    } else if (a == "--native") {
      use_native = true;
    } else if (a == "--debug-info") {
      source_debug = true;
    } else if (a == "--embed-context") {
      if (embed_context) {
        std::cerr << "Error: --embed-context was provided more than once\n";
        return 1;
      }
      embed_context = true;
    } else if (a == "--jit") {
      use_jit = true;
    } else if (a == "--no-bundle" || a == "--bundle=false") {
      bundle_output = false; // Produce a separate .nil file
    } else if (a == "--bundle" || a == "--bundle=true") {
      bundle_output = true; // Explicit enable (already default)
    } else if (a == "-l" || a == "--link") {
      if (i + 1 >= argc) {
        std::cerr << "Missing library path after " << a << "\n";
        print_usage();
        return 1;
      }
      link_libraries.push_back(argv[++i]);
    } else if (a == "--no-stdlib") {
      no_stdlib = true;
    } else if (a == "--no-typecheck") {
      skip_typecheck = true;
    } else if (a == "--unmanaged") {
      unmanaged = true;
    } else if (a == "-v") {
      g_verbosity_level = 2; // Verbose
    } else if (a == "-vv") {
      g_verbosity_level = 3; // Debug
    } else if (a == "-vvv") {
      g_verbosity_level = 3; // Max debug (same as -vv)
    } else if (a == "-q" || a == "--quiet") {
      g_verbosity_level = 0; // Silent (errors only)
    } else if (!a.empty() && a.front() == '-') {
      std::cerr << "Error: unknown option: " << a << "\n";
      return 1;
    } else {
      input_paths.emplace_back(a);
    }
  }

  // ... (existing output path logic)
  if (output_path.empty()) {
    if (!input_paths.empty()) {
      const std::string &last = input_paths.back();
      if (last.size() >= 4 && last.substr(last.size() - 4) == ".nil") {
        output_path = last;
        input_paths.pop_back();
      }
    }
  }

  if (input_paths.empty() || output_path.empty()) {
    std::cerr
        << "Error: Must provide at least one input file and an output file\n";
    print_usage();
    return 1;
  }

  if (use_jit && !use_native) {
    std::cerr << "Error: --jit requires --native\n";
    return 1;
  }
  if (embed_context && (!use_native || use_jit)) {
    std::cerr << "Error: --embed-context requires native file output, not NIL, bundles or JIT\n";
    return 1;
  }
  if (source_debug && (use_native || emit_library)) {
    std::cerr << "Error: --debug-info currently requires executable NIL output; native DWARF and library debug capsules are unavailable\n";
    return 1;
  }
  if (has_program_argument_boundary && (!use_native || !use_jit)) {
    std::cerr << "Error: program arguments after -- require --native --jit\n";
    return 1;
  }
  if (emit_library && (!emit_binary_nil || bundle_output || use_native)) {
    std::cerr << "Error: --emit-library requires --emit-binary-nil and "
                 "--no-bundle, and cannot be combined with --native\n";
    return 1;
  }

  // ... (existing parsing logic)

  // Discover the canonical standard-library root for explicit imports. The
  // current library does not yet have a stable implicit prelude, so no source
  // files are injected into every compilation.
  std::filesystem::path sdk_stdlib;
  std::filesystem::path sdk_nillib;
  if (!no_stdlib) {
    std::vector<std::filesystem::path> candidates;

    const char *configured_stdlib = std::getenv("NIL_STDLIB_PATH");
    if (!configured_stdlib || *configured_stdlib == '\0')
      configured_stdlib = std::getenv("NY_STDLIB_PATH"); // Legacy environment.
    if (configured_stdlib && *configured_stdlib != '\0')
      candidates.emplace_back(configured_stdlib);

    // A user may pass the library source root directly or an SDK/repository
    // root containing it.
    for (const auto &p : sdk_include_paths) {
      candidates.push_back(p);
      candidates.push_back(std::filesystem::path(p) / "nilstd");
      candidates.push_back(std::filesystem::path(p) / "share/nil/stdlib");
      candidates.push_back(std::filesystem::path(p) / "share/ny/stdlib");
      candidates.push_back(std::filesystem::path(p) / "nilstd/src");
    }

    const auto compiler_path = resolve_executable_path(argv[0]);
    if (compiler_path) {
      const auto exe_path = compiler_path->parent_path();
      // Canonical SDK layout: <sdk>/nilcompiler beside <sdk>/nilstd.
      candidates.push_back(exe_path / "nilstd");
      // Compatibility with conventional CMake installs that keep tools in
      // <prefix>/bin and with SDKs created before the root-tool layout.
      candidates.push_back(exe_path.parent_path() / "nilstd");
      candidates.push_back(exe_path.parent_path() / "share/nil/stdlib");
      candidates.push_back(exe_path.parent_path() / "share/ny/stdlib");
      // Development layout when invoked from a component build directory.
      candidates.push_back(exe_path.parent_path() / "nilstd/src");
    }
    candidates.push_back(std::filesystem::current_path() / "nilstd/src");

    for (const auto &cand : candidates) {
      std::error_code candidate_ec;
      if (std::filesystem::is_directory(cand, candidate_ec) &&
          (std::filesystem::exists(cand / "ny", candidate_ec) ||
           std::filesystem::exists(cand / "prelude.ny", candidate_ec))) {
        sdk_stdlib = std::filesystem::weakly_canonical(cand, candidate_ec);
        if (candidate_ec)
          sdk_stdlib = cand;
        break;
      }
    }

    std::vector<std::filesystem::path> library_candidates;
    const char *configured_library = std::getenv("NIL_LIBRARY_PATH");
    if (!configured_library || *configured_library == '\0')
      configured_library = std::getenv("NY_LIBRARY_PATH"); // Legacy environment.
    if (configured_library && *configured_library != '\0')
      library_candidates.emplace_back(configured_library);
    for (const auto &p : sdk_include_paths) {
      library_candidates.emplace_back(p);
      library_candidates.push_back(std::filesystem::path(p) / "nillib");
    }
    if (const auto compiler_path = resolve_executable_path(argv[0])) {
      const auto executable_directory = compiler_path->parent_path();
      library_candidates.push_back(executable_directory / "nillib");
      library_candidates.push_back(executable_directory.parent_path() /
                                   "nillib");
    }
    for (const auto &candidate : library_candidates) {
      std::error_code candidate_error;
      if (std::filesystem::is_directory(candidate, candidate_error) &&
          (candidate.filename() == "nillib" ||
           std::filesystem::exists(candidate / "ny", candidate_error) ||
           std::filesystem::exists(candidate / "prelude.nil",
                                   candidate_error))) {
        sdk_nillib =
            std::filesystem::weakly_canonical(candidate, candidate_error);
        if (candidate_error)
          sdk_nillib = candidate;
        break;
      }
    }
  }

  // Parse all files: store their AST and metadata
  struct unit {
    std::string path;
    std::string source;
    std::shared_ptr<ny::ast_program> program;
    std::string module_name;
    std::vector<std::string> imports;
    bool primary = false;
  };

  std::vector<unit> units;
  std::unordered_set<std::string> parsed_paths;
  std::vector<std::string> worklist = input_paths;
  std::unordered_set<std::string> primary_paths;
  for (const auto &input : input_paths) {
    std::error_code primary_error;
    auto canonical = std::filesystem::canonical(input, primary_error);
    primary_paths.insert(primary_error ? input : canonical.string());
  }
  std::unordered_map<std::string, std::string> embedded_sources;
  std::unordered_set<std::string> loaded_libraries;
  std::unordered_set<std::string> resolved_import_paths;
  bool import_resolution_failed = false;

  auto resolve_to_path =
      [&](const std::string &import_path) -> std::optional<std::string> {
    // Standard modules have neutral family directories; legacy namespace is an alias.
    std::string rel_slash = ny::standard_module_relative_path(import_path);
    for (size_t i = 0; i < rel_slash.length();) {
      if (rel_slash[i] == ':' && i + 1 < rel_slash.length() &&
          rel_slash[i + 1] == ':') {
        rel_slash[i] = '/';
        rel_slash.erase(i + 1, 1);
      } else if (rel_slash[i] == '.') {
        rel_slash[i] = '/';
        ++i;
      } else {
        ++i;
      }
    }

    // Dot-joined form is retained for older dot-directory source layouts.
    std::string rel_dot = import_path;
    for (size_t i = 0; i < rel_dot.length();) {
      if (rel_dot[i] == ':' && i + 1 < rel_dot.length() &&
          rel_dot[i + 1] == ':') {
        rel_dot[i] = '.';
        rel_dot.erase(i + 1, 1);
      } else {
        ++i;
      }
    }

    // A Matrix book IS a NIL module, so a book resolves from the include path
    // exactly as an Ny module does. Without the Matrix extensions here, an Ny
    // "import geo.point" resolved from -I while the identical Matrix "read
    // everything from geo.point" did not, and every page had to be named on
    // the command line. '.ny' stays first so an existing layout resolves
    // exactly as it did before.
    static const char *const source_extensions[] = {".ny", ".matrix", ".mat"};
    // Matrix joins words in a name with '-', and a book path carries that
    // spelling: 'oculus.window.geometry.rectangle-ext' is a NIL module named
    // '...::rectangle_ext', because NIL identifiers admit no hyphen. The file
    // on disk keeps the spelling the author wrote, so a lookup built from the
    // module name must try the hyphenated form too or it searches for a file
    // nobody would name.
    const auto hyphenated = [](std::string text) {
      std::replace(text.begin(), text.end(), '_', '-');
      return text;
    };
    std::vector<std::string> candidates;
    const auto add_forms = [&](const std::string &stem) {
      for (const char *extension : source_extensions) {
        candidates.push_back(stem + extension);
        candidates.push_back(stem + "/package" + extension);
        candidates.push_back(stem + "/main" + extension);
      }
    };
    // Slash-form candidates (classic layout: nyhttp/server.ny)
    add_forms(rel_slash);
    const auto canonical_import = ny::canonical_std_namespace(import_path);
    if (canonical_import.rfind("nil::std::", 0) == 0 || canonical_import.rfind("nil.std.", 0) == 0) {
      add_forms("ny/lang/" + rel_slash);
      add_forms("nil/std/" + rel_slash);
    }
    if (hyphenated(rel_slash) != rel_slash)
      add_forms(hyphenated(rel_slash));
    // Dot-form candidates (new layout: ny.lang.core/package.ny)
    if (rel_dot != rel_slash) {
      add_forms(rel_dot);
      if (hyphenated(rel_dot) != rel_dot)
        add_forms(hyphenated(rel_dot));
    }
    std::vector<std::filesystem::path> search_dirs = {
        std::filesystem::current_path()};
    for (const auto &p : sdk_include_paths) {
      search_dirs.push_back(p);
    }
    if (!sdk_stdlib.empty()) {
      search_dirs.push_back(sdk_stdlib);
    }

    for (const auto &dir : search_dirs) {
      for (const auto &cand : candidates) {
        std::filesystem::path p = dir / cand;
        std::error_code status_error;
        if (std::filesystem::is_regular_file(p, status_error)) {
          std::error_code ec;
          auto canonical = std::filesystem::canonical(p, ec);
          resolved_import_paths.insert(import_path);
          return ec ? p.string() : canonical.string();
        }
      }
    }

    std::vector<std::string> binary_candidates;
    binary_candidates.push_back(rel_slash + ".nil");
    if (canonical_import.rfind("nil::std::", 0) == 0 || canonical_import.rfind("nil.std.", 0) == 0) {
      binary_candidates.push_back("ny/lang/" + rel_slash + ".nil");
      binary_candidates.push_back("ny/lang/" + rel_slash + "/package.nil");
    }
    binary_candidates.push_back(rel_slash + "/package.nil");
    binary_candidates.push_back(rel_slash + "/main.nil");
    if (rel_dot != rel_slash) {
      binary_candidates.push_back(rel_dot + ".nil");
      binary_candidates.push_back(rel_dot + "/package.nil");
      binary_candidates.push_back(rel_dot + "/main.nil");
    }

    std::vector<std::filesystem::path> binary_search_dirs;
    for (const auto &path : sdk_include_paths) {
      binary_search_dirs.emplace_back(path);
      binary_search_dirs.push_back(std::filesystem::path(path) / "nillib");
    }
    if (!sdk_nillib.empty())
      binary_search_dirs.push_back(sdk_nillib);

    for (const auto &directory : binary_search_dirs) {
      for (const auto &candidate : binary_candidates) {
        const std::filesystem::path path = directory / candidate;
        std::error_code status_error;
        if (!std::filesystem::is_regular_file(path, status_error))
          continue;

        std::error_code canonical_error;
        const auto canonical =
            std::filesystem::canonical(path, canonical_error);
        const std::string library_key =
            canonical_error ? path.string() : canonical.string();
        resolved_import_paths.insert(import_path);
        if (!loaded_libraries.insert(library_key).second)
          return std::nullopt;

        std::ifstream library(path, std::ios::binary);
        if (!library) {
          std::cerr << "Error: cannot open NIL library: " << path << "\n";
          import_resolution_failed = true;
          return std::nullopt;
        }
        try {
          const auto metadata = ny::parseBinaryLibraryMetadata(library);
          for (const auto &source : metadata.sources) {
            const std::string source_key =
                "nillib://" + ny::canonical_std_namespace(metadata.module_name) + "/" + source.path;
            auto inserted =
                embedded_sources.emplace(source_key, source.contents);
            if (!inserted.second && inserted.first->second != source.contents) {
              std::cerr << "Error: conflicting embedded source '" << source.path
                        << "' in NIL library " << path << "\n";
              import_resolution_failed = true;
              return std::nullopt;
            }
            if (inserted.second && !parsed_paths.count(source_key))
              worklist.push_back(source_key);
          }
        } catch (const std::exception &error) {
          std::cerr << "Error: invalid NIL library " << path << ": "
                    << error.what() << "\n";
          import_resolution_failed = true;
        }
        return std::nullopt;
      }
    }
    return std::nullopt;
  };

  while (!worklist.empty()) {
    std::string raw_p = worklist.back();
    worklist.pop_back();

    const auto embedded = embedded_sources.find(raw_p);
    std::error_code ec;
    std::string p = raw_p;
    if (embedded == embedded_sources.end()) {
      const auto canonical = std::filesystem::canonical(raw_p, ec);
      if (!ec)
        p = canonical.string();
    }

    if (parsed_paths.count(p)) {
      continue;
    }
    parsed_paths.insert(p);

    std::string src;
    if (analysis && analysis->source_stdin && p == analysis->primary_path) {
      src = analysis->stdin_source;
    } else if (embedded != embedded_sources.end()) {
      src = embedded->second;
    } else {
      std::error_code input_error;
      const auto input_status = std::filesystem::status(p, input_error);
      if (input_error || !std::filesystem::exists(input_status)) {
        std::cerr << "Error: cannot open input file: " << p << "\n";
        return 1;
      }
      if (!std::filesystem::is_regular_file(input_status)) {
        std::cerr << "Error: input is not a regular file: " << p << "\n";
        return 1;
      }
      if (analysis && std::filesystem::file_size(p) > 2 * 1024 * 1024)
        throw std::runtime_error("analysis source exceeds 2 MiB: " + p);
      std::ifstream ifs(p, std::ios::binary);
      if (!ifs) {
        std::cerr << "Error: cannot open input file: " << p << "\n";
        return 1;
      }
      try {
        src.assign(std::istreambuf_iterator<char>(ifs),
                   std::istreambuf_iterator<char>());
      } catch (const std::ios_base::failure &) {
        std::cerr << "Error: cannot read input file: " << p << "\n";
        return 1;
      }
      if (ifs.bad()) {
        std::cerr << "Error: cannot read input file: " << p << "\n";
        return 1;
      }
    }

    if (analysis) {
      ny::validate_analysis_utf8(src);
      if (units.size() >= 256) throw std::runtime_error("analysis exceeds 256 source units");
      analysis->sources[p] = src;
    }

    // Embedded library identities are diagnostic keys, not native source
    // paths. Source-aware debugging may use a separately available nilstd file,
    // but cannot advertise a synthetic URI as an editor-openable Source.path.
    const std::string diagnostic_path =
        embedded == embedded_sources.end() ? p : std::string();

    // Two surfaces, one semantic core: a '.matrix' page goes through the
    // Matrix front end, everything else through Ny's. Both produce the same
    // ast.hpp program, so the type checker, codegen and every contract
    // checker below run unchanged.
    std::shared_ptr<ny::ast_program> prog;
    bool parse_failed = false;
    if (analysis && p == analysis->primary_path ? analysis->language == "matrix" : is_matrix_source(p)) {
      matrix::parser_options matrix_options;
      matrix_options.filename = diagnostic_path;
      matrix::parser matrix_parser(src, matrix_options);
      prog = matrix_parser.parse_program();
      if (matrix_parser.has_errors()) {
        // Render through matrix_diagnostics: a span with a caret, the source
        // line, the surface's own type vocabulary, and a hint. See
        // matrix/docs/diagnostics-and-analyzers.md for why this record is
        // also the analyzer surface.
        const bool colour = ny_stderr_is_terminal();
        for (const auto &d : matrix_parser.rich_diagnostics()) {
          if (analysis) analysis->add(d.code.empty() ? "MX0001" : d.code, "error",
              matrix::to_surface_vocabulary(d.message), p, d.primary.line,
              d.primary.column, d.primary.length, d.hint);
          std::cerr << matrix::render_diagnostic(d, src, p, colour);
        }
        parse_failed = true;
      }
    } else {
      ny::parser_options parse_options;
      parse_options.filename = diagnostic_path;
      ny::parser parser(src, parse_options);
      prog = parser.parse_program();
      if (parser.has_errors()) {
        std::cerr << "Parse errors in " << p << ":\n";
        for (const auto &d : parser.diagnostics()) {
          if (analysis) analysis->add("NIL0001", "error", d.message, p, d.line, 0, 0, d.hint.value_or(""));
          std::cerr << "  " << p << ":" << d.line << ": " << d.message << "\n";
        }
        parse_failed = true;
      }
    }
    if (parse_failed)
      return 1;

    for (auto &declaration : prog->declarations)
      if (declaration && declaration->source_file.empty()) declaration->source_file = diagnostic_path;

    unit u;
    u.path = p;
    u.source = src;
    u.program = prog;
    u.module_name = find_module_name(prog, p);
    u.imports = collect_import_paths(prog);
    u.primary = primary_paths.count(p) != 0;

    // Dynamically resolve missing imports
    for (const auto &imp : u.imports) {
      std::string target = imp;
      auto alias_it = import_aliases.find(imp);
      if (alias_it != import_aliases.end()) {
        target = alias_it->second;
      }

      if (auto resolved = resolve_to_path(target)) {
        if (!parsed_paths.count(*resolved)) {
          worklist.push_back(*resolved);
        }
      }
    }

    units.push_back(std::move(u));
  }

  if (import_resolution_failed)
    return 1;

  // ... (existing module resolution and topological sort)
  // Build module name set and map
  std::unordered_map<std::string, size_t> module_to_index;
  for (size_t i = 0; i < units.size(); ++i) {
    const std::string &mname = units[i].module_name;
    // Allow multiple files with the same module name (split-module pattern).
    // The last file's index wins in module_to_index (used only for dep edges);
    // all units are still compiled and type-checked individually.
    module_to_index[mname] = i;
  }

  if (analysis) {
    std::unordered_set<std::string> declared_modules;
    for (const auto &u : units) for (const auto &d : u.program->declarations)
      if (d && d->kind == ny::ast_node_kind::module_decl_node)
        declared_modules.insert(static_cast<const ny::ast_module_decl &>(*d).name);
    bool unresolved = false;
    for (const auto &u : units) for (const auto &d : u.program->declarations) {
      if (!d || d->kind != ny::ast_node_kind::import_decl_node) continue;
      const auto &import = static_cast<const ny::ast_import_decl &>(*d);
      auto target = import.module_path;
      const auto alias = import_aliases.find(target);
      if (alias != import_aliases.end()) target = alias->second;
      if (!resolved_import_paths.count(target) && !declared_modules.count(target)) {
        analysis->add("NIL0003", "error", "Cannot resolve import '" + target + "'", u.path, d->line);
        unresolved = true;
      }
    }
    if (unresolved) return 1;
  }

  // Build dependency graph
  size_t n = units.size();
  std::vector<std::vector<size_t>> adj(n);
  for (size_t i = 0; i < units.size(); ++i) {
    for (const auto &imp : units[i].imports) {
      auto resolved = resolve_import_to_known_module(imp, [&]() {
        std::unordered_set<std::string> s;
        s.reserve(module_to_index.size());
        for (auto &kv : module_to_index)
          s.insert(kv.first);
        return s;
      }());
      if (resolved.has_value()) {
        auto it = module_to_index.find(*resolved);
        if (it != module_to_index.end()) {
          adj[it->second].push_back(i);
        }
      }
    }
  }

  // Topological sort
  std::vector<int> indeg(n, 0);
  for (size_t u = 0; u < n; ++u) {
    for (size_t v : adj[u]) {
      indeg[v]++;
    }
  }

  std::queue<size_t> q;
  for (size_t i = 0; i < n; ++i) {
    if (indeg[i] == 0)
      q.push(i);
  }

  std::vector<size_t> topo;
  topo.reserve(n);
  while (!q.empty()) {
    size_t u = q.front();
    q.pop();
    topo.push_back(u);
    for (size_t v : adj[u]) {
      indeg[v]--;
      if (indeg[v] == 0)
        q.push(v);
    }
  }

  if (topo.size() != n) {
    if (g_verbosity_level >= 3) {
      std::cerr << "Dependency analysis: " << n << " modules, " << topo.size()
                << " ordered\n";
      std::cerr << "Ordered modules:\n";
      for (size_t i = 0; i < topo.size(); ++i) {
        std::cerr << "  " << i << ": " << units[topo[i]].module_name << " ("
                  << units[topo[i]].path << ")\n";
      }
      std::cerr << "Modules participating in a possible cycle:\n";
      std::unordered_set<size_t> topo_set(topo.begin(), topo.end());
      for (size_t i = 0; i < n; ++i) {
        if (topo_set.find(i) == topo_set.end()) {
          std::cerr << "  " << i << ": " << units[i].module_name << " ("
                    << units[i].path << ")\n";
          std::cerr << "    Incoming edges from: ";
          for (size_t j = 0; j < n; ++j) {
            for (size_t v : adj[j]) {
              if (v == i)
                std::cerr << units[j].module_name << " ";
            }
          }
          std::cerr << "\n";
        }
      }
    }
    std::cerr << "Module dependency cycle detected among input files.\n";
    return 1;
  }

  // Merge ASTs
  auto combined = std::make_shared<ny::ast_program>();

  // Debug: Print modules in topological order
  if (g_verbosity_level >= 3) {
    std::cerr << "Modules in topological order:\n";
    for (size_t idx : topo) {
      std::cerr << "  - " << units[idx].path
                << " (module: " << units[idx].module_name << ")\n";
    }
  }

  for (size_t idx : topo) {
    const auto &unit_prog = units[idx].program;
    for (const auto &d : unit_prog->declarations) {
      combined->declarations.push_back(d);
    }
  }

  // Debug: Print declaration statistics
  if (g_verbosity_level >= 3) {
    int n_modules = 0, n_functions = 0, n_structs = 0, n_imports = 0,
        n_other = 0;
    for (const auto &d : combined->declarations) {
      if (!d)
        continue;
      switch (d->kind) {
      case ny::ast_node_kind::module_decl_node:
        n_modules++;
        break;
      case ny::ast_node_kind::function_decl_node:
        n_functions++;
        break;
      case ny::ast_node_kind::struct_decl_node:
        n_structs++;
        break;
      case ny::ast_node_kind::import_decl_node:
        n_imports++;
        break;
      default:
        n_other++;
        break;
      }
    }
    std::cerr << "Combined AST: " << n_modules << " modules, " << n_functions
              << " functions, " << n_structs << " structs, " << n_imports
              << " imports, " << n_other << " other\n";
  }

  // Type check
  ny::type_checker checker;

  // A Matrix page must not be told about 'i64' and 'String': those are Ny's
  // internal spellings and do not exist in the language the author is
  // writing. The shared checker reports them, so its messages are rendered
  // through matrix_diagnostics, which maps them back to 'number' and 'text'.
  //
  // ny::diagnostic carries a line but no file and no column. With one input
  // page the source line can be shown; with several the line number cannot be
  // attributed to a page, so only the message is rendered. Giving
  // ny::diagnostic a span is step 3 in
  // matrix/docs/diagnostics-and-analyzers.md, and it is what closes this gap
  // for both languages.
  bool any_matrix_input = false;
  for (const auto &u : units)
    // A dependency's frontend must not change the caller's diagnostic
    // vocabulary. Until diagnostics carry their own source spans, choose the
    // renderer from explicit inputs, not transitively imported pages.
    any_matrix_input = any_matrix_input ||
                       (u.primary && is_matrix_source(u.path));
  const unit *only_page = units.size() == 1 ? &units.front() : nullptr;

  const auto report_checker_diagnostics = [&](const char *heading) {
    // The rendered form carries its own 'error:' prefix per diagnostic, so
    // the group heading would only repeat it.
    if (!any_matrix_input) std::cerr << heading << "\n";
    if (!any_matrix_input) {
      for (const auto &diag : checker.get_diagnostics())
        std::cerr << "  line " << diag.line << ": " << diag.message << "\n";
      return;
    }
    const bool colour = ny_stderr_is_terminal();
    for (const auto &diag : checker.get_diagnostics()) {
      matrix::diagnostic rich;
      rich.level = matrix::severity::error;
      rich.message = diag.message;
      rich.primary.line = diag.line;
      rich.primary.column = 0; // no column available from the shared checker
      // ny::type_checker::diagnostic has no hint field; see step 3 in the
      // diagnostics design for giving it one.
      std::cerr << matrix::render_diagnostic(
          rich, only_page ? only_page->source : std::string(),
          only_page ? only_page->path : std::string(), colour);
    }
  };

  const bool checked = checker.check(*combined);
  if (analysis) {
    analysis->semantic_checked = checked;
    for (const auto &u : units) analysis->reflect(*u.program, u.path);
    for (const auto &d : checker.get_diagnostics()) {
      std::string path = d.source_file;
      // Ownership and older contracts do not yet carry file provenance.
      // Never attach their line to an arbitrary editor document.
      if (path.empty() && only_page) path = only_page->path;
      const bool matrix_diagnostic = path.empty() || path == analysis->primary_path
          ? analysis->language == "matrix" : is_matrix_source(path);
      const auto message = matrix_diagnostic
          ? matrix::to_surface_vocabulary(d.message) : d.message;
      analysis->add("NIL0002", "error", message, path, d.line);
    }
    return checked ? 0 : 1;
  }
  if (!checked) {
    if (!skip_typecheck) {
      report_checker_diagnostics("Type checking failed:");
      return 1;
    } else if (g_verbosity_level >= 1) {
      report_checker_diagnostics(
          "Type checking warnings (--no-typecheck, continuing):");
    }
  }

  std::string library_module_name;
  std::vector<ny::nil_embedded_source> library_sources;
  std::vector<std::string> library_dependencies;
  if (emit_library) {
    std::unordered_set<std::string> module_names;
    std::unordered_set<std::string> source_names;
    std::unordered_set<std::string> dependencies;
    std::vector<std::filesystem::path> source_roots;
    for (const auto &root : sdk_include_paths) {
      std::error_code root_error;
      const auto canonical =
          std::filesystem::weakly_canonical(root, root_error);
      source_roots.push_back(root_error ? std::filesystem::path(root)
                                        : canonical);
    }
    if (!sdk_stdlib.empty())
      source_roots.push_back(sdk_stdlib);

    for (const auto &source_unit : units) {
      if (!source_unit.primary)
        continue;
      module_names.insert(source_unit.module_name);

      std::filesystem::path logical_path;
      const std::filesystem::path absolute_source(source_unit.path);
      for (const auto &root : source_roots) {
        const auto relative = absolute_source.lexically_relative(root);
        if (relative.empty() || relative.is_absolute())
          continue;
        const auto first = relative.begin();
        if (first != relative.end() && *first != "..") {
          logical_path = relative;
          break;
        }
      }
      if (logical_path.empty())
        logical_path = absolute_source.filename();
      const std::string logical_name = logical_path.generic_string();
      if (logical_name.empty() ||
          logical_name.find("..") != std::string::npos ||
          !source_names.insert(logical_name).second) {
        std::cerr << "Error: library sources do not have unique safe logical "
                     "paths\n";
        return 1;
      }
      if (source_unit.source.size() > 16U * 1024U * 1024U) {
        std::cerr << "Error: a NIL library source exceeds the 16 MiB limit: "
                  << logical_name << "\n";
        return 1;
      }
      library_sources.push_back({logical_name, source_unit.source});
    }

    if (module_names.size() != 1U || library_sources.empty()) {
      std::cerr << "Error: --emit-library inputs must belong to exactly one "
                   "logical Ny module\n";
      return 1;
    }
    library_module_name = *module_names.begin();

    for (const auto &source_unit : units) {
      if (!source_unit.primary)
        continue;
      for (std::string dependency : source_unit.imports) {
        for (std::size_t i = 0; i < dependency.size();) {
          if (dependency[i] == '.') {
            dependency.replace(i, 1, "::");
            i += 2;
          } else {
            ++i;
          }
        }
        if (dependency == library_module_name ||
            dependency.rfind(library_module_name + "::", 0) == 0)
          continue;
        dependencies.insert(std::move(dependency));
      }
    }
    library_dependencies.assign(dependencies.begin(), dependencies.end());
    std::sort(library_dependencies.begin(), library_dependencies.end());
    std::sort(library_sources.begin(), library_sources.end(),
              [](const auto &left, const auto &right) {
                return left.path < right.path;
              });
  }

  // Native AOT and bundled executables are assembled in a freshly reserved
  // directory beside their final destination. Keeping every intermediate
  // behind that private directory removes the predictable sibling-file race
  // while retaining same-filesystem atomic publication.
  std::optional<std::filesystem::path> private_output_workspace;
  const char *workspace_purpose = nullptr;
  if (use_native && !use_jit)
    workspace_purpose = "ny-native";
  else if (bundle_output && !use_native)
    workspace_purpose = "ny-bundle";
  if (workspace_purpose) {
    std::error_code workspace_error;
    private_output_workspace = create_private_staging_directory(
        std::filesystem::path(output_path), workspace_purpose, workspace_error);
    if (!private_output_workspace) {
      std::cerr << "Error: could not create a private output staging "
                   "directory next to "
                << output_path << ": " << workspace_error.message() << "\n";
      return 1;
    }
  }
  remove_directory_on_exit private_output_cleanup(
      private_output_workspace.value_or(std::filesystem::path()));

  // JIT consumes the in-memory NIL module directly and therefore creates no
  // temporary NIL file. Bundling writes its payload only inside the private
  // workspace; an explicit standalone NIL output remains the sole direct
  // file-emission route.
  std::string nil_out_path = output_path;
  if (bundle_output && !use_native) {
    if (!private_output_workspace) {
      std::cerr << "Error: private bundle workspace was not created\n";
      return 1;
    }
    nil_out_path = (*private_output_workspace / "module.nil").string();
  }
  std::filesystem::path reported_output_path = nil_out_path;

  ny::codegen_options opts;
  opts.include_source_debug = source_debug;
  ny::codegen generator(opts);
  std::vector<ny::codegen_diagnostic> codegen_diags;
  std::unique_ptr<ny::nil_module> native_nil_module;
  bool ok;
  if (use_native) {
    native_nil_module = generator.generate_module(*combined, &codegen_diags);
    ok = static_cast<bool>(native_nil_module);
  } else if (emit_library) {
    auto library_module = generator.generate_module(*combined, &codegen_diags);
    ok = static_cast<bool>(library_module);
    if (library_module) {
      library_module->module_name = library_module_name;
      library_module->is_library = true;
      library_module->embedded_sources = library_sources;
      library_module->dependencies = library_dependencies;
      std::ofstream output(nil_out_path, std::ios::binary);
      if (!output) {
        ok = false;
      } else {
        ny::nil_binary_writer writer(output, opts);
        writer.emit_binary_module(*library_module);
        output.flush();
        ok = static_cast<bool>(output);
      }
    }
  } else if (emit_binary_nil) {
    ok = generator.emit_nil_binary_to_file(*combined, nil_out_path,
                                           &codegen_diags);
  } else {
    ok = generator.emit_nil_to_file(*combined, nil_out_path, &codegen_diags);
  }

  if (!codegen_diags.empty()) {
    std::cerr << "Code generation warnings/errors:\n";
    for (const auto &diag : codegen_diags) {
      std::cerr << "  line " << diag.line << ": " << diag.message;
      if (!diag.hint.empty())
        std::cerr << " (" << diag.hint << ")";
      std::cerr << "\n";
    }
  }

  // Verify result
  if (!ok) {
    std::cerr << "Code generation failed\n";
    return 1;
  }

  // Bundle into standalone executable if requested (default: true)
  if (bundle_output && !use_native) {
    const auto compiler_path = resolve_executable_path(argv[0]);
    std::filesystem::path interpreter_path;
    if (compiler_path) {
      interpreter_path = compiler_path->parent_path() /
#ifdef _WIN32
                         "nilinterpreter.exe";
#else
                         "nilinterpreter";
#endif
    }
    if (auto resolved = existing_executable_path(interpreter_path))
      interpreter_path = *resolved;
    else if (auto resolved = find_executable_on_path(
#ifdef _WIN32
                 "nilinterpreter.exe"
#else
                 "nilinterpreter"
#endif
                 ))
      interpreter_path = *resolved;
    else
      interpreter_path.clear();

    if (!interpreter_path.empty()) {
      // Determine output executable path
      std::filesystem::path out_exe = output_path;
      // Replace .nil extension with appropriate extension
      // On Windows: .exe, on Linux: no extension
      if (out_exe.extension() == ".nil") {
#ifdef _WIN32
        out_exe.replace_extension(".exe");
#else
        out_exe.replace_extension("");
#endif
      } else if (!out_exe.has_extension()) {
#ifdef _WIN32
        out_exe += ".exe";
#endif
      }

      // Read and validate the NIL payload before touching the requested
      // executable path.
      std::error_code nil_permission_error;
      constexpr auto private_data_permissions =
          std::filesystem::perms::owner_read |
          std::filesystem::perms::owner_write;
      if (!ensure_staged_regular_file_permissions(
              nil_out_path, private_data_permissions, nil_permission_error)) {
        std::cerr << "Error: failed to secure generated NIL payload: "
                  << nil_permission_error.message() << "\n";
        return 1;
      }
      std::ifstream nil_in(nil_out_path, std::ios::binary);
      if (!nil_in) {
        std::cerr << "Error: failed to open generated NIL payload: "
                  << nil_out_path << "\n";
        return 1;
      }
      std::vector<char> nil_data((std::istreambuf_iterator<char>(nil_in)),
                                 std::istreambuf_iterator<char>());
      if (nil_in.bad() || nil_data.empty() ||
          nil_data.size() > static_cast<std::size_t>(
                                std::numeric_limits<std::streamsize>::max())) {
        std::cerr << "Error: failed to read a valid generated NIL payload: "
                  << nil_out_path << "\n";
        return 1;
      }

      if (!private_output_workspace) {
        std::cerr << "Error: private bundle workspace was not created\n";
        return 1;
      }
      const auto staging_path = *private_output_workspace /
#ifdef _WIN32
                                "program.exe";
#else
                                "program";
#endif

      // Build beside the requested path so the completed file can replace
      // an older output atomically without exposing a partial bundle.
      std::error_code output_error;
      std::filesystem::copy_file(interpreter_path, staging_path,
                                 std::filesystem::copy_options::none,
                                 output_error);
      if (output_error) {
        std::cerr << "Error: failed to stage bundled executable: "
                  << output_error.message() << "\n";
        return 1;
      }
      if (!ensure_staged_regular_file_permissions(
              staging_path, std::filesystem::perms::owner_all, output_error)) {
        std::cerr << "Error: failed to secure staged bundled executable: "
                  << output_error.message() << "\n";
        return 1;
      }

      // Append the bytecode followed by a canonical little-endian payload
      // size and the bundle marker.
      std::ofstream bundle(staging_path, std::ios::binary | std::ios::app);
      if (bundle) {
        bundle.write(nil_data.data(),
                     static_cast<std::streamsize>(nil_data.size()));
        write_u64_little_endian(bundle, static_cast<uint64_t>(nil_data.size()));
        bundle.write("NYBUNDLE", 8);
        bundle.close();
      }
      if (!bundle) {
        std::cerr << "Error: failed to write staged bundled executable: "
                  << staging_path << "\n";
        return 1;
      }

      output_error.clear();
      if (!replace_output_file(staging_path, out_exe, output_error)) {
        std::cerr << "Error: failed to install bundled executable: "
                  << output_error.message() << "\n";
        return 1;
      }

      std::error_code absolute_error;
      const auto absolute_output =
          std::filesystem::absolute(out_exe, absolute_error);
      ny::diag_success(
          "Bundled executable: " +
          (absolute_error ? out_exe.string() : absolute_output.string()) +
          " (" + std::to_string(nil_data.size()) + " bytes bytecode embedded)");
      reported_output_path = out_exe;
    } else {
      std::cerr
          << "Error: cannot create bundled executable because "
             "nilinterpreter was not found next to nilcompiler or on PATH\n";
      std::cerr << "Use --no-bundle to produce a standalone NIL file.\n";
      return 1;
    }
  }

  // Native Codegen (LLVM)
  if (use_native) {
#if defined(NY_HAS_LLVM)
    ny::diag_success("[LLVM] Initializing native backend...");
    ny::NativeCodegen native;
    if (!native.initialize()) {
      std::cerr << "Failed to initialize native backend\n";
      return 1;
    }

    // Wire --unmanaged flag to native backend memory mode
    native.setMemoryMode(unmanaged ? ny::MemoryMode::UNSAFE_MANUAL
                                   : ny::MemoryMode::SAFE_GC);
    native.setEmbedContext(embed_context);

    if (!native_nil_module) {
      std::cerr << "Failed to generate NIL module for native compilation\n";
      return 1;
    }

    if (!native.generateModule(*native_nil_module)) {
      std::cerr << "Native code generation failed\n";
      return 1;
    }

    if (use_jit) {
      // Load libraries for JIT
      for (const auto &lib : link_libraries) {
        if (!native.loadLibrary(lib)) {
          std::cerr << "Warning: Failed to load JIT library: " << lib << "\n";
        }
      }
      int ret = native.executeJIT(program_args);
      ny::diag_success("[LLVM] JIT execution returned: " + std::to_string(ret));
      return ret;
    } else {
      // Emit object file and link
      std::filesystem::path exe_path = output_path;
#ifdef _WIN32
      // Native Windows applications conventionally use the .exe suffix.
      if (exe_path.extension() != ".exe") {
        exe_path += ".exe";
      }
      constexpr const char *object_filename = "module.obj";
      constexpr const char *linked_filename = "program.exe";
#else
      constexpr const char *object_filename = "module.o";
      constexpr const char *linked_filename = "program";
#endif
      if (!private_output_workspace) {
        std::cerr << "[Native] Internal error: private staging workspace "
                     "was not created\n";
        return 1;
      }
      const auto obj_path = *private_output_workspace / object_filename;
      const auto link_output_path = *private_output_workspace / linked_filename;
      const std::string obj_path_text = obj_path.string();
      const std::string link_output_path_text = link_output_path.string();

      ny::diag_success("[LLVM] Compiling native object...");
      if (native.emitObjectFile(obj_path_text)) {
        std::error_code object_permission_error;
        constexpr auto private_object_permissions =
            std::filesystem::perms::owner_read |
            std::filesystem::perms::owner_write;
        if (!ensure_staged_regular_file_permissions(obj_path,
                                                    private_object_permissions,
                                                    object_permission_error)) {
          std::cerr << "[Native] Failed to secure staged object file: "
                    << object_permission_error.message() << "\n";
          return 1;
        }
        const auto native_runtime = find_native_runtime_archive(argv[0]);
        if (!native_runtime) {
          std::cerr << "[Native] Required NIL runtime archive was not found. "
                       "Checked:\n";
          for (const auto &candidate :
               native_runtime_archive_candidates(argv[0])) {
            std::cerr << "  " << candidate.string() << "\n";
          }
          std::cerr << "[Native] Rebuild NIL or use a complete NIL SDK.\n";
          return 1;
        }
        bool linked = false;
        int link_ret = 0;
        std::string cmd;

#ifdef _WIN32
        // Prefer an explicitly discovered MSVC toolchain on Windows.
        // Strategy 1: Try to find Visual Studio link.exe
        auto [vs_linker, vs_lib_path] = find_vs_linker();
        if (!vs_linker.empty()) {
          auto [sdk_um_path, sdk_ucrt_path] = find_windows_sdk_lib();

          // Use cmd.exe /c to properly handle paths with spaces
          cmd = "cmd.exe /c \"\"" + vs_linker + "\" /nologo";

          // Add library paths
          if (!vs_lib_path.empty()) {
            cmd += " /LIBPATH:\"" + vs_lib_path + "\"";
          }
          if (!sdk_um_path.empty()) {
            cmd += " /LIBPATH:\"" + sdk_um_path + "\"";
          }
          if (!sdk_ucrt_path.empty()) {
            cmd += " /LIBPATH:\"" + sdk_ucrt_path + "\"";
          }

          cmd += " /out:\"" + link_output_path_text + "\" \"" + obj_path_text +
                 "\" \"" + native_runtime->string() + "\" kernel32.lib";
          for (const auto &lib : link_libraries) {
            cmd += " \"" + lib + "\"";
          }
          cmd += "\"";

          ny::diag_success("[LLVM] Using Visual Studio linker...");
          link_ret = std::system(cmd.c_str());
          if (link_ret == 0) {
            linked = true;
            ny::diag_success("[LLVM] Visual Studio linker completed.");
          }
        }

        // Strategy 2: Try clang with LLVM.
        if (!linked) {
          cmd = "clang++ -o \"" + link_output_path_text + "\" \"" +
                obj_path_text + "\" \"" + native_runtime->string() + "\"";
          for (const auto &lib : link_libraries) {
            cmd += " \"" + lib + "\"";
          }

          ny::diag_success("[LLVM] Trying clang linker...");
          link_ret = std::system(cmd.c_str());
          if (link_ret == 0) {
            linked = true;
            ny::diag_success("[LLVM] Clang linker completed.");
          }
        }

        // Strategy 3: Try link.exe in PATH
        if (!linked) {
          cmd = "link.exe /nologo /out:\"" + link_output_path_text + "\" \"" +
                obj_path_text + "\" \"" + native_runtime->string() +
                "\" kernel32.lib";
          for (const auto &lib : link_libraries) {
            cmd += " \"" + lib + "\"";
          }

          ny::diag_success("[LLVM] Trying MSVC linker in PATH...");
          link_ret = std::system(cmd.c_str());
          if (link_ret == 0) {
            linked = true;
            ny::diag_success("[LLVM] MSVC linker completed.");
          }
        }

        // Strategy 4: Try LLVM's lld-link
        if (!linked) {
          cmd = "lld-link /out:\"" + link_output_path_text + "\" \"" +
                obj_path_text + "\" \"" + native_runtime->string() +
                "\" kernel32.lib";
          for (const auto &lib : link_libraries) {
            cmd += " \"" + lib + "\"";
          }

          ny::diag_success("[LLVM] Trying lld-link...");
          link_ret = std::system(cmd.c_str());
          if (link_ret == 0) {
            linked = true;
            ny::diag_success("[LLVM] lld-link completed.");
          }
        }
#else
        // The current LLVM emitter produces a non-PIC object. Link it as a
        // non-PIE executable where the host linker supports that mode.
        const std::string linker_driver = NIL_NATIVE_LINKER_DRIVER;
        cmd = quote_posix_shell_argument(linker_driver) + " -pthread ";
#if defined(__linux__)
        cmd += "-no-pie ";
#elif defined(__APPLE__)
        cmd += "-Wl,-no_pie ";
#endif
        cmd += "-o " + quote_posix_shell_argument(link_output_path_text) + " " +
               quote_posix_shell_argument(obj_path_text) + " " +
               quote_posix_shell_argument(native_runtime->string());
#if defined(__linux__)
        cmd += " -ldl";
#endif
        for (const auto &lib : link_libraries) {
          cmd += " " + quote_posix_shell_argument(lib);
        }

        ny::diag_success("[LLVM] Linking native executable with " +
                         linker_driver + "...");
        link_ret = std::system(cmd.c_str());
        if (link_ret == 0)
          linked = true;
#endif

        if (!linked) {
          std::cerr << "[Native] All linker strategies failed.\n";
          std::cerr << "[Native] The private staging workspace will be "
                       "cleaned.\n";
          std::cerr << "[Native] Link command failed: " << cmd << "\n";
          return 1;
        }

        std::error_code executable_permission_error;
        if (!ensure_staged_regular_file_permissions(
                link_output_path, std::filesystem::perms::owner_all,
                executable_permission_error)) {
          std::cerr << "[Native] Failed to secure linked executable: "
                    << executable_permission_error.message() << "\n";
          return 1;
        }

        std::error_code install_error;
        if (!replace_output_file(link_output_path, exe_path, install_error)) {
          std::cerr << "[Native] Failed to install linked executable: "
                    << install_error.message() << "\n";
          return 1;
        }
        ny::diag_success("[LLVM] Successfully created " + exe_path.string());
        return 0;
      } else {
        std::cerr << "[Native] Failed to emit object file\n";
        return 1;
      }
    }
#else
    std::cerr
        << "Error: Native backend (LLVM) is not compiled in this build.\n";
    return 1;
#endif
  }

  if (g_verbosity_level >= 1) {
    std::cout << "Compiled files to '" << reported_output_path.string()
              << "' in topological order:\n";
    for (size_t idx : topo) {
      std::cout << "  - " << units[idx].path
                << " (module: " << units[idx].module_name << ")\n";
    }
  }
  return 0;
}

int main(int argc, char **argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--") break;
    if (std::string(argv[i]) == "--analyze")
      return ny::run_analysis_cli(argc, argv, compile_main);
  }
  return compile_main(argc, argv, nullptr);
}
