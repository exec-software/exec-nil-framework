#ifndef NY_EXECUTABLE_CONTEXT_HPP
#define NY_EXECUTABLE_CONTEXT_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace ny {
struct nil_module;

// Opt-in self-description, never executable code or authenticated provenance.
// All entry points throw std::runtime_error on invalid or oversized records.
std::vector<std::uint8_t> encode_context_manifest(const nil_module &module);
std::string context_manifest_json(const nil_module &module);
std::string decode_context_manifest_json(const std::vector<std::uint8_t> &bytes);
std::string inspect_context_artifact(const std::vector<char> &bytes);
} // namespace ny

#endif
