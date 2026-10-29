#pragma once

#include "json.hpp"
#include "../nilcompiler/src/nildata.hpp"

#include <filesystem>
#include <string_view>

namespace nilbuild {
namespace project_format_detail {

inline void bounded(const std::string &text) {
  if (text.size() > nil::data::max_bytes)
    throw std::runtime_error("project document exceeds 1 MiB");
}

inline void count_tree(const json &value, unsigned depth, std::size_t &nodes) {
  if (depth > nil::data::max_depth)
    throw std::runtime_error("project document nesting exceeds 32 levels");
  if (++nodes > nil::data::max_nodes)
    throw std::runtime_error("project document exceeds 65536 values");
  for (const auto &item : value.array) count_tree(item, depth + 1, nodes);
  for (const auto &item : value.object) count_tree(item.second, depth + 1, nodes);
}

inline void validate_tree(const json &value) {
  std::size_t nodes = 0;
  count_tree(value, 0, nodes);
}

inline std::string trim(std::string_view text) {
  const auto first = text.find_first_not_of(" \t\r");
  if (first == std::string_view::npos) return {};
  return std::string(text.substr(first, text.find_last_not_of(" \t\r") - first + 1));
}

// The compatibility YAML reader intentionally accepts a declarative subset:
// block mappings/sequences, plain and single-quoted text, and JSON scalars or
// flow containers. Tags, anchors, aliases, merges, block strings and multiple
// documents are rejected. Keeping this local does not restrict nil.std.yaml.
class yaml_parser {
  struct line { std::string text; std::size_t indent, number; };
  std::vector<line> lines;
  std::size_t position = 0;
  std::size_t nodes = 0;

  [[noreturn]] void fail(const std::string &message, std::size_t number) const {
    throw std::runtime_error(message + " at YAML line " + std::to_string(number));
  }

  // Find a mapping delimiter or comment outside quoted strings. A colon is a
  // delimiter only before whitespace/end, so paths and URLs remain plain text.
  static std::size_t delimiter(const std::string &text, char wanted) {
    char quoted = 0;
    bool escaped = false;
    bool token_start = true;
    unsigned flow_depth = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
      const char c = text[i];
      if (escaped) { escaped = false; continue; }
      if (quoted == '"' && c == '\\') { escaped = true; continue; }
      if (quoted) {
        if (c == quoted) {
          if (quoted == '\'' && i + 1 < text.size() && text[i + 1] == '\'') ++i;
          else quoted = 0;
        }
        continue;
      }
      if ((c == '"' || c == '\'') && token_start) { quoted = c; token_start = false; continue; }
      const bool followed_by_space = i + 1 == text.size() || text[i + 1] == ' ' || text[i + 1] == '\t';
      if (c == wanted) {
        if (wanted == '#' && (i == 0 || text[i - 1] == ' ' || text[i - 1] == '\t')) return i;
        if (wanted == ':' && followed_by_space) return i;
      }
      if (c == ' ' || c == '\t') continue;
      if ((c == '[' || c == '{') && token_start) { ++flow_depth; token_start = true; }
      else if ((c == ']' || c == '}') && flow_depth) { --flow_depth; token_start = false; }
      else if ((c == ':' && (flow_depth || followed_by_space)) || (c == ',' && flow_depth) ||
               (c == '-' && token_start && followed_by_space)) token_start = true;
      else token_start = false;
    }
    return std::string::npos;
  }

  void node(unsigned depth, std::size_t number) {
    if (depth > nil::data::max_depth) fail("project document nesting exceeds 32 levels", number);
    if (++nodes > nil::data::max_nodes) fail("project document exceeds 65536 values", number);
  }

  json scalar(const std::string &text, unsigned depth, std::size_t number) {
    if (text.empty()) { node(depth, number); return {}; }
    if (text.front() == '"' || text.front() == '[' || text.front() == '{') {
      json result;
      try { result = json_parser(text).parse(); }
      catch (const std::exception &error) {
        fail(std::string("quoted scalars and flow containers require JSON syntax: ") + error.what(), number);
      }
      // Include flow values in the same document-wide depth and node budget.
      try { count_tree(result, depth, nodes); }
      catch (const std::exception &error) { fail(error.what(), number); }
      return result;
    }
    node(depth, number);
    if (text.front() == '\'') {
      std::string result;
      for (std::size_t i = 1; i < text.size(); ++i) {
        if (text[i] != '\'') { result += text[i]; continue; }
        if (i + 1 == text.size()) return json::str(std::move(result));
        if (text[i + 1] == '\'') { result += '\''; ++i; continue; }
        fail("unexpected text after quoted scalar", number);
      }
      fail("unterminated single-quoted scalar", number);
    }
    if (text == "true" || text == "false") return json::flag(text == "true");
    if (text == "null" || text == "~") return {};
    if (text == "---" || text == "..." || text.front() == '%' ||
        text.front() == '!' || text.front() == '&' || text.front() == '*' ||
        text.front() == '|' || text.front() == '>' || text.front() == '@' ||
        text.front() == '`' || text.front() == ']' || text.front() == '}' ||
        text.front() == ',' || text.front() == '?' || text.front() == ':' ||
        text == "-" || (text.size() > 1 && text[0] == '-' && text[1] == ' '))
      fail("unsupported YAML syntax; quote literal text or use NILData", number);
    if (delimiter(text, ':') != std::string::npos)
      fail("plain scalars containing ': ' must be quoted", number);
    if (text.front() == '-' || (text.front() >= '0' && text.front() <= '9')) {
      try {
        auto result = json_parser(text).parse();
        if (result.type == json::kind::number) return result;
      } catch (const std::exception &) {
        // Non-JSON numeric-looking scalars (such as a version or filename)
        // remain text; there is no implicit date, octal or floating conversion.
      }
    }
    return json::str(text);
  }

  static bool sequence(const std::string &text) {
    return text == "-" || (text.size() > 1 && text[0] == '-' && (text[1] == ' ' || text[1] == '\t'));
  }

  std::string key(const std::string &text, std::size_t number) {
    if (text.empty()) fail("expected a nonempty mapping key", number);
    std::string result;
    if (text.front() == '"' || text.front() == '\'') {
      // Keys are not value nodes. Decode them through the scalar rules while
      // restoring the value budget afterward.
      const auto before = nodes;
      auto parsed = scalar(text, 0, number);
      nodes = before;
      if (parsed.type != json::kind::string) fail("mapping keys must be text", number);
      result = std::move(parsed.text);
    } else {
      if (text.front() == '&' || text.front() == '*' || text.front() == '!' ||
          text.front() == '[' || text.front() == '{' || text.front() == '?' ||
          text.front() == '%' || text.front() == '@' || text.front() == '`')
        fail("unsupported YAML mapping key; quote literal text", number);
      result = text;
    }
    if (result == "<<") fail("YAML merge keys are not supported", number);
    return result;
  }

  json nested_or_scalar(const std::string &text, unsigned depth, const line &opening) {
    if (!text.empty()) return scalar(text, depth, opening.number);
    if (position < lines.size() && lines[position].indent > opening.indent)
      return block(depth, lines[position].indent);
    node(depth, opening.number);
    return {};
  }

  json block(unsigned depth, std::size_t indent) {
    const auto opening = lines[position];
    const bool list = sequence(opening.text);
    if (!list && (opening.text.front() == '{' || opening.text.front() == '[' ||
                  delimiter(opening.text, ':') == std::string::npos)) {
      ++position;
      return scalar(opening.text, depth, opening.number);
    }
    node(depth, opening.number);
    auto result = list ? json::list() : json::map();
    while (position < lines.size() && lines[position].indent >= indent) {
      const auto current = lines[position];
      if (current.indent != indent) fail("unexpected indentation", current.number);
      ++position;
      if (list) {
        if (!sequence(current.text)) fail("expected sequence item '-'", current.number);
        result.array.push_back(nested_or_scalar(trim(std::string_view(current.text).substr(1)), depth + 1, current));
      } else {
        if (sequence(current.text)) fail("cannot mix mapping entries and sequence items", current.number);
        const auto split = delimiter(current.text, ':');
        if (split == std::string::npos) fail("expected 'key: value' mapping entry", current.number);
        auto name = key(trim(std::string_view(current.text).substr(0, split)), current.number);
        if (result.object.count(name)) fail("duplicate YAML field: " + name, current.number);
        auto value = nested_or_scalar(trim(std::string_view(current.text).substr(split + 1)), depth + 1, current);
        result.object.emplace(std::move(name), std::move(value));
      }
    }
    return result;
  }

public:
  explicit yaml_parser(const std::string &source) {
    bounded(source);
    valid_utf8(source);
    for (unsigned char c : source)
      if (c < 0x20 && c != '\n' && c != '\r' && c != '\t')
        throw std::runtime_error("YAML input contains an unescaped control character");
    std::size_t start = 0, number = 1;
    while (start < source.size()) {
      auto end = source.find('\n', start);
      if (end == std::string::npos) end = source.size();
      auto text = source.substr(start, end - start);
      const auto comment = delimiter(text, '#');
      if (comment != std::string::npos) text.resize(comment);
      const auto first = text.find_first_not_of(' ');
      if (first != std::string::npos && text[first] == '\t') fail("tabs are not allowed for indentation", number);
      const auto content = trim(text);
      if (!content.empty()) lines.push_back({content, first, number});
      start = end + 1;
      ++number;
    }
  }

  json parse() {
    if (!lines.empty() && lines.front().text == "---" && lines.front().indent == 0) ++position;
    if (!lines.empty() && lines.back().text == "..." && lines.back().indent == 0) lines.pop_back();
    if (position == lines.size()) throw std::runtime_error("expected one YAML document");
    if (lines[position].indent) fail("the root value must start at column one", lines[position].number);
    auto result = block(0, 0);
    if (position != lines.size()) fail("unexpected trailing YAML content", lines[position].number);
    return result;
  }
};

inline void append(std::string &out, const std::string &text) {
  if (text.size() > nil::data::max_bytes - out.size())
    throw std::runtime_error("project document exceeds 1 MiB");
  out += text;
}

inline bool block_value(const json &value) {
  return (value.type == json::kind::object && !value.object.empty()) ||
         (value.type == json::kind::array && !value.array.empty());
}

inline void emit_yaml(std::string &out, const json &value, unsigned depth) {
  const std::string indent(depth * 2, ' ');
  if (!block_value(value)) { append(out, indent + dump(value) + "\n"); return; }
  if (value.type == json::kind::object) {
    for (const auto &entry : value.object) {
      append(out, indent + quote(entry.first) + ':');
      if (block_value(entry.second)) { append(out, "\n"); emit_yaml(out, entry.second, depth + 1); }
      else append(out, " " + dump(entry.second) + "\n");
    }
  } else {
    for (const auto &entry : value.array) {
      append(out, indent + '-');
      if (block_value(entry)) { append(out, "\n"); emit_yaml(out, entry, depth + 1); }
      else append(out, " " + dump(entry) + "\n");
    }
  }
}
} // namespace project_format_detail

inline std::string project_document_format(const std::filesystem::path &path) {
  auto extension = path.extension().string();
  for (auto &c : extension) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  if (extension == ".nildata") return "nildata";
  if (extension == ".yaml" || extension == ".yml") return "yaml";
  // Explicit legacy manifests historically accepted arbitrary file names.
  // Preserve that behavior while recognizing the new formats by extension.
  return "json";
}

inline json parse_project_document(const std::string &text, const std::filesystem::path &path) {
  project_format_detail::bounded(text);
  const auto format = project_document_format(path);
  json result;
  if (format == "nildata") {
    const auto converted = nil::data::to_json(text);
    result = json_parser(converted).parse();
  } else if (format == "yaml") result = project_format_detail::yaml_parser(text).parse();
  else result = json_parser(text).parse();
  project_format_detail::validate_tree(result);
  return result;
}

inline std::string serialize_project_document(const json &value, const std::string &format) {
  project_format_detail::validate_tree(value);
  const auto encoded = dump(value);
  project_format_detail::bounded(encoded);
  // Also validate generated strings and number lexemes, including callers that
  // construct json values directly rather than obtaining them from a parser.
  json_parser(encoded).parse();
  if (format == "nildata") return nil::data::from_json(encoded);
  if (format == "json") {
    auto result = encoded + '\n';
    project_format_detail::bounded(result);
    return result;
  }
  if (format == "yaml" || format == "yml") {
    std::string result;
    project_format_detail::emit_yaml(result, value, 0);
    return result;
  }
  throw std::runtime_error("unknown project format: " + format + "; expected nildata, json or yaml");
}
} // namespace nilbuild
