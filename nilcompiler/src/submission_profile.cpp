#include "submission_profile.hpp"
#include "codegen.hpp"
#include "numeric_literal.hpp"

#include <algorithm>
#include <charconv>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>

namespace ny {
namespace {
constexpr std::size_t max_nodes = 4096, max_items = 256, max_bytes = 1024 * 1024;
constexpr unsigned max_depth = 64;
struct rejection { int line; std::string message; std::string source_file; };
[[noreturn]] void reject(int line, const std::string &message) {
  throw rejection{line, "pure submission profile: " + message, {}};
}
void bytes(std::size_t &total, const std::string &text, int line) {
  if (text.size() > max_bytes - total) reject(line, "text budget exceeded");
  total += text.size();
}
bool name(const std::string &text) {
  return !text.empty() && text.size() <= 256 &&
    std::all_of(text.begin(), text.end(), [](unsigned char c) {
      // Keep names unambiguous when the validated artifact is serialized as
      // textual NIL; punctuation such as comments and quotes is not a name.
      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
             (c >= '0' && c <= '9') || c == '_' || c == '$' || c == '.' || c >= 128;
    });
}
bool integer_type(const std::string &type) {
  return type == "i32" || type == "i64" || type == "u8" || type == "bool";
}
bool scalar_type(const std::string &type) {
  return integer_type(type) || type == "f64" || type == "text";
}
struct shape { std::string name; std::vector<shape> elements; };
class shape_parser {
  const std::string &text;
  std::size_t at = 0, nodes = 0;
  bool aliases;
  void space() {
    if (aliases) while (at < text.size() && (text[at] == ' ' || text[at] == '\t')) ++at;
  }
  shape part(unsigned depth) {
    if (depth > 32 || ++nodes > max_items) reject(0, "type shape exceeds limits");
    space();
    const auto first = at;
    while (at < text.size() && ((text[at] >= 'a' && text[at] <= 'z') ||
           (text[at] >= 'A' && text[at] <= 'Z') || (text[at] >= '0' && text[at] <= '9'))) ++at;
    auto base = text.substr(first, at - first);
    space();
    if (aliases && (base == "String" || base == "string")) base = "text";
    if (aliases && base == "int") base = "i32";
    if (scalar_type(base)) return {base, {}};
    if ((base != "array" && base != "tuple") || at == text.size() || text[at++] != '<')
      reject(0, "unsupported type shape");
    shape result{base, {}};
    do {
      result.elements.push_back(part(depth + 1));
      space();
      if (at == text.size()) reject(0, "unterminated type shape");
      if (text[at] == '>') { ++at; break; }
      if (text[at++] != ',') reject(0, "invalid type separator");
    } while (true);
    if (base == "array" && result.elements.size() != 1) reject(0, "array requires one element type");
    return result;
  }
public:
  shape_parser(const std::string &source, bool source_aliases) : text(source), aliases(source_aliases) {}
  shape parse() {
    if (text.size() > 4096) reject(0, "type spelling exceeds limits");
    auto result = part(0);
    space();
    if (at != text.size()) reject(0, "trailing type syntax");
    return result;
  }
};
std::string spelling(const shape &type) {
  if (type.elements.empty()) return type.name;
  std::string result = type.name + '<';
  for (std::size_t i = 0; i < type.elements.size(); ++i) {
    if (i) result += ',';
    result += spelling(type.elements[i]);
  }
  return result + '>';
}
shape type_shape(const std::string &text, bool aliases = false) { return shape_parser(text, aliases).parse(); }
std::size_t count(const std::string &text, std::size_t maximum, int line) {
  std::size_t value = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
      value > maximum || std::to_string(value) != text) reject(line, "invalid bounded integer operand");
  return value;
}
std::string literal_type(const std::string &text, int line, bool minimum = false) {
  if (text.empty() || text.size() > 256) reject(line, "numeric literal exceeds limits");
  if (is_floating_literal_spelling(text)) {
    double value;
    if (const auto *error = parse_floating_literal(text, value)) reject(line, error);
    return "f64";
  }
  integer_literal_info value;
  if (const auto *error = parse_integer_literal(text, value, minimum)) reject(line, error);
  return "integer";
}
bool binary_operator(token_kind op) {
  switch (op) {
  case token_kind::plus: case token_kind::minus: case token_kind::star:
  case token_kind::slash: case token_kind::percent: case token_kind::mod_kw:
  case token_kind::double_equal: case token_kind::not_equal: case token_kind::less:
  case token_kind::less_equal: case token_kind::greater: case token_kind::greater_equal:
  case token_kind::amp: case token_kind::pipe: case token_kind::caret:
  case token_kind::less_less: case token_kind::greater_greater:
  case token_kind::amp_amp: case token_kind::pipe_pipe: return true;
  default: return false;
  }
}
class ast_admission {
  std::size_t nodes = 0, text_bytes = 0;
  std::vector<std::set<std::string>> scopes{{}};
  template<class Action> void located(const ast_node &node, Action action) {
    try { action(); }
    catch (rejection &error) {
      // The innermost rejected node owns the diagnostic, including JSON paths
      // attached by the typed request decoder. Outer nodes only fill gaps.
      if (error.source_file.empty()) error.source_file = node.source_file;
      if (error.line == 0) error.line = node.line;
      throw;
    }
  }
  void enter(const ast_node &node, unsigned depth) {
    if (depth > max_depth || ++nodes > max_nodes) reject(node.line, "AST budget exceeded");
    if (!node.annotations.empty()) reject(node.line, "annotations are not admitted");
    if (node.rejected_tuple_operation) reject(node.line, "rejected tuple operation");
    bytes(text_bytes, node.source_file, node.line);
  }
  template<class T> const T &as(const ast_node &node) {
    const auto *result = dynamic_cast<const T *>(&node);
    if (!result) reject(node.line, "AST kind does not match its node");
    return *result;
  }
  void declared_type(const std::optional<std::string> &type, int line) {
    if (!type) return;
    try { (void)type_shape(*type, true); }
    catch (const rejection &) { reject(line, "only scalar, array and structural tuple types are admitted"); }
  }
  void binding(const ast_variable_decl &variable, unsigned depth, bool component = false) {
    located(variable, [&] { binding_impl(variable, depth, component); });
  }
  void binding_impl(const ast_variable_decl &variable, unsigned depth, bool component) {
    enter(variable, depth);
    if (variable.is_mut || variable.is_pub || variable.visibility != visibility_kind::visibility_private)
      reject(variable.line, "only private immutable local bindings are admitted");
    declared_type(variable.type_name, variable.line);
    if (component) {
      if (variable.initializer || !variable.tuple_bindings.empty()) reject(variable.line, "invalid tuple binding component");
    } else {
      if (!variable.initializer) reject(variable.line, "local bindings require an initializer");
      expression(variable.initializer, depth + 1);
    }
    if (!variable.tuple_bindings.empty()) {
      if (variable.tuple_bindings.size() > max_items) reject(variable.line, "too many tuple bindings");
      for (const auto &item : variable.tuple_bindings) {
        if (!item) reject(variable.line, "missing tuple binding");
        binding(*item, depth + 1, true);
      }
    } else {
      if (!name(variable.name)) reject(variable.line, "invalid local name");
      if (variable.name == "_" && component) return;
      if (!scopes.back().insert(variable.name).second) reject(variable.line, "duplicate local binding");
      if (scopes.back().size() > max_items) reject(variable.line, "too many local bindings");
    }
  }
  void scoped(const ast_stmt_ptr &node, unsigned depth) {
    scopes.emplace_back(); statement(node, depth); scopes.pop_back();
  }
  void expression(const ast_expr_ptr &node, unsigned depth, bool minimum = false) {
    if (!node) reject(0, "missing expression");
    located(*node, [&] { expression_impl(node, depth, minimum); });
  }
  void expression_impl(const ast_expr_ptr &node, unsigned depth, bool minimum) {
    enter(*node, depth);
    declared_type(node->inferred_type, node->line);
    if (node->checked_class_upcast || node->checked_reference_handle || node->checked_ownership || node->checked_callable_parameters)
      reject(node->line, "reference, ownership, class and callable values are not admitted");
    switch (node->kind) {
    case ast_node_kind::number_literal_node:
      (void)literal_type(as<ast_number_literal>(*node).value, node->line, minimum); return;
    case ast_node_kind::string_literal_node:
      bytes(text_bytes, as<ast_string_literal>(*node).value, node->line); return;
    case ast_node_kind::char_literal_node:
      bytes(text_bytes, as<ast_char_literal>(*node).value, node->line); return;
    case ast_node_kind::boolean_literal_node: (void)as<ast_boolean_literal>(*node); return;
    case ast_node_kind::identifier_expr_node: {
      const auto &id = as<ast_identifier_expr>(*node);
      if (id.resolved_global_name) reject(node->line, "global values are not admitted");
      for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) if (scope->count(id.name)) return;
      reject(node->line, "identifier is not an immutable submission local");
    }
    case ast_node_kind::binary_expr_node: {
      const auto &value = as<ast_binary_expr>(*node);
      if (!binary_operator(value.op)) reject(node->line, "operator is not admitted");
      expression(value.left, depth + 1); expression(value.right, depth + 1); return;
    }
    case ast_node_kind::unary_expr_node: {
      const auto &value = as<ast_unary_expr>(*node);
      if (value.op != token_kind::minus && value.op != token_kind::tilde && value.op != token_kind::not_kw)
        reject(node->line, "unary operator is not admitted");
      expression(value.operand, depth + 1, value.op == token_kind::minus); return;
    }
    case ast_node_kind::cast_expr_node: {
      const auto &value = as<ast_cast_expr>(*node);
      const auto type = type_shape(value.target_type, true);
      if (!type.elements.empty() || !scalar_type(type.name) || type.name == "text")
        reject(node->line, "only numeric scalar casts are admitted");
      expression(value.operand, depth + 1); return;
    }
    case ast_node_kind::array_literal_expr_node: {
      const auto &value = as<ast_array_literal_expr>(*node);
      if (value.elements.size() > max_items) reject(node->line, "array literal exceeds limits");
      for (const auto &item : value.elements) expression(item, depth + 1);
      return;
    }
    case ast_node_kind::struct_literal_expr_node: {
      const auto &value = as<ast_struct_literal_expr>(*node);
      if (!value.tuple_literal || value.type_name != "tuple" || !value.type_args.empty() || value.fields.empty() || value.fields.size() > max_items)
        reject(node->line, "only structural tuple construction is admitted");
      for (std::size_t i = 0; i < value.fields.size(); ++i) {
        if (value.fields[i].first != std::to_string(i)) reject(node->line, "invalid tuple position");
        expression(value.fields[i].second, depth + 1);
      }
      return;
    }
    case ast_node_kind::index_expr_node: {
      const auto &value = as<ast_index_expr>(*node);
      expression(value.object, depth + 1); expression(value.index, depth + 1); return;
    }
    case ast_node_kind::member_access_expr_node: {
      const auto &value = as<ast_member_access_expr>(*node);
      (void)count(value.member_name, max_items - 1, node->line);
      expression(value.object, depth + 1); return;
    }
    case ast_node_kind::if_expr_node: {
      const auto &value = as<ast_if_expr>(*node);
      expression(value.condition, depth + 1);
      expression(value.then_expr, depth + 1); expression(value.else_expr, depth + 1); return;
    }
    case ast_node_kind::interpolated_string_expr_node: {
      const auto &value = as<ast_interpolated_string_expr>(*node);
      if (value.parts.size() > max_items) reject(node->line, "too many formatted text parts");
      for (const auto &part : value.parts) {
        if (part.is_literal) {
          if (part.expression) reject(node->line, "hidden expression in formatted text literal");
          bytes(text_bytes, part.literal_value, node->line);
        } else {
          if (!part.literal_value.empty()) reject(node->line, "invalid formatted text expression");
          expression(part.expression, depth + 1);
        }
      }
      return;
    }
    default: reject(node->line, "expression kind is not admitted (calls, effects and queries are excluded)");
    }
  }
  void statement(const ast_stmt_ptr &node, unsigned depth) {
    if (!node) reject(0, "missing statement");
    located(*node, [&] { statement_impl(node, depth); });
  }
  void statement_impl(const ast_stmt_ptr &node, unsigned depth) {
    enter(*node, depth);
    switch (node->kind) {
    case ast_node_kind::block_stmt_node: {
      scopes.emplace_back();
      for (const auto &item : as<ast_block_stmt>(*node).statements) statement(item, depth + 1);
      scopes.pop_back(); return;
    }
    case ast_node_kind::decl_stmt_node: {
      const auto &declaration = as<ast_decl_stmt>(*node).declaration;
      if (!declaration || declaration->kind != ast_node_kind::variable_decl_node)
        reject(node->line, "only local binding declarations are admitted");
      binding(as<ast_variable_decl>(*declaration), depth + 1); return;
    }
    case ast_node_kind::expr_stmt_node: expression(as<ast_expr_stmt>(*node).expression, depth + 1); return;
    case ast_node_kind::if_stmt_node: {
      const auto &value = as<ast_if_stmt>(*node);
      expression(value.condition, depth + 1); scoped(value.then_branch, depth + 1);
      if (value.else_branch) scoped(value.else_branch, depth + 1);
      return;
    }
    default: reject(node->line, "statement kind is not admitted (effects, loops and declarations are excluded)");
    }
  }
public:
  void validate(const ast_block_stmt &body, const ast_expr_ptr &value) {
    located(body, [&] {
      enter(body, 0);
      for (const auto &item : body.statements) statement(item, 1);
      if (value) expression(value, 1);
    });
  }
};

struct stack_value {
  std::string type;
  // Only a NEW_STRUCT result may be initialized, in ascending field order.
  // Complete tuples cease to be mutable as soon as their final field is set.
  std::optional<std::size_t> constructing;
  bool operator==(const stack_value &other) const { return type == other.type && constructing == other.constructing; }
};
struct flow {
  std::vector<stack_value> stack;
  std::map<std::string, stack_value> locals;
  std::set<std::string> assigned;
};
std::string value_type(const std::string &type) { return integer_type(type) ? "integer" : type; }
bool numeric(const stack_value &value) { return !value.constructing && (value.type == "integer" || value.type == "f64"); }

void nil_admission(const nil_module &module) {
  if (module.module_name != "root" || module.is_library || !module.dependencies.empty() ||
      !module.embedded_sources.empty() || !module.classes.empty() || !module.enums.empty() || !module.tests.empty() ||
      module.functions.size() != 1) reject(0, "NIL must contain only the synthetic root submission function");
  const auto &function = module.functions.front();
  if (function.name != "__nil_submission" || !function.params.empty() || !function.generic_params.empty() ||
      function.is_inline || function.is_test || !function.annotations.empty() || function.source_line < 0)
    reject(0, "invalid synthetic function metadata");
  std::size_t text_bytes = 0;
  bytes(text_bytes, function.source_file, 0);
  if (function.body.empty() || function.body.size() > max_nodes || module.structs.size() > max_items ||
      module.string_constants.size() > max_items) reject(0, "NIL module budget exceeded");
  std::map<std::string, shape> schemas;
  for (const auto &definition : module.structs) {
    const auto type = type_shape(definition.name);
    if (type.name != "tuple" || definition.annotations != std::vector<std::string>{"@nil_tuple"} ||
        definition.fields.size() != type.elements.size() || !schemas.emplace(definition.name, type).second)
      reject(0, "only exact generated tuple schemas are admitted");
    bytes(text_bytes, definition.name, 0);
    for (std::size_t i = 0; i < type.elements.size(); ++i) {
      if (definition.fields[i].first != std::to_string(i) || definition.fields[i].second != spelling(type.elements[i]))
        reject(0, "tuple schema does not match its structural name");
      bytes(text_bytes, definition.fields[i].second, 0);
    }
  }
  const auto check_type = [&](const std::string &text) {
    const auto type = type_shape(text);
    const auto visit = [&](const auto &self, const shape &part) -> void {
      if (part.name == "tuple" && !schemas.count(spelling(part))) reject(0, "missing generated tuple schema");
      for (const auto &child : part.elements) self(self, child);
    };
    visit(visit, type);
    return type;
  };
  for (const auto &schema : schemas) (void)check_type(schema.first);
  if (function.return_type != "void") (void)check_type(function.return_type);
  std::set<std::string> constants;
  for (const auto &constant : module.string_constants) {
    if (!name(constant.first) || !constants.insert(constant.first).second) reject(0, "invalid or duplicate string constant");
    bytes(text_bytes, constant.first, 0); bytes(text_bytes, constant.second, 0);
  }
  std::map<std::string, std::size_t> labels;
  const std::set<std::string> arithmetic = {"ADD","SUB","MUL","DIV","MOD","NEG","BIT_AND","BIT_OR","BIT_XOR","SHL","SHR"};
  const std::set<std::string> comparisons = {"CMP_EQ","CMP_NE","CMP_LT","CMP_LE","CMP_GT","CMP_GE"};
  const std::set<std::string> bare = {"RET","POP","NOT","STR_CONCAT","GET_INDEX"};
  const auto &body = function.body;
  for (std::size_t pc = 0; pc < body.size(); ++pc) {
    const auto &instruction = body[pc]; const auto &op = instruction.opname; const auto &args = instruction.args;
    const int line = instruction.debug_line;
    if (args.size() > 2) reject(line, "too many NIL instruction operands");
    if (line < 0 || !instruction.source_file.empty() || !instruction.debug_locals.empty()) reject(line, "unexpected instruction debug metadata");
    bytes(text_bytes, op, line);
    for (const auto &argument : args) bytes(text_bytes, argument, line);
    auto arity = [&](std::size_t expected) { if (args.size() != expected) reject(line, "invalid operands for " + op); };
    if (bare.count(op) || comparisons.count(op)) arity(0);
    else if (arithmetic.count(op)) {
      arity(1);
      if (args[0] != "i32" && args[0] != "i64" && args[0] != "u8" && args[0] != "f64") reject(line, "invalid arithmetic metadata");
      if ((op.rfind("BIT_",0) == 0 || op == "SHL" || op == "SHR") && args[0] == "f64") reject(line, "invalid integer arithmetic metadata");
    } else if (op == "PUSH_CONST") { arity(1); (void)literal_type(args[0], line); }
    else if (op == "PUSH_CONST_STR") { arity(1); if (!constants.count(args[0])) reject(line, "unknown string constant"); }
    else if (op == "CAST" || op == "TO_STRING") {
      arity(1);
      if (!integer_type(args[0]) && args[0] != "f64") reject(line, "unsupported scalar conversion metadata");
    } else if (op == "LOAD_LOCAL" || op == "STORE_LOCAL" || op == "JMP" || op == "JMP_IF" || op == "JMP_IF_NOT" || op == "LABEL") {
      arity(1); if (!name(args[0])) reject(line, "invalid local or label name");
      if (op == "LABEL" && !labels.emplace(args[0], pc).second) reject(line, "duplicate label");
    } else if (op == "NEW_ARRAY_LITERAL") { arity(2); (void)count(args[0], max_items, line); (void)check_type(args[1]); }
    else if (op == "NEW_STRUCT") { arity(1); if (!schemas.count(args[0])) reject(line, "unknown tuple schema"); }
    else if (op == "SET_FIELD" || op == "GET_FIELD") { arity(1); (void)count(args[0], max_items - 1, line); }
    else reject(line, "forbidden or unknown NIL opcode " + op);
  }
  // Inspect every branch, including unreachable ones, before data flow.
  for (std::size_t pc = 0; pc < body.size(); ++pc) {
    const auto &instruction = body[pc];
    if (instruction.opname != "JMP" && instruction.opname != "JMP_IF" && instruction.opname != "JMP_IF_NOT") continue;
    const auto target = labels.find(instruction.args[0]);
    if (target == labels.end() || target->second <= pc) reject(instruction.debug_line, "branches require an existing forward local label");
  }
  std::vector<std::optional<flow>> incoming(body.size());
  incoming.front() = flow{};
  auto enqueue = [&](std::size_t pc, const flow &state, int line) {
    if (pc >= body.size()) reject(line, "submission function falls through without a return");
    if (!incoming[pc]) { incoming[pc] = state; return; }
    auto &prior = *incoming[pc];
    if (prior.stack != state.stack) reject(line, "incompatible stack shapes at branch join");
    for (auto local = prior.locals.begin(); local != prior.locals.end();) {
      const auto other = state.locals.find(local->first);
      if (other == state.locals.end()) local = prior.locals.erase(local);
      else { if (!(local->second == other->second)) reject(line, "incompatible local shapes at branch join"); ++local; }
    }
    prior.assigned.insert(state.assigned.begin(), state.assigned.end());
    if (prior.assigned.size() > max_items) reject(line, "local budget exceeded");
  };
  for (std::size_t pc = 0; pc < body.size(); ++pc) {
    const auto &instruction = body[pc]; const auto &op = instruction.opname; const auto &args = instruction.args;
    const int line = instruction.debug_line;
    if (!incoming[pc]) reject(line, "unreachable NIL instructions are not admitted");
    flow state = std::move(*incoming[pc]); incoming[pc].reset();
    const auto pop = [&]() {
      if (state.stack.empty()) reject(line, "NIL operand stack underflow");
      auto result = state.stack.back(); state.stack.pop_back();
      if (result.constructing) reject(line, "tuple escapes before initialization completes");
      return result;
    };
    const auto push = [&](stack_value value) {
      if (state.stack.size() >= max_items) reject(line, "NIL operand stack budget exceeded");
      state.stack.push_back(std::move(value));
    };
    const auto matches = [&](const stack_value &value, const std::string &type) {
      if (value.constructing || value.type != value_type(type)) reject(line, "value does not match its scalar or collection metadata");
    };
    if (op == "PUSH_CONST") push({literal_type(args[0], line), {}});
    else if (op == "PUSH_CONST_STR") push({"text", {}});
    else if (op == "LOAD_LOCAL") {
      const auto found = state.locals.find(args[0]);
      if (found == state.locals.end()) reject(line, "load of an uninitialized local");
      push(found->second);
    } else if (op == "STORE_LOCAL") {
      const auto value = pop();
      if (!state.assigned.insert(args[0]).second) reject(line, "local mutation is not admitted");
      if (state.assigned.size() > max_items) reject(line, "local budget exceeded");
      state.locals.emplace(args[0], value);
    } else if (arithmetic.count(op)) {
      const auto right = pop();
      if (!numeric(right)) reject(line, "arithmetic requires scalar operands");
      if (op != "NEG") {
        const auto left = pop(); if (!numeric(left)) reject(line, "arithmetic requires scalar operands");
        if (args[0] != "f64" && left.type != "integer") reject(line, "integer arithmetic requires integer operands");
      }
      if (args[0] != "f64" && right.type != "integer") reject(line, "integer arithmetic requires integer operands");
      push({value_type(args[0]), {}});
    } else if (comparisons.count(op)) {
      const auto right = pop(), left = pop();
      if (!(numeric(left) && numeric(right)) && !(left.type == "text" && right.type == "text")) reject(line, "comparison requires scalar or text operands");
      push({"integer", {}});
    } else if (op == "CAST" || op == "TO_STRING") {
      const auto value = pop(); if (!numeric(value)) reject(line, "scalar conversion requires a numeric value");
      if (op == "TO_STRING") matches(value, args[0]);
      push({op == "TO_STRING" ? "text" : value_type(args[0]), {}});
    } else if (op == "NOT") {
      const auto value = pop(); if (value.type != "integer") reject(line, "boolean operator requires an integer/boolean");
      push({"integer", {}});
    } else if (op == "STR_CONCAT") {
      const auto right = pop(), left = pop(); matches(right, "text"); matches(left, "text"); push({"text", {}});
    } else if (op == "NEW_ARRAY_LITERAL") {
      const auto size = count(args[0], max_items, line);
      for (std::size_t i = 0; i < size; ++i) matches(pop(), args[1]);
      push({"array<" + args[1] + ">", {}});
    } else if (op == "GET_INDEX") {
      matches(pop(), "i32"); const auto array = pop();
      const auto type = type_shape(array.type);
      if (type.name != "array") reject(line, "indexing requires an admitted array");
      push({value_type(spelling(type.elements.front())), {}});
    } else if (op == "NEW_STRUCT") push({args[0], 0});
    else if (op == "SET_FIELD") {
      const auto value = pop();
      if (state.stack.empty() || !state.stack.back().constructing) reject(line, "tuple mutation is not admitted");
      auto &target = state.stack.back(); const auto &fields = schemas.at(target.type).elements;
      const auto index = count(args[0], max_items - 1, line);
      if (index != *target.constructing || index >= fields.size()) reject(line, "tuple fields require ordered, single initialization");
      matches(value, spelling(fields[index]));
      if (++*target.constructing == fields.size()) target.constructing.reset();
    } else if (op == "GET_FIELD") {
      const auto value = pop(); const auto schema = schemas.find(value.type);
      const auto index = count(args[0], max_items - 1, line);
      if (schema == schemas.end() || index >= schema->second.elements.size()) reject(line, "field access requires an existing tuple position");
      push({value_type(spelling(schema->second.elements[index])), {}});
    } else if (op == "POP") (void)pop();
    else if (op == "RET") {
      if (function.return_type != "void") matches(pop(), function.return_type);
      if (!state.stack.empty()) reject(line, "return leaves values on the operand stack");
      continue;
    } else if (op == "JMP" || op == "JMP_IF" || op == "JMP_IF_NOT") {
      if (op != "JMP") matches(pop(), "bool");
      enqueue(labels.at(args[0]), state, line);
      if (op == "JMP") continue;
    }
    enqueue(pc + 1, state, line);
  }
}
} // namespace

std::vector<submission_diagnostic>
validate_submission_ast(const ast_block_stmt &body, const ast_expr_ptr &value) {
  try { ast_admission{}.validate(body, value); return {}; }
  catch (const rejection &error) { return {{error.line, error.message, error.source_file}}; }
}
void validate_nil_submission_profile(const nil_module &module) {
  try { nil_admission(module); }
  catch (const rejection &error) {
    throw std::runtime_error(error.message + (error.line > 0 ? " at line " + std::to_string(error.line) : ""));
  }
}
} // namespace ny
