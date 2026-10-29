#include "annotation_arguments.hpp"
#include "numeric_literal.hpp"

#include <set>
#include <stdexcept>

namespace ny {
namespace {
bool identifier_start(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
bool identifier_part(char c) {
  return identifier_start(c) || (c >= '0' && c <= '9');
}
bool space(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}
bool qualified_symbol(const std::string &value) {
  std::size_t cursor = 0;
  while (cursor < value.size()) {
    if (!identifier_start(value[cursor++])) return false;
    while (cursor < value.size() && identifier_part(value[cursor])) ++cursor;
    if (cursor == value.size()) return true;
    if (value[cursor] == '.') ++cursor;
    else if (value.compare(cursor, 2, "::") == 0) cursor += 2;
    else return false;
    if (cursor == value.size()) return false;
  }
  return false;
}
} // namespace

annotation_arguments_result parse_annotation_arguments(const std::string &raw,
                                                       bool raw_text) {
  annotation_arguments_result result;
  std::size_t cursor = 0;
  bool named_seen = false;
  std::set<std::string> names;
  const auto skip_space = [&] { while (cursor < raw.size() && space(raw[cursor])) ++cursor; };
  const auto fail = [&](const std::string &message, std::size_t at) {
    result.error = message;
    result.error_offset = at;
    result.arguments.clear();
    return result;
  };
  if (raw.size() > 1024U * 1024U)
    return fail("annotation arguments exceed the 1 MiB limit", 0);
  skip_space();
  while (cursor < raw.size()) {
    if (result.arguments.size() == 1024)
      return fail("annotation has more than 1024 arguments", cursor);
    annotation_argument argument;
    const auto start = cursor;
    if (identifier_start(raw[cursor])) {
      while (cursor < raw.size() && identifier_part(raw[cursor])) ++cursor;
      const auto end = cursor;
      skip_space();
      if (cursor < raw.size() && raw[cursor] == '=') {
        argument.name = raw.substr(start, end - start);
        if (!names.insert(*argument.name).second)
          return fail("duplicate annotation property '" + *argument.name + "'", start);
        named_seen = true;
        ++cursor;
        skip_space();
      } else cursor = start;
    }
    if (!argument.name && named_seen)
      return fail("positional annotation arguments must precede named properties", start);
    if (cursor == raw.size()) return fail("expected an annotation literal", cursor);
    const auto value_start = cursor;
    if (raw[cursor] == '"') {
      argument.kind = "text";
      ++cursor;
      bool closed = false;
      while (cursor < raw.size()) {
        char c = raw[cursor++];
        if (c == '"') { closed = true; break; }
        if (c == '\\' && !raw_text) {
          if (cursor == raw.size()) return fail("unterminated annotation text escape", cursor - 1);
          c = raw[cursor++];
          switch (c) {
          case '"': case '\\': break;
          case 'n': c = '\n'; break;
          case 'r': c = '\r'; break;
          case 't': c = '\t'; break;
          case 'v': c = '\v'; break;
          case 'f': c = '\f'; break;
          case 'b': c = '\b'; break;
          case '0': c = '\0'; break;
          default: return fail("unsupported annotation text escape", cursor - 2);
          }
        }
        if (c == '\0') return fail("annotation text cannot contain NUL", cursor - 1);
        argument.value += c;
      }
      if (!closed) return fail("unterminated annotation text literal", value_start);
    } else {
      while (cursor < raw.size() && raw[cursor] != ',' && !space(raw[cursor])) ++cursor;
      argument.value = raw.substr(value_start, cursor - value_start);
      if (argument.value == "true" || argument.value == "false" ||
          argument.value == "yes" || argument.value == "no") {
        argument.kind = "boolean";
        argument.value = argument.value == "true" || argument.value == "yes"
                             ? "true" : "false";
      } else if (!argument.value.empty() &&
               ((argument.value.front() >= '0' && argument.value.front() <= '9') ||
                argument.value.front() == '-' || argument.value.front() == '+')) {
        const char *error = nullptr;
        if (is_floating_literal_spelling(argument.value)) {
          double number = 0;
          error = parse_floating_literal(argument.value, number);
          argument.kind = "floating";
        } else {
          integer_literal_info number;
          error = parse_integer_literal(argument.value, number);
          argument.kind = "integer";
        }
        if (error) return fail(error, value_start);
      } else if (qualified_symbol(argument.value)) argument.kind = "symbol";
      else return fail("expected a text, boolean, integer, floating, or qualified-symbol literal; expressions, arrays, and maps are unavailable", value_start);
    }
    result.arguments.push_back(std::move(argument));
    skip_space();
    if (cursor == raw.size()) break;
    if (raw[cursor] != ',') return fail("expected ',' between annotation arguments", cursor);
    ++cursor;
    skip_space();
    if (cursor == raw.size()) return fail("expected an annotation argument after ','", cursor);
  }
  return result;
}

std::string quote_annotation_text(const std::string &value) {
  std::string result = "\"";
  for (char c : value) {
    switch (c) {
    case '"': result += "\\\""; break;
    case '\\': result += "\\\\"; break;
    case '\n': result += "\\n"; break;
    case '\r': result += "\\r"; break;
    case '\t': result += "\\t"; break;
    case '\v': result += "\\v"; break;
    case '\f': result += "\\f"; break;
    case '\b': result += "\\b"; break;
    case '\0': throw std::invalid_argument("annotation text cannot contain NUL");
    default: result += c; break;
    }
  }
  return result + '"';
}

std::string serialize_annotation_arguments(const std::vector<annotation_argument> &arguments) {
  std::string result;
  for (const auto &argument : arguments) {
    if (!result.empty()) result += ", ";
    if (argument.name) result += *argument.name + "=";
    result += argument.kind == "text" ? quote_annotation_text(argument.value) : argument.value;
  }
  // Avoid exposing a serializer that could turn malformed API values into
  // executable-looking metadata or silently change their declared kind.
  const auto parsed = parse_annotation_arguments(result);
  if (!parsed.error.empty() || parsed.arguments.size() != arguments.size())
    throw std::invalid_argument("invalid annotation argument representation");
  for (std::size_t i = 0; i < arguments.size(); ++i)
    if (parsed.arguments[i].name != arguments[i].name ||
        parsed.arguments[i].kind != arguments[i].kind ||
        parsed.arguments[i].value != arguments[i].value)
      throw std::invalid_argument("annotation argument kind does not match its value");
  return result;
}

bool annotation_name_matches(const std::string &raw, const std::string &name) {
  std::size_t start = 0;
  while (start < raw.size() && space(raw[start])) ++start;
  const auto prefix = "@" + name;
  if (raw.compare(start, prefix.size(), prefix) != 0) return false;
  auto cursor = start + prefix.size();
  while (cursor < raw.size() && space(raw[cursor])) ++cursor;
  return cursor == raw.size() || raw[cursor] == '(';
}

} // namespace ny
