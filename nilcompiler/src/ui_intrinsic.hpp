/*
 * Copyright (C) 2008 - 2026 Execute Software. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "dynamic_type.hpp"
#include "standard_namespace.hpp"
#include <optional>
#include <string>
#include <vector>

namespace ny::ui {
enum class Output { capability, number, text };
struct Intrinsic {
  std::string operation;
  std::vector<std::string> parameters;
  Output output = Output::number;

  std::string return_type() const {
    if (output == Output::capability) return "bool";
    return std::string("nil::std::core::Result<") +
        (output == Output::text ? "text" : "i64") + ",text>";
  }
};

inline std::optional<Intrinsic> intrinsic(const std::string &name) {
  if (name.rfind("ui.v1.", 0) != 0) return std::nullopt;
  Intrinsic value;
  value.operation = name.substr(6);
  const auto &op = value.operation;
  if (op == "available") value.output = Output::capability;
  else if (op == "window") value.parameters = {"text", "i64", "i64"};
  else if (op == "widget") value.parameters = {"text", "i64", "text"};
  else if (op == "grid_attach") value.parameters = {"i64", "i64", "i64", "i64", "i64", "i64"};
  else if (op == "set_text" || op == "read_number" || op == "event_number")
    value.parameters = {"i64", "text"};
  else if (op == "set_number") value.parameters = {"i64", "text", "i64"};
  else if (op == "read_text" || op == "event_text") {
    value.parameters = {"i64"}; value.output = Output::text;
  } else if (op == "activate" || op == "poll" || op == "release_event" || op == "close")
    value.parameters = {"i64"};
  else return std::nullopt;
  return value;
}

// Preserve nominal ownership: an unrelated enum named Result is not this ABI.
// Source aliases have already resolved before this check; legacy standard
// namespace aliases remain accepted in loaded NIL.
inline std::string abi_type(const std::string &type) {
  return canonical_std_namespace(canonical_dynamic_type(type));
}

inline bool signature_matches(const Intrinsic &intrinsic,
                              const std::vector<std::string> &parameters,
                              const std::string &result) {
  if (parameters.size() != intrinsic.parameters.size() ||
      abi_type(result) != intrinsic.return_type()) return false;
  for (std::size_t index = 0; index < parameters.size(); ++index)
    if (abi_type(parameters[index]) != intrinsic.parameters[index]) return false;
  return true;
}

template <typename EnumDefinition>
inline bool result_schema_matches(const Intrinsic &intrinsic,
                                  const EnumDefinition &definition) {
  if (abi_type(definition.name) != intrinsic.return_type() ||
      definition.variants.size() != 2) return false;
  bool ok = false, error = false;
  for (const auto &variant : definition.variants) {
    if (variant.name == "Ok")
      ok = abi_type(variant.associated_type) ==
          (intrinsic.output == Output::text ? "text" : "i64");
    else if (variant.name == "Error") error = abi_type(variant.associated_type) == "text";
    else return false;
  }
  return ok && error;
}

inline std::string signature(const Intrinsic &intrinsic) {
  std::string value = "(";
  for (std::size_t index = 0; index < intrinsic.parameters.size(); ++index) {
    if (index) value += ", ";
    value += intrinsic.parameters[index];
  }
  return value + ") -> " + intrinsic.return_type();
}
} // namespace ny::ui
