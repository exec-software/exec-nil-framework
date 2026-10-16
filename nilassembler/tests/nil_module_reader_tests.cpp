#include "codegen.hpp"
#include "nil_loader.hpp"
#include "../../tests/nil_legacy_fixture.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

int failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

ny::nil_module sample_module() {
  ny::nil_module module;
  module.module_name = "reader_sample";
  module.string_constants.push_back({"S1", "hello\nworld"});
  module.structs.push_back({"point", {{"x", "i64"}, {"y", "i64"}}});
  module.enums.push_back({"status", {{"ready", ""}, {"failed", "i32"}}});
  module.tests.push_back("main");

  ny::nil_function function;
  function.name = "main";
  function.params = {"callback: fn(i32, i32) -> i32"};
  function.return_type = "i32";
  function.is_inline = true;
  function.is_test = true;
  function.annotations = {"@test", "@ai_context(\"preserve context\")"};
  function.source_line = 37;
  function.body = {{"PUSH_CONST", {"7"}, 41}, {"RET", {}, 42}};
  module.functions.push_back(std::move(function));
  return module;
}

void test_text_module_round_trip() {
  const auto expected = sample_module();
  std::stringstream stream;
  ny::nil_writer writer(stream);
  writer.emit_text_module(expected);
  const auto parsed = ny::parseTextNilModule(stream);

  check(parsed->module_name == expected.module_name,
        "text reader should retain the module name");
  check(parsed->string_constants == expected.string_constants,
        "text reader should retain exact string constants");
  check(parsed->structs.size() == 1 && parsed->structs[0].name == "point" &&
            parsed->structs[0].fields == expected.structs[0].fields,
        "text reader should retain struct schemas");
  check(parsed->enums.size() == 1 && parsed->enums[0].name == "status" &&
            parsed->enums[0].variants.size() == 2 &&
            parsed->enums[0].variants[1].name == "failed" &&
            parsed->enums[0].variants[1].associated_type == "i32",
        "text reader should retain enum schemas");
  check(parsed->tests == expected.tests,
        "text reader should retain the test registry");
  check(parsed->functions.size() == 1, "text reader should retain functions");
  if (parsed->functions.size() == 1) {
    const auto &function = parsed->functions[0];
    check(
        function.name == "main" &&
            function.params == expected.functions[0].params,
        "text reader should retain function names and nested parameter types");
    check(function.return_type == "i32",
          "text reader should retain the explicit return type");
    check(function.is_inline && function.is_test &&
              function.annotations.size() == 3,
          "text reader should retain emitted function annotations");
    check(function.body.size() == 2 && function.body[0].debug_line == 41 &&
              function.body[1].debug_line == 42,
          "text reader should retain instructions and debug lines");
  }
}

void test_zero_parameter_parenthesized_return_round_trip() {
  ny::nil_module expected;
  expected.module_name = "parenthesized_returns";
  ny::nil_function callback;
  callback.name = "callback_factory";
  callback.return_type = "fn(i32, i32) -> i32";
  callback.body = {{"RET", {}, 7}};
  expected.functions.push_back(callback);
  ny::nil_function tuple;
  tuple.name = "tuple_factory";
  tuple.return_type = "(i32, i64)";
  tuple.body = {{"RET", {}, 8}};
  expected.functions.push_back(tuple);

  std::stringstream stream;
  ny::nil_writer writer(stream);
  writer.emit_text_module(expected);
  const auto parsed = ny::parseTextNilModule(stream);
  check(parsed->functions.size() == 2 &&
            parsed->functions[0].name == "callback_factory" &&
            parsed->functions[0].params.empty() &&
            parsed->functions[0].return_type == "fn(i32, i32) -> i32" &&
            parsed->functions[1].name == "tuple_factory" &&
            parsed->functions[1].params.empty() &&
            parsed->functions[1].return_type == "(i32, i64)",
        "text reader should distinguish parenthesized return types from "
        "parameter lists");
}

void test_binary_executable_round_trip() {
  auto expected = sample_module();

  std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
  ny::nil_binary_writer writer(stream, {});
  writer.emit_binary_module(expected);
  stream.seekg(0);
  const auto parsed = ny::parseBinaryNilModule(stream);

  check(parsed->module_name == expected.module_name &&
            parsed->string_constants == expected.string_constants &&
            parsed->tests == expected.tests,
        "binary executable reader should retain module records");
  check(parsed->functions.size() == 1 &&
            parsed->functions[0].params == expected.functions[0].params &&
            parsed->functions[0].generic_params.empty() &&
            parsed->functions[0].return_type == "i32" &&
            parsed->functions[0].body.size() == 2 &&
            parsed->functions[0].body[0].debug_line == 41,
        "binary executable reader should retain function records");
  check(parsed->structs.size() == 1 &&
            parsed->structs[0].fields == expected.structs[0].fields &&
            parsed->enums.size() == 1 &&
            parsed->enums[0].variants.size() == 2 &&
            parsed->enums[0].variants[1].associated_type == "i32",
        "binary v3 retains struct and enum schemas");
  check(parsed->functions[0].annotations == expected.functions[0].annotations &&
            parsed->functions[0].is_inline && parsed->functions[0].is_test &&
            parsed->functions[0].source_line == 37,
        "binary v3 retains exact function flags, annotations and source line");
}

void test_binary_library_is_rejected() {
  auto library = sample_module();
  library.is_library = true;
  library.embedded_sources.push_back({"sample.ny", "module sample\n"});
  std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
  ny::nil_binary_writer writer(stream, {});
  writer.emit_binary_module(library);
  stream.seekg(0);

  bool rejected = false;
  try {
    (void)ny::parseBinaryNilModule(stream);
  } catch (const std::runtime_error &error) {
    rejected =
        std::string(error.what()).find("libraries cannot be assembled") !=
        std::string::npos;
  }
  check(rejected, "binary executable reader should reject NIL v2 libraries");
}

void test_legacy_binary_executable() {
  auto expected = sample_module();
  const auto bytes = ny_test::legacy_executable_bytes(expected);
  std::istringstream stream(bytes, std::ios::in | std::ios::binary);
  const auto parsed = ny::parseBinaryNilModule(stream);
  check(parsed->functions.size() == 1 &&
            parsed->functions[0].return_type == "i32" &&
            parsed->functions[0].body.size() == 2 &&
            parsed->structs.empty() && parsed->enums.empty() &&
            parsed->functions[0].annotations.empty(),
        "legacy v1 remains readable without inventing omitted metadata");
}

void test_binary_metadata_rejections() {
  for (int invalid = 0; invalid < 6; ++invalid) {
    auto module = sample_module();
    if (invalid == 0) module.structs[0].name.clear();
    if (invalid == 1) module.structs[0].fields[0].second.clear();
    if (invalid == 2) module.enums[0].variants[0].name.clear();
    if (invalid == 3) module.functions[0].annotations = {"not-an-annotation"};
    if (invalid == 4) module.functions[0].source_line = -1;
    if (invalid == 5) module.enums[0].name = std::string("bad\0name", 8);
    std::ostringstream encoded(std::ios::out | std::ios::binary);
    ny::nil_binary_writer writer(encoded, {});
    writer.emit_binary_module(module);
    for (bool native : {false, true}) {
      std::istringstream input(encoded.str(), std::ios::in | std::ios::binary);
      bool rejected = false;
      try {
        if (native) {
          (void)ny::parseBinaryNilModule(input);
        } else {
          std::unordered_map<std::string, ny::Value> constants;
          (void)ny::parseBinaryProgram(input, constants);
        }
      } catch (const std::runtime_error &) {
        rejected = true;
      }
      check(rejected, "both v3 readers reject malformed schema/function metadata");
    }
  }
}

void test_malformed_text_is_rejected() {
  std::istringstream malformed(
      "module malformed\nfunction main -> i32:\nUNKNOWN_OPCODE\nend "
      "function\n");
  bool rejected = false;
  try {
    (void)ny::parseTextNilModule(malformed);
  } catch (const std::runtime_error &error) {
    rejected =
        std::string(error.what()).find("UNKNOWN_OPCODE") != std::string::npos;
  }
  check(rejected, "text module reader should reject unknown opcodes");

  std::istringstream malformed_parameters(
      "module malformed\nfunction main(value: i32,) -> i32:\nRET\nend "
      "function\n");
  rejected = false;
  try {
    (void)ny::parseTextNilModule(malformed_parameters);
  } catch (const std::runtime_error &error) {
    rejected = std::string(error.what()).find("Empty function parameter") !=
               std::string::npos;
  }
  check(rejected, "text module reader should reject empty parameters");

  const std::vector<std::pair<std::string, std::string>> malformed_signatures = {
      {"value", "expected 'name: exact_type'"},
      {": i32", "parameter name before ':' is empty"},
      {"9value: i32", "is not a valid identifier"},
      {"value:", "exact type after ':' is empty"},
      {"value: void", "has no native ABI value representation"},
      {"value: i32, value: i64", "duplicate parameter name 'value'"},
  };
  for (const auto &[parameters, expected_error] : malformed_signatures) {
    std::istringstream input("module malformed\nfunction helper(" + parameters +
                             ") -> i32:\nRET\nend function\n");
    rejected = false;
    try {
      (void)ny::parseTextNilModule(input);
    } catch (const std::runtime_error &error) {
      const std::string message = error.what();
      rejected = message.find("textual function 'helper' at line 2") !=
                     std::string::npos &&
                 message.find(expected_error) != std::string::npos;
    }
    check(rejected, "text reader should reject malformed parameter list '" +
                        parameters + "'");
  }

  std::istringstream malformed_return(
      "module malformed\nfunction helper -> fn(i32)) -> i32:\nRET\nend "
      "function\n");
  rejected = false;
  try {
    (void)ny::parseTextNilModule(malformed_return);
  } catch (const std::runtime_error &error) {
    rejected = std::string(error.what()).find("unbalanced type delimiter") !=
               std::string::npos;
  }
  check(rejected,
        "text reader should reject malformed parenthesized return types");
}

bool forged_binary_rejected(ny::nil_module module,
                            const std::string &expected_error) {
  std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
  ny::nil_binary_writer writer(stream, {});
  writer.emit_binary_module(module);
  stream.seekg(0);
  try {
    (void)ny::parseBinaryNilModule(stream);
  } catch (const std::runtime_error &error) {
    const std::string message = error.what();
    return message.find("binary function 'main'") != std::string::npos &&
           message.find(expected_error) != std::string::npos;
  }
  return false;
}

void test_forged_binary_signatures_are_rejected() {
  const std::vector<std::pair<std::vector<std::string>, std::string>> cases = {
      {{"value"}, "expected 'name: exact_type'"},
      {{": i32"}, "parameter name before ':' is empty"},
      {{"9value: i32"}, "is not a valid identifier"},
      {{"value:"}, "exact type after ':' is empty"},
      {{"value: void"}, "has no native ABI value representation"},
      {{"value: i32", "value: i64"}, "duplicate parameter name 'value'"},
  };
  for (const auto &[parameters, expected_error] : cases) {
    auto malformed = sample_module();
    malformed.structs.clear();
    malformed.enums.clear();
    malformed.functions[0].params = parameters;
    check(forged_binary_rejected(malformed, expected_error),
          "binary reader should reject forged parameter ABI metadata");
  }

  auto malformed = sample_module();
  malformed.structs.clear();
  malformed.enums.clear();
  malformed.functions[0].generic_params = {"T"};
  check(forged_binary_rejected(malformed,
                               "unresolved serialized generic parameters"),
        "binary reader should reject unresolved generic functions");
}

} // namespace

int main() {
  test_text_module_round_trip();
  test_zero_parameter_parenthesized_return_round_trip();
  test_binary_executable_round_trip();
  test_binary_library_is_rejected();
  test_legacy_binary_executable();
  test_binary_metadata_rejections();
  test_malformed_text_is_rejected();
  test_forged_binary_signatures_are_rejected();
  if (failures != 0) {
    std::cerr << failures << " NIL module reader test(s) failed\n";
    return 1;
  }
  std::cout << "NIL module reader tests passed\n";
  return 0;
}
