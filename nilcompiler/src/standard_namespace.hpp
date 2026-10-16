#pragma once

#include <string>
#include <initializer_list>
#include <utility>

namespace ny {

// Call only on parsed namespace/type paths, never on source text or member
// access segments. The legacy root is an alias; unrelated names stay intact.
inline std::string canonical_std_namespace(std::string name) {
  for (const auto &separator : {std::string("::"), std::string(".")}) {
    const std::string old_root = "ny" + separator + "lang";
    if (name == old_root || name.rfind(old_root + separator, 0) == 0) {
      name.replace(0, old_root.size(), "nil" + separator + "std");
      break;
    }
  }
  return name;
}

// Standard-library files omit their namespace root on disk. Other package
// paths retain every segment. Accept either spelling of a parsed module path.
inline std::string standard_module_relative_path(std::string name) {
  name = canonical_std_namespace(std::move(name));
  for (const auto &separator : {std::string("::"), std::string(".")}) {
    const std::string root = "nil" + separator + "std";
    if (name == root) return {};
    if (name.rfind(root + separator, 0) == 0) {
      name.erase(0, root.size() + separator.size());
      break;
    }
  }
  for (std::size_t position = 0; position < name.size(); ++position) {
    if (name.compare(position, 2, "::") == 0) name.replace(position, 2, "/");
    else if (name[position] == '.') name[position] = '/';
  }
  return name;
}

} // namespace ny
