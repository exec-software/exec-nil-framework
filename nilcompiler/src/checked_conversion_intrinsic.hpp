#pragma once

#include "annotation_arguments.hpp"
#include "dynamic_type.hpp"
#include "standard_namespace.hpp"
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace ny::checked_conversion {

inline std::optional<std::string> payload(const std::string &provider) {
  if (provider == "convert.try_to_i32") return "i32";
  if (provider == "convert.try_to_i64") return "i64";
  if (provider == "convert.try_to_f64") return "f64";
  if (provider == "convert.try_to_bool") return "bool";
  return std::nullopt;
}

inline std::string abi_type(const std::string &type) {
  return canonical_std_namespace(canonical_dynamic_type(type));
}

inline std::string result_type(const std::string &value) {
  return "nil::std::core::Result<" + value + ",text>";
}

inline bool result_matches(const std::string &type, const std::string &value,
                           bool legacy = false) {
  const auto canonical = abi_type(type);
  return canonical == result_type(value) ||
      (legacy && canonical == "Result<" + value + ",text>");
}

inline std::string requirement(const std::string &provider) {
  return provider + " requires the standard Result ABI (text) -> " +
      result_type(*payload(provider));
}

inline std::optional<std::string> annotation_provider(const std::string &annotation) {
  if (!annotation_name_matches(annotation, "intrinsic")) return std::nullopt;
  const auto begin = annotation.find('('), end = annotation.find_last_of(')');
  if (begin == std::string::npos || end == std::string::npos || end <= begin)
    return std::nullopt;
  const auto arguments = parse_annotation_arguments(annotation.substr(begin + 1, end - begin - 1));
  if (!arguments.error.empty() || arguments.arguments.size() != 1 ||
      arguments.arguments.front().name) return std::nullopt;
  const auto &argument = arguments.arguments.front();
  if ((argument.kind != "text" && argument.kind != "symbol") || !payload(argument.value))
    return std::nullopt;
  return argument.value;
}

template <typename Module>
inline void validate_result(const Module &module, const std::string &provider,
                            const std::string &type) {
  const auto value = *payload(provider);
  if (!result_matches(type, value, true))
    throw std::runtime_error(requirement(provider) + "; unrelated nominal enums are not compatible");
  std::size_t schemas = 0;
  for (const auto &definition : module.enums) {
    if (abi_type(definition.name) != abi_type(type)) continue;
    ++schemas;
    bool ok = false, error = false;
    if (definition.variants.size() == 2) {
      for (const auto &variant : definition.variants) {
        if (variant.name == "Ok" && abi_type(variant.associated_type) == value) ok = true;
        if (variant.name == "Error" && abi_type(variant.associated_type) == "text") error = true;
      }
    }
    if (!ok || !error)
      throw std::runtime_error(requirement(provider) + "; exact Ok and Error payloads are required");
  }
  if (schemas != 1)
    throw std::runtime_error(requirement(provider) + "; exactly one matching Result schema is required");
}

// The wire format historically allowed unqualified Result and an i64 return
// operand on a matching annotated wrapper. Preserve that tested profile, but
// never infer a custom enum's identity from its shape or final name segment.
template <typename Module, typename Instruction>
inline void validate_instruction(const Module &module, const Instruction &instruction,
                                 const std::optional<std::string> &declared_provider = std::nullopt,
                                 const std::string &declared_result = "") {
  const auto &args = instruction.args;
  if (instruction.opname != "INTEROP_CALL" || args.size() < 2 ||
      args[0] != "intrinsic" || !payload(args[1])) return;
  if (args.size() != 4 || args[2] != "1")
    throw std::runtime_error(requirement(args[1]) + "; one argument and an explicit result type are required");
  if (abi_type(args[3]) == "i64" && declared_provider == args[1])
    validate_result(module, args[1], declared_result);
  else
    validate_result(module, args[1], args[3]);
}

template <typename Module>
inline void validate_module(const Module &module) {
  for (const auto &function : module.functions) {
    std::optional<std::string> declared_provider;
    for (const auto &annotation : function.annotations) {
      if (const auto provider = annotation_provider(annotation)) {
        validate_result(module, *provider, function.return_type);
        bool exact_parameter = false;
        if (function.params.size() == 1) {
          const auto colon = function.params[0].find(':');
          exact_parameter = colon != std::string::npos &&
              abi_type(function.params[0].substr(colon + 1)) == "text";
        }
        // Historical wrappers took no formal arguments and prepared their
        // text operand inside the body. Only their matching i64-metadata
        // instruction identifies that already-tested wire compatibility form.
        bool legacy_wrapper = false;
        if (function.params.empty()) {
          for (const auto &instruction : function.body)
            if (instruction.opname == "INTEROP_CALL" &&
                instruction.args == std::vector<std::string>{"intrinsic", *provider, "1", "i64"})
              legacy_wrapper = true;
        }
        if ((!exact_parameter && !legacy_wrapper) || !function.generic_params.empty())
          throw std::runtime_error(requirement(*provider) + "; incompatible wrapper parameters");
        declared_provider = provider;
      }
    }
    for (const auto &instruction : function.body)
      validate_instruction(module, instruction, declared_provider, function.return_type);
  }
}

} // namespace ny::checked_conversion
