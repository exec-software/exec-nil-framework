#include "nildata.hpp"
#include "../../common/strict_json.hpp"

#include <iterator>
#include <stdexcept>
#include <utility>

namespace nil::data {
namespace {
using nil::detail::json;

void bounded(const std::string &value, const char *description) {
  if (value.size() > max_bytes)
    throw std::runtime_error(std::string(description) + " exceeds 1 MiB");
}

std::string trim(const std::string &text) {
  const auto first = text.find_first_not_of(" \t\r");
  if (first == std::string::npos) return {};
  return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

bool identifier(const std::string &name) {
  const auto letter = [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
  };
  if (name.empty() || !letter(name.front())) return false;
  for (char c : name)
    if (!letter(c) && !(c >= '0' && c <= '9')) return false;
  return true;
}

bool decimal(const json &value) {
  return value.text.find_first_of(".eE") != std::string::npos;
}

void count_tree(const json &value, unsigned depth, std::size_t &nodes) {
  if (depth > max_depth) throw std::runtime_error("NILData nesting exceeds 32 levels");
  if (++nodes > max_nodes) throw std::runtime_error("NILData exceeds 65536 values");
  for (const auto &item : value.array) count_tree(item, depth + 1, nodes);
  for (const auto &item : value.object) count_tree(item.second, depth + 1, nodes);
}

void validate_tree(const json &value) {
  std::size_t nodes = 0;
  count_tree(value, 0, nodes);
}

json parse_json(const std::string &source) {
  bounded(source, "JSON input");
  auto value = nil::detail::json_parser(source).parse();
  validate_tree(value);
  return value;
}

std::string json_text(const json &value) {
  validate_tree(value);
  auto result = nil::detail::dump(value);
  bounded(result, "JSON output");
  return result;
}

struct line {
  std::string text;
  std::size_t number;
};

class reader {
protected:
  std::vector<line> lines;
  std::size_t position = 0;
  std::size_t nodes = 0;
  [[noreturn]] void fail(const std::string &message, std::size_t number = 0) const {
    if (!number)
      number = position < lines.size() ? lines[position].number :
               lines.empty() ? 1 : lines.back().number + 1;
    throw std::runtime_error(message + " at line " + std::to_string(number));
  }
  void node(unsigned depth) {
    if (depth > max_depth) fail("NILData nesting exceeds 32 levels");
    if (++nodes > max_nodes) fail("NILData exceeds 65536 values");
  }
  explicit reader(const std::string &source, bool comments) {
    bounded(source, "NILData input");
    nil::detail::valid_utf8(source);
    if (source.find('\0') != std::string::npos)
      throw std::runtime_error("NILData input contains raw NUL; use a JSON Unicode escape");
    std::size_t start = 0, number = 1;
    while (start < source.size()) {
      auto end = source.find('\n', start);
      if (end == std::string::npos) end = source.size();
      auto text = source.substr(start, end - start);
      if (comments) {
        bool quoted = false, escaped = false;
        for (std::size_t i = 0; i < text.size(); ++i) {
          const char c = text[i];
          if (escaped) { escaped = false; continue; }
          if (quoted && c == '\\') { escaped = true; continue; }
          if (c == '"') quoted = !quoted;
          if (!quoted && c == '#') { text.resize(i); break; }
        }
      }
      lines.push_back({trim(text), number++});
      start = end + 1;
    }
  }
  void blanks() {
    while (position < lines.size() && lines[position].text.empty()) ++position;
  }
};

class parser : private reader {
  json scalar(const std::string &text, std::size_t number) {
    json value;
    try {
      value = nil::detail::json_parser(text).parse();
    } catch (const std::exception &error) {
      fail(error.what(), number);
    }
    if (value.type == json::kind::array || value.type == json::kind::object)
      fail("containers require 'an object' or 'an array' blocks", number);
    return value;
  }

  std::pair<std::string, std::string> member(const line &input) {
    std::size_t end = 0;
    std::string key;
    if (!input.text.empty() && input.text.front() == '"') {
      bool escaped = false, closed = false;
      for (end = 1; end < input.text.size(); ++end) {
        const char c = input.text[end];
        if (escaped) { escaped = false; continue; }
        if (c == '\\') { escaped = true; continue; }
        if (c == '"') { ++end; closed = true; break; }
      }
      if (!closed) fail("unterminated quoted member name", input.number);
      key = scalar(input.text.substr(0, end), input.number).text;
    } else {
      end = input.text.find_first_of(" \t");
      if (end == std::string::npos) fail("expected member 'name is value'", input.number);
      key = input.text.substr(0, end);
      if (!identifier(key)) fail("member name must be an ASCII identifier or quoted text", input.number);
    }
    if (end == input.text.size() || (input.text[end] != ' ' && input.text[end] != '\t'))
      fail("expected whitespace before 'is'", input.number);
    auto rest = trim(input.text.substr(end));
    if (rest.size() < 3 || rest.compare(0, 2, "is") || (rest[2] != ' ' && rest[2] != '\t'))
      fail("expected 'is' followed by a value", input.number);
    rest = trim(rest.substr(3));
    if (rest.empty()) fail("expected value after 'is'", input.number);
    return {std::move(key), std::move(rest)};
  }

  json value(const std::string &text, unsigned depth, std::size_t opening, bool root = false) {
    node(depth);
    const bool object = text == "an object" || (root && text == "object");
    const bool array = text == "an array" || (root && text == "array");
    if (!object && !array) return scalar(text, opening);
    auto out = object ? json::map() : json::list();
    const std::string close = object ? "end object" : "end array";
    while (true) {
      blanks();
      if (position == lines.size()) fail("missing '" + close + "' for block opened", opening);
      const auto current = lines[position++];
      if (current.text == close) return out;
      auto entry = member(current);
      if (object) {
        if (out.object.count(entry.first)) fail("duplicate member " + nil::detail::quote(entry.first), current.number);
        out.object.emplace(entry.first, value(entry.second, depth + 1, current.number));
      } else {
        if (entry.first != "item" || current.text.front() == '"')
          fail("array entries must use 'item is value'", current.number);
        out.array.push_back(value(entry.second, depth + 1, current.number));
      }
    }
  }
public:
  explicit parser(const std::string &source) : reader(source, true) {}
  json parse() {
    blanks();
    if (position == lines.size() || lines[position].text != "nildata 1")
      fail("expected 'nildata 1' header");
    ++position;
    blanks();
    if (position == lines.size()) fail("expected one root value");
    const auto root = lines[position++];
    auto result = value(root.text, 0, root.number, true);
    blanks();
    if (position != lines.size()) fail("unexpected trailing NILData value or token");
    return result;
  }
};

void append(std::string &out, const std::string &text) {
  if (text.size() > max_bytes - out.size()) throw std::runtime_error("NILData output exceeds 1 MiB");
  out += text;
}

void emit(std::string &out, const json &value, unsigned depth, const std::string &prefix) {
  const std::string indent(depth * 2, ' ');
  if (value.type != json::kind::object && value.type != json::kind::array) {
    append(out, indent + prefix + nil::detail::dump(value) + "\n");
    return;
  }
  const bool object = value.type == json::kind::object;
  append(out, indent + prefix + (depth == 0 ? (object ? "object\n" : "array\n") :
                                             (object ? "an object\n" : "an array\n")));
  if (object) {
    for (const auto &entry : value.object)
      emit(out, entry.second, depth + 1,
           (identifier(entry.first) ? entry.first : nil::detail::quote(entry.first)) + " is ");
  } else {
    for (const auto &entry : value.array) emit(out, entry, depth + 1, "item is ");
  }
  append(out, indent + (object ? "end object\n" : "end array\n"));
}

std::string data_text(const json &value) {
  validate_tree(value);
  std::string out = "nildata 1\n";
  emit(out, value, 0, "");
  return out;
}

class legacy_parser : private reader {
  bool compiler;
  std::pair<std::string, json> block(unsigned depth) {
    blanks();
    if (position == lines.size()) fail("expected legacy block");
    const auto opening = lines[position++];
    const auto split = opening.text.find(' ');
    if (split == std::string::npos) fail("expected named legacy object, list or field", opening.number);
    const auto kind = opening.text.substr(0, split);
    const auto name = trim(opening.text.substr(split + 1));
    if (name.empty() || (kind != "object" && kind != "list" && kind != "field") ||
        (compiler && kind == "list"))
      fail("unexpected legacy block", opening.number);
    node(depth);
    const auto close = "end " + name;
    if (kind == "field") {
      std::string body;
      bool first = true;
      while (position < lines.size() && lines[position].text != close) {
        if (!first) body += '\n';
        body += lines[position++].text;
        first = false;
      }
      if (position == lines.size()) fail("missing legacy '" + close + "'", opening.number);
      ++position;
      return {name, json::str(std::move(body))};
    }
    auto out = kind == "object" ? json::map() : json::list();
    while (true) {
      blanks();
      if (position == lines.size()) fail("missing legacy '" + close + "'", opening.number);
      if (lines[position].text == close) { ++position; return {name, std::move(out)}; }
      if (kind == "object") {
        const auto child_line = lines[position].number;
        auto child = block(depth + 1);
        if (!out.object.emplace(child.first, std::move(child.second)).second)
          fail("duplicate legacy member " + nil::detail::quote(child.first), child_line);
      } else {
        const auto item_line = lines[position].number;
        if (lines[position++].text != "item") fail("expected legacy 'item'", item_line);
        if (position == lines.size()) fail("missing legacy 'end item'", item_line);
        auto next = lines[position].text;
        json child;
        if (next.compare(0, 7, "object ") == 0 || next.compare(0, 5, "list ") == 0 ||
            next.compare(0, 6, "field ") == 0) {
          child = block(depth + 1).second;
        } else {
          node(depth + 1);
          std::string body;
          bool first = true;
          while (position < lines.size() && lines[position].text != "end item") {
            if (!first) body += '\n';
            body += lines[position++].text;
            first = false;
          }
          child = json::str(std::move(body));
        }
        if (position == lines.size() || lines[position].text != "end item")
          fail("missing legacy 'end item'", item_line);
        ++position;
        out.array.push_back(std::move(child));
      }
    }
  }
public:
  legacy_parser(const std::string &source, bool compiler_dialect)
      : reader(source, false), compiler(compiler_dialect) {}
  json parse() {
    if (!compiler) {
      auto root = block(0).second;
      blanks();
      if (position != lines.size()) fail("unexpected trailing legacy block");
      return root;
    }
    node(0);
    auto root = json::map();
    blanks();
    while (position < lines.size()) {
      const auto opening = lines[position].number;
      auto child = block(1);
      if (!root.object.emplace(child.first, std::move(child.second)).second)
        fail("duplicate legacy member " + nil::detail::quote(child.first), opening);
      blanks();
    }
    return root;
  }
};

void require(const json &value, json::kind kind, const char *description) {
  if (value.type != kind) throw std::runtime_error(std::string("expected ") + description);
}

std::string extracted(const std::string &value) {
  if (value.find('\0') != std::string::npos)
    throw std::runtime_error("text containing NUL cannot be extracted; use its JSON representation");
  bounded(value, "extracted text");
  return value;
}

std::string perform(const std::string &operation, const std::string &source,
                    const std::string &argument, std::int64_t index) {
  bounded(source, "source");
  bounded(argument, "argument");
  bounded(operation, "operation");
  if (operation.find('\0') != std::string::npos || source.find('\0') != std::string::npos ||
      argument.find('\0') != std::string::npos)
    throw std::runtime_error("runtime input contains raw NUL; encode it as a JSON Unicode escape");
  if (operation == "load") return to_json(source);
  if (operation == "legacy") return json_text(legacy_parser(source, false).parse());
  if (operation == "import_legacy_compiler" || operation == "legacy_compiler")
    return json_text(legacy_parser(source, true).parse());
  if (operation == "null") return "null";
  if (operation == "object") return "{}";
  if (operation == "array") return "[]";
  if (operation == "string") {
    nil::detail::valid_utf8(argument);
    return json_text(json::str(argument));
  }
  if (operation == "integer" || operation == "decimal" || operation == "flag") {
    auto value = parse_json(argument);
    if (operation == "flag") {
      require(value, json::kind::boolean, "true or false");
      if (argument != "true" && argument != "false")
        throw std::runtime_error("boolean constructor requires exactly true or false");
    }
    else {
      require(value, json::kind::number, "JSON number");
      if (decimal(value) != (operation == "decimal"))
        throw std::runtime_error(operation == "decimal" ? "decimal requires a fraction or exponent" : "integer cannot contain a fraction or exponent");
      if (argument != value.text) throw std::runtime_error("number constructor requires one exact number lexeme");
    }
    return json_text(value);
  }
  auto value = parse_json(source);
  if (operation == "from_json" || operation == "to_json") return json_text(value);
  if (operation == "dump") return data_text(value);
  if (operation == "kind") {
    switch (value.type) {
    case json::kind::null: return "null";
    case json::kind::boolean: return "boolean";
    case json::kind::number: return decimal(value) ? "decimal" : "integer";
    case json::kind::string: return "text";
    case json::kind::array: return "array";
    case json::kind::object: return "object";
    }
  }
  if (operation == "text") { require(value, json::kind::string, "text"); return extracted(value.text); }
  if (operation == "number") { require(value, json::kind::number, "number"); return value.text; }
  if (operation == "boolean") { require(value, json::kind::boolean, "boolean"); return value.boolean ? "true" : "false"; }
  if (operation == "size") {
    if (value.type == json::kind::array) return std::to_string(value.array.size());
    require(value, json::kind::object, "array or object");
    return std::to_string(value.object.size());
  }
  if (operation == "get" || operation == "has" || operation == "erase_key") {
    require(value, json::kind::object, "object");
    nil::detail::valid_utf8(argument);
    const auto found = value.object.find(argument);
    if (operation == "erase_key") {
      if (found != value.object.end()) value.object.erase(found);
      return json_text(value);
    }
    if (operation == "has") return found == value.object.end() ? "false" : "true";
    if (found == value.object.end()) throw std::runtime_error("missing object member " + nil::detail::quote(argument));
    return json_text(found->second);
  }
  if (operation == "at" || operation == "replace" || operation == "erase_at") {
    require(value, json::kind::array, "array");
    if (index < 0 || static_cast<std::uint64_t>(index) >= value.array.size()) throw std::runtime_error("array index out of range");
    if (operation == "replace") {
      value.array[static_cast<std::size_t>(index)] = parse_json(argument);
      return json_text(value);
    }
    if (operation == "erase_at") {
      value.array.erase(value.array.begin() + static_cast<std::size_t>(index));
      return json_text(value);
    }
    return json_text(value.array[static_cast<std::size_t>(index)]);
  }
  if (operation == "key") {
    require(value, json::kind::object, "object");
    if (index < 0 || static_cast<std::uint64_t>(index) >= value.object.size()) throw std::runtime_error("object key index out of range");
    return extracted(std::next(value.object.begin(), static_cast<std::size_t>(index))->first);
  }
  if (operation == "set") {
    require(value, json::kind::object, "object");
    auto pair = parse_json(argument);
    require(pair, json::kind::array, "[key, value] pair");
    if (pair.array.size() != 2) throw std::runtime_error("set requires [key, value]");
    require(pair.array[0], json::kind::string, "text member name");
    value.object[pair.array[0].text] = std::move(pair.array[1]);
    return json_text(value);
  }
  if (operation == "append") {
    require(value, json::kind::array, "array");
    value.array.push_back(parse_json(argument));
    return json_text(value);
  }
  throw std::runtime_error("unknown NILData operation");
}

std::string safe_error(const std::string &error) {
  // Diagnostics cross C-string runtime APIs too. Escape controls and truncate
  // at a UTF-8 boundary rather than exposing NUL or splitting a code point.
  std::string out;
  constexpr char hex[] = "0123456789abcdef";
  for (unsigned char c : error) {
    if (c < 32 || c == 127) {
      out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15];
    } else out += char(c);
    if (out.size() > 4096) break;
  }
  while (!out.empty()) {
    try { nil::detail::valid_utf8(out); break; }
    catch (...) { out.pop_back(); }
  }
  return out.empty() ? "NILData operation failed" : out;
}
} // namespace

std::string from_json(const std::string &source) { return data_text(parse_json(source)); }
std::string to_json(const std::string &source) { return json_text(parser(source).parse()); }
std::string normalize(const std::string &source) { return data_text(parser(source).parse()); }
std::string import_legacy(const std::string &source, bool compiler_dialect) {
  return data_text(legacy_parser(source, compiler_dialect).parse());
}

std::vector<std::string> request(const std::string &operation,
                                 const std::string &source,
                                 const std::string &argument,
                                 std::int64_t index) {
  try {
    auto result = perform(operation, source, argument, index);
    bounded(result, "NILData result");
    if (result.find('\0') != std::string::npos) throw std::runtime_error("NILData result contains NUL");
    return {"", std::move(result)};
  } catch (const std::exception &error) {
    return {safe_error(error.what()), ""};
  } catch (...) {
    return {"NILData operation failed", ""};
  }
}
} // namespace nil::data
