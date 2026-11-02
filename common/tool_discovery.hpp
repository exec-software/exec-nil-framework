#ifndef NIL_COMMON_TOOL_DISCOVERY_HPP
#define NIL_COMMON_TOOL_DISCOVERY_HPP

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace nil::detail {

inline bool is_executable_file(const std::filesystem::path &path) {
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error)) return false;
#ifdef _WIN32
  return true;
#else
  return access(path.c_str(), X_OK) == 0;
#endif
}

// Tool discovery does not invoke a shell or expand PATHEXT. Only the Windows
// executable suffix is implicit; explicit filenames retain their spelling.
inline std::optional<std::filesystem::path> executable_candidate(
    std::filesystem::path path) {
  if (is_executable_file(path)) return path;
#ifdef _WIN32
  if (!path.has_extension()) {
    path += L".exe";
    if (is_executable_file(path)) return path;
  }
#endif
  return {};
}

inline std::optional<std::filesystem::path> executable_on_path(
    const std::string &name, const std::filesystem::path &caller) {
  const char *raw = std::getenv("PATH");
  if (!raw) return {};
  const std::string paths(raw);
#ifdef _WIN32
  constexpr char separator = ';';
#else
  constexpr char separator = ':';
#endif
  std::size_t start = 0;
  for (;;) {
    const auto end = paths.find(separator, start);
    auto item = paths.substr(start, end == std::string::npos ? end : end - start);
#ifdef _WIN32
    // Quoted PATH components are common in Windows installations. Quotes are
    // not part of a directory name and must not be forwarded to filesystem.
    if (item.size() >= 2 && item.front() == '"' && item.back() == '"')
      item = item.substr(1, item.size() - 2);
#endif
    auto directory = std::filesystem::path(item);
    if (!directory.is_absolute()) directory = caller / directory;
    if (auto found = executable_candidate((directory / name).lexically_normal()))
      return found;
    if (end == std::string::npos) return {};
    start = end + 1;
  }
}

inline std::filesystem::path running_executable_path(
    const char *argument, const std::filesystem::path &caller) {
#ifdef _WIN32
  // argv[0] can be just "nilbuild" when launched through PATH. Ask Windows
  // for the loaded image instead so adjacent SDK tools remain discoverable.
  std::wstring buffer(256, L'\0');
  while (buffer.size() <= 32768) {
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                           static_cast<DWORD>(buffer.size()));
    if (length == 0) break;
    if (length < buffer.size()) {
      buffer.resize(length);
      return std::filesystem::path(buffer);
    }
    buffer.resize(buffer.size() * 2);
  }
#elif defined(__linux__)
  std::error_code error;
  auto path = std::filesystem::read_symlink("/proc/self/exe", error);
  if (!error) return path;
#endif
  const std::filesystem::path supplied(argument);
  if (supplied.has_parent_path())
    return (supplied.is_absolute() ? supplied : caller / supplied).lexically_normal();
  if (auto found = executable_on_path(argument, caller)) return *found;
  return (caller / supplied).lexically_normal();
}

}  // namespace nil::detail

#endif  // NIL_COMMON_TOOL_DISCOVERY_HPP
