#pragma once

#include <filesystem>
#include <istream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace nil::detail::compiler_modules {
namespace fs = std::filesystem;
using files = std::set<std::string>;
#if defined(__APPLE__)
// Defined in the Darwin-only translation unit (-fblocks).
bool collect_darwin(files &observed);
#endif

// Linux file-backed mappings are a separate identity graph from source inputs.
// Keep valid observations on failure; callers may still protect/hash them, but
// must not certify completeness or reuse an artifact from a partial inventory.
template <typename Readable>
bool collect_mapped_files(std::istream &mappings, files &observed, Readable readable) {
  bool complete = static_cast<bool>(mappings), any_file = false;
  std::string mapping;
  while (std::getline(mappings, mapping)) {
    std::istringstream fields(mapping);
    std::string address, permissions, offset, device, inode, path;
    if (!(fields >> address >> permissions >> offset >> device >> inode)) {
      complete = false;
      continue;
    }
    std::getline(fields >> std::ws, path);
    if (inode.find_first_not_of("0123456789") != std::string::npos) {
      complete = false;
      continue;
    }
    if (path.empty() || path.front() == '[') {
      // A nonzero inode with no filename is a truncated/unresolved file-backed
      // observation, not an anonymous or kernel-provided mapping.
      if (inode != "0") complete = false;
      continue;
    }
    if (inode == "0" || path.front() != '/' || path.find(" (deleted)") != std::string::npos || !readable(path)) {
      complete = false;
      continue;
    }
    observed.insert(path);
    any_file = true;
  }
  return complete && any_file && mappings.eof() && !mappings.bad();
}

// Add one known optional system component whether or not this process happened
// to load it. This is a conservative fingerprint superset, not an exclusion:
// every other observed file remains, including a different same-basename DLL.
// The operations overload makes error handling deterministic in unit tests.
template <typename Status, typename Equivalent>
bool stabilize_optional_anchor(files &observed, const fs::path &anchor,
                               Status status, Equivalent equivalent) {
  try {
    if (!anchor.is_absolute() || anchor.filename().empty()) return false;
    std::error_code error;
    const auto parent = status(anchor.parent_path(), error);
    if (error || !fs::is_directory(parent)) return false;
    const auto metadata = status(anchor, error);
    if (error) return error == std::errc::no_such_file_or_directory;
    if (metadata.type() == fs::file_type::not_found) return true;
    if (!fs::is_regular_file(metadata)) return false;

    files normalized;
    for (const auto &name : observed) {
      const auto path = fs::u8path(name);
      if (!path.is_absolute()) return false;
      const bool same = equivalent(path, anchor, error);
      if (error) return false;
      if (!same) normalized.insert(name);
    }
    normalized.insert(anchor.u8string());
    // Failed identity checks must not partially remove observed entries.
    observed.swap(normalized);
    return true;
  } catch (...) {
    return false;
  }
}

inline bool stabilize_optional_anchor(files &observed, const fs::path &anchor) {
  return stabilize_optional_anchor(observed, anchor,
      [](const fs::path &path, std::error_code &error) { return fs::status(path, error); },
      [](const fs::path &left, const fs::path &right, std::error_code &error) {
        return fs::equivalent(left, right, error);
      });
}

#ifdef _WIN32
inline std::optional<fs::path> system_apphelp_anchor() {
  // Resolve the real system directory, never an environment variable, PATH,
  // working directory, or an app-local DLL. No module is loaded by this probe.
  std::vector<wchar_t> directory(32768);
  const UINT count = GetSystemDirectoryW(directory.data(), static_cast<UINT>(directory.size()));
  if (!count || count >= directory.size()) return std::nullopt;
  const fs::path path(std::wstring(directory.data(), count));
  if (!path.is_absolute()) return std::nullopt;
  return path / L"apphelp.dll";
}

inline bool collect_windows(files &observed) {
  HANDLE snapshot = INVALID_HANDLE_VALUE;
  // Keep the existing bounded retry for one snapshot's documented transient
  // loader condition. This does not retry a compilation or a changed graph.
  for (int attempt = 0; attempt < 8; ++attempt) {
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                      GetCurrentProcessId());
    if (snapshot != INVALID_HANDLE_VALUE || GetLastError() != ERROR_BAD_LENGTH) break;
  }
  if (snapshot == INVALID_HANDLE_VALUE) return false;
  struct snapshot_cleanup {
    HANDLE handle;
    ~snapshot_cleanup() { CloseHandle(handle); }
  } cleanup{snapshot};
  MODULEENTRY32W module{};
  module.dwSize = sizeof(module);
  if (!Module32FirstW(snapshot, &module)) return false;
  try {
    do {
      const fs::path path(module.szExePath);
      std::error_code error;
      if (!path.is_absolute() || !fs::is_regular_file(path, error) || error) return false;
      observed.insert(path.u8string());
    } while (Module32NextW(snapshot, &module));
    // Filesystem and system-directory queries may overwrite GetLastError.
    const DWORD terminal_error = GetLastError();
    if (terminal_error != ERROR_NO_MORE_FILES) return false;
    const auto anchor = system_apphelp_anchor();
    return anchor && stabilize_optional_anchor(observed, *anchor);
  } catch (...) {
    return false;
  }
}
#endif
} // namespace nil::detail::compiler_modules
