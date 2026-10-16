#ifndef NY_COMPILER_ARRAY_INTRINSIC_HPP
#define NY_COMPILER_ARRAY_INTRINSIC_HPP

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

namespace ny {

enum class array_intrinsic_operation { create, get, set, length };

inline std::string canonical_array_scalar(std::string type) {
  type.erase(std::remove_if(type.begin(), type.end(), [](unsigned char ch) {
    return std::isspace(ch) != 0;
  }), type.end());
  std::transform(type.begin(), type.end(), type.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  if (type == "string" || type == "str")
    return "text";
  return type;
}

inline bool is_array_intrinsic(const std::string &name) {
  return name == "array.new_with_value" || name == "array.new_u8" ||
         name == "array.new_f32" || name == "array.get" ||
         name == "array.get_u8" || name == "array.get_f32" ||
         name == "array.set" || name == "array.set_u8" ||
         name == "array.set_f32" || name == "array.length";
}

struct array_intrinsic_contract {
  array_intrinsic_operation operation;
  std::string element;
  std::string result;
  std::size_t argument_count;
};

// Typed array intrinsics use the existing five-operand INTEROP_CALL form:
// provider, operation, arity, exact result type, exact element type. The final
// operand is a language type, never a guessed host FFI calling convention.
inline array_intrinsic_contract parse_array_intrinsic(
    const std::vector<std::string> &operands) {
  if (operands.size() != 5 || operands[0] != "intrinsic" ||
      !is_array_intrinsic(operands[1]))
    throw std::runtime_error("array intrinsic requires exact typed metadata");
  const auto &name = operands[1];
  const std::string element = canonical_array_scalar(operands[4]);
  if (element != "i32" && element != "i64" && element != "u8" &&
      element != "bool" && element != "f64" && element != "text")
    throw std::runtime_error("array intrinsic has unsupported element type '" + element + "'");
  if ((name.find("_u8") != std::string::npos && element != "u8") ||
      name.find("_f32") != std::string::npos)
    throw std::runtime_error("array intrinsic element metadata disagrees with its operation");
  const auto operation = name.rfind("array.new", 0) == 0
      ? array_intrinsic_operation::create
      : name.rfind("array.get", 0) == 0 ? array_intrinsic_operation::get
      : name.rfind("array.set", 0) == 0 ? array_intrinsic_operation::set
      : array_intrinsic_operation::length;
  const std::size_t count = operation == array_intrinsic_operation::length ? 1
      : operation == array_intrinsic_operation::set ? 3 : 2;
  const std::string result = operation == array_intrinsic_operation::create
      ? "array<" + element + ">"
      : operation == array_intrinsic_operation::get ? element
      : operation == array_intrinsic_operation::length ? "i32" : "void";
  auto encoded_result = canonical_array_scalar(operands[3]);
  if (encoded_result == "array<string>" || encoded_result == "array<str>")
    encoded_result = "array<text>";
  if (operands[2] != std::to_string(count) || encoded_result != result)
    throw std::runtime_error("array intrinsic arity/result metadata mismatch");
  return {operation, element, result, count};
}

} // namespace ny
#endif
