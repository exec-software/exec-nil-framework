#ifndef NY_COMPILER_ARRAY_INTRINSIC_HPP
#define NY_COMPILER_ARRAY_INTRINSIC_HPP

#include <algorithm>
#include <cctype>
#include <optional>
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
  if (type == "boolean" || type == "flag")
    return "bool";
  if (type == "string" || type == "str")
    return "text";
  return type;
}

// Array storage preserves nominal type identity. Normalize built-in aliases
// recursively, but never lowercase a record name inside a tuple shape.
inline std::string canonical_array_type(std::string type) {
  type.erase(std::remove_if(type.begin(), type.end(), [](unsigned char ch) {
    return std::isspace(ch) != 0;
  }), type.end());
  std::size_t position = 0;
  const auto parse = [&](const auto &self, unsigned depth) -> std::string {
    if (depth >= 64)
      throw std::runtime_error("array element type is excessively nested");
    const auto begin = position;
    while (position < type.size() && type[position] != '<' &&
           type[position] != '>' && type[position] != ',') {
      const auto character = static_cast<unsigned char>(type[position]);
      if (!std::isalnum(character) && character != '_' && character != ':' &&
          character != '.' && character != '-')
        throw std::runtime_error("array element type has invalid syntax");
      ++position;
    }
    if (begin == position)
      throw std::runtime_error("array element type has an empty type name");
    std::string name = type.substr(begin, position - begin);
    const auto scalar = canonical_array_scalar(name);
    if (scalar == "i32" || scalar == "i64" || scalar == "u8" ||
        scalar == "bool" || scalar == "f64" || scalar == "f32" ||
        scalar == "text" || scalar == "void" || scalar == "anything")
      name = scalar;
    if (position < type.size() && type[position] == '<') {
      ++position;
      name += '<';
      bool first = true;
      do {
        if (!first) { name += ','; ++position; }
        name += self(self, depth + 1);
        first = false;
      } while (position < type.size() && type[position] == ',');
      if (position == type.size() || type[position] != '>')
        throw std::runtime_error("array element type has unbalanced generic arguments");
      ++position;
      name += '>';
    }
    return name;
  };
  auto result = parse(parse, 0);
  if (position != type.size())
    throw std::runtime_error("array element type has trailing type syntax");
  return result;
}

inline std::optional<std::vector<std::string>>
array_tuple_elements(const std::string &type) {
  std::string canonical;
  try { canonical = canonical_array_type(type); }
  catch (const std::runtime_error &) { return std::nullopt; }
  if (canonical.rfind("tuple<", 0) != 0 || canonical.back() != '>')
    return std::nullopt;
  std::vector<std::string> elements;
  std::size_t begin = 6;
  unsigned depth = 0;
  for (std::size_t index = begin; index + 1 < canonical.size(); ++index) {
    if (canonical[index] == '<') ++depth;
    else if (canonical[index] == '>') --depth;
    else if (canonical[index] == ',' && depth == 0) {
      elements.push_back(canonical.substr(begin, index - begin));
      begin = index + 1;
    }
  }
  elements.push_back(canonical.substr(begin, canonical.size() - begin - 1));
  return elements;
}

inline bool is_array_tuple_type(const std::string &type) {
  return array_tuple_elements(type).has_value();
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
  const std::string element = canonical_array_type(operands[4]);
  if (element != "i32" && element != "i64" && element != "u8" &&
      element != "bool" && element != "f64" && element != "text" && element != "anything" &&
      !is_array_tuple_type(element))
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
  auto encoded_result = canonical_array_type(operands[3]);
  if (encoded_result == "array<string>" || encoded_result == "array<str>")
    encoded_result = "array<text>";
  if (operands[2] != std::to_string(count) || encoded_result != result)
    throw std::runtime_error("array intrinsic arity/result metadata mismatch");
  return {operation, element, result, count};
}

} // namespace ny
#endif
