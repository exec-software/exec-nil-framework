#include "default_analyzer.hpp"
#include "analysis.hpp"
#include <array>
#include <sstream>

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

  void report(const char *code, const char *message, const ast_stmt &statement) {
    session.add(code, "warning", message, session.primary_path, statement.line);
  }

  void declaration(const ast_decl &decl) {
    switch (decl.kind) {
    case ast_node_kind::function_decl_node:
      statement(static_cast<const ast_function_decl &>(decl).body.get()); break;
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
    case ast_node_kind::raise_stmt_node:
    case ast_node_kind::break_stmt_node:
    case ast_node_kind::continue_stmt_node:
      return true;
    case ast_node_kind::if_stmt_node: {
      const auto &branch = static_cast<const ast_if_stmt &>(*node);
      const bool left = statement(branch.then_branch.get());
      const bool right = statement(branch.else_branch.get());
      return left && right;
    }
    case ast_node_kind::while_stmt_node:
      statement(static_cast<const ast_while_stmt &>(*node).body.get()); break;
    case ast_node_kind::for_in_stmt_node:
      statement(static_cast<const ast_for_in_stmt &>(*node).body.get()); break;
    case ast_node_kind::loop_stmt_node:
      statement(static_cast<const ast_loop_stmt &>(*node).body.get()); break;
    case ast_node_kind::match_stmt_node:
      for (const auto &arm : static_cast<const ast_match_stmt &>(*node).arms)
        statement(arm.body.get());
      break;
    case ast_node_kind::attempt_stmt_node: {
      const auto &attempt = static_cast<const ast_attempt_stmt &>(*node);
      statement(attempt.body.get());
      for (const auto &handler : attempt.handlers) if (handler) statement(handler->body.get());
      statement(attempt.recover.get());
      break;
    }
    case ast_node_kind::decl_stmt_node: {
      const auto &decl = static_cast<const ast_decl_stmt &>(*node).declaration;
      if (decl) declaration(*decl);
      break;
    }
    case ast_node_kind::expr_stmt_node: {
      const auto &expression = static_cast<const ast_expr_stmt &>(*node).expression;
      if (!expression || expression->kind != ast_node_kind::call_expr_node ||
          !expression->inferred_type) break;
      // Read ast_expr's checked type (it shadows ast_node::inferred_type).
      // Exact nominal owners exclude unrelated user-defined Option/Result.
      const auto &type = *expression->inferred_type;
      const auto owner = type.substr(0, type.find('<'));
      if (owner == "nil::std::core::Option" || owner == "nil::std::core::Result")
        report("NIL2002", "Call result is a standard Option or Result; retain or handle its outcome", *node);
      break;
    }
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
