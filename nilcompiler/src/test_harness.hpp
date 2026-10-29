/*
 * Copyright (C) 2008 - 2026 Execute Software.  All rights reserved.
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "codegen.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace ny {

// The driver interprets this private completion status; a user exit(0) is
// incomplete, not a successful test. Test stdout/stderr remain ordinary data.
inline constexpr int nil_test_success_status = 77;
inline constexpr int nil_test_invalid_selection_status = 2;

inline std::string test_json_string(const std::string &value) {
  static constexpr char hex[] = "0123456789abcdef";
  std::string result = "\"";
  for (const unsigned char byte : value) {
    if (byte == '"' || byte == '\\') {
      result += '\\'; result += static_cast<char>(byte);
    } else if (byte < 0x20) {
      result += "\\u00"; result += hex[byte >> 4]; result += hex[byte & 15];
    } else result += static_cast<char>(byte);
  }
  return result + '"';
}

inline void validate_test_cases(const nil_module &module) {
  std::unordered_set<std::string> ids;
  for (const auto &test : module.test_cases) {
    if (test.id.empty() || !ids.insert(test.id).second)
      throw std::runtime_error("test discovery requires unique nonempty test IDs");
    const nil_function *body = nullptr;
    for (const auto &function : module.functions) if (function.name == test.function) {
      if (body) throw std::runtime_error("test '" + test.name + "' has an ambiguous function");
      body = &function;
    }
    if (!body || !body->params.empty() || body->return_type != "void")
      throw std::runtime_error("test '" + test.name + "' must have a concrete () -> void body");
  }
}

inline std::string test_manifest_json(const nil_module &module) {
  validate_test_cases(module);
  std::ostringstream out;
  out << "{\"schema\":\"nil.tests.v1\",\"tests\":[";
  bool first = true;
  for (const auto &test : module.test_cases) {
    if (!first) out << ',';
    first = false;
    out << "{\"id\":" << test_json_string(test.id)
        << ",\"name\":" << test_json_string(test.name)
        << ",\"suite\":" << test_json_string(test.suite)
        << ",\"kind\":" << test_json_string(test.kind)
        << ",\"function\":" << test_json_string(test.function)
        << ",\"file\":" << test_json_string(test.file)
        << ",\"line\":" << test.line << '}';
  }
  out << "]}\n";
  return out.str();
}

inline void add_test_harness(nil_module &module) {
  validate_test_cases(module);
  constexpr const char *prefix = "$nil.test.";
  for (const auto &function : module.functions)
    if (function.name.rfind(prefix, 0) == 0)
      throw std::runtime_error("function name '" + function.name + "' is reserved for the test harness");
  for (const auto &constant : module.string_constants)
    if (constant.first.rfind(prefix, 0) == 0)
      throw std::runtime_error("constant name '" + constant.first + "' is reserved for the test harness");

  const auto rename = [&](const std::string &before, const std::string &after) {
    for (auto &function : module.functions) {
      if (function.name == before) function.name = after;
      for (auto &instruction : function.body)
        if ((instruction.opname == "CALL" || instruction.opname == "PUSH_FN_PTR" ||
             instruction.opname == "PARALLEL_MAP") &&
            !instruction.args.empty() && instruction.args.front() == before)
          instruction.args.front() = after;
    }
    for (auto &klass : module.classes) for (auto &method : klass.methods)
      if (method.implementation == before) method.implementation = after;
    for (auto &test : module.tests) if (test == before) test = after;
    for (auto &test : module.test_cases) if (test.function == before) test.function = after;
  };

  const bool has_globals = std::any_of(module.functions.begin(), module.functions.end(),
      [](const auto &function) { return function.name == "__globals_init__"; });
  // Calling the application main from a test is an ordinary function call;
  // process initialization belongs to the selected test invocation only.
  for (auto &function : module.functions) if (function.name == "main" && !function.body.empty()) {
    const auto &first = function.body.front();
    if (first.opname == "CALL" && first.args == std::vector<std::string>{"__globals_init__", "0"})
      function.body.erase(function.body.begin());
  }
  rename("main", "$nil.test.application.main");
  // Native entry treats __globals_init__ specially. Rename it so an invalid
  // selection cannot execute user initialization before reaching the driver.
  rename("__globals_init__", "$nil.test.globals");

  // Native lowering already supports the public console.error annotation ABI;
  // the VM executes the corresponding intrinsic body through the same call.
  nil_function error_output;
  error_output.name = "$nil.test.error";
  error_output.params = {"message:text"};
  error_output.return_type = "void";
  error_output.annotations = {"@intrinsic(\"console.error\")"};
  error_output.body = {{"STORE_LOCAL", {"message"}},
                       {"LOAD_LOCAL", {"message"}},
                       {"INTEROP_CALL", {"intrinsic", "console.error", "1", "void"}},
                       {"RET", {}}};
  module.functions.push_back(std::move(error_output));

  nil_function driver;
  driver.name = "main";
  driver.return_type = "i32";
  const auto emit = [&](const std::string &operation, std::vector<std::string> operands = {}) {
    driver.body.push_back({operation, std::move(operands)});
  };
  const auto return_status = [&](int status) {
    emit("PUSH_CONST", {std::to_string(status)});
    emit("CAST", {"i32"});
    emit("RET");
  };
  emit("INTEROP_CALL", {"std_sys", "arg_count", "0", "i64"});
  emit("PUSH_CONST", {"1"});
  emit("CMP_NE");
  emit("JMP_IF", {"$nil.test.invalid"});
  emit("PUSH_CONST", {"0"});
  emit("INTEROP_CALL", {"std_sys", "arg", "1", "text"});
  emit("STORE_LOCAL", {"test_id"});
  for (std::size_t index = 0; index < module.test_cases.size(); ++index) {
    const auto key = "$nil.test.id." + std::to_string(index);
    module.string_constants.emplace_back(key, module.test_cases[index].id);
    emit("LOAD_LOCAL", {"test_id"}); emit("PUSH_CONST_STR", {key}); emit("CMP_EQ");
    emit("JMP_IF", {"$nil.test.case." + std::to_string(index)});
  }
  emit("JMP", {"$nil.test.invalid"});
  for (std::size_t index = 0; index < module.test_cases.size(); ++index) {
    emit("LABEL", {"$nil.test.case." + std::to_string(index)});
    if (has_globals) emit("CALL", {"$nil.test.globals", "0"});
    emit("CALL", {module.test_cases[index].function, "0"});
    return_status(nil_test_success_status);
  }
  emit("LABEL", {"$nil.test.invalid"});
  module.string_constants.emplace_back("$nil.test.selection.error",
      "niltest: expected exactly one discovered test ID; use niltest list to inspect available tests\n");
  emit("PUSH_CONST_STR", {"$nil.test.selection.error"});
  emit("CALL", {"$nil.test.error", "1"});
  return_status(nil_test_invalid_selection_status);
  module.functions.push_back(std::move(driver));
}
} // namespace ny
