#ifndef NY_SOURCE_DEBUG_HPP
#define NY_SOURCE_DEBUG_HPP

#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace ny {
inline bool valid_debug_utf8(const std::string &value) {
  for (std::size_t i = 0; i < value.size();) {
    const unsigned char first = static_cast<unsigned char>(value[i++]);
    if (first < 0x20U || first == 0x7fU) return false;
    if (first < 0x80U) continue;
    unsigned count;
    unsigned codepoint;
    if (first >= 0xc2U && first <= 0xdfU) { count = 1; codepoint = first & 0x1fU; }
    else if (first >= 0xe0U && first <= 0xefU) { count = 2; codepoint = first & 0x0fU; }
    else if (first >= 0xf0U && first <= 0xf4U) { count = 3; codepoint = first & 0x07U; }
    else return false;
    if (i + count > value.size()) return false;
    for (unsigned byte = 0; byte < count; ++byte) {
      const unsigned char next = static_cast<unsigned char>(value[i++]);
      if ((next & 0xc0U) != 0x80U) return false;
      codepoint = (codepoint << 6U) | (next & 0x3fU);
    }
    if ((count == 1 && codepoint < 0x80U) || (count == 2 && codepoint < 0x800U) ||
        (count == 3 && codepoint < 0x10000U) ||
        (codepoint >= 0xd800U && codepoint <= 0xdfffU) || codepoint > 0x10ffffU)
      return false;
  }
  return true;
}

struct DebugLocalBinding {
  std::string name;
  std::string storage;
  std::string type;
  int scope_depth = 0;
};

inline void validate_source_debug(const std::string &file,
                                  const std::vector<DebugLocalBinding> &locals) {
  const auto valid = [](const std::string &value, bool empty = false) {
    return (empty || !value.empty()) && value.size() <= 16384 &&
           valid_debug_utf8(value);
  };
  if (!valid(file, true) || locals.size() > 1024 ||
      (file.empty() && !locals.empty()))
    throw std::runtime_error("Invalid source debug metadata");
  std::set<std::string> names, storage;
  for (const auto &local : locals) {
    if (!valid(local.name) || !valid(local.storage) || !valid(local.type) ||
        local.scope_depth < 0 || local.scope_depth > 1024 ||
        !names.insert(local.name).second || !storage.insert(local.storage).second)
      throw std::runtime_error("Invalid lexical debug binding");
  }
}
} // namespace ny
#endif
