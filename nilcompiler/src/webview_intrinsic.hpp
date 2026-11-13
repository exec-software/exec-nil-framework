/* Copyright (C) 2026 Execute Software. SPDX-License-Identifier: MIT */
#pragma once
#include "ui_intrinsic.hpp"

namespace ny::webview {
using Intrinsic = ui::Intrinsic;
using Output = ui::Output;
using ui::abi_type;
using ui::signature;
using ui::signature_matches;
using ui::result_schema_matches;

inline std::optional<Intrinsic> intrinsic(const std::string &name) {
  if (name.rfind("webview.v1.", 0) != 0) return std::nullopt;
  Intrinsic value;
  value.operation = name.substr(11);
  const auto &op = value.operation;
  if (op == "configure") value.parameters = {"text"};
  else if (op == "window") value.parameters = {"text", "i64", "i64"};
  else if (op == "navigate" || op == "load_html" || op == "evaluate_function" || op == "event_number")
    value.parameters = {"i64", "text"};
  else if (op == "event_text") { value.parameters = {"i64", "text"}; value.output = Output::text; }
  else if (op == "show" || op == "poll" || op == "release_event" || op == "close")
    value.parameters = {"i64"};
  else return std::nullopt;
  return value;
}
} // namespace ny::webview
