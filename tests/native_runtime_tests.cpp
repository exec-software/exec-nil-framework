#include "native_runtime.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace {

bool expect(bool condition, const char *message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

bool expect_text(const char *actual, const std::string &expected,
                 const char *message) {
  return expect(actual != nullptr && actual == expected, message);
}

bool expect_bytes(const void *value,
                  std::initializer_list<std::uint64_t> expected,
                  const char *message) {
  if (nyrt_array_length(value, NYRT_ARRAY_U8) !=
      static_cast<std::int64_t>(expected.size())) {
    return expect(false, message);
  }
  std::int64_t index = 0;
  for (const std::uint64_t byte : expected) {
    if (nyrt_array_get(value, NYRT_ARRAY_U8, index) != byte)
      return expect(false, message);
    ++index;
  }
  return true;
}

void *make_bytes(std::initializer_list<std::uint64_t> values) {
  void *result = nyrt_array_new(NYRT_ARRAY_U8, 0);
  if (result == nullptr)
    return nullptr;
  for (const std::uint64_t value : values) {
    if (nyrt_array_push(result, NYRT_ARRAY_U8, value) == 0)
      return nullptr;
  }
  return result;
}

class temporary_directory {
public:
  temporary_directory() {
    std::error_code error;
    const auto parent = std::filesystem::temp_directory_path(error);
    if (error)
      throw std::runtime_error("could not resolve the temporary directory");

    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 128; ++attempt) {
      const auto candidate =
          parent / ("ny-native-runtime-" + std::to_string(stamp) + "-" +
                    std::to_string(attempt));
      error.clear();
      if (std::filesystem::create_directory(candidate, error)) {
        path_ = candidate;
        return;
      }
      if (error && error != std::errc::file_exists)
        break;
    }
    throw std::runtime_error("could not create a temporary directory");
  }

  ~temporary_directory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

bool error_is_redacted(const char *error, const std::filesystem::path &path) {
  if (error == nullptr)
    return false;
  const std::string message(error);
  return message.find(path.string()) == std::string::npos &&
         message.find(path.filename().string()) == std::string::npos;
}

void check_substring_contract(bool &ok) {
  const auto maximum = std::numeric_limits<std::int64_t>::max();
  const auto minimum = std::numeric_limits<std::int64_t>::min();
  ok &= expect(nyrt_text_substr(nullptr, 0, 1) == nullptr,
               "null substring receiver did not fail closed");
  ok &= expect_text(nyrt_text_substr("abcdef", 2, 3), "cde",
                    "substring did not select exact bytes");
  ok &= expect_text(nyrt_text_substr("abcdef", minimum, 2), "ab",
                    "negative substring start was not clamped");
  ok &= expect_text(nyrt_text_substr("abcdef", 2, maximum), "cdef",
                    "maximum substring count overflowed instead of clipping");
  ok &= expect_text(nyrt_text_substr("abcdef", maximum, maximum), "",
                    "maximum substring start did not return empty");
  ok &= expect_text(nyrt_text_substr("abcdef", 2, minimum), "",
                    "negative substring count was not clamped");
  ok &= expect_text(nyrt_text_substr("", 0, maximum), "",
                    "empty text substring failed");
  ok &= expect_text(nyrt_text_substr("abcdef", 6, 1), "",
                    "substring at the end did not return empty");
  const std::string utf8 = "\xc3\xa9!";
  ok &= expect_text(nyrt_text_substr(utf8.c_str(), 1, 1), utf8.substr(1, 1),
                    "substring unexpectedly used scalar instead of byte offsets");
}

void check_array_contract(bool &ok) {
  ok &= expect(nyrt_array_new(999, 0) == nullptr,
               "unknown array kind was accepted");
  ok &= expect(nyrt_array_new(NYRT_ARRAY_U8, -1) == nullptr,
               "negative array length was accepted");
  ok &= expect(nyrt_array_new(NYRT_ARRAY_I64,
                              std::numeric_limits<std::int64_t>::max()) ==
                   nullptr,
               "overflowing array allocation was accepted");

  void *forged = reinterpret_cast<void *>(std::uintptr_t{1});
  ok &= expect(nyrt_array_length(forged, NYRT_ARRAY_U8) == -1 &&
                   nyrt_array_get(forged, NYRT_ARRAY_U8, 0) == 0 &&
                   nyrt_array_set(forged, NYRT_ARRAY_U8, 0, 1) == 0 &&
                   nyrt_array_push(forged, NYRT_ARRAY_U8, 1) == 0,
               "forged array descriptor did not fail closed");

  void *bytes = make_bytes({0, 127, 128, 255});
  ok &= expect(bytes != nullptr, "could not allocate byte array");
  ok &= expect_bytes(bytes, {0, 127, 128, 255},
                     "u8 edge values did not round trip");
  ok &= expect(nyrt_array_length(bytes, NYRT_ARRAY_I32) == -1 &&
                   nyrt_array_push(bytes, NYRT_ARRAY_I32, 1) == 0,
               "wrong array kind was accepted");
  ok &= expect(nyrt_array_set(bytes, NYRT_ARRAY_U8, -1, 1) == 0 &&
                   nyrt_array_set(bytes, NYRT_ARRAY_U8, 4, 1) == 0,
               "out-of-bounds byte mutation reported success");
  ok &= expect(nyrt_array_set(bytes, NYRT_ARRAY_U8, 0, 256) == 1 &&
                   nyrt_array_get(bytes, NYRT_ARRAY_U8, 0) == 0,
               "u8 storage did not truncate to eight bits");
  ok &= expect(nyrt_array_set(bytes, NYRT_ARRAY_U8, 0, 0) == 1,
               "in-bounds byte mutation failed");

  void *filled = nyrt_array_new_with_value(NYRT_ARRAY_U8, 3, 511);
  ok &= expect_bytes(filled, {255, 255, 255},
                     "filled byte array did not use u8 semantics");
  void *booleans = nyrt_array_new_with_value(NYRT_ARRAY_BOOL, 2, 42);
  ok &= expect(nyrt_array_get(booleans, NYRT_ARRAY_BOOL, 0) == 1 &&
                   nyrt_array_set(booleans, NYRT_ARRAY_BOOL, 1, 0) == 1 &&
                   nyrt_array_get(booleans, NYRT_ARRAY_BOOL, 1) == 0,
               "bool array values were not canonicalized");

  void *words = nyrt_array_new(NYRT_ARRAY_I32, 0);
  void *alias = words;
  for (std::int32_t value = 0; value < 1024; ++value) {
    ok &= expect(nyrt_array_push(words, NYRT_ARRAY_I32,
                                 static_cast<std::uint64_t>(value)) == 1,
                 "word push failed");
  }
  ok &=
      expect(alias == words && nyrt_array_length(alias, NYRT_ARRAY_I32) == 1024,
             "array growth invalidated its stable handle");
  ok &= expect(nyrt_array_get(alias, NYRT_ARRAY_I32, 1000) == 1000,
               "array alias did not observe grown contents");
  ok &=
      expect(nyrt_array_push(words, NYRT_ARRAY_I32,
                             std::numeric_limits<std::uint64_t>::max()) == 1 &&
                 nyrt_array_get(words, NYRT_ARRAY_I32, 1024) ==
                     std::numeric_limits<std::uint64_t>::max(),
             "i32 storage did not preserve signed low-bit semantics");

  void *wide = nyrt_array_new(NYRT_ARRAY_I64, 0);
  const std::uint64_t minimum_i64_bits = std::uint64_t{1} << 63U;
  ok &= expect(nyrt_array_push(wide, NYRT_ARRAY_I64, minimum_i64_bits) == 1 &&
                   nyrt_array_get(wide, NYRT_ARRAY_I64, 0) == minimum_i64_bits,
               "i64 storage did not preserve its exact bit pattern");

  void *fractional = nyrt_array_new_with_value(
      NYRT_ARRAY_F64, 2, 0x3ff4000000000000ULL); // 1.25, not integer 1.
  ok &= expect(nyrt_array_get(fractional, NYRT_ARRAY_F64, 1) ==
                   0x3ff4000000000000ULL &&
                   nyrt_array_set(fractional, NYRT_ARRAY_F64, 0,
                                  0x8000000000000000ULL) == 1 &&
                   nyrt_array_get(fractional, NYRT_ARRAY_F64, 0) ==
                       0x8000000000000000ULL,
               "f64 arrays did not preserve fractional/negative-zero bits");
  ok &= expect(nyrt_array_length(fractional, NYRT_ARRAY_I64) == -1 &&
                   nyrt_array_set(fractional, NYRT_ARRAY_I64, 0, 1) == 0 &&
                   nyrt_array_set(fractional, NYRT_ARRAY_F64, 2, 1) == 0 &&
                   nyrt_array_get(fractional, NYRT_ARRAY_F64, 2) == 0,
               "f64 array kind/bounds validation failed");
  const char *text = nyrt_i32_to_text(321);
  const auto text_bits = static_cast<std::uint64_t>(
      reinterpret_cast<std::uintptr_t>(text));
  void *texts = nyrt_array_new_with_value(NYRT_ARRAY_TEXT, 2, text_bits);
  void *text_alias = texts;
  for (int index = 0; index < 80; ++index)
    ok &= expect(nyrt_array_push(texts, NYRT_ARRAY_TEXT, text_bits) == 1,
                 "text array growth failed");
  ok &= expect(text_alias == texts &&
                   nyrt_array_length(text_alias, NYRT_ARRAY_TEXT) == 82 &&
                   nyrt_array_get(text_alias, NYRT_ARRAY_TEXT, 81) == text_bits,
               "text array growth lost stable handle or exact text address");
  ok &= expect_text(reinterpret_cast<const char *>(static_cast<std::uintptr_t>(
                       nyrt_array_get(text_alias, NYRT_ARRAY_TEXT, 0))),
                     "321", "text lifetime was shorter than its array");
  ok &= expect(nyrt_array_set(texts, NYRT_ARRAY_F64, 0, 1) == 0 &&
                   nyrt_array_set(texts, NYRT_ARRAY_TEXT, -1, 1) == 0 &&
                   nyrt_array_get(texts, NYRT_ARRAY_TEXT, 82) == 0,
               "text array kind/bounds validation failed");
  ok &= expect(nyrt_buffer_new(NYRT_ARRAY_F64, 1) == nullptr &&
                   nyrt_buffer_new(NYRT_ARRAY_TEXT, 1) == nullptr,
               "new array kinds expanded the raw-pointer ABI unexpectedly");
}

void check_buffer_contract(bool &ok) {
  ok &= expect(nyrt_buffer_new(999, 1) == nullptr,
               "unknown buffer kind was accepted");
  ok &= expect(nyrt_buffer_new(NYRT_ARRAY_U8, -1) == nullptr,
               "negative buffer length was accepted");
  ok &= expect(nyrt_buffer_new(NYRT_ARRAY_I64,
                               std::numeric_limits<std::int64_t>::max()) ==
                   nullptr,
               "overflowing buffer allocation was accepted");
  const auto guard_overflow_length =
      static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) /
      sizeof(std::int64_t);
  if (guard_overflow_length <=
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    ok &= expect(
        nyrt_buffer_new(NYRT_ARRAY_I64, static_cast<std::int64_t>(
                                            guard_overflow_length)) == nullptr,
        "buffer allocation without room for its guard element was accepted");
  }

  void *forged = reinterpret_cast<void *>(std::uintptr_t{1});
  ok &= expect(nyrt_buffer_get(forged, NYRT_ARRAY_U8, 0) == 0 &&
                   nyrt_buffer_set(forged, NYRT_ARRAY_U8, 0, 1) == 0 &&
                   nyrt_buffer_free(forged) == 0,
               "forged raw buffer did not fail closed");

  auto *bytes = static_cast<std::uint8_t *>(nyrt_buffer_new(NYRT_ARRAY_U8, 4));
  ok &= expect(bytes != nullptr, "could not allocate packed byte buffer");
  if (bytes != nullptr) {
    const std::uint64_t values[] = {0, 127, 128, 255};
    for (std::int64_t index = 0; index < 4; ++index) {
      ok &= expect(
          nyrt_buffer_set(bytes, NYRT_ARRAY_U8, index, values[index]) == 1,
          "could not write packed byte buffer");
    }
    ok &= expect(bytes[0] == 0 && bytes[1] == 127 && bytes[2] == 128 &&
                     bytes[3] == 255,
                 "ptr<u8> did not expose four contiguous ABI bytes");
    ok &= expect(nyrt_buffer_get(bytes, NYRT_ARRAY_U8, 2) == 128,
                 "packed byte buffer read changed its value");
    ok &= expect(nyrt_buffer_set(bytes, NYRT_ARRAY_U8, 0, 256) == 1 &&
                     nyrt_buffer_get(bytes, NYRT_ARRAY_U8, 0) == 0 &&
                     nyrt_buffer_set(bytes, NYRT_ARRAY_U8, 0, 511) == 1 &&
                     nyrt_buffer_get(bytes, NYRT_ARRAY_U8, 0) == 255,
                 "packed byte buffer did not use low-eight-bit u8 semantics");
    ok &= expect(nyrt_buffer_get(bytes, NYRT_ARRAY_I32, 0) == 0 &&
                     nyrt_buffer_set(bytes, NYRT_ARRAY_I32, 0, 1) == 0,
                 "packed byte buffer accepted the wrong element kind");
    ok &= expect(nyrt_buffer_get(bytes, NYRT_ARRAY_U8, -1) == 0 &&
                     nyrt_buffer_get(bytes, NYRT_ARRAY_U8, 4) == 0 &&
                     nyrt_buffer_set(bytes, NYRT_ARRAY_U8, 4, 1) == 0,
                 "packed byte buffer bounds did not fail closed");
  }

  auto *booleans =
      static_cast<std::uint8_t *>(nyrt_buffer_new(NYRT_ARRAY_BOOL, 2));
  ok &= expect(booleans != nullptr &&
                   nyrt_buffer_set(booleans, NYRT_ARRAY_BOOL, 0, 42) == 1 &&
                   booleans[0] == 1 &&
                   nyrt_buffer_get(booleans, NYRT_ARRAY_BOOL, 0) == 1,
               "ptr<bool> did not use one-byte canonical storage");

  auto *words = static_cast<std::int32_t *>(nyrt_buffer_new(NYRT_ARRAY_I32, 2));
  const std::uint64_t minimum_i32_bits =
      static_cast<std::uint64_t>(static_cast<std::int64_t>(INT32_MIN));
  ok &= expect(
      words != nullptr &&
          nyrt_buffer_set(words, NYRT_ARRAY_I32, 0, minimum_i32_bits) == 1 &&
          nyrt_buffer_set(words, NYRT_ARRAY_I32, 1, INT32_MAX) == 1 &&
          words[0] == INT32_MIN && words[1] == INT32_MAX &&
          nyrt_buffer_get(words, NYRT_ARRAY_I32, 0) == minimum_i32_bits,
      "ptr<i32> did not use packed signed 32-bit storage");

  auto *second_word = static_cast<std::int32_t *>(
      nyrt_buffer_offset(words, NYRT_ARRAY_I32, 1, 0));
  ok &= expect(second_word == words + 1 &&
                   nyrt_buffer_get(second_word, NYRT_ARRAY_I32, 0) ==
                       static_cast<std::uint64_t>(INT32_MAX) &&
                   nyrt_buffer_get(second_word, NYRT_ARRAY_I32, -1) ==
                       minimum_i32_bits &&
                   nyrt_buffer_set(second_word, NYRT_ARRAY_I32, -1, 42) == 1 &&
                   words[0] == 42,
               "aligned interior ptr<i32> operations lost their base offset");
  ok &= expect(
      nyrt_buffer_offset(second_word, NYRT_ARRAY_I32, 1, 1) == words &&
          nyrt_buffer_offset(words, NYRT_ARRAY_I32, 2, 0) == words + 2 &&
          nyrt_buffer_get(words + 2, NYRT_ARRAY_I32, 0) == 0 &&
          nyrt_buffer_offset(words, NYRT_ARRAY_I32, 3, 0) == nullptr &&
          nyrt_buffer_offset(words, NYRT_ARRAY_I32, INT64_MIN, 0) == nullptr &&
          nyrt_buffer_offset(words, NYRT_ARRAY_I32, INT64_MIN, 1) == nullptr &&
          nyrt_buffer_free(words + 1) == 0,
      "raw pointer offset bounds or base-only release failed");

  auto *word_end = static_cast<std::int32_t *>(
      nyrt_buffer_offset(words, NYRT_ARRAY_I32, 2, 0));
  auto *neighbor =
      static_cast<std::int32_t *>(nyrt_buffer_new(NYRT_ARRAY_I32, 2));
  ok &= expect(
      word_end != nullptr && neighbor != nullptr && word_end != neighbor &&
          nyrt_buffer_offset(word_end, NYRT_ARRAY_I32, 1, 1) == words + 1 &&
          nyrt_buffer_get(word_end, NYRT_ARRAY_I32, -1) ==
              static_cast<std::uint64_t>(INT32_MAX),
      "a live buffer base aliased another buffer's logical one-past pointer");

  auto *wide = static_cast<std::uint64_t *>(nyrt_buffer_new(NYRT_ARRAY_I64, 1));
  const std::uint64_t wide_bits = std::uint64_t{1} << 63U;
  ok &= expect(wide != nullptr &&
                   nyrt_buffer_set(wide, NYRT_ARRAY_I64, 0, wide_bits) == 1 &&
                   wide[0] == wide_bits &&
                   nyrt_buffer_get(wide, NYRT_ARRAY_I64, 0) == wide_bits,
               "ptr<i64> did not preserve its exact 64-bit layout");

  ok &= expect(nyrt_buffer_free(bytes) == 1 && nyrt_buffer_free(bytes) == 0 &&
                   nyrt_buffer_get(bytes, NYRT_ARRAY_U8, 0) == 0 &&
                   nyrt_buffer_offset(bytes, NYRT_ARRAY_U8, 0, 0) == nullptr,
               "raw buffer release did not reject a stale pointer");
  void *replacement = nyrt_buffer_new(NYRT_ARRAY_U8, 4);
  ok &= expect(replacement != nullptr && replacement != bytes &&
                   nyrt_buffer_get(bytes, NYRT_ARRAY_U8, 0) == 0,
               "raw buffer tombstone allowed ABA pointer resurrection");
}

void check_conversion_contract(bool &ok) {
  struct success_case {
    const char *text;
    std::int32_t expected;
  };
  const success_case successes[] = {{"0", 0},
                                    {"+0", 0},
                                    {"-0", 0},
                                    {"2147483647", INT32_MAX},
                                    {"-2147483648", INT32_MIN}};
  for (const auto &test : successes) {
    std::int32_t parsed = 1;
    const char *error = reinterpret_cast<const char *>(std::uintptr_t{1});
    ok &= expect(nyrt_try_i32(test.text, &parsed, &error) == 1 &&
                     parsed == test.expected && error == nullptr,
                 "valid i32 conversion did not match interpreter semantics");
  }

  struct failure_case {
    const char *text;
    const char *expected_error;
  };
  const failure_case failures[] = {
      {"", "convert.try_to_i32: value is empty"},
      {"+", "convert.try_to_i32: value has no digits"},
      {" 1",
       "convert.try_to_i32: value contains a digit outside the requested base"},
      {"2147483648", "convert.try_to_i32: value is outside the i32 range"},
      {"-2147483649", "convert.try_to_i32: value is outside the i32 range"},
      {"9223372036854775808",
       "convert.try_to_i32: value is outside the i64 range"},
      {"-9223372036854775809",
       "convert.try_to_i32: value is outside the i64 range"},
  };
  for (const auto &test : failures) {
    std::int32_t parsed = 7;
    const char *error = nullptr;
    ok &= expect(nyrt_try_i32(test.text, &parsed, &error) == 0 && parsed == 0 &&
                     error != nullptr &&
                     error == std::string(test.expected_error),
                 "invalid i32 conversion did not match interpreter semantics");
  }

  const char *error = nullptr;
  ok &= expect(nyrt_try_i32(nullptr, nullptr, &error) == 0 && error != nullptr,
               "null conversion outputs did not fail closed");
  ok &= expect_text(nyrt_i32_to_text(INT32_MIN), "-2147483648",
                    "minimum i32 formatting failed");
  ok &= expect_text(nyrt_i32_to_text(INT32_MAX), "2147483647",
                    "maximum i32 formatting failed");
  ok &= expect_text(nyrt_i64_to_text(INT64_MIN), "-9223372036854775808",
                    "minimum i64 formatting failed");
  ok &= expect_text(nyrt_i64_to_text(INT64_MAX), "9223372036854775807",
                    "maximum i64 formatting failed");
}

void check_file_contract(bool &ok) {
  try {
    temporary_directory temporary;
    const auto destination = temporary.path() / "secret-customer-alpha.bin";
    const std::string destination_text = destination.string();

    void *original = make_bytes({0, 127, 128, 255});
    const char *error = nullptr;
    ok &= expect(original != nullptr &&
                     nyrt_write_bytes_atomic(destination_text.c_str(), original,
                                             0, &error) == 1 &&
                     error == nullptr,
                 "initial atomic byte write failed");

#ifndef _WIN32
    struct stat status {};
    ok &= expect(::stat(destination_text.c_str(), &status) == 0 &&
                     (status.st_mode & 0777) == 0600,
                 "atomic byte write did not publish a private file");
#endif

    void *read = nullptr;
    ok &= expect(nyrt_read_regular_bytes(destination_text.c_str(), 4, &read,
                                         &error) == 1 &&
                     error == nullptr,
                 "bounded regular-file read failed");
    ok &= expect_bytes(read, {0, 127, 128, 255},
                       "secure byte round trip changed content");

    read = reinterpret_cast<void *>(std::uintptr_t{1});
    ok &= expect(nyrt_read_regular_bytes(destination_text.c_str(), 3, &read,
                                         &error) == 0 &&
                     read == nullptr && error_is_redacted(error, destination),
                 "bounded read did not reject and redact an oversized input");

    void *replacement = make_bytes({9, 0, 255});
    ok &= expect(nyrt_write_bytes_atomic(destination_text.c_str(), replacement,
                                         0, &error) == 0 &&
                     error_is_redacted(error, destination),
                 "atomic no-clobber did not reject an existing destination");
    read = nullptr;
    ok &= expect(nyrt_read_regular_bytes(destination_text.c_str(), 4, &read,
                                         &error) == 1,
                 "could not read destination after no-clobber rejection");
    ok &= expect_bytes(read, {0, 127, 128, 255},
                       "no-clobber failure changed existing content");

    ok &= expect(nyrt_write_bytes_atomic(destination_text.c_str(), replacement,
                                         1, &error) == 1 &&
                     error == nullptr,
                 "atomic replacement failed");
    read = nullptr;
    ok &= expect(nyrt_read_regular_bytes(destination_text.c_str(), 3, &read,
                                         &error) == 1,
                 "could not read atomically replaced destination");
    ok &= expect_bytes(read, {9, 0, 255},
                       "atomic replacement changed byte content");

    const auto empty_path = temporary.path() / "empty.bin";
    const std::string empty_path_text = empty_path.string();
    void *empty = nyrt_array_new(NYRT_ARRAY_U8, 0);
    ok &= expect(nyrt_write_bytes_atomic(empty_path_text.c_str(), empty, 0,
                                         &error) == 1 &&
                     std::filesystem::file_size(empty_path) == 0,
                 "atomic empty-file write failed");

    void *wrong_kind = nyrt_array_new(NYRT_ARRAY_I32, 1);
    ok &= expect(nyrt_write_bytes_atomic(destination_text.c_str(), wrong_kind,
                                         1, &error) == 0 &&
                     error_is_redacted(error, destination),
                 "atomic write accepted a non-byte descriptor");
    ok &= expect(nyrt_read_regular_bytes(destination_text.c_str(), 4, nullptr,
                                         &error) == 0,
                 "bounded read accepted a null output pointer");
    read = nullptr;
    ok &= expect(nyrt_read_regular_bytes(destination_text.c_str(),
                                         512LL * 1024LL * 1024LL + 1, &read,
                                         &error) == 0 &&
                     read == nullptr && error_is_redacted(error, destination),
                 "bounded read accepted a limit beyond the runtime cap");

    const auto missing = temporary.path() / "secret-missing-input.bin";
    const std::string missing_text = missing.string();
    ok &= expect(
        nyrt_read_regular_bytes(missing_text.c_str(), 4, &read, &error) == 0 &&
            read == nullptr && error_is_redacted(error, missing),
        "missing-file failure exposed its path");
    const std::string directory_text = temporary.path().string();
    ok &= expect(nyrt_read_regular_bytes(directory_text.c_str(), 4, &read,
                                         &error) == 0 &&
                     error_is_redacted(error, temporary.path()),
                 "directory read did not fail closed with a redacted error");

    const auto symlink = temporary.path() / "secret-link.bin";
    std::error_code symlink_error;
    std::filesystem::create_symlink(destination, symlink, symlink_error);
    if (!symlink_error) {
      const std::string symlink_text = symlink.string();
      ok &= expect(nyrt_read_regular_bytes(symlink_text.c_str(), 4, &read,
                                           &error) == 0 &&
                       error_is_redacted(error, symlink),
                   "secure read followed a symbolic link");
    }

    std::error_code iteration_error;
    for (std::filesystem::directory_iterator
             entry(temporary.path(), iteration_error),
         end;
         !iteration_error && entry != end; entry.increment(iteration_error)) {
      ok &=
          expect(entry->path().filename().string().rfind(".ny-write-", 0) != 0,
                 "atomic write left a staging file behind");
    }
    ok &= expect(!iteration_error,
                 "could not inspect atomic-write staging cleanup");
  } catch (const std::exception &exception) {
    std::cerr << "native runtime file test setup failed: " << exception.what()
              << '\n';
    ok = false;
  }
}

} // namespace

int main() {
  bool ok = true;
  ok &= expect(nyrt_abi_version() == 1, "native runtime ABI version changed");
  const auto sleep_started = std::chrono::steady_clock::now();
  nyrt_sleep_ms(std::numeric_limits<std::int64_t>::min());
  nyrt_sleep_ms(0);
  nyrt_sleep_ms(2);
  ok &= expect(std::chrono::steady_clock::now() - sleep_started >=
                   std::chrono::milliseconds(2),
               "positive sleep duration was skipped");
  ok &= expect(nyrt_time_now_ms() > 0, "epoch milliseconds were not returned");

  const char *arguments[] = {"", "--looks-like-option", "two words",
                             "snowman-\xE2\x98\x83", nullptr};
  nyrt_context_begin(5, arguments);
  ok &= expect(nyrt_arg_count() == 5, "argument count was not preserved");
  ok &= expect_text(nyrt_arg(0), "", "empty argument was not preserved");
  ok &= expect_text(nyrt_arg(1), "--looks-like-option",
                    "option-looking argument was not preserved");
  ok &= expect_text(nyrt_arg(2), "two words",
                    "spaced argument was not preserved");
  ok &= expect_text(nyrt_arg(3), "snowman-\xE2\x98\x83",
                    "UTF-8 argument was not preserved");
  ok &= expect_text(nyrt_arg(4), "", "null argv entry was not normalized");
  ok &= expect(nyrt_arg(-1) == nullptr && nyrt_arg(5) == nullptr,
               "out-of-range argument lookup did not fail closed");

  check_substring_contract(ok);
  check_array_contract(ok);
  check_buffer_contract(ok);
  check_conversion_contract(ok);
  check_file_contract(ok);

  auto *owned = static_cast<unsigned char *>(nyrt_alloc(8));
  ok &= expect(owned != nullptr, "could not allocate context-owned storage");
  if (owned != nullptr) {
    bool zero_initialized = true;
    for (std::size_t index = 0; index < 8; ++index)
      zero_initialized = zero_initialized && owned[index] == 0;
    ok &= expect(zero_initialized,
                 "context-owned storage was not zero initialized");
  }
  ok &= expect(nyrt_free(owned) == 1 && nyrt_free(owned) == 0,
               "owned allocation release did not reject a stale pointer");
  ok &= expect(nyrt_free(nullptr) == 1,
               "releasing a null owned pointer was not a no-op");
  ok &= expect(nyrt_alloc(std::numeric_limits<std::uint64_t>::max()) == nullptr,
               "impossible context-owned allocation was accepted");

  void *stale = nyrt_array_new(NYRT_ARRAY_U8, 1);
  ok &= expect(stale != nullptr, "could not allocate cleanup sentinel");
  void *stale_owned = nyrt_alloc(1);
  ok &= expect(stale_owned != nullptr,
               "could not allocate owned cleanup sentinel");
  void *stale_buffer = nyrt_buffer_new(NYRT_ARRAY_U8, 1);
  ok &= expect(stale_buffer != nullptr,
               "could not allocate raw-buffer cleanup sentinel");
  nyrt_context_end();
  ok &= expect(nyrt_arg_count() == 0 && nyrt_arg(0) == nullptr,
               "context cleanup retained arguments");
  ok &= expect(nyrt_array_length(stale, NYRT_ARRAY_U8) == -1 &&
                   nyrt_array_get(stale, NYRT_ARRAY_U8, 0) == 0 &&
                   nyrt_array_set(stale, NYRT_ARRAY_U8, 0, 1) == 0 &&
                   nyrt_array_push(stale, NYRT_ARRAY_U8, 1) == 0,
               "context cleanup left a stale descriptor usable");
  ok &= expect(nyrt_free(stale_owned) == 0,
               "context cleanup retained an owned allocation");
  ok &= expect(nyrt_buffer_get(stale_buffer, NYRT_ARRAY_U8, 0) == 0 &&
                   nyrt_buffer_set(stale_buffer, NYRT_ARRAY_U8, 0, 1) == 0 &&
                   nyrt_buffer_free(stale_buffer) == 0,
               "context cleanup left a stale raw buffer usable");
  nyrt_context_end();

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
