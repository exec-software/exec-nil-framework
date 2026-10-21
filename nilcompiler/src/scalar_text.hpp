#ifndef NIL_SCALAR_TEXT_HPP
#define NIL_SCALAR_TEXT_HPP

#include <charconv>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ny {

inline std::string scalar_text_type(std::string_view type) {
  if (type == "int") return "i32";
  if (type == "String" || type == "string") return "text";
  return std::string(type);
}

inline bool supports_scalar_text(std::string_view type) {
  return type == "bool" || type == "i32" || type == "int" ||
         type == "i64" || type == "u8" || type == "f64" ||
         type == "text" || type == "String" || type == "string";
}

// Both execution engines use the same locale-independent, shortest round-trip
// representation. In particular, formatting preserves the sign of zero.
inline std::string scalar_text(std::int64_t value) {
  char buffer[32];
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
  if (result.ec != std::errc{}) throw std::runtime_error("integer formatting failed");
  return std::string(buffer, result.ptr);
}

inline std::string scalar_text(double value) {
  if (std::isnan(value)) return "nan";
  if (std::isinf(value)) return std::signbit(value) ? "-inf" : "inf";
  char buffer[128];
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                   std::chars_format::general);
  if (result.ec != std::errc{}) throw std::runtime_error("floating-point formatting failed");
  return std::string(buffer, result.ptr);
}

} // namespace ny
#endif
