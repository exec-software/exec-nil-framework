// Exercise real Windows sharing/access failures against the publication helper.
// Every file and handle belongs to this fixture; no installed SDK is modified.
#include "../common/atomic_file_replace.hpp"
#include "../common/private_directory.hpp"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>

#ifndef _WIN32
#error This contract requires native Windows file-sharing semantics.
#endif

namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
int checks = 0;

void check(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}

class Handle {
public:
  explicit Handle(HANDLE value) : value_(value) {}
  ~Handle() { if (value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
  Handle(const Handle &) = delete;
  Handle &operator=(const Handle &) = delete;
  HANDLE get() const { return value_; }
  bool close() {
    const HANDLE value = value_;
    value_ = INVALID_HANDLE_VALUE;
    return CloseHandle(value) != FALSE;
  }
private:
  HANDLE value_;
};

class Join {
public:
  explicit Join(std::thread &thread) : thread_(thread) {}
  ~Join() { if (thread_.joinable()) thread_.join(); }
private:
  std::thread &thread_;
};

class ReadOnly {
public:
  explicit ReadOnly(const fs::path &path) : path_(path) {
    check(SetFileAttributesW(path_.c_str(), FILE_ATTRIBUTE_READONLY) != FALSE,
          "set owned target read-only");
  }
  ~ReadOnly() { SetFileAttributesW(path_.c_str(), FILE_ATTRIBUTE_NORMAL); }
private:
  fs::path path_;
};

void write(const fs::path &path, const std::string &bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  stream.close();
  check(!stream.fail(), "write owned file bytes");
}

std::string read(const fs::path &path) {
  std::ifstream stream(path, std::ios::binary);
  check(stream.is_open(), "open owned file for byte oracle");
  const std::string bytes((std::istreambuf_iterator<char>(stream)), {});
  check(!stream.bad(), "read owned file bytes");
  return bytes;
}

bool transient(DWORD error) {
  return error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION ||
         error == ERROR_LOCK_VIOLATION;
}

void bounded(Clock::time_point start) {
  check(Clock::now() - start < std::chrono::seconds(5),
        "publication attempt exceeded five-second fixture bound");
}

void contracts(const fs::path &root) {
  const auto source = root / L"staging space \u03bb.tmp";
  const auto output = root / L"output space \u03bb.exe";
  const std::string old_bytes("old\0\xff", 5), new_bytes("new\0\x80payload", 12);
  std::error_code error = std::make_error_code(std::errc::io_error);
  write(source, new_bytes);
  check(nil::detail::replace_output_file(source, output, error), "publish initially absent output");
  check(!error && !fs::exists(source) && read(output) == new_bytes,
        "successful publication clears stale error and consumes staging file");

  write(output, old_bytes);
  write(source, new_bytes);
  {
    Handle held(CreateFileW(output.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    check(held.get() != INVALID_HANDLE_VALUE, "open transient target without delete sharing");
    // The linker's old single-attempt operation must fail under this exact
    // real OS condition, before another thread is allowed to release it.
    const BOOL single = MoveFileExW(source.c_str(), output.c_str(),
                                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    const DWORD single_error = GetLastError();
    check(single == FALSE && transient(single_error), "old single attempt rejects held target");
    check(read(output) == old_bytes && read(source) == new_bytes,
          "single-attempt negative control preserves both files");
    std::atomic<bool> closed{false};
    std::thread releaser([&] {
      Sleep(160);
      closed.store(held.close());
    });
    Join join(releaser);
    const auto start = Clock::now();
    const bool replaced = nil::detail::replace_output_file(source, output, error);
    releaser.join();
    bounded(start);
    check(closed.load(), "owned transient handle closed successfully");
    check(replaced && !error, "retry publishes after transient handle release");
    check(!fs::exists(source) && read(output) == new_bytes,
          "transient success publishes exact new bytes");
  }

  write(output, old_bytes);
  write(source, new_bytes);
  {
    Handle held(CreateFileW(output.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    check(held.get() != INVALID_HANDLE_VALUE, "open permanently held target");
    const auto start = Clock::now();
    const bool replaced = nil::detail::replace_output_file(source, output, error);
    bounded(start);
    check(!replaced && transient(static_cast<DWORD>(error.value())),
          "permanent sharing failure remains a reported error");
    check(read(output) == old_bytes && read(source) == new_bytes,
          "permanent sharing failure preserves old output and staging bytes");
  }
  {
    ReadOnly readonly(output);
    const auto start = Clock::now();
    const bool replaced = nil::detail::replace_output_file(source, output, error);
    bounded(start);
    check(!replaced && error.value() == ERROR_ACCESS_DENIED, "read-only target fails without permission changes");
    check((GetFileAttributesW(output.c_str()) & FILE_ATTRIBUTE_READONLY) != 0,
          "failed publication leaves read-only attribute intact");
    check(read(output) == old_bytes && read(source) == new_bytes,
          "read-only failure preserves both files");
  }
  check(fs::remove(source), "remove only the owned staging file for missing-source control");
  const auto start = Clock::now();
  const bool replaced = nil::detail::replace_output_file(source, output, error);
  bounded(start);
  check(!replaced && error.value() == ERROR_FILE_NOT_FOUND, "missing staging file is reported");
  check(read(output) == old_bytes && !fs::exists(source), "missing-source failure preserves output");
}
} // namespace

int main() {
  fs::path root;
  try {
    root = nil::detail::create_private_directory(fs::temp_directory_path(), "nil-atomic-replace-");
    contracts(root);
    fs::remove_all(root);
    check(!fs::exists(root), "owned fixture directory removed");
    std::cout << "Windows atomic replacement: " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    if (!root.empty()) {
      std::error_code ignored;
      fs::remove_all(root, ignored);
    }
    std::cerr << error.what() << '\n';
    return 1;
  }
}
