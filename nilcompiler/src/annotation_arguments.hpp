#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ny {

// Values are metadata, never expressions to execute. Text is decoded; the
// other kinds retain a validated literal or qualified-symbol spelling.
struct annotation_argument {
  std::optional<std::string> name;
  std::string kind;
  std::string value;
};

struct annotation_arguments_result {
  std::vector<annotation_argument> arguments;
  std::string error;
  std::size_t error_offset = 0;
};

// raw_text selects Matrix's quoted-text rules (backslashes are ordinary).
// Common retained metadata always uses escaped, double-quoted text.
annotation_arguments_result parse_annotation_arguments(
    const std::string &raw, bool raw_text = false);
std::string quote_annotation_text(const std::string &value);
std::string serialize_annotation_arguments(
    const std::vector<annotation_argument> &arguments);
bool annotation_name_matches(const std::string &raw, const std::string &name);

} // namespace ny
