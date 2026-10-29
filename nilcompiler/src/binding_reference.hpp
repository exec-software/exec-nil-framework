/*
 * Copyright (C) 2008 - 2026 Execute Software.  All rights reserved.
 * SPDX-License-Identifier: MIT
 */
#ifndef NIL_BINDING_REFERENCE_HPP
#define NIL_BINDING_REFERENCE_HPP

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>

namespace ny {
inline constexpr const char *nil_binding_cell_prefix = "$nil.binding.cell.";

inline std::string compact_binding_type(std::string type) {
  type.erase(std::remove_if(type.begin(), type.end(),
      [](unsigned char c) { return std::isspace(c) != 0; }), type.end());
  return type;
}

inline bool is_binding_scalar_type(const std::string &type) {
  return type == "i32" || type == "i64" || type == "u8" || type == "bool" ||
         type == "f64" || type == "text";
}

inline bool contains_binding_cell_type(const std::string &type) {
  return compact_binding_type(type).find(nil_binding_cell_prefix) != std::string::npos;
}

inline std::optional<std::string> binding_cell_value_type(const std::string &raw) {
  const auto type = compact_binding_type(raw);
  const std::string prefix = nil_binding_cell_prefix;
  if (type.rfind(prefix, 0) != 0) return std::nullopt;
  const auto value = type.substr(prefix.size());
  return is_binding_scalar_type(value) ? std::optional<std::string>(value) : std::nullopt;
}

inline std::optional<std::string> binding_reference_value_type(const std::string &raw) {
  const auto type = compact_binding_type(raw);
  if (type.rfind("ref<", 0) != 0 || type.back() != '>') return std::nullopt;
  return binding_cell_value_type(type.substr(4, type.size() - 5));
}
} // namespace ny
#endif
