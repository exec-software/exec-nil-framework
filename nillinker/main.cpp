/*
 * Copyright (C) 2008 - 2026 Execute Software.  All rights reserved.
 * SPDX-License-Identifier: MIT
 * See the LICENSE file in the project root for license terms.
 */
#include "nillinker.hpp"
#include "process_runner.hpp"
#include "secure_file_io.hpp"
#include "nil_version.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifndef NIL_NATIVE_LINKER_DRIVER
#define NIL_NATIVE_LINKER_DRIVER "c++"
#endif

#ifndef NY_NATIVE_INSTALL_LIBDIR
#define NY_NATIVE_INSTALL_LIBDIR "lib"
#endif

namespace {

namespace fs = std::filesystem;

constexpr int usage_error = 2;
constexpr std::size_t maximum_object_files = 4'096;
constexpr std::uint64_t maximum_object_bytes = 512ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t maximum_aggregate_object_bytes =
    4ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr std::uintmax_t maximum_runtime_archive_bytes =
    512ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t default_driver_timeout_ms = 300'000;
constexpr std::uint64_t minimum_driver_timeout_ms = 100;
constexpr std::uint64_t maximum_driver_timeout_ms = 3'600'000;
constexpr std::uint64_t maximum_symbol_index_bytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t maximum_archive_symbols = 1'000'000;
constexpr std::uint64_t maximum_archive_members = 65'536;
constexpr std::size_t maximum_symbol_index_interpretations = 16;
constexpr std::size_t archive_member_header_bytes = 60;
constexpr std::array<std::string_view, 3> required_runtime_symbols = {
    "nyrt_abi_version", "nyrt_context_begin", "nyrt_context_end"};

std::vector<const char *> runtime_archive_names() {
#ifdef _WIN32
  return {"nil.lib", "libnil.a"};
#else
  return {"libnil.a", "nil.lib"};
#endif
}

std::optional<fs::path> canonical_regular_file(const fs::path &path) {
  std::error_code error;
  if (!fs::is_regular_file(path, error) || error)
    return std::nullopt;
  auto canonical = fs::canonical(path, error);
  if (error)
    return std::nullopt;
  return canonical;
}

std::optional<fs::path> normalized_regular_file(const fs::path &path) {
  if (!ny::detail::valid_file_path(path))
    return std::nullopt;
  std::error_code error;
  const auto status = fs::symlink_status(path, error);
  if (error || !fs::is_regular_file(status))
    return std::nullopt;
  auto absolute = fs::absolute(path, error);
  if (error)
    return std::nullopt;
  return absolute.lexically_normal();
}

std::optional<std::string> process_path_argument(const fs::path &path) {
  try {
#ifdef _WIN32
    // process_runner consumes UTF-8 and performs the only UTF-8-to-UTF-16
    // conversion immediately before CreateProcessW.
    return path.u8string();
#else
    return path.string();
#endif
  } catch (...) {
    return std::nullopt;
  }
}

std::string display_path(const fs::path &path) {
  const auto encoded = process_path_argument(path);
  return encoded ? *encoded : "<unrepresentable path>";
}

std::optional<fs::path> cli_filesystem_path(const std::string &value) {
  try {
#ifdef _WIN32
    return fs::u8path(value);
#else
    return fs::path(value);
#endif
  } catch (...) {
    return std::nullopt;
  }
}

bool parse_cli_path(const std::string &value, std::string_view description,
                    fs::path &path, std::string &error) {
  auto parsed = cli_filesystem_path(value);
  if (!parsed) {
    error = std::string(description) + " is not a valid filesystem path";
    return false;
  }
  path = std::move(*parsed);
  return true;
}

class memory_input_buffer final : public std::streambuf {
public:
  explicit memory_input_buffer(const std::vector<char> &bytes)
      : size_(bytes.size()) {
    if (bytes.empty()) {
      setg(nullptr, nullptr, nullptr);
      return;
    }
    begin_ = const_cast<char *>(bytes.data());
    setg(begin_, begin_, begin_ + size_);
  }

protected:
  pos_type seekoff(off_type offset, std::ios_base::seekdir direction,
                   std::ios_base::openmode mode) override {
    if ((mode & std::ios_base::in) == 0)
      return pos_type(off_type(-1));
    std::streamoff base = 0;
    if (direction == std::ios_base::cur) {
      base = size_ == 0 ? 0 : gptr() - eback();
    } else if (direction == std::ios_base::end) {
      base = static_cast<std::streamoff>(size_);
    } else if (direction != std::ios_base::beg) {
      return pos_type(off_type(-1));
    }
    const std::streamoff target = base + offset;
    if (target < 0 ||
        static_cast<std::uint64_t>(target) >
            static_cast<std::uint64_t>(size_))
      return pos_type(off_type(-1));
    if (size_ == 0) {
      setg(nullptr, nullptr, nullptr);
    } else {
      setg(begin_, begin_ + target, begin_ + size_);
    }
    return pos_type(target);
  }

  pos_type seekpos(pos_type position,
                   std::ios_base::openmode mode) override {
    return seekoff(static_cast<off_type>(position), std::ios_base::beg, mode);
  }

private:
  char *begin_ = nullptr;
  std::size_t size_ = 0;
};

bool parse_decimal_field(const char *characters, std::size_t length,
                         std::uint64_t &value) {
  std::size_t first = 0;
  while (first < length && characters[first] == ' ')
    ++first;
  std::size_t last = length;
  while (last > first && characters[last - 1] == ' ')
    --last;
  if (first == last)
    return false;

  value = 0;
  for (std::size_t index = first; index < last; ++index) {
    const unsigned char character =
        static_cast<unsigned char>(characters[index]);
    if (character < '0' || character > '9')
      return false;
    const std::uint64_t digit = character - '0';
    if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U)
      return false;
    value = value * 10U + digit;
  }
  return true;
}

std::uint64_t read_unsigned(const std::vector<unsigned char> &bytes,
                            std::size_t offset, std::size_t width,
                            bool little_endian) {
  std::uint64_t value = 0;
  if (little_endian) {
    for (std::size_t index = 0; index < width; ++index)
      value |= static_cast<std::uint64_t>(bytes[offset + index])
               << (index * 8U);
  } else {
    for (std::size_t index = 0; index < width; ++index)
      value = (value << 8U) | bytes[offset + index];
  }
  return value;
}

struct archive_symbol_index {
  std::array<std::vector<std::uint64_t>, required_runtime_symbols.size()>
      definitions;
};

std::optional<std::size_t> required_symbol_index(std::string_view symbol) {
  // Mach-O and 32-bit COFF decorate external C identifiers with one leading
  // underscore. Archive indexes contain the decorated spelling.
  if (!symbol.empty() && symbol.front() == '_')
    symbol.remove_prefix(1);
  for (std::size_t index = 0; index < required_runtime_symbols.size(); ++index) {
    if (symbol == required_runtime_symbols[index])
      return index;
  }
  return std::nullopt;
}

std::optional<archive_symbol_index>
parse_sysv_symbol_index(const std::vector<unsigned char> &bytes,
                        std::size_t integer_width) {
  if ((integer_width != 4 && integer_width != 8) ||
      bytes.size() < integer_width)
    return std::nullopt;

  const std::uint64_t count =
      read_unsigned(bytes, 0, integer_width, false);
  const std::uint64_t maximum_count =
      (bytes.size() - integer_width) / integer_width;
  if (count > maximum_count || count > maximum_archive_symbols ||
      count > std::numeric_limits<std::size_t>::max())
    return std::nullopt;
  const std::size_t names_begin =
      integer_width + static_cast<std::size_t>(count) * integer_width;

  archive_symbol_index result;
  std::size_t cursor = names_begin;
  for (std::size_t index = 0; index < static_cast<std::size_t>(count); ++index) {
    const auto terminator =
        std::find(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                  bytes.end(), static_cast<unsigned char>(0));
    if (terminator == bytes.end())
      return std::nullopt;
    const std::size_t end =
        static_cast<std::size_t>(terminator - bytes.begin());
    const std::string_view symbol(
        reinterpret_cast<const char *>(bytes.data() + cursor), end - cursor);
    if (auto required = required_symbol_index(symbol)) {
      result.definitions[*required].push_back(
          read_unsigned(bytes, integer_width + index * integer_width,
                        integer_width, false));
    }
    cursor = end + 1;
  }
  return result;
}

std::optional<archive_symbol_index>
parse_bsd_symbol_index(const std::vector<unsigned char> &bytes,
                       std::size_t integer_width, bool little_endian) {
  if ((integer_width != 4 && integer_width != 8) ||
      bytes.size() < integer_width * 2U)
    return std::nullopt;

  const std::uint64_t ranlib_bytes =
      read_unsigned(bytes, 0, integer_width, little_endian);
  const std::uint64_t entry_bytes = integer_width * 2U;
  if (ranlib_bytes % entry_bytes != 0 ||
      ranlib_bytes > bytes.size() - integer_width)
    return std::nullopt;
  const std::size_t string_size_offset =
      integer_width + static_cast<std::size_t>(ranlib_bytes);
  if (string_size_offset > bytes.size() - integer_width)
    return std::nullopt;
  const std::uint64_t string_bytes = read_unsigned(
      bytes, string_size_offset, integer_width, little_endian);
  const std::size_t strings_begin = string_size_offset + integer_width;
  if (string_bytes > bytes.size() - strings_begin)
    return std::nullopt;
  const std::size_t strings_end =
      strings_begin + static_cast<std::size_t>(string_bytes);

  archive_symbol_index result;
  const std::size_t entry_count =
      static_cast<std::size_t>(ranlib_bytes / entry_bytes);
  if (entry_count > maximum_archive_symbols)
    return std::nullopt;
  for (std::size_t index = 0; index < entry_count; ++index) {
    const std::size_t entry = integer_width + index * entry_bytes;
    const std::uint64_t string_offset =
        read_unsigned(bytes, entry, integer_width, little_endian);
    if (string_offset >= string_bytes)
      return std::nullopt;
    const std::size_t symbol_begin =
        strings_begin + static_cast<std::size_t>(string_offset);
    const auto terminator = std::find(
        bytes.begin() + static_cast<std::ptrdiff_t>(symbol_begin),
        bytes.begin() + static_cast<std::ptrdiff_t>(strings_end),
        static_cast<unsigned char>(0));
    if (terminator ==
        bytes.begin() + static_cast<std::ptrdiff_t>(strings_end))
      return std::nullopt;
    const std::size_t symbol_end =
        static_cast<std::size_t>(terminator - bytes.begin());
    const std::string_view symbol(
        reinterpret_cast<const char *>(bytes.data() + symbol_begin),
        symbol_end - symbol_begin);
    if (auto required = required_symbol_index(symbol)) {
      result.definitions[*required].push_back(
          read_unsigned(bytes, entry + integer_width, integer_width,
                        little_endian));
    }
  }
  return result;
}

std::string normalized_archive_member_name(const std::array<char, 16> &raw) {
  std::string name(raw.data(), raw.size());
  while (!name.empty() && name.back() == ' ')
    name.pop_back();
  if (name.size() > 1 && name.back() == '/' && name != "//" &&
      name != "/SYM64/")
    name.pop_back();
  return name;
}

bool is_archive_index_name(const std::string &name) {
  return name == "/" || name == "/SYM64/" ||
         name.rfind("__.SYMDEF", 0) == 0;
}

bool index_defines_runtime(
    const archive_symbol_index &index,
    const std::set<std::uint64_t> &ordinary_member_offsets) {
  for (const auto &definitions : index.definitions) {
    if (std::none_of(definitions.begin(), definitions.end(),
                     [&](std::uint64_t offset) {
                       return ordinary_member_offsets.count(offset) != 0;
                     }))
      return false;
  }
  return true;
}

bool validate_runtime_archive(const std::vector<char> &bytes,
                              std::string &reason) {
  const std::uint64_t archive_size = bytes.size();
  if (archive_size <= 8) {
    reason = "archive is empty";
    return false;
  }
  if (archive_size > maximum_runtime_archive_bytes) {
    reason = "archive exceeds the 512 MiB validation limit";
    return false;
  }

  memory_input_buffer buffer(bytes);
  std::istream input(&buffer);
  std::array<char, 8> magic{};
  input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
  if (input.gcount() != static_cast<std::streamsize>(magic.size()) ||
      std::string(magic.data(), magic.size()) != "!<arch>\n") {
    reason = "archive does not use the supported GNU/BSD ar container";
    return false;
  }

  std::uint64_t position = magic.size();
  std::uint64_t member_count = 0;
  std::set<std::uint64_t> ordinary_member_offsets;
  std::vector<archive_symbol_index> indexes;
  while (position < archive_size) {
    if (++member_count > maximum_archive_members) {
      reason = "archive contains too many members";
      return false;
    }
    if (archive_size - position < archive_member_header_bytes) {
      reason = "archive has a truncated member header";
      return false;
    }
    input.seekg(static_cast<std::streamoff>(position), std::ios::beg);
    std::array<char, archive_member_header_bytes> header{};
    input.read(header.data(), static_cast<std::streamsize>(header.size()));
    if (input.gcount() != static_cast<std::streamsize>(header.size()) ||
        header[58] != '`' || header[59] != '\n') {
      reason = "archive has invalid member framing";
      return false;
    }

    std::uint64_t member_size = 0;
    if (!parse_decimal_field(header.data() + 48, 10, member_size)) {
      reason = "archive member has an invalid size";
      return false;
    }
    const std::uint64_t data_position = position + header.size();
    if (member_size > archive_size - data_position) {
      reason = "archive member extends beyond end of file";
      return false;
    }

    std::array<char, 16> raw_name{};
    std::copy_n(header.data(), raw_name.size(), raw_name.data());
    std::string member_name = normalized_archive_member_name(raw_name);
    std::uint64_t name_bytes = 0;
    if (member_name.rfind("#1/", 0) == 0) {
      if (!parse_decimal_field(member_name.data() + 3,
                               member_name.size() - 3, name_bytes) ||
          name_bytes == 0 || name_bytes > member_size || name_bytes > 65536) {
        reason = "archive has an invalid BSD extended member name";
        return false;
      }
      std::vector<char> extended_name(static_cast<std::size_t>(name_bytes));
      input.seekg(static_cast<std::streamoff>(data_position), std::ios::beg);
      input.read(extended_name.data(),
                 static_cast<std::streamsize>(extended_name.size()));
      if (input.gcount() != static_cast<std::streamsize>(extended_name.size())) {
        reason = "archive has a truncated BSD extended member name";
        return false;
      }
      while (!extended_name.empty() && extended_name.back() == '\0')
        extended_name.pop_back();
      member_name.assign(extended_name.begin(), extended_name.end());
      if (member_name.size() > 1 && member_name.back() == '/')
        member_name.pop_back();
    }

    const std::uint64_t payload_position = data_position + name_bytes;
    const std::uint64_t payload_size = member_size - name_bytes;
    const bool is_index = is_archive_index_name(member_name);
    const bool is_metadata = is_index || member_name == "//";
    if (!is_metadata && payload_size != 0)
      ordinary_member_offsets.insert(position);

    if (is_index && payload_size <= maximum_symbol_index_bytes) {
      std::vector<unsigned char> payload(static_cast<std::size_t>(payload_size));
      input.seekg(static_cast<std::streamoff>(payload_position), std::ios::beg);
      if (!payload.empty()) {
        input.read(reinterpret_cast<char *>(payload.data()),
                   static_cast<std::streamsize>(payload.size()));
      }
      if ((!payload.empty() &&
           input.gcount() != static_cast<std::streamsize>(payload.size())) ||
          input.bad()) {
        reason = "archive has a truncated symbol index";
        return false;
      }

      if (member_name == "/") {
        if (auto parsed = parse_sysv_symbol_index(payload, 4)) {
          if (indexes.size() >= maximum_symbol_index_interpretations) {
            reason = "archive contains too many symbol indexes";
            return false;
          }
          indexes.push_back(std::move(*parsed));
        }
      } else if (member_name == "/SYM64/") {
        if (auto parsed = parse_sysv_symbol_index(payload, 8)) {
          if (indexes.size() >= maximum_symbol_index_interpretations) {
            reason = "archive contains too many symbol indexes";
            return false;
          }
          indexes.push_back(std::move(*parsed));
        }
      } else {
        const std::array<std::size_t, 2> widths = {4, 8};
        for (const std::size_t width : widths) {
          for (const bool little_endian : {true, false}) {
            if (auto parsed =
                    parse_bsd_symbol_index(payload, width, little_endian)) {
              if (indexes.size() >= maximum_symbol_index_interpretations) {
                reason = "archive contains too many symbol indexes";
                return false;
              }
              indexes.push_back(std::move(*parsed));
            }
          }
        }
      }
    }

    position = data_position + member_size;
    if ((member_size & 1U) != 0) {
      if (position >= archive_size) {
        reason = "archive is missing member alignment padding";
        return false;
      }
      input.seekg(static_cast<std::streamoff>(position), std::ios::beg);
      char padding = 0;
      input.read(&padding, 1);
      if (input.gcount() != 1 || padding != '\n') {
        reason = "archive has invalid member alignment padding";
        return false;
      }
      ++position;
    }
  }

  if (ordinary_member_offsets.empty()) {
    reason = "archive contains no nonempty object members";
    return false;
  }
  if (std::none_of(indexes.begin(), indexes.end(),
                   [&](const archive_symbol_index &index) {
                     return index_defines_runtime(index,
                                                  ordinary_member_offsets);
                   })) {
    reason = "archive symbol index does not define the required NIL runtime ABI";
    return false;
  }
  reason.clear();
  return true;
}

bool read_and_validate_runtime_archive(const fs::path &path,
                                       std::string &reason) {
  const auto read = ny::detail::read_regular_file_bounded(
      path, maximum_runtime_archive_bytes);
  if (!read.ok) {
    reason = "cannot read archive securely: " + read.error;
    return false;
  }
  return validate_runtime_archive(read.bytes, reason);
}

void append_runtime_names(std::vector<fs::path> &candidates,
                          const fs::path &directory) {
  if (directory.empty())
    return;
  for (const char *name : runtime_archive_names()) {
    const fs::path candidate = (directory / name).lexically_normal();
    if (std::find(candidates.begin(), candidates.end(), candidate) ==
        candidates.end())
      candidates.push_back(candidate);
  }
}

std::vector<fs::path>
runtime_candidates_under(const fs::path &directory) {
  std::vector<fs::path> candidates;
  append_runtime_names(candidates, directory);
  append_runtime_names(candidates, directory / "native");
  append_runtime_names(candidates, directory / "nillib" / "native");
  return candidates;
}

std::vector<fs::path>
default_runtime_candidates(const fs::path &executable_path) {
  std::vector<fs::path> candidates;
  const fs::path directory = executable_path.parent_path();

  // The sealed SDK contract is authoritative. The direct and native forms
  // retain compatibility with flat staging/install layouts.
  append_runtime_names(candidates, directory / "nillib" / "native");
  append_runtime_names(candidates, directory);
  append_runtime_names(candidates, directory / "native");

  if (directory.filename() == "bin") {
    const fs::path prefix = directory.parent_path();
    append_runtime_names(candidates, prefix / "nillib" / "native");
    const fs::path configured = NY_NATIVE_INSTALL_LIBDIR;
    append_runtime_names(candidates, configured.is_absolute()
                                         ? configured
                                         : prefix / configured);
    append_runtime_names(candidates, prefix / "lib");
  } else if (directory.parent_path().filename() == "bin") {
    const fs::path build_root = directory.parent_path().parent_path();
    append_runtime_names(candidates, build_root / "nillib" / "native");
    const fs::path configured = NY_NATIVE_INSTALL_LIBDIR;
    if (!configured.is_absolute()) {
      append_runtime_names(candidates,
                           build_root / configured / directory.filename());
      append_runtime_names(candidates, build_root / configured);
    }
    append_runtime_names(candidates,
                         build_root / "lib" / directory.filename());
    append_runtime_names(candidates, build_root / "lib");
  }
  return candidates;
}

std::optional<fs::path> resolve_runtime_argument(const fs::path &requested,
                                                 std::string &error_message) {
  if (!ny::detail::valid_file_path(requested)) {
    error_message = "NIL runtime path is empty or contains a null byte";
    return std::nullopt;
  }
  std::error_code status_error;
  const auto status = fs::status(requested, status_error);
  if (status_error) {
    error_message = "cannot inspect NIL runtime path '" + display_path(requested) +
                    "': " + status_error.message();
    return std::nullopt;
  }

  if (fs::is_regular_file(status)) {
    auto runtime = normalized_regular_file(requested);
    if (!runtime) {
      error_message = "NIL runtime archive is not a no-follow regular file: " +
                      display_path(requested);
      return std::nullopt;
    }
    std::string validation_error;
    if (!read_and_validate_runtime_archive(*runtime, validation_error)) {
      error_message = "NIL runtime archive is invalid: " + display_path(*runtime) +
                      ": " + validation_error;
      return std::nullopt;
    }
    return runtime;
  }

  if (!fs::is_directory(status)) {
    error_message = "NIL runtime path is neither an archive nor a directory: " +
                    display_path(requested);
    return std::nullopt;
  }

  for (const auto &candidate : runtime_candidates_under(requested)) {
    auto runtime = normalized_regular_file(candidate);
    if (runtime) {
      std::string validation_error;
      if (read_and_validate_runtime_archive(*runtime, validation_error))
        return runtime;
    }
  }
  error_message = "no NIL runtime archive was found under '" +
                  display_path(requested) + "'";
  return std::nullopt;
}

std::optional<fs::path> find_executable_on_path(const fs::path &name) {
  if (name.empty())
    return std::nullopt;
  const char *raw_path = std::getenv("PATH");
  if (!raw_path)
    return std::nullopt;
#ifdef _WIN32
  constexpr char separator = ';';
#else
  constexpr char separator = ':';
#endif
  std::istringstream entries(raw_path);
  std::string entry;
  while (std::getline(entries, entry, separator)) {
    fs::path candidate = (entry.empty() ? fs::path(".") : fs::path(entry)) /
                         name;
    if (auto resolved = canonical_regular_file(candidate))
      return resolved;
#ifdef _WIN32
    if (!candidate.has_extension()) {
      candidate += ".exe";
      if (auto resolved = canonical_regular_file(candidate))
        return resolved;
    }
#endif
  }
  return std::nullopt;
}

std::optional<fs::path> resolve_executable_path(const char *invocation) {
#ifdef _WIN32
  std::vector<wchar_t> buffer(32768);
  const DWORD length = GetModuleFileNameW(
      nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (length > 0 && length < buffer.size()) {
    if (auto resolved = canonical_regular_file(fs::path(
            std::wstring(buffer.data(), static_cast<std::size_t>(length)))))
      return resolved;
  }
#elif defined(__linux__)
  std::error_code proc_error;
  const auto proc_path = fs::read_symlink("/proc/self/exe", proc_error);
  if (!proc_error && !proc_path.empty()) {
    if (auto resolved = canonical_regular_file(proc_path))
      return resolved;
  }
#elif defined(__APPLE__)
  uint32_t path_size = 0;
  if (_NSGetExecutablePath(nullptr, &path_size) == -1 && path_size > 0) {
    std::vector<char> buffer(path_size);
    if (_NSGetExecutablePath(buffer.data(), &path_size) == 0) {
      if (auto resolved = canonical_regular_file(buffer.data()))
        return resolved;
    }
  }
#endif

  const fs::path invoked(invocation ? invocation : "");
  if (invoked.has_parent_path())
    return canonical_regular_file(invoked);
  return find_executable_on_path(invoked);
}

std::uint16_t read_u16(const unsigned char *bytes, bool little_endian) {
  if (little_endian)
    return static_cast<std::uint16_t>(bytes[0]) |
           (static_cast<std::uint16_t>(bytes[1]) << 8U);
  return (static_cast<std::uint16_t>(bytes[0]) << 8U) |
         static_cast<std::uint16_t>(bytes[1]);
}

#ifdef __APPLE__
std::uint32_t read_u32(const unsigned char *bytes, bool little_endian) {
  std::uint32_t value = 0;
  for (unsigned index = 0; index < 4; ++index) {
    const unsigned source = little_endian ? index : 3U - index;
    value |= static_cast<std::uint32_t>(bytes[source]) << (8U * index);
  }
  return value;
}
#endif

bool validate_host_object(const std::vector<char> &bytes, const fs::path &path,
                          std::string &error_message) {
  std::array<unsigned char, 64> header{};
  const std::size_t bytes_read = std::min(bytes.size(), header.size());
  std::copy_n(reinterpret_cast<const unsigned char *>(bytes.data()), bytes_read,
              header.data());

#if defined(__linux__)
  if (bytes_read < 20 || header[0] != 0x7f || header[1] != 'E' ||
      header[2] != 'L' || header[3] != 'F') {
    error_message = "input is not an ELF host object: " + display_path(path);
    return false;
  }
  if (header[4] != 1 && header[4] != 2) {
    error_message = "ELF object has an unsupported address class: " +
                    display_path(path);
    return false;
  }
  if ((sizeof(void *) == 8 && header[4] != 2) ||
      (sizeof(void *) == 4 && header[4] != 1)) {
    error_message = "ELF object address class does not match this nillinker: " +
                    display_path(path);
    return false;
  }
  if (header[5] != 1 && header[5] != 2) {
    error_message = "ELF object has an unsupported byte order: " +
                    display_path(path);
    return false;
  }
  const bool little_endian = header[5] == 1;
  if (read_u16(header.data() + 16, little_endian) != 1) {
    error_message =
        "ELF input is not a relocatable object: " + display_path(path);
    return false;
  }
#if defined(__x86_64__)
  constexpr std::uint16_t expected_machine = 62;
#elif defined(__aarch64__)
  constexpr std::uint16_t expected_machine = 183;
#elif defined(__riscv)
  constexpr std::uint16_t expected_machine = 243;
#else
  constexpr std::uint16_t expected_machine = 0;
#endif
  if (expected_machine != 0 &&
      read_u16(header.data() + 18, little_endian) != expected_machine) {
    error_message = "ELF object architecture does not match this nillinker: " +
                    display_path(path);
    return false;
  }
#elif defined(__APPLE__)
  if (bytes_read < 20) {
    error_message = "input is too short to be a Mach-O host object: " +
                    display_path(path);
    return false;
  }
  const std::uint32_t magic = read_u32(header.data(), true);
  const bool little_endian = magic == 0xfeedfaceU || magic == 0xfeedfacfU;
  const bool big_endian = magic == 0xcefaedfeU || magic == 0xcffaedfeU;
  if (!little_endian && !big_endian) {
    error_message =
        "input is not a Mach-O host object: " + display_path(path);
    return false;
  }
  if (read_u32(header.data() + 12, little_endian) != 1U) {
    error_message = "Mach-O input is not a relocatable object: " +
                    display_path(path);
    return false;
  }
#elif defined(_WIN32)
  if (bytes_read < 20) {
    error_message = "input is too short to be a COFF host object: " +
                    display_path(path);
    return false;
  }
  const std::uint16_t machine = read_u16(header.data(), true);
  const std::uint16_t sections = read_u16(header.data() + 2, true);
  const std::uint16_t optional_header = read_u16(header.data() + 16, true);
  const bool known_machine = machine == 0x014cU || machine == 0x8664U ||
                             machine == 0xaa64U || machine == 0x01c0U ||
                             machine == 0x01c4U;
  const bool big_object = machine == 0 && sections == 0xffffU;
  if ((!known_machine || sections == 0 || optional_header != 0) &&
      !big_object) {
    error_message =
        "input is not a relocatable COFF object: " + display_path(path);
    return false;
  }
#else
  if (bytes_read <= 0) {
    error_message = "host object is empty: " + display_path(path);
    return false;
  }
#endif
  return true;
}

class remove_directory_on_exit {
public:
  explicit remove_directory_on_exit(fs::path path) : path_(std::move(path)) {}
  remove_directory_on_exit(const remove_directory_on_exit &) = delete;
  remove_directory_on_exit &operator=(const remove_directory_on_exit &) = delete;
  ~remove_directory_on_exit() {
    if (!path_.empty()) {
      std::error_code ignored;
      fs::remove_all(path_, ignored);
    }
  }

private:
  fs::path path_;
};

std::optional<fs::path>
create_private_staging_directory(const fs::path &output,
                                 std::error_code &error) {
  fs::path parent = output.parent_path();
  if (parent.empty())
    parent = ".";

  std::error_code parent_error;
  if (!fs::is_directory(parent, parent_error) || parent_error) {
    error = parent_error ? parent_error
                         : std::make_error_code(std::errc::not_a_directory);
    return std::nullopt;
  }

  try {
    std::random_device random_source;
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
      std::ostringstream suffix;
      suffix << ".ny-link-";
      for (unsigned word = 0; word < 4; ++word) {
        suffix << std::hex << std::setfill('0') << std::setw(8)
               << static_cast<std::uint32_t>(random_source());
      }
      const fs::path candidate = parent / suffix.str();
#ifdef _WIN32
      if (CreateDirectoryW(candidate.c_str(), nullptr)) {
        error.clear();
        return candidate;
      }
      const DWORD native_error = GetLastError();
      if (native_error == ERROR_ALREADY_EXISTS ||
          native_error == ERROR_FILE_EXISTS)
        continue;
      error = std::error_code(static_cast<int>(native_error),
                              std::system_category());
      return std::nullopt;
#else
      if (::mkdir(candidate.c_str(), S_IRWXU) == 0) {
        fs::permissions(candidate, fs::perms::owner_all,
                        fs::perm_options::replace, error);
        const auto status = fs::symlink_status(candidate, parent_error);
        if (error || parent_error || !fs::is_directory(status) ||
            (status.permissions() & fs::perms::all) != fs::perms::owner_all) {
          if (!error)
            error = parent_error
                        ? parent_error
                        : std::make_error_code(std::errc::permission_denied);
          std::error_code ignored;
          fs::remove(candidate, ignored);
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

bool secure_executable(const fs::path &path, std::error_code &error) {
  const auto initial = fs::symlink_status(path, error);
  if (error || !fs::is_regular_file(initial)) {
    if (!error)
      error = std::make_error_code(std::errc::invalid_argument);
    return false;
  }
  const auto size = fs::file_size(path, error);
  if (error || size == 0) {
    if (!error)
      error = std::make_error_code(std::errc::io_error);
    return false;
  }
#ifndef _WIN32
  fs::permissions(path, fs::perms::owner_all, fs::perm_options::replace, error);
  if (error)
    return false;
  const auto secured = fs::symlink_status(path, error);
  if (error || !fs::is_regular_file(secured) ||
      (secured.permissions() & fs::perms::all) != fs::perms::owner_all) {
    if (!error)
      error = std::make_error_code(std::errc::permission_denied);
    return false;
  }
#endif
  error.clear();
  return true;
}

bool replace_output_file(const fs::path &staging, const fs::path &output,
                         std::error_code &error) {
#ifdef _WIN32
  if (MoveFileExW(staging.c_str(), output.c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    error.clear();
    return true;
  }
  error =
      std::error_code(static_cast<int>(GetLastError()), std::system_category());
  return false;
#else
  fs::rename(staging, output, error);
  return !error;
#endif
}

void print_usage(std::ostream &out) {
  out << "Usage: nillinker [options] object...\n"
      << "\nOptions:\n"
      << "  -o, --output <path>   Write the host executable (default: "
#ifdef _WIN32
      << "a.exe"
#else
      << "a.out"
#endif
      << ")\n"
      << "  --runtime <path>      NIL runtime archive, SDK root, or runtime "
         "directory\n"
      << "  --driver-timeout-ms <milliseconds>\n"
         "                        Bound the host driver to 100..3600000 ms "
         "(default: 300000)\n"
      << "  --version             Show the nillinker version\n"
      << "  -h, --help            Show this help\n"
      << "  --                    Treat remaining arguments as object paths\n";
}

struct command_line {
  fs::path output =
#ifdef _WIN32
      "a.exe";
#else
      "a.out";
#endif
  std::optional<fs::path> runtime;
  std::vector<fs::path> objects;
  std::chrono::milliseconds driver_timeout{default_driver_timeout_ms};
  bool output_was_set = false;
  bool runtime_was_set = false;
  bool driver_timeout_was_set = false;
};

bool option_value(int argc, char **argv, int &index, const std::string &option,
                  std::string &value, std::string &error) {
  if (index + 1 >= argc) {
    error = option + " requires a path argument";
    return false;
  }
  value = argv[++index];
  if (value.empty()) {
    error = option + " requires a non-empty path argument";
    return false;
  }
  return true;
}

bool parse_driver_timeout(const std::string &value,
                          std::chrono::milliseconds &timeout,
                          std::string &error) {
  std::uint64_t parsed = 0;
  const auto result = std::from_chars(value.data(),
                                      value.data() + value.size(), parsed);
  if (value.empty() || result.ec != std::errc{} ||
      result.ptr != value.data() + value.size() ||
      parsed < minimum_driver_timeout_ms ||
      parsed > maximum_driver_timeout_ms) {
    error = "driver timeout must be an integer from 100 through 3600000 "
            "milliseconds";
    return false;
  }
  timeout = std::chrono::milliseconds(parsed);
  return true;
}

std::optional<command_line> parse_command_line(int argc, char **argv,
                                               std::string &error) {
  command_line options;
  bool positional_only = false;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (!positional_only && argument == "--") {
      positional_only = true;
      continue;
    }
    if (!positional_only && (argument == "-h" || argument == "--help" ||
                             argument == "--version")) {
      error = argument + " must be used by itself";
      return std::nullopt;
    }
    if (!positional_only && (argument == "-o" || argument == "--output")) {
      if (options.output_was_set) {
        error = "output path was specified more than once";
        return std::nullopt;
      }
      std::string value;
      if (!option_value(argc, argv, index, argument, value, error))
        return std::nullopt;
      if (!parse_cli_path(value, "output path", options.output, error))
        return std::nullopt;
      options.output_was_set = true;
      continue;
    }
    if (!positional_only && argument.rfind("--output=", 0) == 0) {
      if (options.output_was_set) {
        error = "output path was specified more than once";
        return std::nullopt;
      }
      const std::string value = argument.substr(9);
      if (value.empty()) {
        error = "--output requires a non-empty path argument";
        return std::nullopt;
      }
      if (!parse_cli_path(value, "output path", options.output, error))
        return std::nullopt;
      options.output_was_set = true;
      continue;
    }
    if (!positional_only && argument == "--runtime") {
      if (options.runtime_was_set) {
        error = "NIL runtime path was specified more than once";
        return std::nullopt;
      }
      std::string value;
      if (!option_value(argc, argv, index, argument, value, error))
        return std::nullopt;
      fs::path runtime;
      if (!parse_cli_path(value, "NIL runtime path", runtime, error))
        return std::nullopt;
      options.runtime = std::move(runtime);
      options.runtime_was_set = true;
      continue;
    }
    if (!positional_only && argument.rfind("--runtime=", 0) == 0) {
      if (options.runtime_was_set) {
        error = "NIL runtime path was specified more than once";
        return std::nullopt;
      }
      const std::string value = argument.substr(10);
      if (value.empty()) {
        error = "--runtime requires a non-empty path argument";
        return std::nullopt;
      }
      fs::path runtime;
      if (!parse_cli_path(value, "NIL runtime path", runtime, error))
        return std::nullopt;
      options.runtime = std::move(runtime);
      options.runtime_was_set = true;
      continue;
    }
    if (!positional_only && argument == "--driver-timeout-ms") {
      if (options.driver_timeout_was_set) {
        error = "driver timeout was specified more than once";
        return std::nullopt;
      }
      std::string value;
      if (!option_value(argc, argv, index, argument, value, error) ||
          !parse_driver_timeout(value, options.driver_timeout, error))
        return std::nullopt;
      options.driver_timeout_was_set = true;
      continue;
    }
    if (!positional_only &&
        argument.rfind("--driver-timeout-ms=", 0) == 0) {
      if (options.driver_timeout_was_set) {
        error = "driver timeout was specified more than once";
        return std::nullopt;
      }
      if (!parse_driver_timeout(argument.substr(20), options.driver_timeout,
                                error))
        return std::nullopt;
      options.driver_timeout_was_set = true;
      continue;
    }
    if (!positional_only && !argument.empty() && argument.front() == '-') {
      error = "unknown option '" + argument + "'";
      return std::nullopt;
    }
    if (options.objects.size() >= maximum_object_files) {
      error = "host object count exceeds the limit of " +
              std::to_string(maximum_object_files);
      return std::nullopt;
    }
    fs::path object;
    if (!parse_cli_path(argument, "host object path", object, error))
      return std::nullopt;
    options.objects.push_back(std::move(object));
  }
  if (options.objects.empty()) {
    error = "at least one host object is required";
    return std::nullopt;
  }
  if (options.output.empty()) {
    error = "output path must not be empty";
    return std::nullopt;
  }
  return options;
}

} // namespace

namespace ny {

Linker::Linker(std::string driver, fs::path executable_path,
               std::chrono::milliseconds driver_timeout)
    : driver_(std::move(driver)), executable_path_(std::move(executable_path)),
      driver_timeout_(driver_timeout) {}

bool Linker::add_object_file(const fs::path &object_path) {
  error_.clear();
  if (object_files_.size() >= maximum_object_files) {
    error_ = "host object count exceeds the limit of " +
             std::to_string(maximum_object_files);
    return false;
  }
  auto object = normalized_regular_file(object_path);
  if (!object) {
    error_ = "host object does not exist or is not a no-follow regular file: " +
             display_path(object_path);
    return false;
  }
  if (std::find(object_files_.begin(), object_files_.end(), *object) !=
      object_files_.end()) {
    error_ =
        "host object was specified more than once: " + display_path(*object);
    return false;
  }
  std::error_code size_error;
  const auto size = fs::file_size(*object, size_error);
  if (size_error || size == 0) {
    error_ = size_error
                 ? "cannot inspect host object '" + display_path(*object) +
                       "': " + size_error.message()
                 : "host object is empty: " + display_path(*object);
    return false;
  }
  if (size > maximum_object_bytes) {
    error_ = "host object exceeds the 512 MiB per-object limit: " +
             display_path(*object);
    return false;
  }
  const auto size_u64 = static_cast<std::uint64_t>(size);
  if (size_u64 > maximum_aggregate_object_bytes - admitted_object_bytes_) {
    error_ = "host objects exceed the 4 GiB aggregate limit";
    return false;
  }
  admitted_object_bytes_ += size_u64;
  object_files_.push_back(std::move(*object));
  return true;
}

bool Linker::set_runtime(const fs::path &runtime_path) {
  error_.clear();
  auto runtime = resolve_runtime_argument(runtime_path, error_);
  if (!runtime)
    return false;
  runtime_ = std::move(*runtime);
  return true;
}

std::optional<fs::path> Linker::find_default_runtime() {
  if (executable_path_.empty()) {
    error_ = "cannot locate nillinker to discover the adjacent NIL runtime; "
             "pass --runtime <archive-or-sdk>";
    return std::nullopt;
  }
  const fs::path directory = executable_path_.parent_path();
  for (const auto &candidate : default_runtime_candidates(executable_path_)) {
    auto runtime = normalized_regular_file(candidate);
    if (runtime) {
      std::string validation_error;
      if (read_and_validate_runtime_archive(*runtime, validation_error))
        return runtime;
    }
  }
  error_ = "NIL runtime archive was not found beside nillinker under '" +
           display_path(directory) +
           "'; pass --runtime <archive-or-sdk> or install a complete NIL SDK";
  return std::nullopt;
}

bool Linker::link(const fs::path &requested_output) {
  error_.clear();
  if (object_files_.empty()) {
    error_ = "at least one validated host object is required";
    return false;
  }
  if (driver_.empty()) {
    error_ = "native linker driver is not configured";
    return false;
  }
  if (driver_timeout_ < std::chrono::milliseconds(minimum_driver_timeout_ms) ||
      driver_timeout_ > std::chrono::milliseconds(maximum_driver_timeout_ms)) {
    error_ = "driver timeout must be from 100 through 3600000 milliseconds";
    return false;
  }
  if (!ny::detail::valid_file_path(requested_output)) {
    error_ = "output path is empty or contains a null byte";
    return false;
  }
  if (!runtime_) {
    runtime_ = find_default_runtime();
    if (!runtime_)
      return false;
  }

  std::error_code output_error;
  fs::path output = fs::absolute(requested_output, output_error);
  if (output_error) {
    error_ = "cannot resolve output path '" + display_path(requested_output) +
             "': " + output_error.message();
    return false;
  }
  output = output.lexically_normal();
  if (output.filename().empty()) {
    error_ = "output path must name a file: " + display_path(output);
    return false;
  }
#ifdef _WIN32
  // Match the host compiler convention used elsewhere in the NIL toolchain.
  // An explicitly supplied extension is preserved (for example, `tool.com`).
  if (!output.has_extension())
    output += ".exe";
#endif

  const auto output_status = fs::symlink_status(output, output_error);
  if (output_error && output_error != std::errc::no_such_file_or_directory) {
    error_ = "cannot inspect output path '" + display_path(output) + "': " +
             output_error.message();
    return false;
  }
  output_error.clear();
  if (fs::is_directory(output_status)) {
    error_ = "output path is a directory: " + display_path(output);
    return false;
  }
  if (fs::exists(output_status) && !fs::is_regular_file(output_status) &&
      !fs::is_symlink(output_status)) {
    error_ =
        "refusing to replace a special output entry: " + display_path(output);
    return false;
  }
  if (fs::is_regular_file(output_status)) {
    for (const auto &object : object_files_) {
      if (fs::equivalent(object, output, output_error) && !output_error) {
        error_ = "refusing to replace an input object with the linked output: " +
                 display_path(output);
        return false;
      }
      output_error.clear();
    }
    if (fs::equivalent(*runtime_, output, output_error) && !output_error) {
      error_ = "refusing to replace the NIL runtime archive with the linked "
               "output: " +
               display_path(output);
      return false;
    }
    output_error.clear();
  }

  auto staging_directory =
      create_private_staging_directory(output, output_error);
  if (!staging_directory) {
    error_ = "cannot create private output staging directory beside '" +
             display_path(output) + "': " + output_error.message();
    return false;
  }
  remove_directory_on_exit cleanup(*staging_directory);
  const fs::path staged_output =
      *staging_directory /
#ifdef _WIN32
      "program.exe";
#else
      "program";
#endif

  std::vector<fs::path> staged_objects;
  staged_objects.reserve(object_files_.size());
  std::uint64_t captured_object_bytes = 0;
  for (std::size_t index = 0; index < object_files_.size(); ++index) {
    const std::uint64_t remaining =
        maximum_aggregate_object_bytes - captured_object_bytes;
    const std::uint64_t read_limit = std::min(maximum_object_bytes, remaining);
    const auto snapshot = ny::detail::read_regular_file_bounded(
        object_files_[index], read_limit);
    if (!snapshot.ok) {
      if (snapshot.error.find("exceeds maximum_bytes") != std::string::npos &&
          remaining < maximum_object_bytes) {
        error_ = "host objects exceed the 4 GiB aggregate limit while "
                 "capturing immutable inputs";
      } else {
        error_ = "cannot capture host object securely '" +
                 display_path(object_files_[index]) + "': " + snapshot.error;
      }
      return false;
    }
    if (snapshot.bytes.empty()) {
      error_ = "host object is empty: " + display_path(object_files_[index]);
      return false;
    }
    if (!validate_host_object(snapshot.bytes, object_files_[index], error_))
      return false;

    const fs::path staged_object =
        *staging_directory /
        ("object-" + std::to_string(index) +
#ifdef _WIN32
         ".obj"
#else
         ".o"
#endif
        );
    const auto publish =
        ny::detail::write_file_atomic(staged_object, snapshot.bytes, false);
    if (!publish.ok) {
      error_ = "cannot stage immutable host object '" +
               display_path(object_files_[index]) + "': " + publish.error;
      return false;
    }
    captured_object_bytes += snapshot.bytes.size();
    staged_objects.push_back(staged_object);
  }

  auto runtime_snapshot = ny::detail::read_regular_file_bounded(
      *runtime_, maximum_runtime_archive_bytes);
  if (!runtime_snapshot.ok) {
    error_ = "cannot capture NIL runtime archive securely '" +
             display_path(*runtime_) + "': " + runtime_snapshot.error;
    return false;
  }
  std::string runtime_validation_error;
  if (!validate_runtime_archive(runtime_snapshot.bytes,
                                runtime_validation_error)) {
    error_ = "NIL runtime archive changed or is invalid: " +
             display_path(*runtime_) + ": " + runtime_validation_error;
    return false;
  }
  const fs::path staged_runtime =
      *staging_directory /
#ifdef _WIN32
      "nil.lib";
#else
      "libnil.a";
#endif
  const auto runtime_publish = ny::detail::write_file_atomic(
      staged_runtime, runtime_snapshot.bytes, false);
  if (!runtime_publish.ok) {
    error_ = "cannot stage immutable NIL runtime archive: " +
             runtime_publish.error;
    return false;
  }
  std::vector<char>().swap(runtime_snapshot.bytes);

  std::vector<std::string> arguments;
  arguments.push_back(driver_);
  const auto staged_output_argument = process_path_argument(staged_output);
  const auto staged_runtime_argument = process_path_argument(staged_runtime);
  if (!staged_output_argument || !staged_runtime_argument) {
    error_ = "cannot encode native linker staging paths as UTF-8";
    return false;
  }
  std::vector<std::string> staged_object_arguments;
  staged_object_arguments.reserve(staged_objects.size());
  for (const auto &object : staged_objects) {
    auto argument = process_path_argument(object);
    if (!argument) {
      error_ = "cannot encode native linker staging paths as UTF-8";
      return false;
    }
    staged_object_arguments.push_back(std::move(*argument));
  }
#ifdef NILLINKER_MSVC_DRIVER
  arguments.push_back("/nologo");
  for (const auto &object : staged_object_arguments)
    arguments.push_back(object);
  arguments.push_back(*staged_runtime_argument);
  arguments.push_back("/Fe:" + *staged_output_argument);
#else
#ifndef _WIN32
  arguments.push_back("-pthread");
#endif
#if defined(__linux__)
  arguments.push_back("-no-pie");
#elif defined(__APPLE__)
  arguments.push_back("-Wl,-no_pie");
#endif
  arguments.push_back("-o");
  arguments.push_back(*staged_output_argument);
  for (const auto &object : staged_object_arguments)
    arguments.push_back(object);
  arguments.push_back(*staged_runtime_argument);
#if defined(__linux__)
  arguments.push_back("-ldl");
#endif
#endif

  const auto process =
      ny::linker_detail::run_process(arguments, driver_timeout_);
  if (!process.launched) {
    error_ = "cannot launch native linker driver '" + driver_ + "': " +
             process.error;
    return false;
  }
  if (process.timed_out) {
    error_ = process.error.empty()
                 ? "native linker driver timed out after " +
                       std::to_string(driver_timeout_.count()) + " ms"
                 : "native " + process.error;
    return false;
  }
  if (process.exit_code != 0) {
    error_ = "native linker driver failed with exit code " +
             std::to_string(process.exit_code);
    if (!process.error.empty())
      error_ += ": " + process.error;
    return false;
  }

  if (!secure_executable(staged_output, output_error)) {
    error_ = "native linker driver did not produce a secure executable: " +
             output_error.message();
    return false;
  }
  if (!replace_output_file(staged_output, output, output_error)) {
    error_ = "cannot atomically publish linked executable to '" +
             display_path(output) + "': " + output_error.message();
    return false;
  }
  std::cout << "Linked " << object_files_.size() << " host object"
            << (object_files_.size() == 1 ? "" : "s") << " to "
            << display_path(output) << '\n';
  return true;
}

} // namespace ny

int run_linker_cli(int argc, char **argv) {
  if (argc == 2) {
    const std::string argument = argv[1];
    if (argument == "-h" || argument == "--help") {
      print_usage(std::cout);
      return 0;
    }
    if (argument == "--version") {
      std::cout << "nillinker " << NIL_VERSION << '\n';
      return 0;
    }
  }

  std::string error;
  auto options = parse_command_line(argc, argv, error);
  if (!options) {
    std::cerr << "nillinker: error: " << error << '\n';
    print_usage(std::cerr);
    return usage_error;
  }

  const auto executable = resolve_executable_path(argc > 0 ? argv[0] : nullptr);
  ny::Linker linker(NIL_NATIVE_LINKER_DRIVER,
                    executable.value_or(fs::path{}),
                    options->driver_timeout);
  if (options->runtime && !linker.set_runtime(*options->runtime)) {
    std::cerr << "nillinker: error: " << linker.error() << '\n';
    return 1;
  }
  for (std::size_t index = 0; index < options->objects.size(); ++index) {
    if (!linker.add_object_file(options->objects[index])) {
      std::cerr << "nillinker: error: " << linker.error() << '\n';
      if (!options->output_was_set && index + 1 == options->objects.size() &&
          options->objects.size() > 1) {
        std::cerr << "nillinker: note: positional output paths are unsupported; "
                     "use -o <path>\n";
      }
      return 1;
    }
  }
  if (!linker.link(options->output)) {
    std::cerr << "nillinker: error: " << linker.error() << '\n';
    return 1;
  }
  return 0;
}

#ifdef _WIN32

namespace {

class local_argument_list {
public:
  explicit local_argument_list(LPWSTR *arguments) : arguments_(arguments) {}
  ~local_argument_list() {
    if (arguments_ != nullptr)
      LocalFree(arguments_);
  }
  local_argument_list(const local_argument_list &) = delete;
  local_argument_list &operator=(const local_argument_list &) = delete;

private:
  LPWSTR *arguments_;
};

std::optional<std::string> strict_utf8_argument(const wchar_t *argument) {
  if (argument == nullptr)
    return std::nullopt;
  const std::wstring_view wide(argument);
  if (wide.empty())
    return std::string{};
  if (wide.size() > static_cast<std::size_t>(
                        (std::numeric_limits<int>::max)()))
    return std::nullopt;
  const int wide_size = static_cast<int>(wide.size());
  const int utf8_size = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), wide_size, nullptr, 0,
      nullptr, nullptr);
  if (utf8_size <= 0)
    return std::nullopt;
  std::string utf8(static_cast<std::size_t>(utf8_size), '\0');
  const int converted = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), wide_size, utf8.data(),
      utf8_size, nullptr, nullptr);
  if (converted != utf8_size)
    return std::nullopt;
  return utf8;
}

int run_linker_windows_cli() {
  int wide_argument_count = 0;
  LPWSTR *wide_arguments =
      CommandLineToArgvW(GetCommandLineW(), &wide_argument_count);
  if (wide_arguments == nullptr || wide_argument_count <= 0) {
    if (wide_arguments != nullptr)
      LocalFree(wide_arguments);
    std::cerr << "nillinker: error: cannot read the Windows command line\n";
    return 1;
  }
  local_argument_list cleanup(wide_arguments);

  std::vector<std::string> utf8_arguments;
  utf8_arguments.reserve(static_cast<std::size_t>(wide_argument_count));
  for (int index = 0; index < wide_argument_count; ++index) {
    auto converted = strict_utf8_argument(wide_arguments[index]);
    if (!converted) {
      std::cerr << "nillinker: error: Windows command line argument " << index
                << " is not valid Unicode\n";
      return usage_error;
    }
    utf8_arguments.push_back(std::move(*converted));
  }

  std::vector<char *> argv;
  argv.reserve(utf8_arguments.size() + 1);
  for (auto &argument : utf8_arguments)
    argv.push_back(argument.data());
  argv.push_back(nullptr);
  return run_linker_cli(wide_argument_count, argv.data());
}

} // namespace

#endif

int main(int argc, char **argv) {
  try {
#ifdef _WIN32
    (void)argc;
    (void)argv;
    return run_linker_windows_cli();
#else
    return run_linker_cli(argc, argv);
#endif
  } catch (const std::exception &error) {
    std::cerr << "nillinker: error: unexpected failure: " << error.what()
              << '\n';
    return 1;
  }
}
