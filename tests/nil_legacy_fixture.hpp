#ifndef NY_TEST_NIL_LEGACY_FIXTURE_HPP
#define NY_TEST_NIL_LEGACY_FIXTURE_HPP

#include "codegen.hpp"
#include <cstdint>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ny_test {

// Library v2 deliberately retains the v1 function layout. Remove its source
// interface and rebuild a five-section executable header to exercise legacy
// loading independently of the current executable writer's v3 records.
inline std::string legacy_executable_bytes(ny::nil_module module) {
  module.is_library = true;
  std::ostringstream stream(std::ios::out | std::ios::binary);
  ny::nil_binary_writer writer(stream, {});
  writer.emit_binary_module(module);
  const std::string library = stream.str();
  const auto read = [&](std::size_t offset, unsigned width) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < width; ++i)
      value |= std::uint64_t(static_cast<unsigned char>(library.at(offset + i)))
               << (8U * i);
    return value;
  };
  std::vector<std::pair<std::uint32_t, std::string>> sections;
  for (unsigned i = 0; i < 6; ++i) {
    const auto type = static_cast<std::uint32_t>(read(16 + 20 * i, 4));
    if (type != 6)
      sections.emplace_back(type, library.substr(read(20 + 20 * i, 8),
                                                 read(28 + 20 * i, 8)));
  }
  std::string result("NIL\0", 4);
  const auto append = [&](std::uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i)
      result.push_back(static_cast<char>((value >> (8U * i)) & 0xffU));
  };
  append(1, 4);
  append(0, 4);
  append(5, 4);
  std::uint64_t offset = 116;
  for (const auto &section : sections) {
    append(section.first, 4);
    append(offset, 8);
    append(section.second.size(), 8);
    offset += section.second.size();
  }
  for (const auto &section : sections)
    result += section.second;
  return result;
}

} // namespace ny_test
#endif
