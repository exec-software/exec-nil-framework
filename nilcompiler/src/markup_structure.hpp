#ifndef NIL_COMPILER_MARKUP_STRUCTURE_HPP
#define NIL_COMPILER_MARKUP_STRUCTURE_HPP

#include "ast.hpp"

#include <cctype>
#include <string>
#include <vector>

namespace ny {

// The markup a 'begin markup' (Matrix) or '$markup' (Ny) literal contains,
// read as markup rather than as text. Both frontends drive this one
// implementation: they differ in how a hole's tokens are lexed and in which
// words open a directive, never in what a document means.
//
// Two jobs. It checks that the document is well formed -- elements balanced
// and closed in order, every conditional and repeat arm leaving the element
// stack as it found it -- and it tells the caller which context each hole
// landed in, which is what decides the escaping the author never writes.
//
// HTML's closed sets live here rather than in either grammar (NIL-0027 3.4):
// a second dialect is another table, not another parser.
class markup_structure {
public:
  enum class context { text, tag, attribute_value, raw_text, comment };

  std::vector<ast_markup_item> items;
  // The first structural error only. A document whose first element is
  // mismatched produces a cascade of false positives after it, and the first
  // one is the one the author has to fix.
  std::string error, hint;
  int error_line = 0;

  bool failed() const { return !error.empty(); }
  context state() const { return state_; }
  std::size_t open_elements() const { return open_.size(); }
  int open_directive() const { return directive_.empty() ? 0 : directive_.back(); }
  bool inside_directive() const { return !directive_.empty(); }

  // One literal segment of the block, scanned as markup. Context carries
  // across segments, because a hole may sit anywhere in a document.
  void literal(const std::string &text, int line) {
    resolve_pending_url(text);
    std::size_t cursor = 0;
    while (cursor < text.size()) {
      const char c = text[cursor];
      if (state_ == context::comment) {
        const auto close = text.find("-->", cursor);
        const auto stop = close == std::string::npos ? text.size() : close + 3;
        emit(text.substr(cursor, stop - cursor), line);
        if (close != std::string::npos) state_ = context::text;
        cursor = stop;
        continue;
      }
      if (state_ == context::raw_text) {
        const auto close = lower(text).find("</" + raw_name_, cursor);
        const auto stop = close == std::string::npos ? text.size() : close;
        emit(text.substr(cursor, stop - cursor), line);
        cursor = stop;
        if (close == std::string::npos) continue;
        state_ = context::text; // the close tag itself is ordinary text
        continue;
      }
      if (state_ == context::attribute_value) {
        const auto close = text.find(quote_, cursor);
        const auto stop = close == std::string::npos ? text.size() : close + 1;
        emit(text.substr(cursor, stop - cursor), line);
        if (close != std::string::npos) {
          state_ = context::tag;
          attribute_.clear();
        } else {
          value_started_ = true;
        }
        cursor = stop;
        continue;
      }
      if (state_ == context::tag) {
        if (c == '>') {
          emit(">", line);
          ++cursor;
          if (current_pushed_ && !open_.empty() && raw_text_element(open_.back())) {
            raw_name_ = open_.back();
            state_ = context::raw_text;
          } else {
            state_ = context::text;
          }
          continue;
        }
        if (c == '/' && cursor + 1 < text.size() && text[cursor + 1] == '>') {
          emit("/>", line);
          cursor += 2;
          if (current_pushed_ && !open_.empty()) open_.pop_back();
          current_pushed_ = false;
          state_ = context::text;
          continue;
        }
        if (c == '=') {
          emit("=", line);
          ++cursor;
          while (cursor < text.size() && (text[cursor] == ' ' || text[cursor] == '\t'))
            emit(std::string(1, text[cursor++]), line);
          if (cursor < text.size() && (text[cursor] == '"' || text[cursor] == '\'')) {
            quote_ = text[cursor];
            value_started_ = false;
            state_ = context::attribute_value;
            emit(std::string(1, text[cursor++]), line);
          } else {
            fail(line, "an attribute value must be quoted",
                 "write name=\"value\"; the renderer never adds quotes for you");
          }
          continue;
        }
        if (name_byte(static_cast<unsigned char>(c))) {
          const auto begin = cursor;
          while (cursor < text.size() &&
                 name_byte(static_cast<unsigned char>(text[cursor])))
            ++cursor;
          attribute_ = lower(text.substr(begin, cursor - begin));
          emit(text.substr(begin, cursor - begin), line);
          continue;
        }
        emit(std::string(1, c), line);
        ++cursor;
        continue;
      }
      // context::text
      if (c != '<') {
        const auto stop = text.find('<', cursor);
        const auto end = stop == std::string::npos ? text.size() : stop;
        emit(text.substr(cursor, end - cursor), line);
        cursor = end;
        continue;
      }
      if (text.compare(cursor, 4, "<!--") == 0) {
        state_ = context::comment;
        emit("<!--", line);
        cursor += 4;
        continue;
      }
      if (cursor + 1 < text.size() && text[cursor + 1] == '!') {
        const auto close = text.find('>', cursor);
        const auto stop = close == std::string::npos ? text.size() : close + 1;
        emit(text.substr(cursor, stop - cursor), line);
        cursor = stop;
        continue;
      }
      if (cursor + 1 < text.size() && text[cursor + 1] == '/') {
        auto name_end = cursor + 2;
        while (name_end < text.size() &&
               name_byte(static_cast<unsigned char>(text[name_end])))
          ++name_end;
        const auto name = lower(text.substr(cursor + 2, name_end - cursor - 2));
        const auto close = text.find('>', name_end);
        const auto stop = close == std::string::npos ? text.size() : close + 1;
        emit(text.substr(cursor, stop - cursor), line);
        cursor = stop;
        if (!arm_base_.empty() && open_.size() <= arm_base_.back())
          fail(line, "'</" + name + ">' closes an element this arm did not open",
               "each arm of a conditional or a repeat must be a complete fragment");
        else if (open_.empty())
          fail(line, "'</" + name + ">' closes an element that is not open");
        else if (open_.back() != name)
          fail(line, "expected '</" + open_.back() + ">', found '</" + name + ">'");
        else
          open_.pop_back();
        continue;
      }
      auto name_end = cursor + 1;
      while (name_end < text.size() &&
             name_byte(static_cast<unsigned char>(text[name_end])))
        ++name_end;
      if (name_end == cursor + 1) {
        fail(line, "'<' must open an element",
             "write '&lt;' for a literal less-than sign");
        emit("<", line);
        ++cursor;
        continue;
      }
      const auto name = lower(text.substr(cursor + 1, name_end - cursor - 1));
      emit(text.substr(cursor, name_end - cursor), line);
      cursor = name_end;
      current_pushed_ = !void_element(name);
      if (current_pushed_) open_.push_back(name);
      attribute_.clear();
      state_ = context::tag;
    }
  }

  // A value hole. Its escaping is the position's, never the author's.
  void hole(ast_expr_ptr expr, bool raw, int line) {
    resolve_pending_url(std::string());
    ast_markup_item entry;
    entry.kind = ast_markup_item::item_kind::hole;
    entry.line = line;
    entry.expr = std::move(expr);
    if (raw) {
      entry.escape = "raw";
    } else {
      switch (state_) {
      case context::attribute_value:
        if (url_attribute(attribute_)) {
          entry.escape = value_started_ ? "url_component" : "url";
          if (!value_started_) pending_url_ = items.size();
        } else {
          entry.escape = "attribute";
        }
        value_started_ = true;
        break;
      case context::text:
        entry.escape = "text";
        break;
      case context::raw_text:
        entry.escape = "raw";
        fail(line, "a script or style body interpolates only through '{raw ...}'",
             "braces are ordinary characters there, so the body must be exactly one '{raw ...}' hole");
        break;
      case context::comment:
        entry.escape = "text";
        fail(line, "a hole cannot appear inside a comment");
        break;
      case context::tag:
        entry.escape = "attribute";
        fail(line, "a hole cannot name an element or an attribute",
             "element and attribute names are fixed so the document stays checkable");
        break;
      }
    }
    items.push_back(std::move(entry));
  }

  void open_when(ast_expr_ptr condition, int line) {
    add(ast_markup_item::item_kind::when_begin, line).expr = std::move(condition);
    directive_.push_back(1);
    arm_base_.push_back(open_.size());
  }

  // 'otherwise' with no condition closes the arm and opens the final one.
  void else_arm(ast_expr_ptr condition, int line) {
    if (directive_.empty() || directive_.back() != 1) {
      fail(line, "an alternative without an open conditional");
      return;
    }
    if (open_.size() != arm_base_.back())
      fail(line, "this arm leaves an element open",
           "close every element the arm opens before the next one");
    if (condition)
      add(ast_markup_item::item_kind::else_when, line).expr = std::move(condition);
    else
      add(ast_markup_item::item_kind::else_arm, line);
  }

  void open_repeat(std::string binding, ast_expr_ptr source, int line) {
    auto &entry = add(ast_markup_item::item_kind::repeat_begin, line);
    entry.binding = std::move(binding);
    entry.expr = std::move(source);
    directive_.push_back(2);
    arm_base_.push_back(open_.size());
  }

  void close_directive(int kind, int line) {
    if (directive_.empty()) {
      fail(line, "a closing directive without an open one");
      return;
    }
    if (open_.size() != arm_base_.back())
      fail(line, "this arm leaves an element open",
           "each arm of a conditional or a repeat must be a complete fragment");
    if (directive_.back() != kind)
      fail(line, directive_.back() == 1 ? "expected the conditional to close here"
                                        : "expected the repeat to close here");
    add(kind == 2 ? ast_markup_item::item_kind::end_repeat
                  : ast_markup_item::item_kind::end_when, line);
    directive_.pop_back();
    arm_base_.pop_back();
  }

  void finish(int line) {
    resolve_pending_url(std::string());
    if (!open_.empty())
      fail(line, "'<" + open_.back() + ">' is never closed",
           "every element a markup block opens must close inside it");
    if (!directive_.empty())
      fail(line, directive_.back() == 1 ? "a conditional is never closed"
                                        : "a repeat is never closed");
  }

private:
  context state_ = context::text;
  std::vector<std::string> open_;
  std::vector<std::size_t> arm_base_;
  std::vector<int> directive_; // 1 conditional, 2 repeat
  std::string attribute_, raw_name_;
  char quote_ = 0;
  bool value_started_ = false;
  // A void element is never pushed, so '<br/>' must not pop whatever is.
  bool current_pushed_ = false;
  std::size_t pending_url_ = std::string::npos;

  ast_markup_item &add(ast_markup_item::item_kind kind, int line) {
    ast_markup_item entry;
    entry.kind = kind;
    entry.line = line;
    items.push_back(std::move(entry));
    return items.back();
  }

  void emit(const std::string &text, int line) {
    if (text.empty()) return;
    if (!items.empty() && items.back().kind == ast_markup_item::item_kind::literal) {
      items.back().text += text;
      return;
    }
    add(ast_markup_item::item_kind::literal, line).text = text;
  }

  void fail(int line, const std::string &message, const std::string &advice = {}) {
    if (!error.empty()) return;
    error = message;
    hint = advice;
    error_line = line;
  }

  // A hole in a URL attribute is the whole value only when the value ends
  // immediately after it.
  void resolve_pending_url(const std::string &next) {
    if (pending_url_ == std::string::npos) return;
    const bool whole = !next.empty() && next[0] == quote_;
    items[pending_url_].escape = whole ? "url" : "url_component";
    pending_url_ = std::string::npos;
  }

  static std::string lower(std::string value) {
    for (auto &letter : value)
      if (letter >= 'A' && letter <= 'Z') letter += 'a' - 'A';
    return value;
  }
  static bool name_byte(unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '_' || c == ':' || c >= 128;
  }
  static bool void_element(const std::string &name) {
    static const char *const names[] = {
        "area", "base", "br", "col", "embed", "hr", "img", "input",
        "link", "meta", "param", "source", "track", "wbr"};
    for (const auto *candidate : names)
      if (name == candidate) return true;
    return false;
  }
  static bool raw_text_element(const std::string &name) {
    return name == "script" || name == "style";
  }
  // Attributes whose value is a URL. A hole that is the whole value is scheme
  // filtered; a hole inside a longer value is percent encoded, because it is a
  // component of someone else's URL rather than the URL itself.
  static bool url_attribute(const std::string &name) {
    static const char *const names[] = {
        "href", "src", "action", "formaction", "cite", "poster",
        "data", "srcset", "background", "ping", "manifest"};
    for (const auto *candidate : names)
      if (name == candidate) return true;
    return false;
  }
};

} // namespace ny
#endif // NIL_COMPILER_MARKUP_STRUCTURE_HPP
