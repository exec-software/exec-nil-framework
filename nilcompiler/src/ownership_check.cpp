#include "ownership_check.hpp"
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <utility>

namespace ny {
bool is_ownership_type(const std::string &type) {
  return type.rfind("owned<", 0) == 0 || type.rfind("borrow<", 0) == 0 ||
         type.rfind("borrow_mut<", 0) == 0;
}
bool contains_ownership_type(const std::string &type) {
  for (const auto *prefix : {"owned<", "borrow<", "borrow_mut<"}) {
    std::size_t pos = 0;
    while ((pos = type.find(prefix, pos)) != std::string::npos) {
      if (pos == 0 || !(std::isalnum(static_cast<unsigned char>(type[pos - 1])) ||
                        type[pos - 1] == '_')) return true;
      ++pos;
    }
  }
  return false;
}
std::string ownership_element_type(const std::string &type) {
  if (!is_ownership_type(type) || type.back() != '>') return {};
  const auto begin = type.find('<');
  return type.substr(begin + 1, type.size() - begin - 2);
}

namespace {
enum class binding_kind { ordinary, owner, shared, exclusive };
enum class usage { ordinary, read, write };
struct binding {
  std::size_t identity = 0;
  std::size_t owner = 0;
  binding_kind kind = binding_kind::ordinary;
  std::string name;
  std::string type;
  bool mutable_binding = false;
  bool live = true;
  unsigned shared = 0;
  bool exclusive = false;
};
struct state {
  std::vector<std::map<std::string, std::size_t>> scopes;
  std::map<std::size_t, binding> bindings;
};

class checker {
public:
  ownership_check_result run(ast_program &program) {
    for (auto &item : program.declarations) declaration(item.get(), false);
    if (result_.diagnostics.empty()) {
      for (auto *expression : result_.expressions) expression->checked_ownership = true;
      for (auto *function : result_.functions) function->checked_ownership = true;
    }
    return std::move(result_);
  }
private:
  ownership_check_result result_;
  state state_;
  std::size_t next_identity_ = 1;
  unsigned lambda_depth_ = 0;
  unsigned address_depth_ = 0;

  void error(int line, const std::string &message) {
    result_.diagnostics.push_back({line, "Ownership: " + message});
  }
  void proof(ast_expr &expression) {
    expression.checked_ownership = false;
    result_.expressions.push_back(&expression);
  }
  void enter() { state_.scopes.emplace_back(); }
  binding *lookup(const std::string &name) {
    for (auto scope = state_.scopes.rbegin(); scope != state_.scopes.rend(); ++scope) {
      const auto found = scope->find(name);
      if (found != scope->end()) return &state_.bindings.at(found->second);
    }
    return nullptr;
  }
  void leave() {
    for (const auto &entry : state_.scopes.back()) {
      const auto &item = state_.bindings.at(entry.second);
      if (item.kind == binding_kind::shared || item.kind == binding_kind::exclusive) {
        auto owner = state_.bindings.find(item.owner);
        if (owner != state_.bindings.end()) {
          if (item.kind == binding_kind::shared && owner->second.shared)
            --owner->second.shared;
          if (item.kind == binding_kind::exclusive) owner->second.exclusive = false;
        }
      }
    }
    for (const auto &entry : state_.scopes.back()) state_.bindings.erase(entry.second);
    state_.scopes.pop_back();
  }
  void bind(binding value) {
    value.identity = next_identity_++;
    if (value.kind == binding_kind::owner) value.owner = value.identity;
    if (state_.scopes.empty()) enter();
    state_.scopes.back()[value.name] = value.identity;
    state_.bindings.emplace(value.identity, std::move(value));
  }
  void ordinary_binding(const std::string &name) {
    binding value;
    value.name = name;
    bind(std::move(value));
  }
  void forbid_type(const std::string &type, int line, const std::string &where) {
    if (contains_ownership_type(type))
      error(line, "owned buffers and borrows are local-only; unsupported " + where);
  }
  binding *resource(ast_expr *expression, usage use) {
    if (!expression) return nullptr;
    if (expression->kind != ast_node_kind::identifier_expr_node) {
      expr(expression);
      return nullptr;
    }
    auto &identifier = static_cast<ast_identifier_expr &>(*expression);
    auto *value = lookup(identifier.name);
    if (!value || value->kind == binding_kind::ordinary) {
      if (contains_ownership_type(expression->inferred_type.value_or("")))
        error(expression->line, "an owned value must refer to its exact local binding");
      return nullptr;
    }
    proof(*expression);
    if (lambda_depth_) error(expression->line, "owners and borrows cannot be captured by closures");
    if (address_depth_)
      error(expression->line, "raw addresses cannot escape owned or borrowed storage");
    if (!value->live) error(expression->line, "use after move of '" + value->name + "'");
    if (value->kind == binding_kind::owner) {
      if (value->exclusive || (use == usage::write && value->shared))
        error(expression->line, "owner '" + value->name + "' is borrowed");
      if (use == usage::write && !value->mutable_binding)
        error(expression->line, "mutation requires a mutable owned binding");
    } else {
      const auto owner = state_.bindings.find(value->owner);
      if (owner == state_.bindings.end() || !owner->second.live)
        error(expression->line, "borrow outlives its owner");
      if (use == usage::write && value->kind != binding_kind::exclusive)
        error(expression->line, "mutation through an immutable borrow is forbidden");
    }
    if (use == usage::ordinary)
      error(expression->line, "owned values and borrows cannot be copied, erased, or escape; use a checked local borrow or owned_move");
    return value;
  }

  void local(ast_variable_decl &decl, bool local_scope) {
    const std::string declared = decl.type_name.value_or("");
    const std::string inferred = decl.initializer
        ? decl.initializer->inferred_type.value_or("") : "";
    const std::string type = is_ownership_type(declared) ? declared : inferred;
    binding value;
    value.name = decl.name;
    value.type = type;
    value.mutable_binding = decl.is_mut;
    ast_call_expr *call = decl.initializer &&
        decl.initializer->kind == ast_node_kind::call_expr_node
        ? static_cast<ast_call_expr *>(decl.initializer.get()) : nullptr;
    const std::string operation = call ? call->ownership_operation : "";
    if (!is_ownership_type(type) && operation.empty()) {
      forbid_type(declared, decl.line, "nested ownership storage");
      expr(decl.initializer.get());
      if (local_scope) bind(std::move(value));
      return;
    }
    if (!local_scope) error(decl.line, "owned values and borrows cannot be globals");
    if (lambda_depth_) error(decl.line, "ownership declarations in closures are not supported");
    if (operation.empty()) {
      error(decl.line, "owned bindings require owned<T>(count) or owned_move; borrows require a direct borrow initializer");
      expr(decl.initializer.get());
    } else if (operation == "owned") {
      value.kind = binding_kind::owner;
      for (auto &argument : call->args) expr(argument.get());
      proof(*call);
    } else {
      proof(*call);
      binding *source = call->args.size() == 1
          ? resource(call->args.front().get(), usage::read) : nullptr;
      if (!source || source->kind != binding_kind::owner) {
        error(decl.line, "move and borrow require an exact local owned buffer");
      } else if (operation == "owned_move") {
        if (source->shared || source->exclusive)
          error(call->line, "cannot move an owner while a borrow is live");
        source->live = false;
        value.kind = binding_kind::owner;
      } else if (operation == "owned_borrow" || operation == "owned_borrow_mut") {
        const bool exclusive = operation == "owned_borrow_mut";
        if (source->exclusive || (exclusive && source->shared))
          error(call->line, "conflicting immutable/mutable borrows");
        if (exclusive && !source->mutable_binding)
          error(call->line, "mutable borrowing requires a mutable owner");
        if (exclusive) source->exclusive = true;
        else ++source->shared;
        value.kind = exclusive ? binding_kind::exclusive : binding_kind::shared;
        value.owner = source->identity;
      }
    }
    if (local_scope) bind(std::move(value));
  }

  void function(ast_function_decl &decl) {
    decl.checked_ownership = false;
    result_.functions.push_back(&decl);
    for (const auto &param : decl.params)
      forbid_type(param.type_name.value_or(""), param.line, "function parameter");
    forbid_type(decl.return_type.value_or(""), decl.line, "function return type");
    const auto saved = state_;
    state_ = {};
    enter();
    for (const auto &param : decl.params) ordinary_binding(param.name);
    stmt(decl.body.get());
    leave();
    state_ = saved;
    for (auto &test : decl.test_blocks) declaration(test.get(), false);
    for (auto &test : decl.example_blocks) declaration(test.get(), false);
  }
  void declaration(ast_decl *decl, bool local_scope) {
    if (!decl) return;
    switch (decl->kind) {
    case ast_node_kind::variable_decl_node:
      local(static_cast<ast_variable_decl &>(*decl), local_scope); break;
    case ast_node_kind::function_decl_node:
      function(static_cast<ast_function_decl &>(*decl)); break;
    case ast_node_kind::struct_decl_node:
      for (const auto &field : static_cast<ast_struct_decl &>(*decl).fields)
        forbid_type(field.type_name, decl->line, "aggregate field");
      break;
    case ast_node_kind::class_decl_node: {
      auto &value = static_cast<ast_class_decl &>(*decl);
      for (const auto &field : value.fields) forbid_type(field.type_name, decl->line, "class field");
      for (auto &method : value.methods) function(*method);
      break;
    }
    case ast_node_kind::impl_decl_node:
      for (auto &method : static_cast<ast_impl_decl &>(*decl).methods) function(*method);
      break;
    case ast_node_kind::trait_decl_node:
      for (auto &method : static_cast<ast_trait_decl &>(*decl).methods) function(*method);
      break;
    case ast_node_kind::enum_decl_node:
      for (const auto &variant : static_cast<ast_enum_decl &>(*decl).variants)
        forbid_type(variant.associated_type.value_or(""), decl->line, "enum payload");
      break;
    case ast_node_kind::test_block_node: {
      const auto saved = state_;
      state_ = {};
      stmt(static_cast<ast_test_block &>(*decl).body.get());
      state_ = saved;
      break;
    }
    default: break;
    }
  }
  void scoped_stmt(ast_stmt *statement) {
    enter(); stmt(statement); leave();
  }
  void join(state left, state right) {
    // Both branches begin with identical outer bindings. Only their liveness
    // changes may survive; local borrows have already left their branch scope.
    state_ = left;
    for (auto &entry : state_.bindings) {
      const auto other = right.bindings.find(entry.first);
      if (other != right.bindings.end()) entry.second.live &= other->second.live;
    }
  }
  void require_invariant(const state &before, int line, const char *edge) {
    for (const auto &entry : before.bindings) {
      const auto after = state_.bindings.find(entry.first);
      if (after != state_.bindings.end() && entry.second.live != after->second.live)
        error(line, std::string("cannot move an outer owner across a ") + edge + " boundary");
    }
  }
  void pattern(const ast_pattern &value) {
    if (value.kind == ast_pattern_kind::variable_pattern && value.name != "_")
      ordinary_binding(value.name);
    for (const auto &child : value.subpatterns) pattern(child);
  }
  void switch_cases(ast_expr *target, std::vector<ast_switch_arm> &arms, int line) {
    expr(target);
    const auto before = state_;
    for (auto &arm : arms) {
      state_ = before;
      expr(arm.label.get());
      scoped_stmt(arm.body.get());
      // A case can be entered directly or through the preceding case. Until
      // ownership has a general control-flow join, require outer owners to
      // remain live on both paths. Case-local owners/borrows still end here.
      require_invariant(before, arm.line ? arm.line : line, "switch");
    }
    state_ = before;
  }
  void stmt(ast_stmt *statement) {
    if (!statement) return;
    switch (statement->kind) {
    case ast_node_kind::block_stmt_node:
      enter();
      for (auto &item : static_cast<ast_block_stmt &>(*statement).statements) stmt(item.get());
      leave(); break;
    case ast_node_kind::decl_stmt_node:
      declaration(static_cast<ast_decl_stmt &>(*statement).declaration.get(), true); break;
    case ast_node_kind::expr_stmt_node:
      expr(static_cast<ast_expr_stmt &>(*statement).expression.get()); break;
    case ast_node_kind::println_stmt_node:
      for (auto &item : static_cast<ast_println_stmt &>(*statement).args) expr(item.get());
      break;
    case ast_node_kind::return_stmt_node:
      expr(static_cast<ast_return_stmt &>(*statement).value.get()); break;
    case ast_node_kind::break_stmt_node:
      expr(static_cast<ast_break_stmt &>(*statement).value.get()); break;
    case ast_node_kind::if_stmt_node: {
      auto &value = static_cast<ast_if_stmt &>(*statement);
      expr(value.condition.get());
      const auto before = state_;
      scoped_stmt(value.then_branch.get());
      const auto then_state = state_;
      state_ = before;
      scoped_stmt(value.else_branch.get());
      join(then_state, state_); break;
    }
    case ast_node_kind::while_stmt_node: {
      auto &value = static_cast<ast_while_stmt &>(*statement);
      expr(value.condition.get()); const auto before = state_;
      scoped_stmt(value.body.get()); require_invariant(before, value.line, "loop");
      break;
    }
    case ast_node_kind::loop_stmt_node: {
      auto &value = static_cast<ast_loop_stmt &>(*statement);
      const auto before = state_;
      scoped_stmt(value.body.get()); require_invariant(before, value.line, "loop"); break;
    }
    case ast_node_kind::for_in_stmt_node: {
      auto &value = static_cast<ast_for_in_stmt &>(*statement);
      expr(value.iterable.get()); const auto before = state_;
      enter(); for (const auto &var : value.get_variables()) ordinary_binding(var.name);
      stmt(value.body.get()); leave();
      require_invariant(before, value.line, "loop"); break;
    }
    case ast_node_kind::match_stmt_node: {
      auto &value = static_cast<ast_match_stmt &>(*statement);
      expr(value.target.get()); const auto before = state_; auto merged = before;
      for (auto &arm : value.arms) {
        state_ = before; enter(); pattern(arm.pattern);
        expr(arm.guard.get()); stmt(arm.body.get()); leave();
        join(merged, state_); merged = state_;
      }
      state_ = merged; break;
    }
    case ast_node_kind::switch_stmt_node: {
      auto &value = static_cast<ast_switch_stmt &>(*statement);
      switch_cases(value.target.get(), value.arms, value.line);
      break;
    }
    case ast_node_kind::attempt_stmt_node: {
      auto &value = static_cast<ast_attempt_stmt &>(*statement);
      const auto before = state_;
      scoped_stmt(value.body.get()); require_invariant(before, value.line, "fault region");
      for (auto &handler : value.handlers) {
        state_ = before; enter(); ordinary_binding(handler->variable);
        expr(handler->guard.get()); stmt(handler->body.get()); leave();
        require_invariant(before, handler->line, "fault handler");
      }
      state_ = before; scoped_stmt(value.recover.get());
      require_invariant(before, value.line, "fault recovery"); break;
    }
    case ast_node_kind::raise_stmt_node:
      expr(static_cast<ast_raise_stmt &>(*statement).fault_expr.get()); break;
    case ast_node_kind::deallocate_stmt_node:
      expr(static_cast<ast_deallocate_stmt &>(*statement).ptr.get()); break;
    case ast_node_kind::asm_stmt_node:
      for (auto &input : static_cast<ast_asm_stmt &>(*statement).inputs) expr(input.get());
      break;
    default: break;
    }
  }
  void expr(ast_expr *expression, usage use = usage::ordinary) {
    if (!expression) return;
    switch (expression->kind) {
    case ast_node_kind::identifier_expr_node:
      resource(expression, use); break;
    case ast_node_kind::call_expr_node: {
      auto &value = static_cast<ast_call_expr &>(*expression);
      if (!value.ownership_operation.empty()) {
        proof(value);
        error(value.line, "ownership operations require a direct local declaration initializer");
      }
      expr(value.callee.get()); for (auto &argument : value.args) expr(argument.get()); break;
    }
    case ast_node_kind::unary_expr_node: {
      auto &value = static_cast<ast_unary_expr &>(*expression);
      if (value.op == token_kind::amp) ++address_depth_;
      if (value.op == token_kind::star && value.operand &&
          is_ownership_type(value.operand->inferred_type.value_or(""))) {
        resource(value.operand.get(), use == usage::write ? usage::write : usage::read);
        proof(value);
      } else expr(value.operand.get());
      if (value.op == token_kind::amp) --address_depth_;
      break;
    }
    case ast_node_kind::index_expr_node: {
      auto &value = static_cast<ast_index_expr &>(*expression);
      if (value.object && is_ownership_type(value.object->inferred_type.value_or(""))) {
        resource(value.object.get(), use == usage::write ? usage::write : usage::read);
        proof(value);
      } else expr(value.object.get());
      expr(value.index.get()); break;
    }
    case ast_node_kind::assignment_expr_node: {
      auto &value = static_cast<ast_assignment_expr &>(*expression);
      if (value.target && value.target->kind == ast_node_kind::identifier_expr_node) {
        const auto &name = static_cast<ast_identifier_expr &>(*value.target).name;
        const auto *target = lookup(name);
        if (target && target->kind != binding_kind::ordinary)
          error(value.line, "owner/borrow rebinding is unsupported; move into a new local declaration");
      }
      expr(value.target.get(), usage::write); expr(value.value.get()); break;
    }
    case ast_node_kind::binary_expr_node: {
      auto &value = static_cast<ast_binary_expr &>(*expression);
      expr(value.left.get()); expr(value.right.get()); break;
    }
    case ast_node_kind::member_access_expr_node:
      expr(static_cast<ast_member_access_expr &>(*expression).object.get()); break;
    case ast_node_kind::cast_expr_node: {
      auto &value = static_cast<ast_cast_expr &>(*expression);
      forbid_type(value.target_type, value.line, "cast target");
      expr(value.operand.get()); break;
    }
    case ast_node_kind::array_literal_expr_node:
      for (auto &item : static_cast<ast_array_literal_expr &>(*expression).elements) expr(item.get());
      break;
    case ast_node_kind::query_expr_node: {
      auto &value = static_cast<ast_query_expr &>(*expression);
      expr(value.source.get()); expr(value.skip.get()); expr(value.take.get());
      const auto before = state_;
      enter();
      ordinary_binding(value.binding.name);
      expr(value.predicate.get()); expr(value.order_key.get()); expr(value.projection.get());
      leave();
      require_invariant(before, value.line, "query");
      break;
    }
    case ast_node_kind::struct_literal_expr_node:
      for (auto &field : static_cast<ast_struct_literal_expr &>(*expression).fields) expr(field.second.get());
      break;
    case ast_node_kind::allocate_expr_node: {
      auto &value = static_cast<ast_allocate_expr &>(*expression);
      forbid_type(value.type_name, value.line, "raw allocation element type");
      forbid_type(value.inferred_type.value_or(""), value.line, "raw allocation element type");
      expr(value.size.get()); break;
    }
    case ast_node_kind::ref_expr_node:
      expr(static_cast<ast_ref_expr &>(*expression).operand.get()); break;
    case ast_node_kind::try_expr_node:
      expr(static_cast<ast_try_expr &>(*expression).inner.get()); break;
    case ast_node_kind::assert_expr_node:
      for (auto &argument : static_cast<ast_assert_expr &>(*expression).args) expr(argument.get());
      break;
    case ast_node_kind::hotreload_expr_node:
      for (auto &argument : static_cast<ast_hotreload_expr &>(*expression).args) expr(argument.get());
      break;
    case ast_node_kind::interpolated_string_expr_node:
      for (auto &part : static_cast<ast_interpolated_string_expr &>(*expression).parts) expr(part.expression.get());
      break;
    case ast_node_kind::if_expr_node: {
      auto &value = static_cast<ast_if_expr &>(*expression);
      expr(value.condition.get()); expr(value.then_expr.get()); expr(value.else_expr.get()); break;
    }
    case ast_node_kind::match_expr_node: {
      auto &value = static_cast<ast_match_expr &>(*expression);
      expr(value.target.get()); const auto before = state_; auto merged = before;
      for (auto &arm : value.cases) {
        state_ = before; enter(); expr(arm.pattern.get()); expr(arm.guard.get());
        stmt(arm.body.get()); leave(); join(merged, state_); merged = state_;
      }
      state_ = merged; break;
    }
    case ast_node_kind::switch_expr_node: {
      auto &value = static_cast<ast_switch_expr &>(*expression);
      switch_cases(value.target.get(), value.arms, value.line);
      break;
    }
    case ast_node_kind::lambda_expr_node: {
      auto &value = static_cast<ast_lambda_expr &>(*expression);
      forbid_type(value.return_type.value_or(""), value.line, "closure return");
      ++lambda_depth_; enter();
      for (const auto &param : value.params) {
        forbid_type(param.type_name.value_or(""), value.line, "closure parameter");
        ordinary_binding(param.name);
      }
      stmt(value.body.get()); leave(); --lambda_depth_; break;
    }
    default: break;
    }
  }
};
} // namespace
ownership_check_result check_ownership(ast_program &program) {
  return checker().run(program);
}
} // namespace ny
