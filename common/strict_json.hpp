#pragma once

// Small, strict JSON value parser shared by framework command-line tools.
// It rejects duplicate keys, malformed UTF-8, non-JSON numbers and excessive
// nesting rather than silently normalizing ambiguous configuration.
#include <cctype>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace nil::detail {
struct json {
  enum class kind { null, boolean, number, string, array, object } type = kind::null;
  bool boolean = false;
  std::string text;
  std::vector<json> array;
  std::map<std::string, json> object;
  static json str(std::string value) { json j; j.type = kind::string; j.text = std::move(value); return j; }
  static json flag(bool value) { json j; j.type = kind::boolean; j.boolean = value; return j; }
  static json list() { json j; j.type = kind::array; return j; }
  static json map() { json j; j.type = kind::object; return j; }
  const json &at(const std::string &key) const {
    const auto found = object.find(key);
    if (type != kind::object || found == object.end()) throw std::runtime_error("missing JSON field: " + key);
    return found->second;
  }
};

inline void valid_utf8(const std::string &s) {
  for (std::size_t i = 0; i < s.size();) {
    unsigned char c = s[i++];
    if (c < 0x80) continue;
    unsigned count, value, minimum;
    if (c >= 0xc2 && c <= 0xdf) { count = 1; value = c & 31; minimum = 0x80; }
    else if (c >= 0xe0 && c <= 0xef) { count = 2; value = c & 15; minimum = 0x800; }
    else if (c >= 0xf0 && c <= 0xf4) { count = 3; value = c & 7; minimum = 0x10000; }
    else throw std::runtime_error("JSON requires valid UTF-8");
    while (count--) {
      if (i == s.size() || (static_cast<unsigned char>(s[i]) & 0xc0) != 0x80)
        throw std::runtime_error("JSON requires valid UTF-8");
      value = (value << 6) | (static_cast<unsigned char>(s[i++]) & 63);
    }
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
      throw std::runtime_error("JSON requires valid UTF-8");
  }
}

class json_parser {
  const std::string &source;
  std::size_t position = 0;
  [[noreturn]] void fail(const std::string &message) const {
    throw std::runtime_error(message + " at JSON byte " + std::to_string(position));
  }
  void space() { while (position < source.size() && (source[position] == ' ' || source[position] == '\n' || source[position] == '\r' || source[position] == '\t')) ++position; }
  bool consume(char c) { if (position < source.size() && source[position] == c) { ++position; return true; } return false; }
  unsigned hex4() {
    unsigned n = 0;
    for (int i = 0; i < 4; ++i) {
      if (position == source.size()) fail("incomplete Unicode escape");
      char c = source[position++];
      unsigned digit;
      if (c >= '0' && c <= '9') digit = c - '0';
      else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
      else fail("invalid Unicode escape");
      n = n * 16 + digit;
    }
    return n;
  }
  void scalar(std::string &out, unsigned n) {
    if (n < 0x80) out += char(n);
    else if (n < 0x800) { out += char(0xc0 | (n >> 6)); out += char(0x80 | (n & 63)); }
    else if (n < 0x10000) { out += char(0xe0 | (n >> 12)); out += char(0x80 | ((n >> 6) & 63)); out += char(0x80 | (n & 63)); }
    else { out += char(0xf0 | (n >> 18)); out += char(0x80 | ((n >> 12) & 63)); out += char(0x80 | ((n >> 6) & 63)); out += char(0x80 | (n & 63)); }
  }
  std::string string() {
    if (!consume('"')) fail("expected JSON string");
    std::string out;
    while (position < source.size()) {
      unsigned char c = source[position++];
      if (c == '"') return out;
      if (c < 0x20) fail("unescaped control character");
      if (c != '\\') { out += char(c); continue; }
      if (position == source.size()) fail("incomplete escape");
      switch (source[position++]) {
      case '"': out += '"'; break; case '\\': out += '\\'; break; case '/': out += '/'; break;
      case 'b': out += '\b'; break; case 'f': out += '\f'; break; case 'n': out += '\n'; break;
      case 'r': out += '\r'; break; case 't': out += '\t'; break;
      case 'u': {
        unsigned n = hex4();
        if (n >= 0xd800 && n <= 0xdbff) {
          if (!consume('\\') || !consume('u')) fail("missing low surrogate");
          unsigned low = hex4();
          if (low < 0xdc00 || low > 0xdfff) fail("invalid low surrogate");
          n = 0x10000 + ((n - 0xd800) << 10) + low - 0xdc00;
        } else if (n >= 0xdc00 && n <= 0xdfff) fail("unpaired low surrogate");
        scalar(out, n); break;
      }
      default: fail("invalid string escape");
      }
    }
    fail("unterminated JSON string");
  }
  bool digit() const { return position < source.size() && source[position] >= '0' && source[position] <= '9'; }
  json value(unsigned depth) {
    if (depth > 32) fail("JSON nesting exceeds 32 levels");
    space();
    if (position == source.size()) fail("expected JSON value");
    if (source[position] == '"') return json::str(string());
    if (consume('{')) {
      auto out = json::map(); space();
      if (consume('}')) return out;
      do {
        space(); auto key = string(); space();
        if (!consume(':')) fail("expected ':'");
        auto item = value(depth + 1);
        if (!out.object.emplace(key, std::move(item)).second) fail("duplicate JSON field: " + key);
        space(); if (consume('}')) return out;
      } while (consume(','));
      fail("expected ',' or '}'");
    }
    if (consume('[')) {
      auto out = json::list(); space();
      if (consume(']')) return out;
      do { out.array.push_back(value(depth + 1)); space(); if (consume(']')) return out; } while (consume(','));
      fail("expected ',' or ']'");
    }
    for (const auto &word : {std::string("true"), std::string("false"), std::string("null")}) {
      if (source.compare(position, word.size(), word) == 0) { position += word.size(); return word == "null" ? json{} : json::flag(word == "true"); }
    }
    auto start = position;
    consume('-');
    if (!consume('0')) { if (!digit()) fail("expected JSON value"); while (digit()) ++position; }
    if (consume('.')) { if (!digit()) fail("missing decimal digits"); while (digit()) ++position; }
    if (consume('e') || consume('E')) { if (!consume('+')) consume('-'); if (!digit()) fail("missing exponent digits"); while (digit()) ++position; }
    json out; out.type = json::kind::number; out.text = source.substr(start, position - start); return out;
  }
public:
  explicit json_parser(const std::string &input) : source(input) {}
  json parse() { valid_utf8(source); auto out = value(0); space(); if (position != source.size()) fail("unexpected trailing JSON data"); return out; }
};

inline std::string quote(const std::string &text) {
  std::string out = "\"";
  constexpr char hex[] = "0123456789abcdef";
  for (unsigned char c : text) {
    if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
    else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
    else out += char(c);
  }
  return out + '"';
}
inline std::string dump(const json &value) {
  switch (value.type) {
  case json::kind::null: return "null";
  case json::kind::boolean: return value.boolean ? "true" : "false";
  case json::kind::string: return quote(value.text);
  case json::kind::number: return value.text;
  case json::kind::array: { std::string out = "["; for (const auto &v : value.array) { if (out.size() > 1) out += ','; out += dump(v); } return out + ']'; }
  case json::kind::object: { std::string out = "{"; for (const auto &v : value.object) { if (out.size() > 1) out += ','; out += quote(v.first) + ':' + dump(v.second); } return out + '}'; }
  }
  throw std::runtime_error("invalid JSON kind");
}
} // namespace nil::detail
