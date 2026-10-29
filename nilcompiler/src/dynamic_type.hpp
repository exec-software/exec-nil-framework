#ifndef NIL_COMPILER_DYNAMIC_TYPE_HPP
#define NIL_COMPILER_DYNAMIC_TYPE_HPP

#include <cctype>
#include <string>
#include <string_view>

namespace ny {

// Dynamic identities use the canonical NIL spelling, never a source alias or
// the host representation of a value. Nominal case and qualification matter.
inline std::string canonical_dynamic_type(std::string_view written) {
  std::string compact;
  for (unsigned char ch : written)
    if (!std::isspace(ch)) compact += static_cast<char>(ch);
  std::string result;
  for (std::size_t i = 0; i < compact.size();) {
    if (std::isalnum(static_cast<unsigned char>(compact[i])) || compact[i] == '_') {
      const auto begin = i;
      while (i < compact.size() &&
             (std::isalnum(static_cast<unsigned char>(compact[i])) ||
              compact[i] == '_' || compact[i] == ':')) ++i;
      auto token = compact.substr(begin, i - begin);
      if (token == "String" || token == "string" || token == "str") token = "text";
      if (token == "boolean" || token == "flag") token = "bool";
      result += token;
    } else result += compact[i++];
  }
  return result;
}

// Shape exclusion only. Consumers must additionally prove their concrete
// schemas and source-value provenance. A box cannot extend a borrow's lifetime
// or turn an erased word into a pointer, class, or callable.
inline bool dynamic_type_syntax_supported(const std::string &written,
                                           bool allow_anything = false) {
  const auto type = canonical_dynamic_type(written);
  if (type.empty() || type.front() == '<' || type.find('$') != std::string::npos ||
      type.find('&') != std::string::npos || type.find('*') != std::string::npos ||
      (type == "anything" && !allow_anything)) return false;
  for (std::size_t i = 0; i < type.size();) {
    if (!std::isalnum(static_cast<unsigned char>(type[i])) && type[i] != '_') {
      ++i;
      continue;
    }
    const auto begin = i;
    while (i < type.size() &&
           (std::isalnum(static_cast<unsigned char>(type[i])) ||
            type[i] == '_' || type[i] == ':')) ++i;
    const auto token = type.substr(begin, i - begin);
    if (token == "any" || token == "owned" || token == "borrow" ||
        token == "borrow_mut" || token == "ref" || token == "slice" ||
        token == "__local" || token == "unknown" || token == "error") return false;
    if (token == "void" && (begin < 2 || type.substr(begin - 2, 2) != "->"))
      return false;
  }
  return true;
}

} // namespace ny
#endif
