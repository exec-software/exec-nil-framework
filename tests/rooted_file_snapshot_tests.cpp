// All paths and concurrent namespace changes belong to this private fixture.
#include "../nilcompiler/src/secure_file_io.hpp"
#include "../common/private_directory.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
namespace fs = std::filesystem;
int checks = 0;

void check(bool good, const char *label) {
  ++checks;
  if (!good) throw std::runtime_error(label);
}

class Fixture {
public:
  Fixture() : path(nil::detail::create_private_directory(fs::temp_directory_path(), "nil-rooted-snapshot-")) {}
  ~Fixture() { std::error_code ignored; fs::remove_all(path, ignored); }
  fs::path path;
};

void write(const fs::path &path, const std::string &bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  output.close();
  check(!output.fail(), "write owned fixture");
}

ny::detail::RootedFileSnapshot read(const fs::path &root, const std::string &relative,
                                   std::uint64_t maximum = 1048576) {
  return ny::detail::read_rooted_file_snapshot_utf8(root.u8string(), relative, maximum);
}

std::string body(const ny::detail::RootedFileSnapshot &result) {
  return std::string(result.bytes.begin(), result.bytes.end());
}

void refused(const ny::detail::RootedFileSnapshot &result, const char *label,
             const fs::path &private_root) {
  check(!result.ok && result.bytes.empty(), label);
  check(result.error.find(private_root.u8string()) == std::string::npos &&
        result.error.rfind("file.read_rooted_snapshot: ", 0) == 0,
        "refusal has a fixed diagnostic without private paths");
}

void timestamp(const fs::path &path, std::int64_t milliseconds) {
#ifdef _WIN32
  HANDLE file = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL, nullptr);
  check(file != INVALID_HANDLE_VALUE, "open owned timestamp fixture");
  const auto ticks = static_cast<std::uint64_t>(milliseconds + 11644473600000LL) * 10000;
  FILETIME held {static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32U)};
  const bool okay = SetFileTime(file, nullptr, nullptr, &held) != 0;
  CloseHandle(file);
  check(okay, "set owned timestamp fixture");
#else
  timespec times[2] {};
  times[0].tv_sec = times[1].tv_sec = milliseconds / 1000;
  times[0].tv_nsec = times[1].tv_nsec = (milliseconds % 1000) * 1000000;
  check(::utimensat(AT_FDCWD, path.c_str(), times, 0) == 0, "set owned timestamp fixture");
#endif
}

void directory_link(const fs::path &link, const fs::path &target) {
#ifdef _WIN32
  // Junctions exercise reparse refusal without requiring symlink privilege.
  check(fs::create_directory(link), "create owned junction directory");
  const std::wstring shown = fs::absolute(target).wstring();
  const std::wstring substitute = L"\\??\\" + shown;
  struct Junction {
    DWORD tag;
    USHORT data_length;
    USHORT reserved;
    USHORT substitute_offset;
    USHORT substitute_length;
    USHORT print_offset;
    USHORT print_length;
    WCHAR names[1];
  };
  const auto name_bytes = (substitute.size() + shown.size() + 2) * sizeof(WCHAR);
  std::vector<char> storage(offsetof(Junction, names) + name_bytes, 0);
  auto *data = reinterpret_cast<Junction *>(storage.data());
  data->tag = IO_REPARSE_TAG_MOUNT_POINT;
  data->data_length = static_cast<USHORT>(storage.size() - 8);
  data->substitute_length = static_cast<USHORT>(substitute.size() * sizeof(WCHAR));
  data->print_offset = static_cast<USHORT>((substitute.size() + 1) * sizeof(WCHAR));
  data->print_length = static_cast<USHORT>(shown.size() * sizeof(WCHAR));
  std::memcpy(data->names, substitute.data(), data->substitute_length);
  std::memcpy(reinterpret_cast<char *>(data->names) + data->print_offset,
              shown.data(), data->print_length);
  HANDLE held = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  check(held != INVALID_HANDLE_VALUE, "open owned junction directory");
  DWORD returned = 0;
  const bool okay = DeviceIoControl(held, FSCTL_SET_REPARSE_POINT, data,
      static_cast<DWORD>(storage.size()), nullptr, 0, &returned, nullptr) != 0;
  CloseHandle(held);
  check(okay, "set owned junction reparse data");
#else
  fs::create_directory_symlink(target, link);
#endif
}

void contracts(const fs::path &base) {
  const auto root = base / "public";
  const auto outside = base / "outside";
  fs::create_directories(root / "nested");
  fs::create_directory(outside);
  const std::string binary("A\0\xff\x80Z", 5);
  write(root / "nested" / "asset.bin", binary);
  write(outside / "asset.bin", "outside-owned-marker");
  timestamp(root / "nested" / "asset.bin", 1700000000123LL);
  const auto exact = read(root, "nested/asset.bin", binary.size());
  check(exact.ok && body(exact) == binary && exact.error.empty(), "read exact bounded binary payload");
  check(exact.modified_ms == 1700000000123LL, "return same-handle milliseconds without clock sampling");
  const auto short_limit = read(root, "nested/asset.bin", binary.size() - 1);
  refused(short_limit, "reject oversized before allocation", base);
  check(short_limit.error == "file.read_rooted_snapshot: input exceeds maximum_bytes",
        "oversized refusal is distinct for HTTP mapping");
  refused(read(root, "nested/asset.bin", 512ULL * 1024 * 1024 + 1), "reject excessive requested limit", base);
  write(root / "empty", "");
  const auto empty = read(root, "empty", 0);
  check(empty.ok && empty.bytes.empty(), "zero limit accepts empty regular file");
  refused(read(root, "nested/asset.bin", 0), "zero limit rejects nonempty file", base);
  refused(read(root, "absent"), "missing file refused", base);
  refused(read(root, "nested"), "directory cannot be served as a regular file", base);
  refused(read(root / "empty", "asset"), "regular file cannot be a root directory", base);

  const std::vector<std::string> bad_names = {
      "", ".", "..", "/nested/asset.bin", "nested/../empty", "nested/./asset.bin",
      "nested//asset.bin", "nested/asset.bin/", "nested\\asset.bin", "C:asset.bin",
      "empty:stream", std::string("empty\0ignored", 13), std::string("\xc0\xaf", 2),
      std::string("\xed\xa0\x80", 3), std::string("\xf4\x90\x80\x80", 4)};
  for (const auto &name : bad_names)
    refused(read(root, name), "invalid relative spelling refused", base);
  refused(ny::detail::read_rooted_file_snapshot_utf8("", "empty", 10), "empty root refused", base);
  refused(ny::detail::read_rooted_file_snapshot_utf8(std::string("\xc0\xaf", 2), "empty", 10),
          "malformed UTF-8 root refused before filesystem access", base);
  const auto unicode = fs::u8path(u8"space λ 🧪");
  fs::create_directory(root / unicode);
  write(root / unicode / "asset.bin", binary);
  check(body(read(root, unicode.u8string() + "/asset.bin")) == binary, "UTF-8 descendant names round-trip");
  directory_link(root / "linked", outside);
  // This control intentionally demonstrates the old API's documented final-
  // component-only boundary, using only a marker owned by the test fixture.
  const auto legacy = ny::detail::read_regular_file_bounded(root / "linked" / "asset.bin", 100);
  check(legacy.ok && std::string(legacy.bytes.begin(), legacy.bytes.end()) == "outside-owned-marker",
        "negative control: final-component-only API follows intermediate link");
  refused(read(root, "linked/asset.bin"), "intermediate directory link refused", base);
  refused(read(root, "linked"), "final directory reparse/link refused", base);
  refused(read(root / "linked", "asset.bin"), "root final reparse/link refused", base);
  refused(read(root / "linked" / "", "asset.bin"), "trailing slash cannot disguise a root link", base);
  refused(read(root / "linked" / ".", "asset.bin"), "trailing dot cannot disguise a root link", base);
  check(body(read(root / "", "nested/asset.bin")) == binary,
        "trailing slash on an ordinary root remains valid");
  check(body(read(root / ".", "nested/asset.bin")) == binary,
        "trailing dot on an ordinary root remains valid");
#ifndef _WIN32
  fs::create_symlink(outside / "asset.bin", root / "final-link");
  refused(read(root, "final-link"), "final file symlink refused", base);
  check(::mkfifo((root / "pipe").c_str(), 0600) == 0, "create owned FIFO");
  const auto began = std::chrono::steady_clock::now();
  refused(read(root, "pipe"), "FIFO is refused without opening a writer", base);
  check(std::chrono::steady_clock::now() - began < std::chrono::seconds(2), "FIFO refusal is nonblocking");
#endif
}

void namespace_race(const fs::path &base) {
  const auto root = base / "race-root";
  const auto outside = base / "race-outside";
  fs::create_directories(root / "live");
  fs::create_directory(outside);
  write(root / "live" / "asset", "inside-owned-marker");
  write(outside / "asset", "outside-owned-marker");
  directory_link(root / "link-spare", outside);
  std::atomic<bool> stop{false};
  std::atomic<int> swaps{0};
  std::atomic<bool> mutator_failed{false};
  std::thread mutator([&] {
    // Windows can briefly reject a directory rename while a descendant file
    // handle denies delete sharing. Complete each owned swap before stopping.
    const auto finish_rename = [&](const fs::path &from, const fs::path &to) {
      const auto bound = std::chrono::steady_clock::now() + std::chrono::seconds(2);
      std::error_code error;
      do {
        fs::rename(from, to, error);
        if (!error) return true;
        std::this_thread::yield();
      } while (std::chrono::steady_clock::now() < bound);
      return false;
    };
    while (!stop.load()) {
      std::error_code error;
      fs::rename(root / "live", root / "safe-spare", error);
      if (error) { std::this_thread::yield(); continue; }
      if (!finish_rename(root / "link-spare", root / "live")) { mutator_failed = true; break; }
      ++swaps;
      std::this_thread::yield();
      if (!finish_rename(root / "live", root / "link-spare") ||
          !finish_rename(root / "safe-spare", root / "live")) { mutator_failed = true; break; }
    }
  });
  bool escaped = false;
  int reads = 0;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  try {
    while (reads < 500 && std::chrono::steady_clock::now() < deadline) {
      const auto result = read(root, "live/asset");
      ++reads;
      if (result.ok && body(result) != "inside-owned-marker") escaped = true;
    }
  } catch (...) {
    stop = true;
    mutator.join();
    throw;
  }
  stop = true;
  mutator.join();
  check(!mutator_failed.load(), "owned namespace mutator completed its swaps");
  check(swaps.load() > 0 && reads > 0, "concurrent namespace changes actually occurred");
  check(!escaped, "anchored opens never follow replacement link outside root");
}

void coupled_snapshot_race(const fs::path &base) {
  const auto root = base / "snapshot-race";
  fs::create_directory(root);
  const std::string first(65536, 'a'), second(65536, 'b');
  write(root / "live", first);
  write(root / "spare", second);
  timestamp(root / "live", 1700000000123LL);
  timestamp(root / "spare", 1700000001567LL);
  std::atomic<bool> stop{false};
  std::atomic<int> swaps{0};
  std::atomic<bool> failed{false};
  std::thread publisher([&] {
    const auto finish = [&](const char *from, const char *to) {
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
      std::error_code error;
      do {
        fs::rename(root / from, root / to, error);
        if (!error) return true;
        std::this_thread::yield();
      } while (std::chrono::steady_clock::now() < deadline);
      return false;
    };
    while (!stop.load()) {
      std::error_code error;
      fs::rename(root / "live", root / "park", error);
      if (error) { std::this_thread::yield(); continue; }
      if (!finish("spare", "live") || !finish("park", "spare")) {
        failed = true;
        break;
      }
      ++swaps;
    }
  });
  bool mixed = false;
  int succeeded = 0;
  try {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    for (int index = 0; index < 500 && std::chrono::steady_clock::now() < deadline; ++index) {
      const auto result = read(root, "live");
      if (result.ok) {
        ++succeeded;
        const auto bytes = body(result);
        if (!((bytes == first && result.modified_ms == 1700000000123LL) ||
              (bytes == second && result.modified_ms == 1700000001567LL))) mixed = true;
      }
    }
  } catch (...) {
    stop = true;
    publisher.join();
    throw;
  }
  stop = true;
  publisher.join();
  check(!failed.load() && swaps.load() > 0, "same-handle fixture published distinct file versions");
  check(succeeded > 0, "same-handle fixture observed successful snapshots");
  check(!mixed, "bytes and timestamp always identify the same opened file version");
}

} // namespace

int main() {
  try {
    Fixture fixture;
    contracts(fixture.path);
    namespace_race(fixture.path);
    coupled_snapshot_race(fixture.path);
    std::cout << checks << " rooted snapshot checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "rooted snapshot contract failed: " << error.what() << '\n';
    return 1;
  }
}
