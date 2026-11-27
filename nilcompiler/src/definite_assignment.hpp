#ifndef NIL_DEFINITE_ASSIGNMENT_HPP
#define NIL_DEFINITE_ASSIGNMENT_HPP

#include "ast.hpp"
#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ny::detail {

struct assignment_diagnostic {
  int line;
  std::string message, source_file, hint;
};

// Run after name/type resolution, but before either backend. This is a lexical
// must-analysis: only bindings assigned on every reachable predecessor may be
// read. It does not guess call effects, loop trip counts or alias writes.
class definite_assignment_checker {
  const std::map<const ast_expr *, ast_pattern> &patterns_;
  struct binding {
    const ast_variable_decl *declaration = nullptr;
    bool assigned = true;
  };
  struct flow {
    std::vector<std::map<std::string, binding>> scopes;
    bool reachable = true;
  };
  enum class transfer_kind { return_, fault, break_, continue_ };
  struct transfer {
    transfer_kind kind;
    const ast_node *target;
    flow state;
    // Empty means an unknown call/assertion fault, not a catchable nominal type.
    std::string fault_type;
  };
  struct control { const ast_node *node; bool loop; };
  flow state_;
  std::vector<transfer> transfers_;
  std::vector<control> controls_;
  unsigned attempts_ = 0;
  std::string source_;
  std::vector<assignment_diagnostic> diagnostics_;
  std::set<std::pair<const ast_expr *, const ast_variable_decl *>> reported_;

  void enter() { state_.scopes.emplace_back(); }
  void leave() { state_.scopes.pop_back(); }
  binding *lookup(const std::string &name) {
    for (auto it = state_.scopes.rbegin(); it != state_.scopes.rend(); ++it) {
      auto found = it->find(name);
      if (found != it->end()) return &found->second;
    }
    return nullptr;
  }
  void bind(const std::string &name, const ast_variable_decl *decl = nullptr,
            bool assigned = true) {
    if (name.empty() || name == "_") return;
    if (state_.scopes.empty()) enter();
    state_.scopes.back()[name] = {decl, assigned};
  }
  static flow join(const flow &base, const std::vector<flow> &paths) {
    flow result = base;
    result.reachable = std::any_of(paths.begin(), paths.end(),
        [](const flow &value) { return value.reachable; });
    for (std::size_t scope = 0; scope < result.scopes.size(); ++scope) {
      for (auto &[name, value] : result.scopes[scope]) {
        value.assigned = true;
        for (const auto &path : paths) {
          if (!path.reachable) continue;
          if (scope >= path.scopes.size()) { value.assigned = false; break; }
          const auto found = path.scopes[scope].find(name);
          if (found == path.scopes[scope].end() ||
              found->second.declaration != value.declaration || !found->second.assigned) {
            value.assigned = false;
            break;
          }
        }
      }
    }
    return result;
  }
  static std::optional<bool> boolean(const ast_expr *expression) {
    if (expression && expression->kind == ast_node_kind::boolean_literal_node)
      return static_cast<const ast_boolean_literal &>(*expression).value;
    return std::nullopt;
  }
  void read(ast_identifier_expr &expression) {
    if (expression.resolved_global_name) return;
    const auto *value = lookup(expression.name);
    if (!value || value->assigned || !value->declaration ||
        !reported_.emplace(&expression, value->declaration).second) return;
    const auto &decl = *value->declaration;
    diagnostics_.push_back({expression.line,
        "Local '" + expression.name + "' may be read before definite assignment "
        "(declared at line " + std::to_string(decl.line) + ")",
        expression.source_file.empty() ? source_ : expression.source_file,
        "Initialize '" + expression.name + "' at its declaration or assign it on every path before this read"});
  }
  void assign(ast_expr *expression) {
    if (expression && expression->kind == ast_node_kind::identifier_expr_node) {
      auto &identifier = static_cast<ast_identifier_expr &>(*expression);
      if (!identifier.resolved_global_name)
        if (auto *value = lookup(identifier.name)) value->assigned = true;
    }
  }
  void escape(transfer_kind kind, const ast_node *target = nullptr,
              std::string fault_type = {}) {
    if (state_.reachable) transfers_.push_back({kind, target, state_, std::move(fault_type)});
    state_.reachable = false;
  }
  void possible_fault() {
    // Ordinary calls can propagate a typed fault before their destination is
    // assigned. Only retain those edges while a surrounding attempt needs them.
    if (attempts_ && state_.reachable)
      transfers_.push_back({transfer_kind::fault, nullptr, state_});
  }
  void scoped(ast_stmt *statement) { enter(); stmt(statement); leave(); }
  void local(ast_variable_decl &decl) {
    expr(decl.initializer.get());
    if (decl.tuple_bindings.empty()) bind(decl.name, &decl, bool(decl.initializer));
    else for (auto &item : decl.tuple_bindings)
      if (item) bind(item->name, item.get(), bool(decl.initializer));
  }
  void pattern(const ast_pattern &value) {
    if (value.kind == ast_pattern_kind::variable_pattern) bind(value.name);
    for (const auto &child : value.subpatterns) pattern(child);
  }
  static bool unconditional(const ast_pattern &value) {
    return value.kind == ast_pattern_kind::wildcard_pattern ||
           value.kind == ast_pattern_kind::variable_pattern ||
           (value.kind == ast_pattern_kind::constructor_pattern && value.name.empty() &&
            std::all_of(value.subpatterns.begin(), value.subpatterns.end(), unconditional));
  }
  bool expression_pattern(ast_expr *value) {
    const auto found = patterns_.find(value);
    if (found != patterns_.end()) {
      pattern(found->second);
      return unconditional(found->second);
    }
    // Bare identifiers and ordinary calls are evaluated comparisons here,
    // unlike the variable bindings in statement-style match patterns.
    expr(value);
    return false;
  }
  void isolated(ast_stmt *body, const std::vector<ast_param> &params,
                const std::string &source, bool capture = false) {
    const auto saved = state_;
    auto saved_transfers = std::move(transfers_);
    auto saved_controls = std::move(controls_);
    const auto saved_attempts = attempts_;
    const auto saved_source = source_;
    if (!capture) state_ = {};
    state_.reachable = true;
    transfers_.clear(); controls_.clear(); attempts_ = 0;
    if (!source.empty()) source_ = source;
    enter(); bind("self");
    for (const auto &parameter : params) bind(parameter.name);
    stmt(body);
    state_ = saved;
    transfers_ = std::move(saved_transfers);
    controls_ = std::move(saved_controls);
    attempts_ = saved_attempts; source_ = saved_source;
  }
  void declaration(ast_decl *decl, bool in_body = false) {
    if (!decl) return;
    switch (decl->kind) {
    case ast_node_kind::variable_decl_node:
      if (in_body) local(static_cast<ast_variable_decl &>(*decl));
      else expr(static_cast<ast_variable_decl &>(*decl).initializer.get());
      break;
    case ast_node_kind::function_decl_node: {
      auto &value = static_cast<ast_function_decl &>(*decl);
      isolated(value.body.get(), value.params, value.source_file);
      for (auto &test : value.test_blocks) declaration(test.get());
      for (auto &test : value.example_blocks) declaration(test.get());
      break;
    }
    case ast_node_kind::class_decl_node:
      for (auto &method : static_cast<ast_class_decl &>(*decl).methods) declaration(method.get());
      break;
    case ast_node_kind::impl_decl_node:
      for (auto &method : static_cast<ast_impl_decl &>(*decl).methods) declaration(method.get());
      break;
    case ast_node_kind::trait_decl_node:
      for (auto &method : static_cast<ast_trait_decl &>(*decl).methods) declaration(method.get());
      break;
    case ast_node_kind::test_block_node:
      isolated(static_cast<ast_test_block &>(*decl).body.get(), {}, decl->source_file);
      break;
    default: break;
    }
  }
  void loop(ast_node &node, ast_stmt *body, const flow &entry, bool zero_iterations,
            const std::vector<ast_param> &variables = {}) {
    const auto first_transfer = transfers_.size();
    controls_.push_back({&node, true});
    enter();
    for (const auto &variable : variables) bind(variable.name);
    stmt(body);
    leave();
    controls_.pop_back();
    std::vector<flow> exits;
    if (zero_iterations) exits.push_back(entry);
    std::vector<transfer> outward;
    for (std::size_t index = first_transfer; index < transfers_.size(); ++index) {
      const auto &transfer = transfers_[index];
      if (transfer.target == &node && transfer.kind == transfer_kind::break_)
        exits.push_back(transfer.state);
      else if (!(transfer.target == &node && transfer.kind == transfer_kind::continue_))
        outward.push_back(transfer);
    }
    transfers_.resize(first_transfer);
    transfers_.insert(transfers_.end(), outward.begin(), outward.end());
    state_ = join(entry, exits);
  }
  void switch_cases(ast_node &node, ast_expr *target, std::vector<ast_switch_arm> &arms) {
    expr(target);
    const auto entry = state_;
    const auto first_transfer = transfers_.size();
    controls_.push_back({&node, false});
    flow fallthrough = entry; fallthrough.reachable = false;
    bool has_default = false;
    for (auto &arm : arms) {
      has_default = has_default || !arm.label;
      // Every case can be entered by dispatch, as well as by fallthrough.
      state_ = join(entry, {entry, fallthrough});
      scoped(arm.body.get());
      fallthrough = state_;
    }
    controls_.pop_back();
    std::vector<flow> exits{fallthrough};
    if (!has_default) exits.push_back(entry);
    std::vector<transfer> outward;
    for (std::size_t index = first_transfer; index < transfers_.size(); ++index) {
      const auto &transfer = transfers_[index];
      if (transfer.target == &node && transfer.kind == transfer_kind::break_)
        exits.push_back(transfer.state);
      else outward.push_back(transfer);
    }
    transfers_.resize(first_transfer);
    transfers_.insert(transfers_.end(), outward.begin(), outward.end());
    state_ = join(entry, exits);
  }
  void attempt(ast_attempt_stmt &value) {
    const auto entry = state_;
    const auto first_transfer = transfers_.size();
    ++attempts_;
    scoped(value.body.get());
    const auto normal_body = state_;
    std::vector<transfer> body_transfers(transfers_.begin() + first_transfer, transfers_.end());
    transfers_.resize(first_transfer);
    std::vector<flow> paths{normal_body};
    std::vector<transfer> faults;
    std::vector<transfer> outward;
    for (const auto &transfer : body_transfers) {
      if (transfer.kind == transfer_kind::fault) faults.push_back(transfer);
      else outward.push_back(transfer);
    }
    for (auto &handler : value.handlers) {
      if (!handler) continue;
      std::vector<flow> matching;
      std::vector<transfer> remaining;
      for (const auto &fault : faults) {
        const bool exact = handler->checked_fault_type && !fault.fault_type.empty() &&
                           *handler->checked_fault_type == fault.fault_type;
        const bool catchall = !handler->fault_type;
        if (catchall || exact || fault.fault_type.empty()) matching.push_back(fault.state);
        if (handler->guard || !(catchall || exact)) remaining.push_back(fault);
      }
      faults = std::move(remaining);
      state_ = join(entry, matching);
      enter(); bind(handler->variable);
      expr(handler->guard.get()); stmt(handler->body.get()); leave();
      paths.push_back(state_);
    }
    outward.insert(outward.end(), faults.begin(), faults.end());
    outward.insert(outward.end(), transfers_.begin() + first_transfer, transfers_.end());
    transfers_.resize(first_transfer);
    --attempts_;
    // Equivalent destinations need one must-state. Avoid multiplying recovery
    // work by every faulting call at each level of nested attempts.
    std::vector<transfer> destinations;
    for (auto &transfer : outward) {
      auto previous = std::find_if(destinations.begin(), destinations.end(),
          [&](const auto &item) { return item.kind == transfer.kind &&
              item.target == transfer.target && item.fault_type == transfer.fault_type; });
      if (previous == destinations.end()) {
        transfer.state = join(entry, {transfer.state});
        destinations.push_back(std::move(transfer));
      } else previous->state = join(entry, {previous->state, transfer.state});
    }
    // Recovery runs on normal completion and on each outgoing transfer. Joining
    // only normal predecessors must not certify reads performed by recovery on
    // an early return, break, or a fault before an assignment.
    state_ = join(entry, paths);
    scoped(value.recover.get());
    const auto normal = state_;
    for (auto &transfer : destinations) {
      state_ = join(entry, {transfer.state});
      scoped(value.recover.get());
      if (state_.reachable) {
        transfer.state = state_;
        transfers_.push_back(std::move(transfer));
      }
    }
    state_ = normal;
  }
  void stmt(ast_stmt *statement) {
    if (!statement || !state_.reachable) return;
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
      for (auto &value : static_cast<ast_println_stmt &>(*statement).args) expr(value.get());
      break;
    case ast_node_kind::if_stmt_node: {
      auto &value = static_cast<ast_if_stmt &>(*statement);
      expr(value.condition.get());
      const auto entry = state_;
      const auto known = boolean(value.condition.get());
      if (known && !*known) state_.reachable = false;
      scoped(value.then_branch.get()); const auto yes = state_;
      state_ = entry;
      if (known && *known) state_.reachable = false;
      scoped(value.else_branch.get());
      state_ = join(entry, {yes, state_}); break;
    }
    case ast_node_kind::while_stmt_node: {
      auto &value = static_cast<ast_while_stmt &>(*statement);
      expr(value.condition.get()); const auto entry = state_;
      const auto known = boolean(value.condition.get());
      if (known && !*known) break;
      loop(value, value.body.get(), entry, !known || !*known); break;
    }
    case ast_node_kind::for_in_stmt_node: {
      auto &value = static_cast<ast_for_in_stmt &>(*statement);
      expr(value.iterable.get()); const auto entry = state_;
      loop(value, value.body.get(), entry, true, value.get_variables()); break;
    }
    case ast_node_kind::loop_stmt_node: {
      auto &value = static_cast<ast_loop_stmt &>(*statement);
      const auto entry = state_; loop(value, value.body.get(), entry, false); break;
    }
    case ast_node_kind::return_stmt_node:
      expr(static_cast<ast_return_stmt &>(*statement).value.get()); escape(transfer_kind::return_); break;
    case ast_node_kind::raise_stmt_node: {
      auto &value = static_cast<ast_raise_stmt &>(*statement);
      expr(value.fault_expr.get());
      escape(transfer_kind::fault, nullptr, value.checked_fault_type.value_or("")); break;
    }
    case ast_node_kind::break_stmt_node: {
      auto &value = static_cast<ast_break_stmt &>(*statement);
      expr(value.value.get()); escape(transfer_kind::break_, value.checked_target); break;
    }
    case ast_node_kind::continue_stmt_node: {
      const auto destination = std::find_if(controls_.rbegin(), controls_.rend(),
          [](const control &value) { return value.loop; });
      escape(transfer_kind::continue_, destination == controls_.rend() ? nullptr : destination->node); break;
    }
    case ast_node_kind::switch_stmt_node: {
      auto &value = static_cast<ast_switch_stmt &>(*statement);
      switch_cases(value, value.target.get(), value.arms); break;
    }
    case ast_node_kind::match_stmt_node: {
      auto &value = static_cast<ast_match_stmt &>(*statement);
      expr(value.target.get()); const auto entry = state_;
      std::vector<flow> paths;
      bool complete = value.checked_exhaustive;
      for (std::size_t index = 0; index < value.arms.size(); ++index) {
        if (value.checked_exhaustive && index > value.checked_exhaustive_arm) break;
        auto &arm = value.arms[index];
        state_ = entry; enter(); pattern(arm.pattern);
        expr(arm.guard.get()); stmt(arm.body.get()); leave(); paths.push_back(state_);
        if (!arm.guard && unconditional(arm.pattern)) complete = true;
      }
      if (!complete) paths.push_back(entry);
      state_ = join(entry, paths); break;
    }
    case ast_node_kind::attempt_stmt_node:
      attempt(static_cast<ast_attempt_stmt &>(*statement)); break;
    case ast_node_kind::deallocate_stmt_node:
      expr(static_cast<ast_deallocate_stmt &>(*statement).ptr.get()); break;
    case ast_node_kind::asm_stmt_node: {
      auto &value = static_cast<ast_asm_stmt &>(*statement);
      for (auto &input : value.inputs) expr(input.get());
      if (value.output) if (auto *output = lookup(*value.output)) output->assigned = true;
      break;
    }
    case ast_node_kind::inline_test_stmt_node: {
      auto &value = static_cast<ast_inline_test_stmt &>(*statement);
      const auto entry = state_;
      if (value.test) scoped(value.test->body.get());
      // Inline tests can be omitted by a production build.
      state_ = join(entry, {entry, state_}); break;
    }
    default: break;
    }
  }
  void markup(ast_markup_expr &value) {
    struct region { flow entry; std::vector<flow> paths; bool repeat, otherwise; };
    std::vector<region> regions;
    using kind = ast_markup_item::item_kind;
    for (auto &item : value.items) {
      if (item.kind == kind::when_begin || item.kind == kind::repeat_begin) {
        expr(item.expr.get());
        regions.push_back({state_, {}, item.kind == kind::repeat_begin, false});
        enter();
        if (item.kind == kind::repeat_begin) bind(item.binding);
      } else if ((item.kind == kind::else_when || item.kind == kind::else_arm) && !regions.empty()) {
        leave(); auto &region = regions.back(); region.paths.push_back(state_);
        state_ = region.entry; enter(); expr(item.expr.get());
        region.otherwise = item.kind == kind::else_arm;
      } else if ((item.kind == kind::end_when || item.kind == kind::end_repeat) && !regions.empty()) {
        leave(); auto region = std::move(regions.back()); regions.pop_back();
        region.paths.push_back(state_);
        if (region.repeat || !region.otherwise) region.paths.push_back(region.entry);
        state_ = join(region.entry, region.paths);
      } else expr(item.expr.get());
    }
  }
  void expr(ast_expr *expression) {
    if (!expression || !state_.reachable) return;
    switch (expression->kind) {
    case ast_node_kind::identifier_expr_node:
      read(static_cast<ast_identifier_expr &>(*expression)); break;
    case ast_node_kind::assignment_expr_node: {
      auto &value = static_cast<ast_assignment_expr &>(*expression);
      // Indexed/member/pointer destinations evaluate their storage receiver
      // before the RHS. A direct x = y writes x without first reading x.
      if (value.target && value.target->kind != ast_node_kind::identifier_expr_node)
        expr(value.target.get());
      expr(value.value.get());
      assign(value.target.get()); break;
    }
    case ast_node_kind::binary_expr_node: {
      auto &value = static_cast<ast_binary_expr &>(*expression);
      expr(value.left.get()); const auto entry = state_;
      if (value.op == token_kind::amp_amp || value.op == token_kind::pipe_pipe) {
        const auto known = boolean(value.left.get());
        const bool and_ = value.op == token_kind::amp_amp;
        if (known && *known != and_) break;
        expr(value.right.get());
        if (!known) state_ = join(entry, {entry, state_});
      } else expr(value.right.get());
      break;
    }
    case ast_node_kind::unary_expr_node:
      expr(static_cast<ast_unary_expr &>(*expression).operand.get()); break;
    case ast_node_kind::call_expr_node: {
      auto &value = static_cast<ast_call_expr &>(*expression);
      expr(value.callee.get()); for (auto &argument : value.args) expr(argument.get());
      possible_fault(); break;
    }
    case ast_node_kind::member_access_expr_node: {
      auto &value = static_cast<ast_member_access_expr &>(*expression);
      if (!value.resolved_module_function) expr(value.object.get());
      break;
    }
    case ast_node_kind::index_expr_node: {
      auto &value = static_cast<ast_index_expr &>(*expression);
      expr(value.object.get()); expr(value.index.get()); break;
    }
    case ast_node_kind::cast_expr_node:
      expr(static_cast<ast_cast_expr &>(*expression).operand.get()); break;
    case ast_node_kind::array_literal_expr_node:
      for (auto &item : static_cast<ast_array_literal_expr &>(*expression).elements) expr(item.get());
      break;
    case ast_node_kind::struct_literal_expr_node:
      for (auto &field : static_cast<ast_struct_literal_expr &>(*expression).fields) expr(field.second.get());
      break;
    case ast_node_kind::allocate_expr_node:
      expr(static_cast<ast_allocate_expr &>(*expression).size.get()); break;
    case ast_node_kind::ref_expr_node:
      expr(static_cast<ast_ref_expr &>(*expression).operand.get()); break;
    case ast_node_kind::try_expr_node:
      expr(static_cast<ast_try_expr &>(*expression).inner.get());
      if (state_.reachable) transfers_.push_back({transfer_kind::return_, nullptr, state_});
      break;
    case ast_node_kind::assert_expr_node:
      for (auto &argument : static_cast<ast_assert_expr &>(*expression).args) expr(argument.get());
      possible_fault(); break;
    case ast_node_kind::hotreload_expr_node:
      for (auto &argument : static_cast<ast_hotreload_expr &>(*expression).args) expr(argument.get());
      break;
    case ast_node_kind::interpolated_string_expr_node:
      for (auto &part : static_cast<ast_interpolated_string_expr &>(*expression).parts) expr(part.expression.get());
      break;
    case ast_node_kind::if_expr_node: {
      auto &value = static_cast<ast_if_expr &>(*expression);
      expr(value.condition.get()); const auto entry = state_;
      const auto known = boolean(value.condition.get());
      if (known && !*known) state_.reachable = false;
      expr(value.then_expr.get()); const auto yes = state_;
      state_ = entry;
      if (known && *known) state_.reachable = false;
      expr(value.else_expr.get()); state_ = join(entry, {yes, state_}); break;
    }
    case ast_node_kind::match_expr_node: {
      auto &value = static_cast<ast_match_expr &>(*expression);
      expr(value.target.get()); const auto entry = state_;
      std::vector<flow> paths; bool complete = false;
      for (auto &arm : value.cases) {
        state_ = entry; enter(); const bool all = expression_pattern(arm.pattern.get());
        expr(arm.guard.get()); stmt(arm.body.get()); leave(); paths.push_back(state_);
        if (all && !arm.guard) complete = true;
      }
      if (!complete) paths.push_back(entry);
      state_ = join(entry, paths); break;
    }
    case ast_node_kind::switch_expr_node: {
      auto &value = static_cast<ast_switch_expr &>(*expression);
      switch_cases(value, value.target.get(), value.arms); break;
    }
    case ast_node_kind::lambda_expr_node: {
      auto &value = static_cast<ast_lambda_expr &>(*expression);
      isolated(value.body.get(), value.params, value.source_file, true); break;
    }
    case ast_node_kind::query_expr_node: {
      auto &value = static_cast<ast_query_expr &>(*expression);
      expr(value.source.get()); expr(value.skip.get()); expr(value.take.get());
      const auto entry = state_;
      enter(); bind(value.binding.name);
      expr(value.predicate.get()); expr(value.order_key.get()); expr(value.projection.get());
      leave(); state_ = join(entry, {entry, state_}); break;
    }
    case ast_node_kind::markup_expr_node:
      markup(static_cast<ast_markup_expr &>(*expression)); break;
    default: break;
    }
  }
public:
  explicit definite_assignment_checker(const std::map<const ast_expr *, ast_pattern> &patterns)
      : patterns_(patterns) {}
  std::vector<assignment_diagnostic> run(ast_program &program) {
    for (auto &item : program.declarations) declaration(item.get());
    return std::move(diagnostics_);
  }
};

inline std::vector<assignment_diagnostic> check_definite_assignment(
    ast_program &program, const std::map<const ast_expr *, ast_pattern> &patterns) {
  return definite_assignment_checker(patterns).run(program);
}
} // namespace ny::detail
#endif
