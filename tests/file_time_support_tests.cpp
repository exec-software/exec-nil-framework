#include "file_time_support.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifndef _WIN32
#include <fcntl.h>
#endif

namespace {
int checks = 0;
void require(bool condition, const char *message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
struct PrivateDirectory {
  std::filesystem::path path;
  PrivateDirectory() {
    const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 128; ++attempt) {
      auto candidate = std::filesystem::temp_directory_path() /
          ("nil-file-time-" + std::to_string(seed) + "-" + std::to_string(attempt));
      std::error_code error;
      if (std::filesystem::create_directory(candidate, error)) {
        path = std::move(candidate);
        return;
      }
      if (error && error != std::errc::file_exists)
        throw std::runtime_error("cannot create private timestamp fixture");
    }
    throw std::runtime_error("cannot allocate private timestamp fixture");
  }
  ~PrivateDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
};
}

int main() {
  try {
    using ny::file_time_support::from_unix_parts;
    using ny::file_time_support::from_windows_ticks;
    using ny::file_time_support::modified_at;
    constexpr auto maximum = (std::numeric_limits<std::int64_t>::max)();
    constexpr auto minimum = (std::numeric_limits<std::int64_t>::min)();
    require(from_unix_parts(0, 0) == 0, "Unix epoch");
    require(from_unix_parts(0, 999999) == 0, "submillisecond floor");
    require(from_unix_parts(0, 1000000) == 1, "positive millisecond");
    require(from_unix_parts(-1, 999999999) == -1, "negative submillisecond floor");
    require(from_unix_parts(-1, 999000000) == -1, "negative exact millisecond");
    require(from_unix_parts(-1, 0) == -1000, "negative second");
    require(from_unix_parts(1600000000, 123456789) == 1600000000123LL, "known Unix instant");
    require(!from_unix_parts(0, -1), "negative nanoseconds rejected");
    require(!from_unix_parts(0, 1000000000), "unnormalized nanoseconds rejected");
    require(from_unix_parts(maximum / 1000, 807999999) == maximum, "maximum exact result");
    require(!from_unix_parts(maximum / 1000, 808000000), "upper overflow rejected");
    require(from_unix_parts(minimum / 1000 - 1, 192000000) == minimum, "minimum exact result");
    require(!from_unix_parts(minimum / 1000 - 1, 191999999), "lower overflow rejected");
    require(!from_unix_parts(maximum, 0), "large positive second rejected");
    require(!from_unix_parts(minimum, 0), "large negative second rejected");
    constexpr std::uint64_t epoch_ticks = 116444736000000000ULL;
    require(from_windows_ticks(0) == -11644473600000LL, "Windows epoch");
    require(from_windows_ticks(epoch_ticks) == 0, "Windows Unix epoch");
    require(from_windows_ticks(epoch_ticks - 1) == -1, "Windows negative submillisecond floor");
    require(from_windows_ticks(epoch_ticks + 9999) == 0, "Windows positive submillisecond floor");
    require(from_windows_ticks(epoch_ticks + 10000) == 1, "Windows millisecond");
    require(from_windows_ticks(UINT64_MAX) == 1833029933770955LL, "Windows full unsigned range");
    require(!modified_at(""), "empty path rejected");
    require(!modified_at(std::string("\xff", 1)), "invalid UTF-8 rejected");

    PrivateDirectory private_directory;
    const auto path = private_directory.path /
        std::filesystem::u8path(u8"time-café-日本語-🦊.txt");
    { std::ofstream output(path, std::ios::binary); output << "timestamp fixture\n";
      require(static_cast<bool>(output), "fixture written"); }
#ifdef _WIN32
    const auto file = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(file != INVALID_HANDLE_VALUE, "fixture attribute handle");
    constexpr std::uint64_t ticks = epoch_ticks + 16000000001234567ULL;
    const FILETIME stamp{static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32)};
    const bool changed = SetFileTime(file, nullptr, nullptr, &stamp) != FALSE;
    const bool closed = CloseHandle(file) != FALSE;
    require(changed && closed, "exact Windows timestamp installed");
#else
    const timespec stamps[2]{{1600000000, 123456700}, {1600000000, 123456700}};
    require(utimensat(AT_FDCWD, path.c_str(), stamps, 0) == 0, "exact POSIX timestamp installed");
#endif
    for (int i = 0; i < 100; ++i)
      require(modified_at(path.u8string()) == 1600000000123LL, "stable exact Unicode-file timestamp");
    require(!modified_at(path.u8string() + ".absent"), "missing file rejected");
    require(!modified_at(path.u8string() + std::string("\0ignored", 8)), "NUL suffix cannot select prefix file");
    require(modified_at(private_directory.path.u8string()).has_value(), "directory metadata retained");
    std::cout << "file time support: " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "file time support: " << error.what() << '\n';
    return 1;
  }
}
