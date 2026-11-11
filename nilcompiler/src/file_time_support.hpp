#pragma once

#include "../../common/strict_json.hpp"
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

#ifdef _WIN32
#include "../../common/windows_environment.hpp"
#else
#include <sys/stat.h>
#endif

namespace ny::file_time_support {

// Floor to Unix milliseconds, including times before 1970. No clock readings
// participate in this conversion: unchanged metadata has an unchanged result.
inline std::optional<std::int64_t> from_unix_parts(std::int64_t seconds,
                                                  std::int64_t nanoseconds) {
  if (nanoseconds < 0 || nanoseconds >= 1000000000) return std::nullopt;
  const auto fraction = static_cast<std::uint64_t>(nanoseconds / 1000000);
  constexpr auto maximum = static_cast<std::uint64_t>(
      (std::numeric_limits<std::int64_t>::max)());
  if (seconds >= 0) {
    const auto whole = static_cast<std::uint64_t>(seconds);
    if (whole > maximum / 1000) return std::nullopt;
    const auto base = whole * 1000;
    if (fraction > maximum - base) return std::nullopt;
    return static_cast<std::int64_t>(base + fraction);
  }
  const auto magnitude = static_cast<std::uint64_t>(-(seconds + 1)) + 1;
  constexpr auto minimum_magnitude = maximum + 1;
  if (magnitude > (minimum_magnitude + 999) / 1000) return std::nullopt;
  const auto result = magnitude * 1000 - fraction;
  if (result > minimum_magnitude) return std::nullopt;
  if (result == minimum_magnitude) return (std::numeric_limits<std::int64_t>::min)();
  return -static_cast<std::int64_t>(result);
}

// FILETIME is an unsigned count of 100 ns ticks since 1601. The epoch offset
// is exactly divisible by a millisecond, so division before subtraction also
// floors negative Unix timestamps correctly and cannot overflow an i64.
inline std::int64_t from_windows_ticks(std::uint64_t ticks) {
  constexpr std::int64_t epoch_milliseconds = 11644473600000LL;
  return static_cast<std::int64_t>(ticks / 10000) - epoch_milliseconds;
}

// Follow symlinks as last_write_time did, and retain directory timestamps.
// Text paths are strict UTF-8 on every host; never pass an embedded NUL to the
// OS. This API intentionally returns no path-bearing host diagnostic.
inline std::optional<std::int64_t> modified_at(const std::string &path) {
  if (path.empty() || path.find('\0') != std::string::npos) return std::nullopt;
  try {
    nil::detail::valid_utf8(path);
#ifdef _WIN32
    const auto wide = nil::detail::environment_utf16(path);
    if (!wide) return std::nullopt;
    const HANDLE file = CreateFileW(wide->c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (file == INVALID_HANDLE_VALUE) return std::nullopt;
    FILETIME written{};
    const bool obtained = GetFileTime(file, nullptr, nullptr, &written) != FALSE;
    const bool closed = CloseHandle(file) != FALSE;
    if (!obtained || !closed) return std::nullopt;
    const auto ticks = (static_cast<std::uint64_t>(written.dwHighDateTime) << 32) |
                       written.dwLowDateTime;
    return from_windows_ticks(ticks);
#else
    struct stat state{};
    if (::stat(path.c_str(), &state) != 0) return std::nullopt;
#ifdef __APPLE__
    const auto written = state.st_mtimespec;
#else
    const auto written = state.st_mtim;
#endif
    static_assert(std::numeric_limits<decltype(written.tv_sec)>::is_signed &&
                  std::numeric_limits<decltype(written.tv_sec)>::digits <= 63,
                  "file timestamps require a signed at-most-64-bit time_t");
    return from_unix_parts(static_cast<std::int64_t>(written.tv_sec),
                           static_cast<std::int64_t>(written.tv_nsec));
#endif
  } catch (...) {
    return std::nullopt;
  }
}

} // namespace ny::file_time_support
