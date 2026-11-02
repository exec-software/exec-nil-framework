#include "native_runtime.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {
namespace fs = std::filesystem;
using inventory = std::map<std::string, std::pair<bool, std::string>>;

void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

class context_scope {
public:
  context_scope() { nyrt_context_begin(0, nullptr); }
  ~context_scope() { nyrt_context_end(); }
};

class cwd_scope {
public:
  explicit cwd_scope(const fs::path &directory) : previous_(fs::current_path()) {
    fs::current_path(directory);
  }
  ~cwd_scope() {
    std::error_code ignored;
    fs::current_path(previous_, ignored);
  }
private:
  fs::path previous_;
};

fs::path make_private_directory() {
  const auto parent = fs::temp_directory_path();
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  for (int attempt = 0; attempt < 128; ++attempt) {
    const auto candidate = parent / ("nil-utf8-file-paths-" + std::to_string(stamp) +
                                     "-" + std::to_string(attempt));
    std::error_code error;
    if (fs::create_directory(candidate, error))
      return fs::absolute(candidate);
    if (error && error != std::errc::file_exists)
      throw std::runtime_error("cannot create private file-path fixture");
  }
  throw std::runtime_error("cannot allocate unique file-path fixture");
}

void host_write(const fs::path &path, const std::string &bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  require(static_cast<bool>(output), "host could not create fixture file");
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  output.close();
  require(static_cast<bool>(output), "host could not finish fixture file");
}

std::string host_read(const fs::path &path) {
  require(fs::file_size(path) <= 4096, "unexpectedly large fixture file");
  std::ifstream input(path, std::ios::binary);
  require(static_cast<bool>(input), "host could not read fixture file");
  std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  require(!input.bad(), "host file read failed");
  return bytes;
}

inventory host_inventory(const fs::path &root) {
  inventory result;
  for (const auto &entry : fs::recursive_directory_iterator(root)) {
    require(result.size() < 256, "unexpected fixture namespace growth");
    const auto status = entry.symlink_status();
    const auto name = entry.path().lexically_relative(root).generic_u8string();
    if (fs::is_directory(status))
      result.emplace(name, std::make_pair(true, std::string{}));
    else {
      require(fs::is_regular_file(status), "fixture contains a nonregular entry");
      result.emplace(name, std::make_pair(false, host_read(entry.path())));
    }
  }
  return result;
}

void check_inventory(const fs::path &root, const inventory &expected) {
  require(host_inventory(root) == expected,
          "host oracle found changed filenames, file bytes, or staging orphans");
}

void directory(const fs::path &root, const std::string &name, inventory &expected) {
  require(fs::create_directory(root / fs::u8path(name)), "fixture directory already exists");
  expected.emplace(name, std::make_pair(true, std::string{}));
}

void file(const fs::path &root, const std::string &name, const std::string &bytes,
          inventory &expected) {
  host_write(root / fs::u8path(name), bytes);
  require(expected.emplace(name, std::make_pair(false, bytes)).second,
          "duplicate fixture filename");
}

void *array(const std::string &bytes) {
  auto *result = nyrt_array_new(NYRT_ARRAY_U8, static_cast<std::int64_t>(bytes.size()));
  require(result != nullptr, "runtime could not allocate U8 array");
  for (std::size_t index = 0; index < bytes.size(); ++index)
    require(nyrt_array_set(result, NYRT_ARRAY_U8, static_cast<std::int64_t>(index),
                          static_cast<unsigned char>(bytes[index])) == 1,
            "runtime could not initialize U8 array");
  return result;
}

void array_equals(const void *value, const std::string &bytes) {
  require(nyrt_array_length(value, NYRT_ARRAY_U8) == static_cast<std::int64_t>(bytes.size()),
          "runtime read returned wrong array length");
  for (std::size_t index = 0; index < bytes.size(); ++index)
    require(nyrt_array_get(value, NYRT_ARRAY_U8, static_cast<std::int64_t>(index)) ==
                static_cast<unsigned char>(bytes[index]), "runtime read returned wrong bytes");
}

void redacted(const char *error, const std::string &path) {
  require(error != nullptr && error[0] != '\0', "failed operation returned no error");
  const std::string message(error);
  require(message.size() <= 512 && message.find(path) == std::string::npos &&
              message.find("secret-") == std::string::npos &&
              message.find("nil-utf8-file-paths-") == std::string::npos,
          "file operation error exposed its path");
  for (const unsigned char byte : message)
    require(byte < 128, "file operation error exposed non-ASCII path bytes");
}

std::string spelling(const fs::path &root, const std::string &name, bool absolute) {
  return absolute ? (root / fs::u8path(name)).u8string() : name;
}

void valid_paths(const fs::path &root, inventory &expected) {
  const std::vector<std::string> names = {
      u8"caf\u00e9", u8"\u65e5\u672c\u8a9e", u8"\U0001f98a", u8"cafe\u0301"};
  for (int absolute = 0; absolute < 2; ++absolute) {
    const std::string parent = std::string(absolute ? "absolute-" : "relative-") +
                               u8"caf\u00e9-\u4e2d-\U0001f98a";
    directory(root, parent, expected);
    for (std::size_t index = 0; index < names.size(); ++index) {
      // No root frames are registered: the native embedding context owns these
      // unmanaged-call results until context_end, as required by the ABI.
      context_scope context;
      const std::string base = parent + "/secret-" + names[index];
      const auto input = base + ".input", created = base + ".new";
      const auto protected_file = base + ".protected", replaced = base + ".replace";
      std::string payload;
      for (unsigned byte = 0; byte < 256; ++byte)
        payload.push_back(static_cast<char>((byte + index * 17) % 256));
      std::string replacement = payload;
      replacement.front() = 42;
      replacement.back() = 7;
      const std::string sentinel("host-protected\0\xff", 16);
      file(root, input, payload, expected);
      file(root, protected_file, sentinel, expected);
      file(root, replaced, "host-original", expected);
      check_inventory(root, expected);

      const auto input_path = spelling(root, input, absolute != 0);
      void *read = nullptr;
      const char *error = nullptr;
      require(nyrt_read_regular_bytes(input_path.c_str(), 256, &read, &error) == 1 && error == nullptr,
              "runtime could not read host-created exact Unicode filename");
      array_equals(read, payload);
      const auto created_path = spelling(root, created, absolute != 0);
      require(nyrt_write_bytes_atomic(created_path.c_str(), read, 0, &error) == 1 && error == nullptr,
              "runtime could not create exact Unicode filename");
      expected.emplace(created, std::make_pair(false, payload));
      check_inventory(root, expected);

      const auto protected_path = spelling(root, protected_file, absolute != 0);
      require(nyrt_write_bytes_atomic(protected_path.c_str(), read, 0, &error) == 0,
              "runtime no-clobber accepted an existing Unicode filename");
      redacted(error, protected_path);
      check_inventory(root, expected);

      const auto replaced_path = spelling(root, replaced, absolute != 0);
      require(nyrt_write_bytes_atomic(replaced_path.c_str(), array(replacement), 1, &error) == 1 &&
                  error == nullptr, "runtime could not replace exact Unicode filename");
      expected.at(replaced).second = replacement;
      check_inventory(root, expected);
    }
  }
}

void append_utf8(std::string &text, unsigned scalar) {
  if (scalar < 0x80) text.push_back(static_cast<char>(scalar));
  else if (scalar < 0x800) {
    text.push_back(static_cast<char>(0xc0 | (scalar >> 6)));
    text.push_back(static_cast<char>(0x80 | (scalar & 0x3f)));
  } else {
    text.push_back(static_cast<char>(0xe0 | (scalar >> 12)));
    text.push_back(static_cast<char>(0x80 | ((scalar >> 6) & 0x3f)));
    text.push_back(static_cast<char>(0x80 | (scalar & 0x3f)));
  }
}

// Independent legacy-1252 aliases make an incorrect ACP conversion observable
// even when the machine's default is not 1252. No process codepage is changed.
std::string cp1252_alias(const std::string &bytes) {
  const unsigned control[] = {
      0x20ac, 0x81, 0x201a, 0x192, 0x201e, 0x2026, 0x2020, 0x2021,
      0x2c6, 0x2030, 0x160, 0x2039, 0x152, 0x8d, 0x17d, 0x8f,
      0x90, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
      0x2dc, 0x2122, 0x161, 0x203a, 0x153, 0x9d, 0x17e, 0x178};
  std::string result;
  for (const unsigned char byte : bytes)
    append_utf8(result, byte >= 0x80 && byte <= 0x9f ? control[byte - 0x80] : byte);
  return result;
}

void malformed_paths(const fs::path &root, inventory &expected) {
  directory(root, "malformed", expected);
  const std::vector<std::string> invalid = {
      "\x80", "\xc0\xaf", "\xe0\x80\xaf", "\xe2\x82",
      "\xed\xa0\x80", "\xed\xbf\xbf", "\xf4\x90\x80\x80", "\xf5\x80\x80\x80"};
  for (std::size_t index = 0; index < invalid.size(); ++index) {
    const auto prefix = "malformed/secret-invalid-" + std::to_string(index) + "-";
    const auto name = prefix + invalid[index] + ".bin";
    require(name.find('\0') == std::string::npos, "malformed fixture accidentally contains NUL");
    file(root, prefix + cp1252_alias(invalid[index]) + ".bin", "host-mojibake-sentinel", expected);
    std::string replacement;
    for (std::size_t count = 1; count <= invalid[index].size(); ++count) {
      replacement += u8"\ufffd";
      file(root, prefix + replacement + ".bin", "host-replacement-sentinel", expected);
    }
#ifndef _WIN32
    // POSIX accepts these raw filename bytes. A successful read must still be
    // rejected by the language UTF-8 boundary, not by a missing-file accident.
    host_write(root / fs::path(name), "host-raw-byte-sentinel");
    expected.emplace(name, std::make_pair(false, std::string("host-raw-byte-sentinel")));
#endif
    for (int absolute = 0; absolute < 2; ++absolute) {
      context_scope context;
      // Do not pass malformed bytes through fs::u8path: the runtime, not the
      // host C++ path constructor, must be the component rejecting this input.
      const auto path = absolute ? root.generic_u8string() + "/" + name : name;
      auto *bytes = array("attempted replacement");
      void *read = reinterpret_cast<void *>(std::uintptr_t{1});
      const char *error = nullptr;
      const auto read_result = nyrt_read_regular_bytes(path.c_str(), 256, &read, &error);
      check_inventory(root, expected);
      require(read_result == 0 && read == nullptr, "runtime accepted a malformed UTF-8 read path");
      redacted(error, path);
      require(std::string(error) == "file.read_regular_bytes: invalid path",
              "malformed read path failed for a reason other than UTF-8 validation");
      for (const int replace : {0, 1}) {
        // The no-clobber route must reject a new malformed name, not merely
        // fail because one of the deliberately precreated aliases exists.
        const auto write_path = replace ? path : path + ".new";
        const auto write_result = nyrt_write_bytes_atomic(write_path.c_str(), bytes, replace, &error);
        check_inventory(root, expected);
        require(write_result == 0, "runtime accepted a malformed UTF-8 write path");
        redacted(error, write_path);
        require(std::string(error) == "file.write_bytes_atomic: invalid destination path",
                "malformed write path failed for a reason other than UTF-8 validation");
      }
    }
  }
}
} // namespace

int main(int argc, char **argv) {
  fs::path root;
  try {
#ifdef _WIN32
    const auto acp = GetACP();
    std::cout << "Windows GetACP=" << acp << '\n';
#ifdef NIL_EXPECT_UTF8_ACP
    require(acp == CP_UTF8, "manifested test process must use UTF-8 ACP");
#endif
    if (argc == 2 && std::string(argv[1]) == "--expect-acp=1252")
      require(acp == 1252, "manual legacy-ACP control requires GetACP=1252");
    else
      require(argc == 1, "unexpected probe argument");
#else
    (void)argv;
    require(argc == 1, "unexpected probe argument");
#endif
    root = make_private_directory();
    {
      cwd_scope cwd(root);
      inventory expected;
      valid_paths(root, expected);
      malformed_paths(root, expected);
      check_inventory(root, expected);
    }
    // Only the exact private directory created by this process is removed.
    // Failures leave all evidence intact instead of hiding the broken namespace.
    std::error_code error;
    const auto removed = fs::remove_all(root, error);
    require(!error && removed > 0 && !fs::exists(root), "private fixture cleanup failed");
    std::cout << "UTF-8 file path ABI: 8 valid cases and 16 malformed path cases passed; cleanup verified\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "UTF-8 file path ABI: " << error.what() << '\n';
    if (!root.empty())
      std::cerr << "Private fixture retained: " << root.u8string() << '\n';
    return 1;
  }
}
