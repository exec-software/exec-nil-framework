#include "submission_request.hpp"

#include "matrix_lexer.hpp"
#include "nildata.hpp"
#include "numeric_literal.hpp"
#include "../../common/strict_json.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <stdexcept>
#include <utility>

namespace matrix {
namespace {
using nil::detail::json;
constexpr std::size_t max_source_bytes = 64 * 1024;
constexpr std::size_t max_ast_nodes = 4096;
constexpr unsigned max_expression_depth = 32;

[[noreturn]] void fail(const std::string &path, const std::string &message) {
  throw submission_request_error(path, message);
}

std::string item_path(const std::string &path, std::size_t index) {
  return path + "[" + std::to_string(index) + "]";
}

void fields(const json &node, const std::string &path,
            std::initializer_list<const char *> names) {
  if (node.type != json::kind::object) fail(path, "expected an object");
  for (const auto &field : node.object) {
    if (std::none_of(names.begin(), names.end(), [&](const char *name) {
          return field.first == name;
        }))
      fail(path + "[" + nil::detail::quote(field.first) + "]", "unknown field");
  }
  for (const auto *name : names)
    if (!node.object.count(name)) fail(path + "." + name, "missing field");
}

const std::string &text(const json &node, const std::string &path) {
  if (node.type != json::kind::string) fail(path, "expected text");
  return node.text;
}

const std::vector<json> &array(const json &node, const std::string &path) {
  if (node.type != json::kind::array) fail(path, "expected an array");
  return node.array;
}

// Native text cannot represent an embedded NUL. Reject it even in unknown
// fields, rather than letting a consumer see a truncated schema or name.
void reject_nul(const json &node, const std::string &path) {
  if (node.type == json::kind::string && node.text.find('\0') != std::string::npos)
    fail(path, "embedded NUL is not supported");
  for (std::size_t i = 0; i < node.array.size(); ++i)
    reject_nul(node.array[i], item_path(path, i));
  for (const auto &entry : node.object) {
    const auto child = path + "[" + nil::detail::quote(entry.first) + "]";
    if (entry.first.find('\0') != std::string::npos)
      fail(child, "embedded NUL is not supported in field names");
    reject_nul(entry.second, child);
  }
}

std::string identifier(const json &node, const std::string &path) {
  const auto &spelling = text(node, path);
  lexer scanner(spelling);
  const auto tokens = scanner.scan();
  if (scanner.has_errors() || tokens.size() != 2 ||
      tokens[0].kind != token_kind::identifier ||
      tokens[0].escaped_identifier || tokens[0].start != 0 ||
      tokens[0].length != spelling.size() || tokens[0].lexeme != spelling ||
      tokens[1].kind != token_kind::end_of_file)
    fail(path, "expected one unescaped Matrix identifier");
  return spelling;
}

class decoder {
  std::size_t nodes_ = 0;
  std::map<std::string, std::string> symbols_;

  template <typename T, typename... Args>
  std::shared_ptr<T> make(const std::string &path, Args &&...args) {
    if (++nodes_ > max_ast_nodes) fail(path, "AST exceeds 4096 nodes");
    auto node = std::make_shared<T>(std::forward<Args>(args)...);
    node->source_file = path;
    return node;
  }

  ny::ast_expr_ptr negative(ny::ast_expr_ptr operand, const std::string &path) {
    if (auto literal = std::dynamic_pointer_cast<ny::ast_number_literal>(operand)) {
      if (const auto boundary = ny::negated_integer_boundary(literal->value)) {
        literal->value = *boundary;
        literal->source_file = path;
        return literal;
      }
    }
    return make<ny::ast_unary_expr>(path, ny::token_kind::minus, std::move(operand));
  }

  ny::ast_expr_ptr number(const json &node, const std::string &path,
                          bool allow_minimum_magnitude, unsigned depth) {
    fields(node, path, {"kind", "value"});
    const auto &spelling = text(node.at("value"), path + ".value");
    bool is_json_number = false;
    try {
      const auto parsed = nil::detail::json_parser(spelling).parse();
      is_json_number = parsed.type == json::kind::number && parsed.text == spelling;
    } catch (const std::runtime_error &) {
      // Replace parser offsets with the request field that owns the token.
    }
    if (!is_json_number)
      fail(path + ".value", "expected an exact JSON numeric token as text");
    const bool negated = spelling.front() == '-';
    const auto magnitude = negated ? spelling.substr(1) : spelling;
    if (ny::is_floating_literal_spelling(spelling)) {
      double ignored = 0;
      if (const auto error = ny::parse_floating_literal(spelling, ignored))
        fail(path + ".value", error);
    } else {
      ny::integer_literal_info info;
      if (const auto error = ny::parse_integer_literal(
              spelling, info, allow_minimum_magnitude))
        fail(path + ".value", error);
    }
    auto literal = make<ny::ast_number_literal>(path, magnitude);
    if (!negated) return literal;
    // Match Matrix unary parsing, including i32/i64 minimum-literal folding.
    if (!ny::negated_integer_boundary(magnitude) && depth == max_expression_depth)
      fail(path, "expression nesting exceeds 32 levels");
    return negative(std::move(literal), path);
  }

  ny::ast_expr_ptr expression(const json &node, const std::string &path,
                              unsigned depth = 1,
                              bool allow_minimum_magnitude = false) {
    if (depth > max_expression_depth)
      fail(path, "expression nesting exceeds 32 levels");
    if (node.type != json::kind::object) fail(path, "expected an expression object");
    const auto tag = node.object.find("kind");
    if (tag == node.object.end()) fail(path + ".kind", "missing field");
    const auto &kind = text(tag->second, path + ".kind");
    if (kind == "number")
      return number(node, path, allow_minimum_magnitude, depth);
    if (kind == "text") {
      fields(node, path, {"kind", "value"});
      return make<ny::ast_string_literal>(path, text(node.at("value"), path + ".value"));
    }
    if (kind == "boolean") {
      fields(node, path, {"kind", "value"});
      if (node.at("value").type != json::kind::boolean)
        fail(path + ".value", "expected a boolean");
      return make<ny::ast_boolean_literal>(path, node.at("value").boolean);
    }
    if (kind == "name") {
      fields(node, path, {"kind", "name"});
      const auto spelling = identifier(node.at("name"), path + ".name");
      const auto resolved = symbols_.find(fold_key(spelling));
      return make<ny::ast_identifier_expr>(path, resolved == symbols_.end()
          ? nil_symbol(spelling) : resolved->second);
    }
    if (kind == "unary") {
      fields(node, path, {"kind", "operator", "operand"});
      const auto &op = text(node.at("operator"), path + ".operator");
      if (op != "negative" && op != "not" && op != "bitwise-not")
        fail(path + ".operator", "unknown unary operator");
      auto operand = expression(node.at("operand"), path + ".operand", depth + 1,
                                op == "negative");
      if (op == "negative") return negative(std::move(operand), path);
      return make<ny::ast_unary_expr>(path, op == "not" ? ny::token_kind::not_kw
                                                         : ny::token_kind::tilde,
                                      std::move(operand));
    }
    if (kind == "binary") {
      fields(node, path, {"kind", "operator", "left", "right"});
      static const std::map<std::string, ny::token_kind> operations = {
          {"plus", ny::token_kind::plus}, {"minus", ny::token_kind::minus},
          {"times", ny::token_kind::star}, {"divided-by", ny::token_kind::slash},
          {"modulo", ny::token_kind::percent}, {"equals", ny::token_kind::double_equal},
          {"not-equals", ny::token_kind::not_equal},
          {"less-than", ny::token_kind::less}, {"less-or-equal", ny::token_kind::less_equal},
          {"greater-than", ny::token_kind::greater}, {"greater-or-equal", ny::token_kind::greater_equal},
          {"and", ny::token_kind::amp_amp}, {"or", ny::token_kind::pipe_pipe},
          {"bitwise-and", ny::token_kind::amp}, {"bitwise-or", ny::token_kind::pipe},
          {"bitwise-xor", ny::token_kind::caret},
          {"shift-left", ny::token_kind::less_less}, {"shift-right", ny::token_kind::greater_greater}};
      const auto &op = text(node.at("operator"), path + ".operator");
      const auto found = operations.find(op);
      if (found == operations.end()) fail(path + ".operator", "unknown binary operator");
      auto left = expression(node.at("left"), path + ".left", depth + 1);
      auto right = expression(node.at("right"), path + ".right", depth + 1);
      return make<ny::ast_binary_expr>(path, std::move(left), found->second, std::move(right));
    }
    if (kind == "tuple" || kind == "array") {
      fields(node, path, {"kind", "items"});
      const auto &items = array(node.at("items"), path + ".items");
      if (kind == "tuple") {
        if (items.empty()) fail(path + ".items", "a tuple requires at least one element");
        std::vector<std::pair<std::string, ny::ast_expr_ptr>> members;
        for (std::size_t i = 0; i < items.size(); ++i)
          members.emplace_back(std::to_string(i),
              expression(items[i], item_path(path + ".items", i), depth + 1));
        auto result = make<ny::ast_struct_literal_expr>(path, "tuple", std::move(members));
        result->tuple_literal = true;
        return result;
      }
      std::vector<ny::ast_expr_ptr> elements;
      for (std::size_t i = 0; i < items.size(); ++i)
        elements.push_back(expression(items[i], item_path(path + ".items", i), depth + 1));
      return make<ny::ast_array_literal_expr>(path, std::move(elements));
    }
    if (kind == "index") {
      fields(node, path, {"kind", "target", "index"});
      auto target = expression(node.at("target"), path + ".target", depth + 1);
      auto index = expression(node.at("index"), path + ".index", depth + 1);
      return make<ny::ast_index_expr>(path, std::move(target), std::move(index));
    }
    if (kind == "member") {
      fields(node, path, {"kind", "target", "index"});
      const auto &index = text(node.at("index"), path + ".index");
      std::int64_t ignored = 0;
      const auto converted = std::from_chars(index.data(), index.data() + index.size(), ignored);
      if (index.empty() || index.front() < '0' || index.front() > '9' ||
          (index.size() > 1 && index.front() == '0') ||
          converted.ec != std::errc{} || converted.ptr != index.data() + index.size())
        fail(path + ".index", "expected a canonical nonnegative i64 tuple position as text");
      auto target = expression(node.at("target"), path + ".target", depth + 1);
      return make<ny::ast_member_access_expr>(path, std::move(target), index);
    }
    if (kind == "cast") {
      fields(node, path, {"kind", "value", "type"});
      const auto &type = text(node.at("type"), path + ".type");
      if (type != "bool" && type != "i32" && type != "i64" && type != "u8" && type != "f64")
        fail(path + ".type", "expected a supported canonical primitive type");
      auto value = expression(node.at("value"), path + ".value", depth + 1);
      return make<ny::ast_cast_expr>(path, std::move(value), type);
    }
    if (kind == "conditional") {
      fields(node, path, {"kind", "condition", "then", "otherwise"});
      auto condition = expression(node.at("condition"), path + ".condition", depth + 1);
      auto then = expression(node.at("then"), path + ".then", depth + 1);
      auto otherwise = expression(node.at("otherwise"), path + ".otherwise", depth + 1);
      return make<ny::ast_if_expr>(path, std::move(condition), std::move(then), std::move(otherwise));
    }
    fail(path + ".kind", "unknown expression kind " + nil::detail::quote(kind));
  }

public:
  decoded_submission_request decode(const json &root) {
    reject_nul(root, "$");
    fields(root, "$", {"schema", "profile", "bindings", "value"});
    if (text(root.at("schema"), "$.schema") != "nil.matrix.submission-request.v1")
      fail("$.schema", "unsupported submission request schema");
    if (text(root.at("profile"), "$.profile") != "matrix.pure.v1")
      fail("$.profile", "unsupported submission profile");
    const auto &bindings = array(root.at("bindings"), "$.bindings");
    // Match Matrix's declaration spelling table before resolving references.
    std::vector<std::string> names;
    for (std::size_t i = 0; i < bindings.size(); ++i) {
      const auto path = item_path("$.bindings", i);
      fields(bindings[i], path, {"name", "value"});
      const auto spelling = identifier(bindings[i].at("name"), path + ".name");
      names.push_back(nil_symbol(spelling));
      if (!symbols_.emplace(fold_key(spelling), names.back()).second)
        fail(path + ".name", "duplicate binding after Matrix name normalization");
    }
    decoded_submission_request result;
    result.body = make<ny::ast_block_stmt>("$.bindings");
    for (std::size_t i = 0; i < bindings.size(); ++i) {
      const auto path = item_path("$.bindings", i);
      auto value = expression(bindings[i].at("value"), path + ".value");
      auto declaration = make<ny::ast_variable_decl>(path, names[i], std::move(value));
      result.body->statements.push_back(make<ny::ast_decl_stmt>(path, std::move(declaration)));
    }
    if (root.at("value").type != json::kind::null)
      result.value = expression(root.at("value"), "$.value");
    return result;
  }
};
} // namespace

decoded_submission_request decode_submission_request(const std::string &source) {
  if (source.size() > max_source_bytes) fail("$", "request exceeds 64 KiB");
  if (source.find('\0') != std::string::npos) fail("$", "embedded NUL is not supported");
  json root;
  try {
    const auto encoded = nil::data::to_json(source);
    root = nil::detail::json_parser(encoded).parse();
  } catch (const std::runtime_error &error) {
    fail("$", error.what());
  }
  return decoder{}.decode(root);
}
} // namespace matrix
