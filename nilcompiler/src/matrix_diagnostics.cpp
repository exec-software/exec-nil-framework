#include "matrix_diagnostics.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace matrix {
namespace {

// Internal spelling -> what a Matrix author writes. Tier-one names are bare;
// machine types carry their 'override' marker, because that is how they must
// be written in the surface and a message that omitted it would suggest
// illegal code.
struct vocabulary_entry { const char *internal; const char *surface; };
const vocabulary_entry vocabulary[] = {
    {"String", "text"},   {"i64", "number"},  {"f64", "floating"},
    {"bool", "boolean"},  {"void", "nothing"},
    {"i32", "override integer"}, {"u8", "override byte"},
    // Widths NIL rejects. If one ever reaches a message, name it as the
    // reserved word rather than as a type the reader could use.
    {"i8", "a reserved width"},   {"i16", "a reserved width"},
    {"u16", "a reserved width"},  {"u32", "a reserved width"},
    {"u64", "a reserved width"},  {"f32", "a reserved width"},
};

bool word_char(unsigned char c) {
  return std::isalnum(c) != 0 || c == '_' || c == '-';
}

// Replace `from` with `to` only where it stands as a whole word, so a message
// mentioning 'i64_total' is left alone.
void replace_word(std::string &text, const std::string &from,
                  const std::string &to) {
  if (from.empty()) return;
  size_t at = 0;
  while ((at = text.find(from, at)) != std::string::npos) {
    const bool left_ok =
        at == 0 || !word_char(static_cast<unsigned char>(text[at - 1]));
    const size_t after = at + from.size();
    const bool right_ok =
        after >= text.size() ||
        !word_char(static_cast<unsigned char>(text[after]));
    if (left_ok && right_ok) {
      text.replace(at, from.size(), to);
      at += to.size();
    } else {
      at = after;
    }
  }
}

std::string source_line(const std::string &source, int line) {
  if (line <= 0) return {};
  int current = 1;
  size_t start = 0;
  while (current < line) {
    const size_t next = source.find('\n', start);
    if (next == std::string::npos) return {};
    start = next + 1;
    ++current;
  }
  const size_t end = source.find('\n', start);
  std::string text = source.substr(
      start, end == std::string::npos ? std::string::npos : end - start);
  if (!text.empty() && text.back() == '\r') text.pop_back();
  return text;
}

const char *severity_name(severity level) {
  switch (level) {
  case severity::error:   return "error";
  case severity::warning: return "warning";
  case severity::note:    return "note";
  case severity::help:    return "help";
  }
  return "error";
}

// Bright red for an error, yellow for a warning, blue otherwise.
const char *severity_colour(severity level) {
  switch (level) {
  case severity::error:   return "\033[1;31m";
  case severity::warning: return "\033[1;33m";
  default:                return "\033[1;34m";
  }
}

std::string json_escape(const std::string &text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (const char c : text) {
    switch (c) {
    case '"':  out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n";  break;
    case '\r': out += "\\r";  break;
    case '\t': out += "\\t";  break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) {
        static const char *digits = "0123456789abcdef";
        out += "\\u00";
        out += digits[(c >> 4) & 0xF];
        out += digits[c & 0xF];
      } else {
        out += c;
      }
    }
  }
  return out;
}

} // namespace

std::string to_surface_vocabulary(const std::string &message) {
  std::string text = message;
  for (const auto &entry : vocabulary) {
    // Quoted first, so 'i64' becomes 'number' rather than ''number''.
    replace_word(text, std::string("'") + entry.internal + "'",
                 std::string("'") + entry.surface + "'");
    replace_word(text, entry.internal, entry.surface);
  }
  return text;
}

std::string render_diagnostic(const diagnostic &d, const std::string &source,
                              const std::string &path, bool colour) {
  const char *reset = colour ? "\033[0m" : "";
  const char *bold = colour ? "\033[1m" : "";
  const char *tint = colour ? severity_colour(d.level) : "";

  std::ostringstream out;
  out << tint << severity_name(d.level);
  if (!d.code.empty()) out << "[" << d.code << "]";
  out << reset << bold << ": " << to_surface_vocabulary(d.message) << reset
      << "\n";

  const std::string gutter(std::to_string(d.primary.line).size(), ' ');
  if (!path.empty() || d.primary.line > 0) {
    out << gutter << " --> " << (path.empty() ? "<input>" : path) << ":"
        << d.primary.line;
    // A column of zero means none was available -- the shared type checker
    // reports a line only. Printing ':0' would imply a position that does
    // not exist.
    if (d.primary.column > 0) out << ":" << d.primary.column;
    out << "\n";
  }

  const std::string text = source_line(source, d.primary.line);
  if (!text.empty()) {
    out << gutter << " |\n";
    out << d.primary.line << " | " << text << "\n";

    // The caret sits under the primary span; secondary spans on the same line
    // are underlined on the same row so the relationship is visible at once.
    std::string marks(text.size() + 1, ' ');
    const auto mark = [&](const span &at, char glyph) {
      if (at.line != d.primary.line || at.column < 1) return;
      const size_t begin = static_cast<size_t>(at.column) - 1;
      const size_t width = at.length > 0 ? static_cast<size_t>(at.length) : 1;
      if (begin >= marks.size()) return;
      for (size_t i = begin; i < std::min(marks.size(), begin + width); ++i)
        marks[i] = glyph;
    };
    for (const auto &secondary : d.secondary) mark(secondary.at, '-');
    mark(d.primary, '^');
    while (!marks.empty() && marks.back() == ' ') marks.pop_back();
    if (!marks.empty())
      out << gutter << " | " << tint << marks << reset << "\n";

    for (const auto &secondary : d.secondary) {
      if (secondary.label.empty()) continue;
      out << gutter << " | ";
      const int column = secondary.at.line == d.primary.line
                             ? secondary.at.column
                             : d.primary.column;
      out << std::string(column > 1 ? column - 1 : 0, ' ')
          << to_surface_vocabulary(secondary.label) << "\n";
    }
  }

  if (!d.hint.empty())
    out << gutter << " = " << bold << "help" << reset << ": "
        << to_surface_vocabulary(d.hint) << "\n";
  if (d.fix)
    out << gutter << " = " << bold << "fix" << reset << ": "
        << to_surface_vocabulary(d.fix->description) << " -> `"
        << d.fix->replacement << "`\n";
  return out.str();
}

std::string render_diagnostic_json(const diagnostic &d,
                                   const std::string &path) {
  std::ostringstream out;
  out << "{\"severity\":\"" << severity_name(d.level) << "\"";
  if (!d.code.empty()) out << ",\"code\":\"" << json_escape(d.code) << "\"";
  out << ",\"message\":\"" << json_escape(to_surface_vocabulary(d.message))
      << "\",\"file\":\"" << json_escape(path) << "\",\"line\":"
      << d.primary.line << ",\"column\":" << d.primary.column
      << ",\"length\":" << d.primary.length;
  if (!d.secondary.empty()) {
    out << ",\"secondary\":[";
    for (size_t i = 0; i < d.secondary.size(); ++i) {
      const auto &s = d.secondary[i];
      if (i) out << ",";
      out << "{\"line\":" << s.at.line << ",\"column\":" << s.at.column
          << ",\"length\":" << s.at.length << ",\"label\":\""
          << json_escape(to_surface_vocabulary(s.label)) << "\"}";
    }
    out << "]";
  }
  if (!d.hint.empty())
    out << ",\"hint\":\"" << json_escape(to_surface_vocabulary(d.hint)) << "\"";
  if (d.fix) {
    out << ",\"fix\":{\"line\":" << d.fix->at.line << ",\"column\":"
        << d.fix->at.column << ",\"length\":" << d.fix->at.length
        << ",\"replacement\":\"" << json_escape(d.fix->replacement)
        << "\",\"description\":\""
        << json_escape(to_surface_vocabulary(d.fix->description)) << "\"}";
  }
  out << "}";
  return out.str();
}

} // namespace matrix
