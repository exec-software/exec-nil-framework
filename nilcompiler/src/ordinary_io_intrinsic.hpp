#pragma once

#include <optional>
#include <string>
#include <vector>

namespace ny::ordinary_io {

enum class Output { direct, result, option };
struct Contract {
  std::vector<std::string> parameters;
  std::string payload;
  Output output = Output::direct;
};

// The native boundary is deliberately limited to concrete, representable
// values. In particular environment.list's array of named structs is not a
// scalar array and must not be reinterpreted as one.
inline std::optional<Contract> intrinsic(const std::string &name) {
  if (name == "path.separator" || name == "environment.current_directory" ||
      name == "environment.newline" || name == "environment.machine_name" ||
      name == "environment.user_name")
    return Contract{{}, "text"};
  if (name == "path.combine" || name == "path.relative" || name == "path.with_extension")
    return Contract{{"text", "text"}, "text"};
  if (name == "path.basename" || name == "path.dirname" || name == "path.stem" ||
      name == "path.extension" || name == "path.extension_no_dot" ||
      name == "path.absolute" || name == "path.normalize")
    return Contract{{"text"}, "text"};
  if (name == "path.ends_with") return Contract{{"text", "text"}, "bool"};
  if (name == "path.is_absolute" || name == "file.exists")
    return Contract{{"text"}, "bool"};
  if (name == "file.read_text") return Contract{{"text"}, "text", Output::result};
  if (name == "file.read_bytes") return Contract{{"text"}, "array<u8>", Output::result};
  if (name == "file.size") return Contract{{"text"}, "i64", Output::result};
  if (name == "file.delete" || name == "environment.unset" ||
      name == "environment.set_current_directory")
    return Contract{{"text"}, "bool", Output::result};
  if (name == "file.copy" || name == "file.move" || name == "file.write_text" ||
      name == "file.append_text" || name == "environment.set")
    return Contract{{"text", "text"}, "bool", Output::result};
  if (name == "file.write_bytes")
    return Contract{{"text", "array<u8>"}, "bool", Output::result};
  if (name == "environment.get") return Contract{{"text"}, "text", Output::option};
  if (name == "file.handle_open") return Contract{{"text", "i32"}, "i64"};
  if (name == "file.handle_read" || name == "file.handle_write")
    return Contract{{"i64", "ptr<u8>", "i32"}, "i32"};
  if (name == "file.handle_flush" || name == "file.handle_close")
    return Contract{{"i64"}, "i32"};
  if (name == "file.handle_seek") return Contract{{"i64", "i64", "i32"}, "i64"};
  return std::nullopt;
}

} // namespace ny::ordinary_io
