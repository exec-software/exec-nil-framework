#include "default_analyzer.hpp"
#include "analysis.hpp"
#include <array>
#include <sstream>
#include <unordered_set>

namespace ny {
namespace {
struct rule {
  const char *code, *title, *description, *phase;
  bool has_fix;
};
constexpr std::array<rule, 4> rules{{
    {"NIL1001", "Trailing whitespace", "Remove whitespace at the end of a source line outside text literals.", "syntax", true},
    {"NIL1002", "Missing final newline", "End the source file with a newline outside text literals.", "syntax", true},
    {"NIL2001", "Unreachable statement", "A statement follows an unconditional return, raise, break, or continue in the same block, or an if whose two branches exit.", "semantic", false},
    {"NIL2002", "Discarded checked outcome", "A standalone call discards a standard Option or Result; retain or handle its outcome.", "semantic", false},
}};

class analyzer {
  analysis_session &session;
  std::unordered_set<const ast_expr *> visited_expressions;

  void report(const char *code, const char *message, const ast_stmt &statement) {
    session.add(code, "warning", message, session.primary_path, statement.line);
  }

  void declaration(const ast_decl &decl) {
    switch (decl.kind) {
    case ast_node_kind::function_decl_node: {
      const auto &function = static_cast<const ast_function_decl &>(decl);
      for (const auto &param : function.params)
        if (param.default_value) expression(param.default_value->get());
      statement(function.body.get()); break;
    }
    case ast_node_kind::class_decl_node:
      for (const auto &method : static_cast<const ast_class_decl &>(decl).methods)
        if (method) declaration(*method);
      break;
    case ast_node_kind::trait_decl_node:
      for (const auto &method : static_cast<const ast_trait_decl &>(decl).methods)
        if (method) declaration(*method);
      break;
    case ast_node_kind::impl_decl_node:
      for (const auto &method : static_cast<const ast_impl_decl &>(decl).methods)
        if (method) declaration(*method);
      break;
    case ast_node_kind::test_block_node:
      statement(static_cast<const ast_test_block &>(decl).body.get()); break;
    case ast_node_kind::variable_decl_node:
      expression(static_cast<const ast_variable_decl &>(decl).initializer.get()); break;
    default: break;
    }
  }

  // Value-producing switches may occur inside any expression. Walk their
  // enclosing expressions so case bodies receive the same diagnostics as
  // ordinary blocks; a case break never makes the enclosing function exit.
  void expression(const ast_expr *node) {
    if (!node || !visited_expressions.insert(node).second) return;
    switch (node->kind) {
    case ast_node_kind::switch_expr_node: {
      const auto &value = static_cast<const ast_switch_expr &>(*node);
      expression(value.target.get());
      for (const auto &arm : value.arms) {
        expression(arm.label.get()); statement(arm.body.get());
      }
      break;
    }
    case ast_node_kind::call_expr_node: {
      const auto &value = static_cast<const ast_call_expr &>(*node);
      expression(value.callee.get());
      for (const auto &arg : value.args) expression(arg.get());
      break;
    }
    case ast_node_kind::binary_expr_node: {
      const auto &value = static_cast<const ast_binary_expr &>(*node);
      expression(value.left.get()); expression(value.right.get()); break;
    }
    case ast_node_kind::assignment_expr_node: {
      const auto &value = static_cast<const ast_assignment_expr &>(*node);
      expression(value.target.get()); expression(value.value.get()); break;
    }
    case ast_node_kind::unary_expr_node:
      expression(static_cast<const ast_unary_expr &>(*node).operand.get()); break;
    case ast_node_kind::cast_expr_node:
      expression(static_cast<const ast_cast_expr &>(*node).operand.get()); break;
    case ast_node_kind::ref_expr_node:
      expression(static_cast<const ast_ref_expr &>(*node).operand.get()); break;
    case ast_node_kind::try_expr_node:
      expression(static_cast<const ast_try_expr &>(*node).inner.get()); break;
    case ast_node_kind::member_access_expr_node:
      expression(static_cast<const ast_member_access_expr &>(*node).object.get()); break;
    case ast_node_kind::index_expr_node: {
      const auto &value = static_cast<const ast_index_expr &>(*node);
      expression(value.object.get()); expression(value.index.get()); break;
    }
    case ast_node_kind::array_literal_expr_node:
      for (const auto &item : static_cast<const ast_array_literal_expr &>(*node).elements)
        expression(item.get());
      break;
    case ast_node_kind::struct_literal_expr_node:
      for (const auto &field : static_cast<const ast_struct_literal_expr &>(*node).fields)
        expression(field.second.get());
      break;
    case ast_node_kind::allocate_expr_node:
      expression(static_cast<const ast_allocate_expr &>(*node).size.get()); break;
    case ast_node_kind::if_expr_node: {
      const auto &value = static_cast<const ast_if_expr &>(*node);
      expression(value.condition.get()); expression(value.then_expr.get());
      expression(value.else_expr.get()); break;
    }
    case ast_node_kind::match_expr_node: {
      const auto &value = static_cast<const ast_match_expr &>(*node);
      expression(value.target.get());
      for (const auto &arm : value.cases) {
        expression(arm.pattern.get()); expression(arm.guard.get()); statement(arm.body.get());
      }
      break;
    }
    case ast_node_kind::lambda_expr_node: {
      const auto &lambda = static_cast<const ast_lambda_expr &>(*node);
      for (const auto &param : lambda.params)
        if (param.default_value) expression(param.default_value->get());
      statement(lambda.body.get()); break;
    }
    case ast_node_kind::interpolated_string_expr_node:
      for (const auto &part : static_cast<const ast_interpolated_string_expr &>(*node).parts)
        expression(part.expression.get());
      break;
    case ast_node_kind::query_expr_node: {
      const auto &value = static_cast<const ast_query_expr &>(*node);
      expression(value.source.get()); expression(value.skip.get()); expression(value.take.get());
      expression(value.predicate.get()); expression(value.order_key.get());
      expression(value.projection.get()); break;
    }
    case ast_node_kind::assert_expr_node:
      for (const auto &arg : static_cast<const ast_assert_expr &>(*node).args) expression(arg.get());
      break;
    case ast_node_kind::hotreload_expr_node:
      for (const auto &arg : static_cast<const ast_hotreload_expr &>(*node).args) expression(arg.get());
      break;
    default: break;
    }
  }

  // This proof describes leaving the current block, not function termination.
  // Loops and fault handlers deliberately do not propagate it to their parent.
  bool statement(const ast_stmt *node) {
    if (!node) return false;
    switch (node->kind) {
    case ast_node_kind::block_stmt_node: {
      bool exited = false;
      for (const auto &child : static_cast<const ast_block_stmt &>(*node).statements) {
        if (!child) continue;
        if (exited) report("NIL2001", "Statement is unreachable after an unconditional exit", *child);
        else exited = statement(child.get());
      }
      return exited;
    }
    case ast_node_kind::return_stmt_node:
      expression(static_cast<const ast_return_stmt &>(*node).value.get()); return true;
    case ast_node_kind::raise_stmt_node:
      expression(static_cast<const ast_raise_stmt &>(*node).fault_expr.get()); return true;
    case ast_node_kind::break_stmt_node:
      expression(static_cast<const ast_break_stmt &>(*node).value.get()); return true;
    case ast_node_kind::continue_stmt_node:
      return true;
    case ast_node_kind::if_stmt_node: {
      const auto &branch = static_cast<const ast_if_stmt &>(*node);
      expression(branch.condition.get());
      const bool left = statement(branch.then_branch.get());
      const bool right = statement(branch.else_branch.get());
      return left && right;
    }
    case ast_node_kind::while_stmt_node: {
      const auto &value = static_cast<const ast_while_stmt &>(*node);
      expression(value.condition.get()); statement(value.body.get()); break;
    }
    case ast_node_kind::for_in_stmt_node: {
      const auto &value = static_cast<const ast_for_in_stmt &>(*node);
      expression(value.iterable.get()); statement(value.body.get()); break;
    }
    case ast_node_kind::loop_stmt_node:
      statement(static_cast<const ast_loop_stmt &>(*node).body.get()); break;
    case ast_node_kind::match_stmt_node: {
      const auto &value = static_cast<const ast_match_stmt &>(*node);
      expression(value.target.get());
      for (const auto &arm : value.arms) {
        expression(arm.guard.get()); statement(arm.body.get());
      }
      break;
    }
    case ast_node_kind::switch_stmt_node: {
      const auto &value = static_cast<const ast_switch_stmt &>(*node);
      expression(value.target.get());
      for (const auto &arm : value.arms) {
        expression(arm.label.get()); statement(arm.body.get());
      }
      break;
    }
    case ast_node_kind::attempt_stmt_node: {
      const auto &attempt = static_cast<const ast_attempt_stmt &>(*node);
      statement(attempt.body.get());
      for (const auto &handler : attempt.handlers) if (handler) {
        expression(handler->guard.get()); statement(handler->body.get());
      }
      statement(attempt.recover.get());
      break;
    }
    case ast_node_kind::decl_stmt_node: {
      const auto &decl = static_cast<const ast_decl_stmt &>(*node).declaration;
      if (decl) declaration(*decl);
      break;
    }
    case ast_node_kind::expr_stmt_node: {
      const auto &value = static_cast<const ast_expr_stmt &>(*node).expression;
      expression(value.get());
      if (!value || value->kind != ast_node_kind::call_expr_node ||
          !value->inferred_type) break;
      // Read ast_expr's checked type (it shadows ast_node::inferred_type).
      // Exact nominal owners exclude unrelated user-defined Option/Result.
      const auto &type = *value->inferred_type;
      const auto owner = type.substr(0, type.find('<'));
      if (owner == "nil::std::core::Option" || owner == "nil::std::core::Result")
        report("NIL2002", "Call result is a standard Option or Result; retain or handle its outcome", *node);
      break;
    }
    case ast_node_kind::println_stmt_node:
      for (const auto &arg : static_cast<const ast_println_stmt &>(*node).args) expression(arg.get());
      break;
    case ast_node_kind::deallocate_stmt_node:
      expression(static_cast<const ast_deallocate_stmt &>(*node).ptr.get()); break;
    case ast_node_kind::asm_stmt_node:
      for (const auto &input : static_cast<const ast_asm_stmt &>(*node).inputs) expression(input.get());
      break;
    default: break;
    }
    return false;
  }

public:
  explicit analyzer(analysis_session &value) : session(value) {}
  void run(const ast_program &program) {
    for (const auto &decl : program.declarations) if (decl) declaration(*decl);
  }
};
} // namespace

bool is_default_analysis_rule(const std::string &code) {
  for (const auto &entry : rules) if (code == entry.code) return true;
  return false;
}

std::string default_analysis_rules_json() {
  std::ostringstream out;
  out << "{\"schema\":\"nil.analyzers.v1\",\"analyzers\":[{\"id\":\"nil.default\",\"rules\":[";
  bool first = true;
  for (const auto &entry : rules) {
    if (!first) out << ',';
    first = false;
    out << "{\"code\":\"" << entry.code << "\",\"title\":\"" << entry.title
        << "\",\"description\":\"" << entry.description << "\",\"default_severity\":\"warning\",\"phase\":\""
        << entry.phase << "\",\"has_fix\":" << (entry.has_fix ? "true" : "false") << '}';
  }
  out << "]}]}\n";
  return out.str();
}

void run_default_analyzer(const ast_program &program, analysis_session &session) {
  if (session.semantic_checked) analyzer(session).run(program);
}
} // namespace ny
