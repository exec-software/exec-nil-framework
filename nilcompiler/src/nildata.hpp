#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nil::data {
inline constexpr std::size_t max_bytes = 1024 * 1024;
inline constexpr std::size_t max_nodes = 65536;
inline constexpr unsigned max_depth = 32;

// Conversions throw std::runtime_error on malformed or oversized documents.
// Object keys are sorted; JSON number lexemes are preserved exactly.
std::string from_json(const std::string &source);
std::string to_json(const std::string &source);
std::string normalize(const std::string &source);
std::string import_legacy(const std::string &source, bool compiler_dialect = false);

// Values cross the runtime boundary as validated JSON, never native addresses.
// The result is {error, payload}; only one entry is nonempty, except that an
// extracted empty string/key is a successful empty payload.
std::vector<std::string> request(const std::string &operation,
                                 const std::string &source,
                                 const std::string &argument,
                                 std::int64_t index);
} // namespace nil::data
