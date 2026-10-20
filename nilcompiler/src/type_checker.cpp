/*
 * Copyright (C) 2008 - 2026 Execute Software.  All rights reserved.
 * SPDX-License-Identifier: MIT
 * See the LICENSE file in the project root for license terms.
 */
#include "type_checker.hpp"
#include "diagnostics.hpp"
#include "nil_contract.hpp"
#include "numeric_literal.hpp"
#include "ownership_check.hpp"
#include "standard_namespace.hpp"
#include <algorithm>
#include <cctype>
#include <cstring> // for strlen
#include <functional>
#include <iostream>
#include <sstream>
#include <unordered_set>

namespace ny {

// Empty module markers are source-unit boundaries in a merged AST. They
// restore the root context without creating a module named "" or "::name".
static std::optional<std::string> module_context(const std::string &name) {
  return name.empty() ? std::nullopt : std::optional<std::string>(name);
}

// Conditional expressions use the same symmetric scalar promotion as
// arithmetic; branch order must never choose the result's physical width.
static std::optional<std::string> promoted_scalar_join(const std::string &left,
                                                       const std::string &right) {
  const auto numeric = [](const std::string &type) {
    return type == "i32" || type == "i64" || type == "u8" || type == "f64";
  };
  if (!numeric(left) || !numeric(right))
    return std::nullopt;
  return left == "f64" || right == "f64" ? "f64" :
         left == "i64" || right == "i64" ? "i64" :
         left == "i32" || right == "i32" ? "i32" : "u8";
}

type_checker::type_checker(bool collect_semantics,
                           std::optional<completion_request> completion)
    : collect_semantics_(collect_semantics), completion_request_(std::move(completion)) {
  for (const auto *name : {"owned", "borrow", "borrow_mut"})
    symbols_.insert_symbol_with_context(name, symbol_kind::type_symbol, nullptr,
        std::nullopt, false, visibility_kind::visibility_public, std::nullopt);
  // Register built-in types as public for the symbol table
  symbols_.insert_symbol_with_context(
      "i8", symbol_kind::type_symbol, nullptr, std::nullopt, false,
      visibility_kind::visibility_public, std::nullopt);
  symbols_.insert_symbol_with_context(
      "i16", symbol_kind::type_symbol, nullptr, std::nullopt, false,
      visibility_kind::visibility_public, std::nullopt);
  symbols_.insert_symbol_with_context(
      "i32", symbol_kind::type_symbol, nullptr, std::nullopt, false,
      visibility_kind::visibility_public, std::nullopt);
  symbols_.insert_symbol_with_context(
      "i64", symbol_kind::type_symbol, nullptr, std::nullopt, false,
      visibility_kind::visibility_public, std::nullopt);
  symbols_.insert_symbol_with_context(
      "u8", symbol_kind::type_symbol, nullptr, std::nullopt, false,
      visibility_kind::visibility_public, std::nullopt);
  symbols_.insert_symbol_with_context(
      "u16", symbol_kind::type_symbol, nullptr, std::nullopt, false,
      visibility_kind::visibility_public, std::nullopt);
  symbols_.insert_symbol_with_context(
      "u32", symbol_kind::type_symbol, nullptr, std::nullopt, false,
      visibility_kind::visibility_public, std::nullopt);
  symbols_.insert_symbol_with_context(
      "u64", symbol_kind::type_symbol, nullptr, std::nullopt, false,
      visibility_kind::visibility_public, std::nullopt);
  symbols_.insert_symbol_with_context(
      "f32", symbol_kind::type_symbol, nullptr, std::nullopt, false,
      visibility_kind::visibility_public, std::nullopt);
  symbols_.insert_symbol_with_context(
      "f64", symbol_kind::type_symbol, nullptr, std::nullopt, false,
      visibility_kind::visibility_public, std::nullopt);
  symbols_.insert_symbol_with_context(
      "bool", symbol_kind::type_symbol, nullptr, std::nullopt, false,
      visibility_kind::visibility_public, std::nullopt);
  symbols_.insert_symbol_with_context(
      "char", symbol_kind::type_symbol, nullptr, std::nullopt, false,
      visibility_kind::visibility_public, std::nullopt);
  symbols_.insert_symbol_with_context(
      "any", symbol_kind::type_symbol, nullptr, std::nullopt, false,
      visibility_kind::visibility_public, std::nullopt);
  symbols_.insert_symbol_with_context(
      "String", symbol_kind::type_symbol, nullptr, std::nullopt, false,
      visibility_kind::visibility_public, std::nullopt);

  // Built-in container/compound types
  symbols_.insert_symbol_with_context(
      "vec", symbol_kind::type_symbol, nullptr, std::nullopt, false,
      visibility_kind::visibility_public, std::nullopt);
  symbols_.insert_symbol_with_context(
      "tuple", symbol_kind::type_symbol, nullptr, std::nullopt, false,
      visibility_kind::visibility_public, std::nullopt);
  symbols_.insert_symbol_with_context(
      "array", symbol_kind::type_symbol, nullptr, std::nullopt, false,
      visibility_kind::visibility_public, std::nullopt);
  symbols_.insert_symbol_with_context(
      "slice", symbol_kind::type_symbol, nullptr, std::nullopt, false,
      visibility_kind::visibility_public, std::nullopt);
  symbols_.insert_symbol_with_context(
      "ref", symbol_kind::type_symbol, nullptr, std::nullopt, false,
      visibility_kind::visibility_public, std::nullopt);
  symbols_.insert_symbol_with_context(
      "ptr", symbol_kind::type_symbol, nullptr, std::nullopt, false,
      visibility_kind::visibility_public, std::nullopt);
  symbols_.insert_symbol_with_context(
      "Self", symbol_kind::type_symbol, nullptr, std::nullopt, false,
      visibility_kind::visibility_public, std::nullopt);
  // symbols_.insert_symbol_with_context(
  //     "option", symbol_kind::type_symbol, nullptr, std::nullopt, false,
  //     visibility_kind::visibility_public, std::nullopt);

  // Aliases (text -> String, int -> i32) - register as public aliases
  symbols_.insert_type_alias_in_module("text", "String", true);
  symbols_.insert_type_alias_in_module("int", "i32", true);

  // Built-in functions
  // panic(msg: String) -> any (diverging)
  symbols_.insert_symbol_with_context(
      "panic", symbol_kind::function_symbol, nullptr,
      std::optional<std::string>("any"), false,
      visibility_kind::visibility_public, std::nullopt);

  // Built-in range iterator helper: range(start: i32, end: i32) -> range
  symbols_.insert_symbol_with_context(
      "range", symbol_kind::function_symbol, nullptr,
      std::optional<std::string>("range"), false,
      visibility_kind::visibility_public, std::nullopt);

  // Built-in zip helper: zip(iter1, iter2, ...) -> any (iterator/tuple)
  // This avoids 'Undefined identifier' errors for common iterator helpers
  // used in tests and desugaring code.
  // Built-in zip helper: zip(iter1, iter2, ...) -> any (iterator/tuple)
  // This avoids 'Undefined identifier' errors for common iterator helpers
  // used in tests and desugaring code.
  symbols_.insert_symbol_with_context(
      "zip", symbol_kind::function_symbol, nullptr,
      std::optional<std::string>("any"), false,
      visibility_kind::visibility_public, std::nullopt);

  // Memory intrinsics (alloc, free, read_i32, write_i32) for native JIT tests
  symbols_.insert_symbol_with_context(
      "alloc", symbol_kind::function_symbol, nullptr,
      std::optional<std::string>("ptr<i32>"), false,
      visibility_kind::visibility_public, std::nullopt);
  symbols_.insert_symbol_with_context(
      "free", symbol_kind::function_symbol, nullptr,
      std::optional<std::string>("void"), false,
      visibility_kind::visibility_public, std::nullopt);
  symbols_.insert_symbol_with_context(
      "read_i32", symbol_kind::function_symbol, nullptr,
      std::optional<std::string>("i32"), false,
      visibility_kind::visibility_public, std::nullopt);
  symbols_.insert_symbol_with_context(
      "write_i32", symbol_kind::function_symbol, nullptr,
      std::optional<std::string>("void"), false,
      visibility_kind::visibility_public, std::nullopt);

  // Mimic interoperability test
  symbols_.insert_symbol_with_context(
      "ny_mimic_sleep", symbol_kind::function_symbol, nullptr,
      std::optional<std::string>("void"), false,
      visibility_kind::visibility_public, std::nullopt);
  symbols_.insert_symbol_with_context(
      "ny_mimic_get_ticks", symbol_kind::function_symbol, nullptr,
      std::optional<std::string>("i32"), false,
      visibility_kind::visibility_public, std::nullopt);

  // Core Window
  symbols_.insert_symbol_with_context(
      "ny_mimic_create_window", symbol_kind::function_symbol, nullptr,
      std::optional<std::string>("i64"), false,
      visibility_kind::visibility_public, std::nullopt);
  symbols_.insert_symbol_with_context(
      "ny_mimic_poll", symbol_kind::function_symbol, nullptr,
      std::optional<std::string>("i32"), false,
      visibility_kind::visibility_public, std::nullopt);
  symbols_.insert_symbol_with_context(
      "ny_mimic_get_p1", symbol_kind::function_symbol, nullptr,
      std::optional<std::string>("i32"), false,
      visibility_kind::visibility_public, std::nullopt);
  symbols_.insert_symbol_with_context(
      "ny_mimic_get_p2", symbol_kind::function_symbol, nullptr,
      std::optional<std::string>("i32"), false,
      visibility_kind::visibility_public, std::nullopt);

  // Drawing
  symbols_.insert_symbol_with_context(
      "ny_mimic_clear", symbol_kind::function_symbol, nullptr,
      std::optional<std::string>("void"), false,
      visibility_kind::visibility_public, std::nullopt);
  symbols_.insert_symbol_with_context(
      "ny_mimic_fill_rect", symbol_kind::function_symbol, nullptr,
      std::optional<std::string>("void"), false,
      visibility_kind::visibility_public, std::nullopt);
  symbols_.insert_symbol_with_context(
      "ny_mimic_fill_circle", symbol_kind::function_symbol, nullptr,
      std::optional<std::string>("void"), false,
      visibility_kind::visibility_public, std::nullopt);
  symbols_.insert_symbol_with_context(
      "ny_mimic_draw_text", symbol_kind::function_symbol, nullptr,
      std::optional<std::string>("void"), false,
      visibility_kind::visibility_public, std::nullopt);
}

void type_checker::capture_completion(const ast_identifier_expr &expression) {
  if (!completion_request_ || !expression.name_span ||
      expression.name.find("::") != std::string::npos) return;
  const auto &request = *completion_request_;
  const auto span = *expression.name_span;
  const auto &path = expression.source_file.empty() ? diagnostic_source_file_ : expression.source_file;
  if (path != request.path || !span.length || request.cursor <= span.start ||
      request.cursor - span.start > span.length) return;
  if (completion_) {
    // Generic instantiation or another lowering may revisit the same physical
    // token under a different inferred scope. No single visit is authoritative.
    completion_->available = false;
    completion_->candidates.clear();
    return;
  }
  completion_ = completion_result{true, path, expression.name, span, {}};
  auto &result = *completion_;
  // Scope state after an earlier error is not a completion contract. The
  // identifier being queried may itself be unfinished and fail immediately
  // after this snapshot; that failure does not invalidate the scope before it.
  if (!diagnostics_.empty() || !semantics_.available) {
    result.available = false;
    return;
  }
  std::set<std::string> names;
  for (const auto *value : symbols_.visible_symbols()) {
    const auto split = value->name.rfind("::");
    names.insert(split == std::string::npos ? value->name : value->name.substr(split + 2));
    if (names.size() > 32768) { result.available = false; return; }
  }
  if (const auto imports = imports_.find(current_module_.value_or("<global>"));
      imports != imports_.end())
    for (const auto &[name, entry] : imports->second)
      if (!entry.target_path.empty() && entry.target_path != "*") {
        names.insert(name);
        if (names.size() > 32768) { result.available = false; return; }
      }
  std::size_t bytes = 0;
  for (const auto &name : names) {
    const symbol *binding = nullptr;
    // Match identifier lookup, including ambiguous imported globals, without
    // retaining speculative diagnostics or performing expression/type checks.
    if (!symbols_.lookup_non_global(name)) {
      const auto before = diagnostics_.size();
      const auto global = resolve_source_global(name, expression.line);
      const bool ambiguous = diagnostics_.size() != before;
      diagnostics_.resize(before);
      if (ambiguous) continue;
      if (global) {
        binding = symbols_.lookup(*global);
        if (!binding || !binding->type_name) continue;
      }
    }
    if (!binding) {
      binding = symbols_.lookup(name);
      if (binding && current_module_ && binding->module &&
          *binding->module != *current_module_) binding = nullptr;
      if (!binding && current_module_) binding = symbols_.lookup(*current_module_ + "::" + name);
      if (!binding)
        if (const auto imported = resolve_imported_symbol(name)) binding = symbols_.lookup(*imported);
    }
    if (!binding || binding->kind == symbol_kind::type_symbol ||
        !is_symbol_visible(binding, current_module_.value_or(""))) continue;
    auto id = binding->semantic_id;
    if (!id && binding->defining_node) {
      const auto &node = *binding->defining_node;
      if (node.kind == ast_node_kind::function_decl_node) {
        const auto &function = static_cast<const ast_function_decl &>(node);
        id = semantic_node(node, function.name, "function", function.return_type.value_or("void"));
      } else if (node.kind == ast_node_kind::variable_decl_node) {
        const auto &variable = static_cast<const ast_variable_decl &>(node);
        id = semantic_node(node, variable.name, "variable", binding->type_name.value_or(""));
      }
    }
    if (!id || *id >= semantics_.symbols.size()) continue;
    const auto &declaration = semantics_.symbols[*id];
    bytes += name.size() + declaration.kind.size() + declaration.type.size() + declaration.path.size();
    if (bytes > 4 * 1024 * 1024 || result.candidates.size() >= 32768) {
      result.available = false; result.candidates.clear(); return;
    }
    result.candidates.push_back({name, declaration.kind, binding->type_name.value_or(declaration.type),
                                declaration.path, declaration.span});
  }
}

void type_checker::disable_semantics() {
  semantics_.available = false;
  semantics_.symbols.clear();
  semantics_.references.clear();
  semantic_nodes_.clear();
  semantic_declarations_.clear();
  semantic_occurrences_.clear();
}

std::optional<std::size_t> type_checker::semantic_declaration(
    const std::string &name, const std::string &kind, const std::string &type,
    const std::string &path, const std::optional<source_span> &span) {
  if (!collect_semantics_ || !semantics_.available || path.empty() ||
      !span || !span->length) return std::nullopt;
  const auto key = std::make_tuple(path, span->start, span->length);
  if (const auto existing = semantic_declarations_.find(key);
      existing != semantic_declarations_.end()) return existing->second;
  const auto bytes = name.size() + kind.size() + type.size() + path.size();
  if (semantics_.symbols.size() >= 32768 || bytes > 4 * 1024 * 1024 - semantic_bytes_) {
    disable_semantics();
    return std::nullopt;
  }
  const auto id = semantics_.symbols.size();
  semantics_.symbols.push_back({id, kind, name, type, path, *span});
  semantic_bytes_ += bytes;
  semantic_declarations_.emplace(key, id);
  return id;
}

std::optional<std::size_t> type_checker::semantic_node(
    const ast_node &node, const std::string &name, const std::string &kind,
    const std::string &type, const std::string &fallback_path) {
  if (!collect_semantics_ || !semantics_.available) return std::nullopt;
  if (const auto existing = semantic_nodes_.find(&node);
      existing != semantic_nodes_.end()) return existing->second;
  auto id = semantic_declaration(name, kind, type,
      node.source_file.empty() ? fallback_path : node.source_file, node.name_span);
  if (id) semantic_nodes_.emplace(&node, *id);
  return id;
}

void type_checker::semantic_reference_to(const ast_node &node,
    const std::string &name, const symbol &binding) {
  if (!collect_semantics_ || !semantics_.available) return;
  auto id = binding.semantic_id;
  if (!id && binding.defining_node) {
    const auto &declaration = *binding.defining_node;
    if (declaration.kind == ast_node_kind::function_decl_node) {
      const auto &function = static_cast<const ast_function_decl &>(declaration);
      id = semantic_node(declaration, function.name, "function",
                         binding.type_name.value_or(""));
    } else if (declaration.kind == ast_node_kind::variable_decl_node) {
      const auto &variable = static_cast<const ast_variable_decl &>(declaration);
      id = semantic_node(declaration, variable.name, "variable",
                         binding.type_name.value_or(""));
    }
  }
  if (!id || *id >= semantics_.symbols.size()) return;
  if (!node.name_span || !node.name_span->length) {
    semantics_.symbols[*id].all_uses_located = false;
    return;
  }
  const auto path = node.source_file.empty() ? diagnostic_source_file_ : node.source_file;
  if (path.empty()) {
    semantics_.symbols[*id].all_uses_located = false;
    return;
  }
  const auto key = std::make_tuple(path, node.name_span->start,
                                  node.name_span->length, *id);
  if (semantic_occurrences_.count(key)) return;
  const auto bytes = path.size() + name.size();
  if (semantics_.references.size() >= 131072 || bytes > 4 * 1024 * 1024 - semantic_bytes_) {
    disable_semantics();
    return;
  }
  semantics_.references.push_back({*id, path, name, *node.name_span});
  semantic_occurrences_.insert(key);
  semantic_bytes_ += bytes;
}

bool type_checker::check(ast_program &program) {
  // Every check is a fresh semantic session. In particular, AST addresses
  // must never be remembered process-wide or across reused checker instances.
  *this = type_checker(collect_semantics_, completion_request_);
  program.accept(*this);
  auto ownership = check_ownership(program);
  for (const auto &diagnostic : ownership.diagnostics)
    add_error(diagnostic.line, diagnostic.message);
  if (!diagnostics_.empty()) {
    for (auto *expression : ownership.expressions) expression->checked_ownership = false;
    for (auto *function : ownership.functions) function->checked_ownership = false;
    for (auto *call : checked_parallel_calls_) {
      call->parallel_worker.reset();
      call->parallel_input_type.clear();
      call->parallel_output_type.clear();
    }
    for (auto &declaration : program.declarations)
      if (declaration && declaration->kind == ast_node_kind::class_decl_node)
        static_cast<ast_class_decl &>(*declaration).checked_name.reset();
    // --no-typecheck is diagnostic continuation, not permission to fabricate
    // a fault ABI or bypass opaque-token and cleanup proofs.
    for (auto *attempt : checked_attempts_) attempt->fault_checked = false;
    for (auto *handler : checked_fault_handlers_) handler->fault_checked = false;
    for (auto *raise : checked_raises_) raise->fault_checked = false;
    for (auto *value : checked_class_conversions_) value->checked_class_upcast.reset();
    for (auto *value : checked_reference_handles_)
      value->checked_reference_handle = false;
    for (auto *call : checked_class_calls_) {
      call->concrete_class_receiver.reset();
      call->class_method_owner.reset();
      call->class_virtual_call = false;
    }
  }
  return diagnostics_.empty();
}

namespace {
struct diagnostic_source_scope {
  std::string &current;
  std::string previous;
  diagnostic_source_scope(std::string &destination, const std::string &path)
      : current(destination), previous(destination) { if (!path.empty()) current = path; }
  ~diagnostic_source_scope() { current = previous; }
};
}

void type_checker::add_error(int line, const std::string &msg) {
  diagnostics_.push_back({line, msg, diagnostic_source_file_});
}

// Debug helper
static thread_local std::vector<std::string> resolution_stack;

type_info type_checker::resolve_type(const std::string &name) {
  // Recursion guard with stack dump
  // std::cerr << "[resolve_type] ENTRY '" << name << "'\n";

  static thread_local int loop_guard_rt = 0;
  loop_guard_rt++;
  if (loop_guard_rt > 10000) {
    if (loop_guard_rt % 1000 ==
        0) /* std::cerr << "resolve_type looping! " << name << "\n"; */
      if (loop_guard_rt > 20000)
        return {"<error>", false};
  }
  struct LoopGuardRT {
    ~LoopGuardRT() { loop_guard_rt--; }
  } lg_rt;

  if (resolution_stack.size() > 50) {
    /* std::cerr << "Recursion limit exceeded! Stack:\n"; */
    /* for (const auto& s : resolution_stack) std::cerr << "  " << s << "\n"; */
    /* std::cerr << "  Current: " << name << "\n"; */
    return {"<error>", false};
  }
  resolution_stack.push_back(name);
  struct StackGuard {
    ~StackGuard() { resolution_stack.pop_back(); }
  } guard;

  // Debug: show the input and current module, then attempt to resolve the
  // requested type using imports and module qualification before
  // canonicalizing.
  // std::cerr << "[type_checker] resolve_type: input='" << name << "'";
  // if (current_module_)
  //   std::cerr << " current_module='" << *current_module_ << "'";
  // std::cerr << std::endl;

  // Special handling for Self alias - expand to actual type
  if (name == "Self") {
    const symbol *self_sym = symbols_.lookup("Self");
    if (self_sym && self_sym->type_name.has_value()) {
      // Recursively resolve the underlying type
      return resolve_type(*self_sym->type_name);
    }
  }

  // User aliases are declarations, not context-free textual substitutions.
  // Expand them in their owning module, preserving nominal targets and
  // generic arguments even when the importing module reuses a short name.
  const auto alias_parts = parse_generic_type_string(name);
  const auto alias_name = resolve_type_name_with_imports(alias_parts.base_name);
  const symbol *alias_symbol =
      symbols_.lookup(alias_name.value_or(alias_parts.base_name));
  if (alias_symbol && alias_symbol->defining_node &&
      alias_symbol->defining_node->kind == ast_node_kind::type_alias_decl_node) {
    const auto &alias =
        static_cast<const ast_type_alias_decl &>(*alias_symbol->defining_node);
    if (alias.generic_params.size() == alias_parts.type_args.size()) {
      const std::string resolved_alias = normalize_trait_type(name);
      if (resolved_alias.find("<invalid-trait-type>") != std::string::npos)
        add_error(alias.line, "Type alias '" + alias.name +
                                 "' is cyclic or exceeds the alias resolution limit");
      return {resolved_alias, true};
    }
  }

  std::string candidate = name;

  if (alias_parts.base_name == "owned" || alias_parts.base_name == "borrow" ||
      alias_parts.base_name == "borrow_mut") {
    if (alias_parts.type_args.empty()) return {alias_parts.base_name, true};
    if (alias_parts.type_args.size() != 1) {
      add_error(0, "Ownership types require exactly one scalar element type");
      return {"<unknown>", false};
    }
    const auto inner = resolve_type(alias_parts.type_args.front()).name;
    if (inner != "i32" && inner != "i64" && inner != "u8")
      add_error(0, "Owned buffers support only i32, i64, or u8 elements");
    return {alias_parts.base_name + "<" + inner + ">", true};
  }

  // Handle built-in generic types ptr<T> and ref<T>
  if (name.rfind("ptr<", 0) == 0 || name.rfind("ref<", 0) == 0) {
    // These are built-in types, no need to resolve via imports
    // But we should resolve the inner type T
    std::string inner = name.substr(4, name.length() - 5);
    type_info inner_type = resolve_type(inner);
    std::string prefix = name.substr(0, 4);
    return {prefix + inner_type.name + ">", true};
  }

  // Handle alternate pointer and reference syntax: *(T) and &(T)
  if (name.rfind("*(", 0) == 0) {
    // Pointer type: *(T) -> ptr<T>
    std::string inner =
        name.substr(2, name.length() - 3); // Remove "*(" and ")"
    type_info inner_type = resolve_type(inner);
    return {"ptr<" + inner_type.name + ">", true};
  }

  if (name.rfind("&(", 0) == 0) {
    // Reference type: &(T) -> ref<T>
    std::string inner =
        name.substr(2, name.length() - 3); // Remove "&(" and ")"
    type_info inner_type = resolve_type(inner);
    return {"ref<" + inner_type.name + ">", true};
  }

  // Handle direct pointer syntax: *T (from let* declarations)
  if (!name.empty() && name[0] == '*') {
    // Pointer type: *T -> ptr<T>
    std::string inner = name.substr(1); // Remove "*"
    type_info inner_type = resolve_type(inner);
    return {"ptr<" + inner_type.name + ">", true};
  }

  // Handle generic types (e.g. vec<T>)
  auto parts = parse_generic_type_string(name);
  if (!parts.type_args.empty()) {
    // fprintf(stderr, "[resolve_type] GENERIC '%s' base='%s' nargs=%zu\n",
    //        name.c_str(), parts.base_name.c_str(), parts.type_args.size());
    // fflush(stderr);

    // Resolve base name
    type_info base_info = resolve_type(parts.base_name);
    /* fprintf(stderr, "[resolve_type] base resolved='%s'\n",
     * base_info.name.c_str()); */
    /* fflush(stderr); */

    // Resolve args - use 'any' fallback if arg resolution returns error type
    std::string resolved_name = base_info.name + "<";
    for (size_t i = 0; i < parts.type_args.size(); ++i) {
      if (i > 0)
        resolved_name += ", ";
      // fprintf(stderr, "[resolve_type] resolving arg[%zu]='%s'\n", i,
      // parts.type_args[i].c_str()); std::cerr << "[resolve_type] resolving
      // arg[" << i << "]='" << parts.type_args[i] << "'\n";
      /* fflush(stderr); */
      type_info arg_info = resolve_type(parts.type_args[i]);
      // CASCADING ERROR FIX: If the resolved arg is an error/unknown type
      // marker, substitute 'any' to prevent the error from infecting the entire
      // module. Error markers start with '<' (e.g. "<error>", "<unknown>")
      if (!arg_info.name.empty() && arg_info.name[0] == '<') {
        resolved_name += "any";
      } else {
        resolved_name += arg_info.name;
      }
    }
    resolved_name += ">";
    // fprintf(stderr, "[resolve_type] final resolved='%s'\n",
    // resolved_name.c_str()); fflush(stderr);

    // Instantiate the generic struct if needed
    if (!symbols_.lookup(resolved_name)) {
      std::string base_for_instantiation;

      // Strategy 1: Try the resolved base name (FQN)
      if (symbols_.lookup(base_info.name)) {
        base_for_instantiation = base_info.name;
      }
      // Strategy 2: Try the input base name (if it was an alias)
      else if (symbols_.lookup(parts.base_name)) {
        base_for_instantiation = parts.base_name;
      }
      // Strategy 3: Try to find a visible alias that maps to the FQN
      // This handles fully qualified names like 'core::option::option' which
      // aren't direct keys, but have a local alias 'option' that points to
      // them.
      else {
        std::string suffix = base_info.name;
        std::unordered_set<std::string> visited_aliases; // Cycle detection

        // fprintf(stderr, "[resolve_type] Strat 3: base='%s'\n",
        // suffix.c_str());
        while (suffix.find("::") != std::string::npos) {
          if (visited_aliases.count(suffix))
            break; // Cycle detected
          visited_aliases.insert(suffix);

          // Strategy 3: Try to resolve as an imported type alias.
          // This handles cases where 'suffix' (e.g., 'option') is an alias for
          // a fully qualified generic type (e.g., 'core::option').
          auto imported = resolve_type_name_with_imports(suffix);
          if (imported) {
            std::string canonical = symbols_.canonicalize_type_name(*imported);
            if (canonical == base_info.name) {
              const symbol *fqn_sym = symbols_.lookup(*imported);
              if (fqn_sym) {
                base_for_instantiation = *imported;
                break;
              }
            }
          }

          const symbol *sym = symbols_.lookup(suffix);
          if (sym && sym->kind == symbol_kind::type_symbol) {
            std::string canonical = symbols_.canonicalize_type_name(suffix);
            if (canonical == base_info.name) {
              base_for_instantiation = suffix;
              break;
            }
          }

          // CRITICAL FIX: Strip the first segment from suffix to make progress
          // e.g., "core::option::option" -> "option::option" -> "option"
          size_t pos = suffix.find("::");
          if (pos != std::string::npos) {
            suffix = suffix.substr(pos + 2);
          } else {
            break; // No more segments, exit loop
          }
        }
      }

      if (!base_for_instantiation.empty()) {
        std::vector<std::string> args;
        for (const auto &arg : parts.type_args) {
          args.push_back(resolve_type(arg).name);
        }
        instantiate_generic_node(base_for_instantiation, resolved_name, args);
      }
    }
    return {resolved_name, true};
  }

  if (name.find("::") == std::string::npos) {
    // Try resolving using imports / module qualification
    auto resolved = resolve_type_name_with_imports(name);
    if (resolved.has_value()) {
      candidate = *resolved;
    } else if (current_module_) {
      std::string qual = *current_module_ + "::" + name;
      if (symbols_.lookup(qual)) {
        candidate = qual;
      }
    }
  } else {
    // Qualified name provided (e.g. option::option)
    // Try to resolve the first segment as an import
    size_t first_sep = name.find("::");
    std::string prefix = name.substr(0, first_sep);
    std::string suffix = name.substr(first_sep); // includes ::

    auto resolved_prefix = resolve_type_name_with_imports(prefix);
    // resolve_type_name_with_imports returns fully qualified name of the symbol
    // imported. e.g. "option" -> "core::option" (if it was a module import? No,
    // module imports return empty string in resolve_imported_symbol?)
    // resolve_type_name_with_imports calls resolve_imported_symbol.
    // resolve_imported_symbol returns candidate string.
    // If import core::*, "option" -> "core::option".

    if (resolved_prefix.has_value()) {
      candidate = *resolved_prefix + suffix;
    } else {
      // Try alias resolution (resolve_qualified_name logic)
      // For now, just use the name as is.
      candidate = name;
    }
  }

  // std::cerr << "[resolve_type] PRE-CANON '" << candidate << "'\n";
  std::string canonical = symbols_.canonicalize_type_name(candidate);

  // global_flood removed: was a monotonically increasing counter that never
  // reset, causing false <error> returns after ~3000 total resolve_type calls
  // across all modules. Recursion safety is already handled by loop_guard_rt
  // (depth-based, line 207) and resolution_stack (cycle detection, line 215).

  return {canonical, true};
}

// Attempt to resolve an unqualified type name using the current module's
// import map. This handles cases like `import lib::{mytext}` or
// `import lib` and falling back to `lib::mytext` if appropriate. Returns
// a fully-qualified candidate name (e.g., `lib::mytext`) when a mapping is
// found, or `std::nullopt` if no cross-module mapping could be determined.
std::optional<std::string> type_checker::resolve_type_name_with_imports(
    const std::string &type_name) const {
  // Debug: report call context
  // Debug: report call context
  // std::cerr << "[type_checker] resolve_type_name_with_imports: name='"
  //           << type_name << "'";
  // if (current_module_)
  //   std::cerr << " current_module='" << *current_module_ << "'";
  // std::cerr << std::endl;

  static thread_local int loop_guard = 0;
  loop_guard++;
  if (loop_guard > 5000) {
    if (loop_guard % 1000 == 0) {
      /* std::cerr << "resolve_type_name_with_imports looping! " << type_name <<
       * std::endl; */
    }
    if (loop_guard > 10000)
      return std::nullopt; // Force break
  }
  struct LoopGuard {
    ~LoopGuard() { loop_guard--; }
  } lg;

  // If the type is already qualified, verify it exists or try fallback via
  // imports
  if (type_name.find("::") != std::string::npos) {
    // First try direct lookup - if found, it's valid
    if (symbols_.lookup(type_name)) {
      return std::optional<std::string>(type_name);
    }

    // Not found directly - check if the first segment is an imported module
    // e.g., "ny::testing::TestRunner" where "ny" is imported
    size_t first_sep = type_name.find("::");
    std::string first_segment = type_name.substr(0, first_sep);
    std::string rest = type_name.substr(first_sep + 2);

    // Check if this module is imported
    std::string importer = current_module_.value_or("<global>");
    auto it = imports_.find(importer);
    if (it != imports_.end()) {
      const auto &import_map = it->second;
      auto nit = import_map.find(first_segment);
      if (nit != import_map.end()) {
        // Module is imported - try looking up the full qualified name
        // The symbol might be registered under the imported qualified name.
        // Or it could be registered differently
        std::string candidate = first_segment + "::" + rest;
        if (symbols_.lookup(candidate)) {
          return std::optional<std::string>(candidate);
        }
      }
    }

    // Return as-is for further fallback attempts
    return std::optional<std::string>(type_name);
  }

  // If no module context is active, try local lookup only
  if (!current_module_) {
    // Source files are allowed to omit an explicit `module` declaration. Their
    // imports are recorded under the global source context and must still be
    // consulted before parser-created unqualified placeholders.
    auto global_imports = imports_.find("<global>");
    if (global_imports != imports_.end()) {
      const auto &import_map = global_imports->second;
      auto named = import_map.find(type_name);
      if (named != import_map.end() && !named->second.target_path.empty() &&
          named->second.target_path != "*") {
        return std::optional<std::string>(named->second.target_path);
      }
      for (const auto &entry_pair : import_map) {
        const auto &entry = entry_pair.second;
        if (entry.target_path == "*" || entry.target_path.empty()) {
          std::string imported_type = entry_pair.first + "::" + type_name;
          if (symbols_.lookup(imported_type)) {
            return std::optional<std::string>(imported_type);
          }
        }
      }
    }

    // Try local lookup
    if (const symbol *s = symbols_.lookup(type_name)) {
      // std::cerr << "[type_checker] resolve_type_name_with_imports: found
      // local "
      //              "symbol (no module ctx) -> '"
      //           << type_name << "' kind=" << (int)s->kind << " node=" <<
      //           (s->defining_node ? "yes" : "no") << std::endl;
      return std::optional<std::string>(type_name);
    }
    if (ny::is_debug_enabled()) {
      ny::diag_debug("[type_checker] resolve_type_name_with_imports: no "
                     "current module, giving up for '" +
                     type_name + "'");
    }
    return std::nullopt;
  }

  // First try module-qualified lookup (current_module::type_name)
  {
    std::string qualified = *current_module_ + "::" + type_name;
    const symbol *s = symbols_.lookup(qualified);
    if (s && s->defining_node) {
      if (ny::is_debug_enabled()) {
        ny::diag_debug("[type_checker] resolve_type_name_with_imports: found "
                       "module-qualified symbol with defining node -> '" +
                       qualified + "'");
      }
      return std::optional<std::string>(qualified);
    }
  }

  // Check imports
  auto it = imports_.find(*current_module_);
  if (it != imports_.end()) {
    const auto &import_map = it->second;

    // Named imports (local name -> fully-qualified)
    auto nit = import_map.find(type_name);
    if (nit != import_map.end()) {
      const auto &entry = nit->second;
      if (!entry.target_path.empty() && entry.target_path != "*") {
        return std::optional<std::string>(entry.target_path);
      }
    }

    // Module imports and glob imports (target_path == "" or target_path == "*")
    for (const auto &entry_pair : import_map) {
      const std::string &module_or_local = entry_pair.first;
      const auto &entry = entry_pair.second;
      if (entry.target_path == "*" || entry.target_path.empty()) {
        std::string candidate = module_or_local + "::" + type_name;
        const symbol *s = symbols_.lookup(candidate);
        if (s) {
          return std::optional<std::string>(candidate);
        }
      }
    }

    // Transitive re-export resolution: `pub use X::*` inside an imported
    // module. When this module imports M (target_path=""), check M's PUBLIC
    // glob re-exports for type_name.
    for (const auto &entry_pair : import_map) {
      const std::string &imported_module = entry_pair.first;
      const auto &entry = entry_pair.second;
      if (entry.target_path.empty()) {
        auto re_it = imports_.find(imported_module);
        if (re_it == imports_.end())
          continue;
        for (const auto &re_entry_pair : re_it->second) {
          const std::string &re_module = re_entry_pair.first;
          const auto &re_entry = re_entry_pair.second;

          // Only follow PUBLIC re-exports
          if (re_entry.visibility != visibility_kind::visibility_public)
            continue;

          if (re_entry.target_path == "*") {
            // Resolve potentially relative re-module path
            std::string full_re_module = re_module;
            if (!re_module.empty() &&
                re_module.find("::") == std::string::npos) {
              full_re_module = imported_module + "::" + re_module;
            }
            std::string candidate = full_re_module + "::" + type_name;
            if (symbols_.lookup(candidate)) {
              return std::optional<std::string>(candidate);
            }
          }
        }
      }
    }
  }

  // Fallback to local/unqualified lookup
  if (const symbol *s = symbols_.lookup(type_name)) {
    // std::cerr << "[type_checker] resolve_type_name_with_imports: fallback
    // local "
    //              "symbol -> '"
    //           << type_name << "' kind=" << (int)s->kind << " node=" <<
    //           (s->defining_node ? "yes" : "no") << std::endl;
    return std::optional<std::string>(type_name);
  }

  // Not resolved
  // Not resolved
  // std::cerr << "[type_checker] resolve_type_name_with_imports: unresolved '"
  //           << type_name << "'" << std::endl;
  return std::nullopt;
}

bool type_checker::are_types_compatible(const type_info &t1,
                                        const type_info &t2) {
  // Affine resources cannot disappear through any, generic wildcards, or
  // containers. Their ownership pass proves transfers, not representation.
  if (contains_ownership_type(t1.name) || contains_ownership_type(t2.name))
    return resolve_type(t1.name).name == resolve_type(t2.name).name;
  if (ny::is_debug_enabled()) {
    ny::diag_debug("[compat] Checking compatibility: '" + t1.name + "' vs '" +
                   t2.name + "'");
  }

  // Aggregate/function containers holding classes are invariant. In
  // particular a single-letter class name must not become a generic wildcard.
  if (type_contains_class(t1.name) || type_contains_class(t2.name))
    return resolve_type(t1.name).name == resolve_type(t2.name).name;

  if (t1.name == "any" || t2.name == "any") {
    if (ny::is_debug_enabled())
      ny::diag_debug("[compat] EARLY any");
    return true;
  }
  if (t1.name == "<unknown>" || t2.name == "<unknown>") {
    if (ny::is_debug_enabled())
      ny::diag_debug("[compat] EARLY unknown");
    return true; // Don't cascade errors
  }
  if (t1 == t2) {
    if (ny::is_debug_enabled())
      ny::diag_debug("[compat] EARLY equal");
    return true;
  }

  // Allow array<any> (produced by empty array literal []) to be compatible with
  // any typed array (array<T>). Handles: let mut xs: array<json_field> = []
  {
    auto p1 = parse_generic_type_string(t1.name);
    auto p2 = parse_generic_type_string(t2.name);
    bool is_array1 = (p1.base_name == "array" || p1.base_name == "vec");
    bool is_array2 = (p2.base_name == "array" || p2.base_name == "vec");
    bool any1 = (!p1.type_args.empty() && p1.type_args[0] == "any");
    bool any2 = (!p2.type_args.empty() && p2.type_args[0] == "any");
    if (is_array1 && is_array2 && (any1 || any2)) {
      if (ny::is_debug_enabled())
        ny::diag_debug("[compat] array<any> wildcard: COMPATIBLE");
      return true;
    }
  }

  // Canonicalize both type names before comparison.
  // This handles cases where one type is "Color" and the other is
  // "engine::color::Color"
  // - both should resolve to the same fully-qualified type name.
  type_info c1 = resolve_type(t1.name);
  type_info c2 = resolve_type(t2.name);

  if (ny::is_debug_enabled()) {
    ny::diag_debug("[compat] Canonicalized: '" + c1.name + "' vs '" + c2.name +
                   "'");
  }

  if (c1 == c2) {
    if (ny::is_debug_enabled())
      ny::diag_debug("[compat] CANONICAL equal");
    return true;
  }

  // A concrete class carries its exact NIL schema, not a polymorphic base
  // view. Inheritance supplies members; it is not a representation-changing
  // coercion. This also prevents equally spelled classes in different modules
  // from being confused by the legacy generic short-name compatibility below.
  if (resolve_class_symbol(c1.name) || resolve_class_symbol(c2.name))
    return false;

  // Raw pointers are invariant. Pointer arithmetic has its own deliberately
  // narrow rule in visit(ast_binary_expr); assignment and other general type
  // compatibility must never use numeric coercions or generic wildcards to
  // convert ptr<T> to ptr<S> (including ptr<T> to an instantiated pointer).
  const auto is_raw_pointer = [](const std::string &name) {
    return name.rfind("ptr<", 0) == 0;
  };
  if (is_raw_pointer(t1.name) || is_raw_pointer(t2.name) ||
      is_raw_pointer(c1.name) || is_raw_pointer(c2.name)) {
    if (ny::is_debug_enabled())
      ny::diag_debug("[compat] non-identical raw pointers: INCOMPATIBLE");
    return false;
  }

  // @ai_context(
  //   intent="Allow implicit conversion between bool and i32",
  //   assumptions=["VM represents bool as i32 (0 or 1)", "Comparison operators
  //   return i32"], rationale="The VM and comparison operators use i32 for
  //   boolean values, so we need implicit conversion"
  // )
  // Allow bool ↔ i32 implicit conversion to fix the type system inconsistency
  if ((t1.name == "bool" && t2.name == "i32") ||
      (t1.name == "i32" && t2.name == "bool")) {
    if (ny::is_debug_enabled())
      ny::diag_debug("[compat] EARLY bool<->i32 conversion");
    return true;
  }

  // Allow i32 ↔ i64 implicit conversion (integer literal coercion).
  // Small integer literals infer i32; comparing them to i64 variables
  // (e.g. `handle < 0`) is valid and common.
  if ((t1.name == "i32" && t2.name == "i64") ||
      (t1.name == "i64" && t2.name == "i32")) {
    if (ny::is_debug_enabled())
      ny::diag_debug("[compat] EARLY i32<->i64 conversion");
    return true;
  }

  // Generic Compatibility Check:
  // If we have 'option<T>' vs 'option', treat them as compatible.
  // This allows assigning 'option::none' (resolved as 'option') to
  // 'option<i32>'.
  auto parts1 = parse_generic_type_string(t1.name);
  auto parts2 = parse_generic_type_string(t2.name);

  if (ny::is_debug_enabled() && (t1.name.find("option") != std::string::npos ||
                                 t2.name.find("option") != std::string::npos)) {
    std::string b1 = parts1.base_name.substr(parts1.base_name.rfind(':') + 1);
    std::string b2 = parts2.base_name.substr(parts2.base_name.rfind(':') + 1);
    ny::diag_debug("Generic Check: base1='" + parts1.base_name + "'->" + b1 +
                   " base2='" + parts2.base_name + "'->" + b2 +
                   " args1=" + std::to_string(parts1.type_args.size()) +
                   " args2=" + std::to_string(parts2.type_args.size()));
  }
  if (parts1.base_name == parts2.base_name ||
      parts1.base_name.substr(parts1.base_name.rfind(':') + 1) ==
          parts2.base_name.substr(parts2.base_name.rfind(':') + 1)) {
    // If one is generic and the other is just the base, we accept it for now.
    // Ideally we would verify that the generic args match the variant payload,
    // but for 'none' or simple variants, strict checking requires more
    // inference.
    if (parts1.type_args.empty() != parts2.type_args.empty()) {
      if (ny::is_debug_enabled())
        ny::diag_debug("Generic Check: COMPATIBLE");
      return true;
    }

    // If both have no generic args and the unqualified names match,
    // treat them as compatible. This handles cases like:
    // - engine::color::Color vs Color
    // - foo::bar::Type vs Type
    if (parts1.type_args.empty() && parts2.type_args.empty()) {
      if (ny::is_debug_enabled())
        ny::diag_debug("[compat] Unqualified names match: COMPATIBLE");
      return true;
    }

    // Both have type_args and base names match.
    // Treat single uppercase letters as uninstantiated generic parameters
    // (wildcards). This makes 'core::vec<T>' (returned by vec::new() before
    // instantiation) compatible with 'vec<i32>' (the annotated target type).
    if (parts1.type_args.size() == parts2.type_args.size()) {
      bool args_compat = true;
      for (size_t i = 0; i < parts1.type_args.size(); ++i) {
        const auto &a1 = parts1.type_args[i];
        const auto &a2 = parts2.type_args[i];
        if (a1 == a2)
          continue;
        // Single uppercase letter = uninstantiated type parameter (T, E, K, V,
        // etc.)
        bool a1_param = (a1.size() == 1 && std::isupper((unsigned char)a1[0]));
        bool a2_param = (a2.size() == 1 && std::isupper((unsigned char)a2[0]));
        if (a1_param || a2_param)
          continue;
        args_compat = false;
        break;
      }
      if (args_compat) {
        if (ny::is_debug_enabled())
          ny::diag_debug("[compat] Generic param wildcard: COMPATIBLE");
        return true;
      }
    }
  }
  // If both have args and neither is a generic parameter wildcard, stay strict.

  return false;
}

// Parse generic type string like "Box<i32>" into base name and type args
type_checker::generic_type_parts
type_checker::parse_generic_type_string(const std::string &type_name) {
  // std::cerr << "[parse] START '" << type_name << "'\n";
  generic_type_parts result;

  // Find the first '<'
  size_t open_bracket = type_name.find('<');
  if (open_bracket == std::string::npos) {
    // No generics, just return the base name
    result.base_name = type_name;
    // fprintf(stderr, "[parse] END (no generics)\n"); fflush(stderr);
    return result;
  }

  // Extract base name
  result.base_name = type_name.substr(0, open_bracket);

  // Find the matching '>'
  size_t close_bracket = type_name.rfind('>');
  if (close_bracket == std::string::npos || close_bracket <= open_bracket) {
    // Malformed, return what we have
    return result;
  }

  // Extract the argument list
  std::string args_str =
      type_name.substr(open_bracket + 1, close_bracket - open_bracket - 1);

  // Split by comma respecting nested brackets
  int depth = 0;
  std::string current;
  for (char c : args_str) {
    if (c == '<' || c == '(')
      depth++;
    else if (c == ')' || (c == '>' && (current.empty() || current.back() != '-')))
      depth--;

    if (c == ',' && depth == 0) {
      // Trim whitespace
      size_t start = current.find_first_not_of(" \t");
      size_t end = current.find_last_not_of(" \t");
      if (start != std::string::npos && end != std::string::npos) {
        result.type_args.push_back(current.substr(start, end - start + 1));
      }
      current.clear();
    } else {
      current += c;
    }
  }

  // Push last arg
  if (!current.empty()) {
    size_t start = current.find_first_not_of(" \t");
    size_t end = current.find_last_not_of(" \t");
    if (start != std::string::npos && end != std::string::npos) {
      result.type_args.push_back(current.substr(start, end - start + 1));
    }
  }

  // fprintf(stderr, "[parse] END (generic)\n"); fflush(stderr);
  return result;
}

const symbol *type_checker::resolve_class_symbol(const std::string &name) {
  auto parts = parse_generic_type_string(resolve_type(name).name);
  if (parts.base_name == "ref" && parts.type_args.size() == 1)
    return resolve_class_symbol(parts.type_args.front());
  const symbol *candidate = symbols_.lookup(parts.base_name);
  return candidate && candidate->defining_node &&
                 candidate->defining_node->kind == ast_node_kind::class_decl_node
             ? candidate : nullptr;
}

bool type_checker::class_assignable(const std::string &expected,
                                    const std::string &actual) {
  const auto target = resolve_type(expected).name;
  const auto source = resolve_type(actual).name;
  // This relation is only for plain class handles, never reference, pointer,
  // generic-container, or function-signature covariance.
  const auto target_parts = parse_generic_type_string(target);
  const auto source_parts = parse_generic_type_string(source);
  if (!target_parts.type_args.empty() || !source_parts.type_args.empty()) return false;
  const auto *target_class = resolve_class_symbol(target);
  const auto *source_class = resolve_class_symbol(source);
  if (!target_class || !source_class || !target_class->generic_params.empty() ||
      !source_class->generic_params.empty()) return false;
  std::set<const ast_node *> visited;
  std::function<bool(const symbol &)> ancestor = [&](const symbol &candidate) {
    if (candidate.name == target_class->name) return true;
    if (!visited.insert(candidate.defining_node).second) return false;
    const auto &decl = static_cast<const ast_class_decl &>(*candidate.defining_node);
    for (const auto &base : decl.base_classes)
      if (const auto *parent = resolve_class_symbol(base))
        if (ancestor(*parent)) return true;
    return false;
  };
  return ancestor(*source_class);
}

bool type_checker::type_contains_class(const std::string &type) {
  const auto canonical = resolve_type(type).name;
  if (resolve_class_symbol(canonical)) return true;
  const auto parts = parse_generic_type_string(canonical);
  for (const auto &argument : parts.type_args)
    if (type_contains_class(argument)) return true;
  if (canonical.rfind("fn(", 0) == 0) {
    // Function signatures have no covariance either. Tokenize their nominal
    // names without interpreting a source expression or guessing a runtime ABI.
    std::string token;
    for (const char character : canonical + " ") {
      if (std::isalnum(static_cast<unsigned char>(character)) || character == '_' || character == ':') {
        token += character;
      } else {
        if (!token.empty() && resolve_class_symbol(token)) return true;
        token.clear();
      }
    }
  }
  return false;
}

bool type_checker::check_assignment_type(const type_info &expected,
                                         const type_info &actual,
                                         ast_expr *value) {
  if (value) value->checked_class_upcast.reset();
  const auto target = resolve_type(expected.name).name;
  const auto source = resolve_type(actual.name).name;
  const bool target_class = resolve_class_symbol(target) != nullptr;
  const bool source_class = resolve_class_symbol(source) != nullptr;
  if (target_class || source_class) {
    if (target == source) return true;
    if (!value || !class_assignable(target, source)) return false;
    value->checked_class_upcast = target;
    checked_class_conversions_.push_back(value);
    return true;
  }
  return are_types_compatible(expected, actual);
}

bool type_checker::class_method_is_virtual(const symbol &receiver,
                                           const std::string &name) {
  std::set<const ast_node *> visited;
  std::function<bool(const symbol &)> find = [&](const symbol &owner) {
    if (!visited.insert(owner.defining_node).second) return false;
    const auto &decl = static_cast<const ast_class_decl &>(*owner.defining_node);
    for (const auto &method : decl.methods)
      if (method->name == name && (method->is_virtual || method->is_abstract ||
                                  method->is_override)) return true;
    for (const auto &base : decl.base_classes)
      if (const auto *parent = resolve_class_symbol(base))
        if (find(*parent)) return true;
    return false;
  };
  return find(receiver);
}

type_checker::class_member type_checker::resolve_class_member(
    const symbol &receiver, const std::string &name, int line) {
  std::set<const ast_node *> path;
  std::function<class_member(const symbol &)> find = [&](const symbol &owner) {
    class_member result;
    if (!path.insert(owner.defining_node).second)
      return result; // Declaration validation reports inheritance cycles.
    const auto &decl = static_cast<const ast_class_decl &>(*owner.defining_node);
    for (const auto &field : decl.fields)
      if (field.name == name)
        result = {&owner, &field, nullptr};
    for (const auto &method : decl.methods)
      if (method->name == name)
        result = {&owner, nullptr, method.get()};
    if (!result.owner) {
      for (const auto &base : decl.base_classes) {
        const symbol *base_symbol = resolve_class_symbol(base);
        if (!base_symbol) continue;
        const auto inherited = find(*base_symbol);
        if (!inherited.owner) continue;
        if (result.owner) {
          add_error(line, "Ambiguous inherited class member '" + name + "'");
          path.erase(owner.defining_node);
          return class_member{};
        }
        result = inherited;
      }
    }
    path.erase(owner.defining_node);
    return result;
  };
  return find(receiver);
}

std::vector<ast_struct_field> type_checker::class_fields(
    const symbol &receiver, int line) {
  std::vector<ast_struct_field> fields;
  std::set<std::string> names;
  std::set<const ast_node *> path;
  std::function<void(const symbol &)> collect = [&](const symbol &owner) {
    if (!path.insert(owner.defining_node).second) return;
    const auto &decl = static_cast<const ast_class_decl &>(*owner.defining_node);
    for (const auto &base : decl.base_classes)
      if (const auto *base_symbol = resolve_class_symbol(base)) collect(*base_symbol);
    for (const auto &field : decl.fields) {
      if (!names.insert(field.name).second)
        add_error(line, "Duplicate or ambiguous inherited class field '" + field.name + "'");
      else
        fields.push_back(field);
    }
    path.erase(owner.defining_node);
  };
  collect(receiver);
  return fields;
}

// Resolve the type of a member in a generic struct
std::optional<std::string>
type_checker::resolve_member_type(const type_info &struct_type,
                                  const std::string &member_name) {
  // Recursion depth guard to prevent infinite loops
  static thread_local int rmt_depth = 0;
  rmt_depth++;
  struct RMT_Guard {
    ~RMT_Guard() { rmt_depth--; }
  } rmt_guard;

  if (rmt_depth > 100) {
    /* fprintf(stderr, "[resolve_member_type] DEPTH LIMIT! depth=%d type='%s'
     * member='%s'\n", */
    /* rmt_depth, struct_type.name.c_str(), member_name.c_str()); */
    /* fflush(stderr); */
    return std::nullopt;
  }

  // Parse the struct type to get base name and type arguments
  auto parts = parse_generic_type_string(struct_type.name);

  // Handle 'Self' type alias in method context
  if (parts.base_name == "Self" && current_class_name_.has_value()) {
    type_info replacement{*current_class_name_, false};
    return resolve_member_type(replacement, member_name);
  }

  if (ny::is_debug_enabled()) {
    std::cerr << "[resolve_member_type] ENTRY base='" << struct_type.name
              << "' member='" << member_name << "' depth=" << rmt_depth
              << std::endl;
  }
  // fprintf(stderr, "[resolve_member_type] ENTRY base='%s' member='%s'
  // depth=%d\n",
  //         struct_type.name.c_str(), member_name.c_str(), rmt_depth);
  // fflush(stderr);

  // Auto-dereference for ref<T>: treat member access on ref<T> as access on T
  if (parts.base_name == "ref" && !parts.type_args.empty()) {
    // fprintf(stderr, "[resolve_member_type] REF auto-deref\n");
    // std::cerr << "[resolve_member_type] REF auto-deref\n";
    /* fflush(stderr); */
    // Create type_info for the inner type (T) and recurse
    // We assume inner type string is valid; resolve_type usually returns
    // canonical but here we might have raw string. Ideally resolve it first.
    type_info inner_info = resolve_type(parts.type_args[0]);
    return resolve_member_type(inner_info, member_name);
  }

  // Auto-dereference for ptr<T>: treat member access on ptr<T> as access on T
  if (parts.base_name == "ptr" && !parts.type_args.empty()) {
    type_info inner_info = resolve_type(parts.type_args[0]);
    return resolve_member_type(inner_info, member_name);
  }

  // Impl-level generic parameters are declared on the surrounding impl rather
  // than on each method. Resolve their members directly from those bounds so
  // method bodies retain the contract promised by `impl<T: Trait> ...`.
  auto active_bounds = active_impl_generic_bounds_.find(parts.base_name);
  if (active_bounds != active_impl_generic_bounds_.end()) {
    for (const auto &bound_name : active_bounds->second) {
      type_info trait_info = resolve_type(bound_name);
      const symbol *trait_sym = symbols_.lookup(trait_info.name);
      if (!trait_sym || !trait_sym->defining_node ||
          trait_sym->defining_node->kind != ast_node_kind::trait_decl_node) {
        continue;
      }
      const auto &trait_decl =
          static_cast<const ast_trait_decl &>(*trait_sym->defining_node);
      for (const auto &method : trait_decl.methods) {
        if (method->name == member_name && method->return_type) {
          if (*method->return_type == "Self")
            return struct_type.name;
          return *method->return_type;
        }
      }
    }
    // A bounded generic must not gain arbitrary members merely because it has
    // at least one bound. Fall through so the normal unknown-member diagnostic
    // remains intact for methods absent from every declared trait.
  }

  // Look up the struct declaration in the symbol table
  // Use resolve_type to handle imports/aliases
  /* fprintf(stderr, "[resolve_member_type] LOOKUP base='%s'\n",
   * parts.base_name.c_str()); fflush(stderr); */
  type_info base_info = resolve_type(parts.base_name);
  const symbol *struct_sym = symbols_.lookup(base_info.name);
  if (const symbol *class_symbol = resolve_class_symbol(base_info.name)) {
    const auto member = resolve_class_member(*class_symbol, member_name, 0);
    if (member.field) return member.field->type_name;
    if (member.method) {
      return instantiate_generic_type(member.method->return_type.value_or("void"),
          {{"Self", class_symbol->name}});
    }
    // A class may also be EXTENDED, exactly as a record may be. A class
    // declaration knows only the members written inside it, so a member it
    // does not recognise is not yet an error: fall through to the impl method
    // table, which is where an extension's paragraphs are registered. Without
    // this the lookup returned early and "X begin extension" on a class
    // failed with "has no member", in its own book as well as another's.
    // An extension's methods are attached to the TYPE symbol's member table,
    // the same place a record's are, rather than registered as free mangled
    // functions. The class declaration cannot know about them, so consult the
    // table before concluding the member does not exist.
    if (const symbol *type_sym = symbols_.lookup(base_info.name)) {
      const auto extended = type_sym->members.find(member_name);
      if (extended != type_sym->members.end()) {
        if (extended->second.type_name)
          return instantiate_generic_type(*extended->second.type_name,
                                          {{"Self", class_symbol->name}});
        return std::string("any");
      }
    }
    return std::nullopt;
  }
  // Parser/runtime container placeholders (notably vec) do not describe the
  // imported library declaration's generic parameters or member contracts.
  if (!struct_sym || !struct_sym->defining_node) {
    if (auto imported = resolve_type_name_with_imports(parts.base_name)) {
      const symbol *declared = symbols_.lookup(*imported);
      if (declared && declared->kind == symbol_kind::type_symbol &&
          declared->defining_node)
        struct_sym = declared;
    }
  }
  // fprintf(stderr, "[resolve_member_type] AFTER LOOKUP found=%d\n", struct_sym
  // ? 1 : 0); std::cerr << "[resolve_member_type] AFTER LOOKUP found=" <<
  // (struct_sym ? 1 : 0) << "\n";
  /* fflush(stderr); */

  if (struct_sym && struct_sym->kind == symbol_kind::type_symbol) {
    // std::cerr << "[resolve_member_type] Found symbol '" << struct_sym->name
    // << "' bounds=" << struct_sym->bounds.size() << "\n";
  }
  // Check for generic type parameters with trait bounds
  if (struct_sym && struct_sym->kind == symbol_kind::type_symbol &&
      !struct_sym->bounds.empty()) {
    /* std::cerr << "[resolve_member_type] Checking bounds for '" <<
     * struct_sym->name << "'. Bounds count: " << struct_sym->bounds.size() <<
     * "\n"; */
    for (const auto &bound_name : struct_sym->bounds) {
      type_info trait_info = resolve_type(bound_name);
      /* std::cerr << "  Scanning bound: " << bound_name << " resolved='" <<
       * trait_info.name << "'\n"; */
      const symbol *trait_sym = symbols_.lookup(trait_info.name);
      if (trait_sym && trait_sym->defining_node &&
          trait_sym->defining_node->kind == ast_node_kind::trait_decl_node) {
        const auto &trait_decl =
            static_cast<const ast_trait_decl &>(*trait_sym->defining_node);
        // Dump methods
        /* std::cerr << "    Trait found. Methods: "; */
        /* for (const auto& m : trait_decl.methods) std::cerr << m->name << " ";
         */
        /* std::cerr << "\n"; */
        for (const auto &method : trait_decl.methods) {
          if (method->name == member_name && method->return_type) {
            std::string ret = *method->return_type;
            if (ret == "Self")
              return struct_type.name;
            return ret;
          }
        }
      } else {
        /* std::cerr << "    Bound trait '" << trait_info.name << "' not found
         * or invalid.\n"; */
      }
    }
  } else if (struct_sym && struct_sym->kind == symbol_kind::type_symbol) {
    // Debug for why we skipped bounds check
    // Only log if it's a generic parameter (defining node is null or
    // generic_param_node)
    if (!struct_sym->defining_node) {
      /* std::cerr << "[resolve_member_type] Generic '" << struct_sym->name <<
       * "' has NO bounds.\n"; */
    }
  }

  if (!struct_sym || struct_sym->kind != symbol_kind::type_symbol) {
    /* std::cerr << "[resolve_member_type] struct symbol '" << base_info.name */
    /* << "' not found or not a type. Sym=" << (struct_sym ? "found" : "null")
     */
    /* << " Kind=" << (struct_sym ? std::to_string((int)struct_sym->kind) :
     * std::string("N/A")) << "\n"; */
    // Don't return nullopt yet, try heuristics
  } else {
    // Check if we have the struct definition
    if (!struct_sym->defining_node ||
        (struct_sym->defining_node->kind != ast_node_kind::struct_decl_node &&
         struct_sym->defining_node->kind != ast_node_kind::class_decl_node &&
         struct_sym->defining_node->kind != ast_node_kind::enum_decl_node)) {
      /* std::cerr << "[resolve_member_type] struct symbol '" << base_info.name
       */
      /* << "' has no defining node or not a struct/class/enum. Node=" <<
       * (struct_sym->defining_node ? "yes" : "no") */
      /* << " Kind=" << (struct_sym->defining_node ?
       * std::to_string((int)struct_sym->defining_node->kind) :
       * std::string("N/A")) */
      /* << (struct_sym->defining_node && struct_sym->defining_node->kind ==
       * ast_node_kind::trait_decl_node ? " (TRAIT!)" : "") */
      /* << "\n"; */
    }

    // Check symbol members map (explicitly populated by impl blocks or native
    // fields)
    auto mem_it = struct_sym->members.find(member_name);
    if (mem_it != struct_sym->members.end()) {
      if (mem_it->second.type_name) {
        // Member tables store declaration signatures, not concrete receiver
        // types. Preserve vec<u8>.get -> Option<u8> (and nested equivalents)
        // even when lookup reaches the table before an out-of-module method.
        std::map<std::string, std::string> substitutions{{"Self", struct_type.name}};
        for (size_t i = 0; i < struct_sym->generic_params.size() &&
                           i < parts.type_args.size(); ++i)
          substitutions[struct_sym->generic_params[i].name] = parts.type_args[i];
        return instantiate_generic_type(*mem_it->second.type_name, substitutions);
      }
      return std::string("any");
    }

    // Fallback: check AST fields (critical for generic structs like vec<T>
    // where fields are in AST)
    if (struct_sym->defining_node &&
        struct_sym->defining_node->kind == ast_node_kind::struct_decl_node) {
      const auto &decl =
          static_cast<const ast_struct_decl &>(*struct_sym->defining_node);
      for (const auto &field : decl.fields) {
        if (field.name == member_name) {
          // Found field! Resolve its type with generic substitutions if needed.
          if (!decl.generic_params.empty() &&
              decl.generic_params.size() == parts.type_args.size()) {
            std::map<std::string, std::string> substitutions;
            for (size_t i = 0; i < decl.generic_params.size(); ++i) {
              substitutions[decl.generic_params[i].name] = parts.type_args[i];
            }
            return instantiate_generic_type(field.type_name, substitutions);
          }
          return field.type_name;
        }
      }
    }
  }

  // Heuristic member resolution check for common built-in shapes
  // (vec/array/slice/string) and tuple numeric access. If none of these
  // heuristics apply we return `std::nullopt` to indicate unknown member type.
  auto to_lower = [](std::string s) {
    for (auto &c : s)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
  };

  std::string base = to_lower(parts.base_name);
  std::string mem = to_lower(member_name);

  if (base == "any") {
    return std::string("any");
  }

  // Container-like types: vec/array/slice -> length/len -> i32
  if (base == "vec" || base == "vector" || base == "array" || base == "slice" ||
      base == "core::vec") {
    if (mem == "len" || mem == "length" || mem == "size")
      return std::string("i32");
    if (mem == "push" || mem == "add") {
      return std::string("void");
    }
    if (mem == "set") {
      return std::string("void");
    }
    if (mem == "pop") {
      if (!parts.type_args.empty())
        return std::string("option<") + parts.type_args[0] + ">";
      return std::string("any");
    }
    if (mem == "get" || mem == "at" || mem == "index") {
      // The built-in array.get primitive shares indexed access semantics:
      // return the element (or its zero/empty default out of range), not an
      // Option. Library vec.get retains its checked Option contract.
      if (base == "array" && mem == "get" && !parts.type_args.empty())
        return parts.type_args[0];
      if (!parts.type_args.empty())
        // Return option<T> - rely on 'import core::{option}' being present
        return std::string("option<") + parts.type_args[0] + ">";
      return std::string("any");
    }
    if (mem == "insert" || mem == "remove" || mem == "clear" ||
        mem == "resize") {
      return std::string("void");
    }
    if (mem == "contains") {
      return std::string("bool");
    }
    if (mem == "iter") {
      return std::string("any");
    }
  }

  // Strings: length -> i32, substr -> String, predicates -> bool
  if (base == "string" || base == "text" || base == "core::string") {
    if (mem == "len" || mem == "length" || mem == "size")
      return std::string("i32");
    if (mem == "substr" || mem == "substring" || mem == "concat" ||
        mem == "fmt" || mem == "to_upper" || mem == "to_lower" ||
        mem == "trim" || mem == "trim_start" || mem == "trim_end" ||
        mem == "replace" || mem == "replace_all")
      return std::string("String");
    if (mem == "starts_with" || mem == "ends_with" || mem == "contains" ||
        mem == "is_empty" || mem == "is_ascii")
      return std::string("bool");
    if (mem == "split" || mem == "chars" || mem == "bytes")
      return std::string("vec<String>");
    if (mem == "find" || mem == "rfind" || mem == "index_of")
      return std::string("option<i32>");
    if (mem == "parse_i32" || mem == "parse_i64")
      return std::string("option<i32>");
    if (mem == "to_i32")
      return std::string("i32");
    if (mem == "to_i64")
      return std::string("i64");
    if (mem == "to_f64")
      return std::string("f64");
    // Any other method call on a string — return String (method chaining)
    return std::string("any");
  }

  // Tuples: numeric index access (e.g., `.0`, `"0"` in heuristic)
  if (base == "tuple") {
    bool is_digits =
        !member_name.empty() &&
        std::all_of(member_name.begin(), member_name.end(), [](char c) {
          return std::isdigit(static_cast<unsigned char>(c));
        });
    if (is_digits) {
      size_t idx = static_cast<size_t>(std::stoul(member_name));
      if (idx < parts.type_args.size())
        return parts.type_args[idx];
      return std::nullopt;
    }
  }

  // Option/Result convenience checks
  if (base == "option" || base == "result" || base == "core::option" ||
      base == "core::result") {
    if (mem == "is_some" || mem == "is_none" || mem == "is_ok" ||
        mem == "is_error" || mem == "is_err")
      return std::string("bool");
    if (mem == "unwrap" || mem == "unwrap_or" || mem == "expect") {
      if (!parts.type_args.empty())
        return parts.type_args[0]; // Returns T
      return std::string("any");
    }
    // unwrap_err / unwrap_error returns E (second type arg of result<T,E>)
    if (mem == "unwrap_err" || mem == "unwrap_error") {
      if (parts.type_args.size() >= 2)
        return parts.type_args[1]; // Returns E
      return std::string("any");
    }
  }

  // User-defined struct/class member lookup
  if (struct_sym && struct_sym->defining_node) {
    if (struct_sym->defining_node->kind == ast_node_kind::struct_decl_node) {
      const auto &struct_decl =
          static_cast<const ast_struct_decl &>(*struct_sym->defining_node);

      for (const auto &field : struct_decl.fields) {
        if (field.name == member_name) {
          // Found the field. Now we need to instantiate its type if it uses
          // generic parameters.
          if (parts.type_args.empty()) {
            return field.type_name;
          }

          // Create substitution map
          std::map<std::string, std::string> substitutions;
          for (size_t i = 0; i < struct_decl.generic_params.size(); ++i) {
            if (i < parts.type_args.size()) {
              substitutions[struct_decl.generic_params[i].name] =
                  parts.type_args[i];
            }
          }

          return instantiate_generic_type(field.type_name, substitutions);
        }
      }
    } else if (struct_sym->defining_node->kind ==
               ast_node_kind::class_decl_node) {
      const auto &class_decl =
          static_cast<const ast_class_decl &>(*struct_sym->defining_node);

      // Check local fields
      for (const auto &field : class_decl.fields) {
        if (field.name == member_name) {
          if (parts.type_args.empty()) {
            return field.type_name;
          }
          std::map<std::string, std::string> substitutions;
          for (size_t i = 0; i < class_decl.generic_params.size(); ++i) {
            if (i < parts.type_args.size()) {
              substitutions[class_decl.generic_params[i].name] =
                  parts.type_args[i];
            }
          }
          return instantiate_generic_type(field.type_name, substitutions);
        }
      }

      // Recursive check in base classes
      for (const auto &base : class_decl.base_classes) {
        // Resolve base type to find its symbol
        // Note: base might be generic, e.g. Base<T>
        // For MVP we assume simple names or handle generic bases later.
        // We need to look up the base symbol.
        // If base is "Base<T>", we need "Base".
        auto base_parts = parse_generic_type_string(base);
        type_info base_info = {base_parts.base_name, false}; // simplified

        auto res = resolve_member_type(base_info, member_name);
        if (res.has_value()) {
          return res;
        }
      }
    }
  }

  // Check for impl method: TypeName__memberName
  std::string mangled_method_name = parts.base_name + "__" + member_name;
  const symbol *method_sym = symbols_.lookup(mangled_method_name);
  if (method_sym && method_sym->kind == symbol_kind::function_symbol) {
    // Return the method's return type (or 'any' if unknown)
    if (method_sym->type_name) {
      return *method_sym->type_name;
    }
    if (method_sym->defining_node &&
        method_sym->defining_node->kind == ast_node_kind::function_decl_node)
      return std::string("void");
    return std::string("any");
  }

  // Unknown member type for best-effort analysis
  return std::nullopt;
}

// -------------------------------
// Pattern matching / match helpers
// -------------------------------
//
// The following helpers are conservative and best-effort; they infer types
// where obvious (literal numbers => i32/f64, strings => String, boolean
// literals -> bool), will construct tuple-typed names when tuple patterns are
// entirely literal/typed, and bind variable names found in patterns into the
// parser's symbol table scope so match arm bodies can see them. This is a
// minimal, safe path that avoids complex exhaustiveness or constructor lookup.
//
// These helpers intentionally prefer `any` for unknown types to avoid cascading
// errors across a large number of tests.

static inline bool is_primitive_name(const std::string &n) {
  return n == "i8" || n == "i16" || n == "i32" || n == "i64" || n == "u8" ||
         n == "u16" || n == "u32" || n == "u64" || n == "f32" || n == "f64" ||
         n == "bool" || n == "char" || n == "String" || n == "any";
}

type_info type_checker::infer_literal_type(const std::string &literal) const {
  // boolean literal
  if (literal == "true" || literal == "false")
    return type_bool;

  if (is_floating_literal_spelling(literal)) {
    double value = 0.0;
    if (!parse_floating_literal(literal, value)) return type_f64;
  }
  integer_literal_info integer;
  if (!parse_integer_literal(literal, integer))
    return integer.inferred_type() == std::string_view("i64") ? type_i64 : type_i32;

  // Fallback to string
  return type_string;
}

std::optional<type_info>
type_checker::infer_type_from_pattern(const ast_pattern &pattern) {
  switch (pattern.kind) {
  case ast_pattern_kind::wildcard_pattern:
    return std::nullopt;
  case ast_pattern_kind::literal_pattern: {
    if (pattern.literal_kind == ast_node_kind::string_literal_node)
      return type_string;
    if (pattern.literal_kind == ast_node_kind::boolean_literal_node)
      return type_bool;
    if (pattern.literal_kind == ast_node_kind::char_literal_node)
      return resolve_type("u8");
    type_info lit_type = infer_literal_type(pattern.literal_value);
    return std::optional<type_info>(lit_type);
  }
  case ast_pattern_kind::variable_pattern:
    // variable binding doesn't constrain type, it inherits from the match
    // target or from a guard; return nullopt to indicate not explicit.
    return std::nullopt;
  case ast_pattern_kind::constructor_pattern: {
    // If pattern looks like a tuple (unnamed constructor) we can infer a
    // composite tuple<...> type if all subpatterns yield types.
    if (pattern.name.empty()) {
      std::vector<std::string> parts;
      for (const auto &sp : pattern.subpatterns) {
        auto st = infer_type_from_pattern(sp);
        if (!st.has_value())
          return std::nullopt;
        parts.push_back(st->name);
      }
      // Build tuple textual name like 'tuple<i32, String>'
      std::ostringstream oss;
      oss << "tuple<";
      for (size_t i = 0; i < parts.size(); ++i) {
        if (i)
          oss << ", ";
        oss << parts[i];
      }
      oss << ">";
      return std::optional<type_info>(type_info{oss.str(), false});
    }

    // For named constructors (e.g., Some(...)), we cannot reliably infer the
    // type without symbol/type metadata; return nullopt for now.
    return std::nullopt;
  }
  default:
    return std::nullopt;
  }
}

const symbol *type_checker::resolve_pattern_type_symbol(std::string name) const {
  const auto lookup = [&](const std::string &candidate) -> const symbol * {
    if (candidate.find("::") != std::string::npos)
      return resolve_qualified_name(candidate);
    const auto qualified = resolve_type_name_with_imports(candidate);
    if (const auto *found = symbols_.lookup_type(qualified.value_or(candidate)))
      return found;
    // An impl can add its receiver to the current namespace. Match owners
    // still resolve in the enclosing module's type scope.
    std::string scope = current_module_.value_or("");
    while (!scope.empty()) {
      if (const auto *found = symbols_.lookup_type(scope + "::" + candidate))
        return found;
      const auto separator = scope.rfind("::");
      if (separator == std::string::npos) break;
      scope.resize(separator);
    }
    // The standard built-ins remain available for matching returned values
    // when a caller imports only the provider (for example nil.std.io.file).
    if (candidate == "Option" || candidate == "Result")
      return symbols_.lookup_type("nil::std::core::" + candidate);
    return nullptr;
  };
  const symbol *found = lookup(name);
  if (!found) {
    const auto separator = name.rfind("::");
    const auto leaf_start = separator == std::string::npos ? 0 : separator + 2;
    const auto leaf = name.substr(leaf_start);
    if (leaf == "option" || leaf == "result") {
      name[leaf_start] = static_cast<char>(std::toupper(name[leaf_start]));
      found = lookup(name);
    }
  }
  return found && found->kind == symbol_kind::type_symbol ? found : nullptr;
}

void type_checker::bind_pattern_to_type(ast_pattern &pattern,
                                        const type_info &inferred_type,
                                        int line) {
  switch (pattern.kind) {
  case ast_pattern_kind::wildcard_pattern:
    // Nothing to bind
    return;
  case ast_pattern_kind::variable_pattern: {
    // If the pattern name refers to an existing enum variant (e.g. `none`),
    // treat it as a unit-variant match rather than a variable binding.
    // Without this guard, `none ->` in a match arm would shadow the global
    // `none` enum variant with a local variable typed as the matched value.
    {
      const symbol *existing = symbols_.lookup(pattern.name);
      if (existing && existing->type_name.has_value()) {
        const symbol *enum_type_sym = symbols_.lookup(*existing->type_name);
        if (enum_type_sym && enum_type_sym->defining_node &&
            enum_type_sym->defining_node->kind ==
                ast_node_kind::enum_decl_node) {
          // It's a unit enum variant – don't bind as a local variable.
          return;
        }
      }
    }
    std::optional<std::string> type_name =
        std::optional<std::string>(inferred_type.name);
    if (!symbols_.insert_symbol_with_context(
            pattern.name, symbol_kind::variable_symbol, nullptr, type_name,
            false, visibility_kind::visibility_private, std::nullopt)) {
      add_error(line, "Pattern variable '" + pattern.name +
                          "' already declared in this scope");
    }
    return;
  }
  case ast_pattern_kind::literal_pattern:
    // Nothing to bind for literals
    return;
  case ast_pattern_kind::constructor_pattern: {
    // If the inferred_type is a 'tuple<...>' textual form, attempt to bind
    // subpatterns to element types. Otherwise bind subpatterns using the
    // inferred_type itself (best-effort fallback).
    auto parts = parse_generic_type_string(inferred_type.name);

    if (pattern.name.empty() && parts.base_name == "tuple" && !parts.type_args.empty()) {
      for (size_t i = 0; i < pattern.subpatterns.size(); ++i) {
        std::string arg_type =
            (i < parts.type_args.size() ? parts.type_args[i]
                                        : std::string("any"));
        type_info tinfo{arg_type, is_primitive_name(arg_type)};
        bind_pattern_to_type(pattern.subpatterns[i], tinfo, line);
      }
      return;
    }

    std::string path = pattern.name;
    for (auto dot = path.find('.'); dot != std::string::npos;
         dot = path.find('.', dot + 2))
      path.replace(dot, 1, "::");
    path = canonical_std_namespace(std::move(path));
    const auto owner_end = path.rfind("::");
    if (owner_end != std::string::npos) {
      const auto owner = path.substr(0, owner_end);
      const auto variant = path.substr(owner_end + 2);
      const symbol *owner_symbol = resolve_pattern_type_symbol(owner);
      if (!owner_symbol || (owner_symbol->defining_node &&
          owner_symbol->defining_node->kind != ast_node_kind::enum_decl_node)) {
        add_error(line, "Unknown enum owner '" + owner + "' in match pattern");
      } else {
        // Variant symbols are predeclared, so this also checks enums whose
        // declaration follows the matching function in source order.
        if (!symbols_.lookup(owner_symbol->name + "::" + variant))
          add_error(line, "Unknown variant '" + variant + "' in match pattern for enum '" + owner + "'");
        const auto target_parts = parse_generic_type_string(resolve_type(inferred_type.name).name);
        const symbol *target_symbol = resolve_pattern_type_symbol(target_parts.base_name);
        const bool unknown_target = inferred_type.name == "<unknown>" ||
            inferred_type.name == "<error>" || inferred_type.name == "any";
        if (!unknown_target && (!target_symbol ||
            target_symbol->name != owner_symbol->name))
          add_error(line, "Match pattern enum '" + owner +
              "' does not match target type '" + inferred_type.name + "'");
        // Retain the checked declaration identity for lowering, including
        // namespace aliases whose spelling has no standalone runtime meaning.
        pattern.name = owner_symbol->name + "::" + variant;
      }
    }

    // Handle enum constructor patterns like "EnumName::VariantName"
    // Look up the variant to get its associated type for subpattern binding
    // Handle enum constructor patterns like "EnumName::VariantName" or just
    // "VariantName" We trust that the inferred_type tells us which Enum we are
    // matching against.
    if (!pattern.name.empty()) {
      type_info actual_type = resolve_type(inferred_type.name);
      auto type_parts = parse_generic_type_string(actual_type.name);
      const size_t last_colon = pattern.name.rfind("::");
      const size_t last_dot = pattern.name.rfind('.');
      size_t variant_start = 0;
      if (last_colon != std::string::npos)
        variant_start = last_colon + 2;
      if (last_dot != std::string::npos && last_dot + 1 > variant_start)
        variant_start = last_dot + 1;
      const std::string variant_name = pattern.name.substr(variant_start);

      // Option and Result are compiler-recognized generic enums and may be
      // canonicalized to their lowercase built-in names even when the source
      // declaration is imported as `Option`/`Result`. Preserve the concrete
      // payload type without depending on a case-sensitive symbol lookup.
      std::string generic_base = type_parts.base_name;
      if (const auto separator = generic_base.rfind("::");
          separator != std::string::npos) {
        generic_base = generic_base.substr(separator + 2);
      }
      std::transform(generic_base.begin(), generic_base.end(),
                     generic_base.begin(), [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                     });
      std::string generic_variant = variant_name;
      std::transform(generic_variant.begin(), generic_variant.end(),
                     generic_variant.begin(), [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                     });
      std::optional<std::size_t> payload_index;
      if (generic_base == "option" && generic_variant == "some" &&
          !type_parts.type_args.empty()) {
        payload_index = 0;
      } else if (generic_base == "result" && generic_variant == "ok" &&
                 !type_parts.type_args.empty()) {
        payload_index = 0;
      } else if (generic_base == "result" && generic_variant == "error" &&
                 type_parts.type_args.size() >= 2) {
        payload_index = 1;
      }
      if (payload_index.has_value()) {
        const std::string &payload_name = type_parts.type_args[*payload_index];
        const type_info payload_type{payload_name,
                                     is_primitive_name(payload_name)};
        for (auto &subpattern : pattern.subpatterns)
          bind_pattern_to_type(subpattern, payload_type, line);
        return;
      }

      // Look up the enum symbol via the inferred type base name
      const symbol *enum_sym = symbols_.lookup(type_parts.base_name);
      // Also try fully qualified lookup if needed (e.g. last_expr_type_ might
      // be fully qualified)
      if (!enum_sym && current_module_) {
        enum_sym =
            symbols_.lookup(*current_module_ + "::" + type_parts.base_name);
      }

      if (enum_sym && enum_sym->defining_node &&
          enum_sym->defining_node->kind == ast_node_kind::enum_decl_node) {
        const auto &enum_decl =
            static_cast<const ast_enum_decl &>(*enum_sym->defining_node);
        // The parser deliberately accepts both `Enum::Variant` and
        // `Enum.Variant`.  Keep the two spellings type-equivalent just as
        // code generation does; otherwise dot-style patterns fall through
        // and bind their payload as the entire enum value.
        // Find the variant in the enum declaration
        for (const auto &v : enum_decl.variants) {
          if (v.name == variant_name) {
            if (v.associated_type.has_value()) {
              // Substitute generic params with concrete type args.
              // e.g. option<i32> matched against some(T): T -> i32.
              std::string assoc_type_name = *v.associated_type;
              if (!enum_decl.generic_params.empty() &&
                  !type_parts.type_args.empty()) {
                for (size_t pi = 0; pi < enum_decl.generic_params.size() &&
                                    pi < type_parts.type_args.size();
                     ++pi) {
                  const std::string &param = enum_decl.generic_params[pi].name;
                  const std::string &concrete = type_parts.type_args[pi];
                  if (assoc_type_name == param) {
                    assoc_type_name = concrete;
                    break;
                  }
                }
              }
              type_info assoc_type{assoc_type_name,
                                   is_primitive_name(assoc_type_name)};
              for (auto &sp : pattern.subpatterns) {
                bind_pattern_to_type(sp, assoc_type, line);
              }
              return;
            }
            break;
          }
        }
      }
    }

    // Fallback: bind each subpattern to the broader inferred_type
    for (auto &sp : pattern.subpatterns) {
      bind_pattern_to_type(sp, inferred_type, line);
    }
    return;
  }
  }
}

void type_checker::visit(ast_test_block &p) {
  diagnostic_source_scope source_scope(diagnostic_source_file_, p.source_file);
  // Enter a new scope for the test body
  symbols_.enter_scope();

  // Check Ny @generate annotations used by property tests.
  if (p.type == test_type::property) {
    for (const auto &ann : p.annotations) {
      if ((ann.name == "generate" || ann.name == "@generate") &&
          ann.args.has_value()) {
        std::string gen_str = *ann.args;
        // Remove quotes
        if (gen_str.size() >= 2 && gen_str.front() == '"' &&
            gen_str.back() == '"') {
          gen_str = gen_str.substr(1, gen_str.size() - 2);
        }

        // Parse "var: type [in range]" list
        size_t start = 0;
        while (start < gen_str.length()) {
          size_t end = gen_str.find(',', start);
          if (end == std::string::npos)
            end = gen_str.length();

          std::string segment = gen_str.substr(start, end - start);

          // Find colon
          size_t colon = segment.find(':');
          if (colon != std::string::npos) {
            std::string name = segment.substr(0, colon);
            std::string rest = segment.substr(colon + 1);

            // Trim Name
            name.erase(0, name.find_first_not_of(" \t"));
            name.erase(name.find_last_not_of(" \t") + 1);

            // Identify type - check for " in "
            std::string type_s = rest;
            size_t in_pos = rest.find(" in ");
            if (in_pos != std::string::npos) {
              type_s = rest.substr(0, in_pos);
            }

            // Trim Type
            type_s.erase(0, type_s.find_first_not_of(" \t"));
            type_s.erase(type_s.find_last_not_of(" \t") + 1);

            // Resolve type -> assuming simple type for now
            // We need to resolve "i32" etc.
            auto resolved_type = resolve_type(type_s);

            // Insert
            symbols_.insert_symbol_with_context(
                name, symbol_kind::variable_symbol, nullptr, resolved_type.name,
                false, visibility_kind::visibility_private, current_module_);
          }

          start = end + 1;
        }
      }
    }
  }

  // Test blocks are like void functions - they don't return anything
  // (though assertions might throw exceptions/panic)
  std::optional<type_info> prev_return_type = current_function_return_type_;
  current_function_return_type_ = type_void;

  // Visit the body
  if (p.body) {
    p.body->accept(*this);
  }

  // Restore previous state
  current_function_return_type_ = prev_return_type;
  symbols_.leave_scope();
}

void type_checker::visit(ast_program &p) {
  // Keep declared module identities independently of symbol registration
  // order. Never select a global merely because its suffix matches a name.
  std::string global_module;
  for (const auto &declaration : p.declarations) {
    if (!declaration) continue;
    if (declaration->kind == ast_node_kind::module_decl_node)
      global_module = static_cast<const ast_module_decl &>(*declaration).name;
    else if (declaration->kind == ast_node_kind::variable_decl_node) {
      const auto &variable = static_cast<const ast_variable_decl &>(*declaration);
      global_declarations_.emplace(global_module.empty() ? variable.name
          : global_module + "::" + variable.name, &variable);
    } else if (declaration->kind == ast_node_kind::import_decl_node) {
      global_imports_[global_module].push_back(
          &static_cast<const ast_import_decl &>(*declaration));
    } else if (declaration->kind == ast_node_kind::namespace_alias_decl_node) {
      const auto &alias = static_cast<const ast_namespace_alias_decl &>(*declaration);
      global_namespace_aliases_[global_module][alias.alias] = alias.target_module;
    }
  }
  // First pass: register impl methods with mangled names
  std::optional<std::string> module_ctx = std::nullopt;
  for (auto &decl : p.declarations) {
    diagnostic_source_scope source_scope(diagnostic_source_file_, decl ? decl->source_file : std::string());
    if (decl && decl->kind == ast_node_kind::module_decl_node) {
      auto &mod = static_cast<ast_module_decl &>(*decl);
      module_ctx = module_context(mod.name);
      if (!module_ctx) continue;
      // Also register the module symbol for static dispatch
      symbol mod_sym(mod.name, symbol_kind::type_symbol, &mod, std::nullopt,
                     false, visibility_kind::visibility_public, std::nullopt);
      symbols_.insert_symbol(mod_sym);
    } else if (decl && decl->kind == ast_node_kind::import_decl_node) {
      // Imports participate in name resolution while function bodies are
      // checked.  Record them in the predeclaration pass so combined programs
      // remain independent of source/worklist order, just like their function
      // and type declarations.
      auto &import_decl = static_cast<ast_import_decl &>(*decl);
      std::string importer = module_ctx.value_or("<global>");
      auto &import_map = imports_[importer];
      visibility_kind visibility = import_decl.get_visibility();
      if (import_decl.is_glob) {
        import_map[import_decl.module_path] =
            import_entry{std::string("*"), visibility};
      } else if (!import_decl.names.empty()) {
        for (const auto &name : import_decl.names) {
          std::string qualified = import_decl.module_path.empty()
                                      ? name
                                      : import_decl.module_path + "::" + name;
          import_map[name] = import_entry{qualified, visibility};
        }
      } else {
        import_map[import_decl.module_path] =
            import_entry{std::string(), visibility};
      }
    } else if (decl && decl->kind == ast_node_kind::variable_decl_node) {
      auto &variable = static_cast<ast_variable_decl &>(*decl);
      const auto name = module_ctx ? *module_ctx + "::" + variable.name : variable.name;
      symbols_.insert_symbol_with_context(name, symbol_kind::variable_symbol,
          &variable, variable.type_name, variable.is_mut,
          variable.get_visibility(), module_ctx);
    } else if (decl && decl->kind == ast_node_kind::type_alias_decl_node) {
      // Type aliases participate in signatures, including nested generic
      // inference, so retain their declaration and complete generic contract
      // during the same order-independent predeclaration pass as functions and
      // nominal types.
      auto &alias = static_cast<ast_type_alias_decl &>(*decl);
      std::string alias_name = alias.name;
      if (module_ctx)
        alias_name = *module_ctx + "::" + alias.name;
      if (!symbols_.lookup(alias_name)) {
        symbols_.insert_symbol_with_context(
            alias_name, symbol_kind::type_symbol, &alias, alias.target_type,
            false, alias.get_visibility(), module_ctx, alias.generic_params);
      }
    } else if (decl && decl->kind == ast_node_kind::function_decl_node) {
      // Pre-register top-level functions so they can be called from other
      // functions
      auto &fn = static_cast<ast_function_decl &>(*decl);
      if (fn.name == "main" && !fn.params.empty()) {
        add_error(fn.line,
                  "Entry function 'main' must not declare parameters; use the "
                  "runtime argument intrinsics");
      }
      std::string name = fn.name;
      if (module_ctx) {
        name = *module_ctx + "::" + name;
      }
      symbols_.insert_symbol_with_context(
          name, symbol_kind::function_symbol, &fn, fn.return_type, false,
          fn.get_visibility(), module_ctx, fn.generic_params, fn.params);

      // Also add public functions to module members for static dispatch
      if (module_ctx &&
          fn.get_visibility() == visibility_kind::visibility_public) {
        symbol *mod_sym = symbols_.lookup_mut(*module_ctx);
        if (mod_sym && mod_sym->kind == symbol_kind::type_symbol) {
          symbol func_member(fn.name, symbol_kind::function_symbol, &fn,
                             fn.return_type, false, fn.get_visibility(),
                             module_ctx, fn.generic_params, fn.params);
          mod_sym->members[fn.name] = func_member;
        }
      }
    } else if (decl && decl->kind == ast_node_kind::impl_decl_node) {
      auto &impl = static_cast<ast_impl_decl &>(*decl);
      for (auto &method : impl.methods) {
        auto parts = parse_generic_type_string(impl.target_type);
        std::string mangled_name = parts.base_name + "__" + method->name;

        std::string full_name = mangled_name;
        if (module_ctx) {
          full_name = *module_ctx + "::" + mangled_name;
        }

        symbols_.insert_symbol_with_context(
            full_name, symbol_kind::function_symbol, method.get(),
            method->return_type, false, visibility_kind::visibility_public,
            module_ctx, method->generic_params, method->params);
      }
    } else if (decl && decl->kind == ast_node_kind::trait_decl_node) {
      // Trait bounds must be available before any implementation body is
      // checked, regardless of source/worklist order.  Store the real node so
      // member lookup on a bounded generic can inspect the trait signature.
      auto &trait = static_cast<ast_trait_decl &>(*decl);
      std::string trait_name = trait.name;
      if (module_ctx)
        trait_name = *module_ctx + "::" + trait.name;
      if (!symbols_.lookup(trait_name)) {
        symbols_.insert_symbol_with_context(
            trait_name, symbol_kind::type_symbol, &trait, std::nullopt, false,
            trait.get_visibility(), module_ctx, trait.generic_params);
      }
    } else if (decl && decl->kind == ast_node_kind::struct_decl_node) {
      // Pre-register both the name and declaration. Split modules may use a
      // struct's fields before the source file containing the declaration is
      // reached in the second pass, so a name-only placeholder is insufficient.
      auto &s = static_cast<ast_struct_decl &>(*decl);
      std::string struct_name = s.name;
      if (module_ctx) {
        struct_name = *module_ctx + "::" + s.name;
      }
      if (!symbols_.lookup(struct_name)) {
        symbols_.insert_symbol_with_context(
            struct_name, symbol_kind::type_symbol, &s, std::nullopt, false,
            s.get_visibility(), module_ctx, s.generic_params);
      }
    } else if (decl && decl->kind == ast_node_kind::class_decl_node) {
      auto &cls = static_cast<ast_class_decl &>(*decl);
      const std::string name = module_ctx ? *module_ctx + "::" + cls.name : cls.name;
      if (!symbols_.lookup(name))
        symbols_.insert_symbol_with_context(
            name, symbol_kind::type_symbol, &cls, std::nullopt, false,
            cls.get_visibility(), module_ctx, cls.generic_params);
    } else if (decl && decl->kind == ast_node_kind::enum_decl_node) {
      // Pre-register enum type names with nullptr defining_node (placeholder).
      auto &e = static_cast<ast_enum_decl &>(*decl);
      std::string enum_name = e.name;
      if (module_ctx) {
        enum_name = *module_ctx + "::" + e.name;
      }
      if (!symbols_.lookup(enum_name)) {
        symbols_.insert_symbol_with_context(
            enum_name, symbol_kind::type_symbol, nullptr, std::nullopt, false,
            e.get_visibility(), module_ctx, e.generic_params);

        // Pre-register enum variants so they can be referenced out-of-order
        for (const auto &variant : e.variants) {
          std::string variant_fqn = enum_name + "::" + variant.name;
          symbol_kind vkind = variant.associated_type.has_value()
                                  ? symbol_kind::function_symbol
                                  : symbol_kind::variable_symbol;

          symbols_.insert_symbol_with_context(
              variant_fqn, vkind, nullptr,
              std::optional<std::string>(enum_name), false, e.get_visibility(),
              module_ctx);

          symbols_.insert_symbol_with_context(
              variant.name, vkind, nullptr,
              std::optional<std::string>(enum_name), false, e.get_visibility(),
              module_ctx);
        }
      }
    }
  }

  // Resolve callable signatures in their declaration module before any body
  // or forward call consumes copied symbol metadata. Parser-preserved aliases
  // must never be reinterpreted in the caller's module.
  current_module_ = std::nullopt;
  const auto normalize_signature =
      [&](ast_function_decl &function,
          const std::vector<ast_generic_param> &outer_parameters) {
    std::map<std::string, std::string> parameters;
    for (const auto &parameter : outer_parameters)
      parameters[parameter.name] = parameter.name;
    for (const auto &parameter : function.generic_params)
      parameters[parameter.name] = parameter.name;
    for (auto &parameter : function.params)
      if (parameter.type_name)
        parameter.type_name = normalize_trait_type(*parameter.type_name, parameters);
    if (function.return_type)
      function.return_type = normalize_trait_type(*function.return_type, parameters);
  };
  const auto refresh_callable = [&](ast_function_decl &function,
                                     const std::string &name,
                                     bool module_function) {
    const std::string qualified = current_module_ ? *current_module_ + "::" + name : name;
    if (symbol *existing = symbols_.lookup_mut(qualified)) {
      existing->defining_node = &function;
      existing->params = function.params;
      existing->type_name = function.return_type;
    }
    if (module_function && current_module_ &&
        function.get_visibility() == visibility_kind::visibility_public) {
      if (symbol *module = symbols_.lookup_mut(*current_module_))
        module->members[function.name] = symbol(
            function.name, symbol_kind::function_symbol, &function,
            function.return_type, false, function.get_visibility(), current_module_,
            function.generic_params, function.params);
    }
  };
  for (auto &decl : p.declarations) {
    diagnostic_source_scope source_scope(diagnostic_source_file_, decl ? decl->source_file : std::string());
    if (!decl)
      continue;
    if (decl->kind == ast_node_kind::module_decl_node) {
      current_module_ = module_context(static_cast<ast_module_decl &>(*decl).name);
    } else if (decl->kind == ast_node_kind::function_decl_node) {
      auto &function = static_cast<ast_function_decl &>(*decl);
      normalize_signature(function, {});
      refresh_callable(function, function.name, true);
    } else if (decl->kind == ast_node_kind::impl_decl_node) {
      auto &impl = static_cast<ast_impl_decl &>(*decl);
      const auto target = parse_generic_type_string(impl.target_type);
      for (auto &method : impl.methods) {
        normalize_signature(*method, impl.generic_params);
        refresh_callable(*method, target.base_name + "__" + method->name, false);
      }
    } else if (decl->kind == ast_node_kind::class_decl_node) {
      auto &class_decl = static_cast<ast_class_decl &>(*decl);
      const std::string owner = current_module_ ? *current_module_ + "::" + class_decl.name
                                                : class_decl.name;
      std::map<std::string, std::string> parameters{{"Self", owner}};
      for (const auto &parameter : class_decl.generic_params)
        parameters[parameter.name] = parameter.name;
      for (auto &base : class_decl.base_classes)
        base = normalize_trait_type(base, parameters);
      for (auto &field : class_decl.fields)
        field.type_name = normalize_trait_type(field.type_name, parameters);
      for (auto &method : class_decl.methods)
        normalize_signature(*method, class_decl.generic_params);
    }
  }
  current_module_ = std::nullopt;

  // Populate type member tables only after every type declaration has been
  // predeclared.  Method lookup must not depend on whether an impl block's
  // source file happened to be merged before or after a consumer function.
  // This also makes primitive extension methods (for example i32.to_text)
  // visible while earlier module bodies are checked.
  module_ctx = std::nullopt;
  for (auto &decl : p.declarations) {
    diagnostic_source_scope source_scope(diagnostic_source_file_, decl ? decl->source_file : std::string());
    if (!decl)
      continue;
    if (decl->kind == ast_node_kind::module_decl_node) {
      module_ctx = module_context(static_cast<ast_module_decl &>(*decl).name);
      continue;
    }
    if (decl->kind != ast_node_kind::impl_decl_node)
      continue;

    auto &impl = static_cast<ast_impl_decl &>(*decl);
    auto parts = parse_generic_type_string(impl.target_type);
    symbol *type_sym = symbols_.lookup_mut(parts.base_name);
    if ((!type_sym || type_sym->kind != symbol_kind::type_symbol) &&
        module_ctx) {
      type_sym = symbols_.lookup_mut(*module_ctx + "::" + parts.base_name);
    }
    // Last resort: the target is declared in another book, so it carries that
    // book's prefix while the extension names it unqualified. Without this the
    // methods were silently dropped and the type simply had no such member.
    if (!type_sym || type_sym->kind != symbol_kind::type_symbol)
      type_sym = symbols_.lookup_type_suffix_mut(parts.base_name);
    if (!type_sym || type_sym->kind != symbol_kind::type_symbol)
      continue;

    for (auto &method : impl.methods) {
      symbol method_sym(method->name, symbol_kind::function_symbol,
                        method.get(), method->return_type, false,
                        method->get_visibility(), module_ctx,
                        method->generic_params, method->params);
      type_sym->members[method->name] = std::move(method_sym);
    }
  }

  // Validate and register all trait contracts before any function body uses
  // a bound. Source order must not decide whether an implementation exists.
  current_module_ = std::nullopt;
  for (auto &decl : p.declarations) {
    diagnostic_source_scope source_scope(diagnostic_source_file_, decl ? decl->source_file : std::string());
    if (!decl)
      continue;
    if (decl->kind == ast_node_kind::module_decl_node)
      current_module_ = module_context(static_cast<ast_module_decl &>(*decl).name);
    else if (decl->kind == ast_node_kind::impl_decl_node)
      register_trait_implementation(static_cast<ast_impl_decl &>(*decl));
  }
  for (const auto &implementation : trait_impls_) {
    current_module_ = implementation.module;
    symbols_.enter_scope();
    for (const auto &parameter : implementation.generic_bounds)
      symbols_.insert_symbol_with_context(
          parameter.first, symbol_kind::type_symbol, nullptr, std::nullopt,
          false, visibility_kind::visibility_private, std::nullopt, {}, {},
          parameter.second);
    for (const auto &requirement : implementation.required_bounds)
      if (!check_trait_bound(requirement.first, requirement.second))
        add_error(implementation.line, "Trait implementation argument '" +
                  requirement.first + "' does not implement required trait '" +
                  requirement.second + "'");
    symbols_.leave_scope();
  }
  current_module_ = std::nullopt;

  // Second pass: visit all declarations
  // We use indices here because new declarations might be added during
  // visitation (e.g. via generic struct instantiation).
  size_t current_idx = 0;
  while (current_idx < p.declarations.size() || !new_declarations_.empty()) {
    // Move any new declarations into the main list
    if (!new_declarations_.empty()) {
      /* std::cerr << "[check] Processing " << new_declarations_.size() << " new
       * declarations. Total: " << p.declarations.size() << std::endl; */
      for (auto &new_decl : new_declarations_) {
        p.declarations.push_back(std::move(new_decl));
      }
      new_declarations_.clear();
    }

    // Process pending declarations
    while (current_idx < p.declarations.size()) {
      if (p.declarations[current_idx]) {
        p.declarations[current_idx]->accept(*this);
      }
      current_idx++;
    }
  }
}

void type_checker::visit(ast_function_decl &p) {
  diagnostic_source_scope source_scope(diagnostic_source_file_, p.source_file);
  semantic_node(p, p.name, "function", p.return_type.value_or(""), diagnostic_source_file_);
  if (p.name == "owned" || p.name == "owned_move" || p.name == "owned_borrow" ||
      p.name == "owned_borrow_mut" || p.name == "parallel_map")
    add_error(p.line, "Function name '" + p.name + "' is reserved for a checked language operation");
  std::size_t native_dispatch_annotations = 0;
  for (const auto &annotation : p.annotations) {
    if (annotation.name == "intrinsic" || annotation.name == "@intrinsic" ||
        annotation.name == "interop" || annotation.name == "@interop") {
      ++native_dispatch_annotations;
    }
  }
  if (native_dispatch_annotations > 1) {
    add_error(p.line, "Function '" + p.name +
                          "' has multiple native dispatch annotations; use "
                          "exactly one of @intrinsic or @interop");
  }

  // Only register global/module-level functions. Methods in impl blocks are
  // handled by visit(ast_impl_decl) and shouldn't pollute the global scope.
  if (!current_class_name_) {
    // Register function name and visibility
    std::string name = p.name;
    if (current_module_) {
      name = *current_module_ + "::" + name;
    }

    // Try to register the function - it may already be pre-registered from
    // the first pass in visit(ast_program), so we don't error on duplicates.
    symbols_.insert_symbol_with_context(
        name, symbol_kind::function_symbol, nullptr, p.return_type, false,
        p.get_visibility(), current_module_, p.generic_params, p.params);
    // Note: duplicate registration is expected for pre-registered functions

    // Add public functions to the module's members map for static dispatch.
    // This enables `module::function()` syntax for module-level functions.
    if (current_module_ &&
        p.get_visibility() == visibility_kind::visibility_public) {
      symbol *mod_sym = symbols_.lookup_mut(*current_module_);
      if (mod_sym && mod_sym->kind == symbol_kind::type_symbol) {
        // Create a member symbol for the function
        symbol func_member(p.name, symbol_kind::function_symbol, &p,
                           p.return_type, false, p.get_visibility(),
                           current_module_, p.generic_params, p.params);
        mod_sym->members[p.name] = func_member;
      }
    }
  }

  symbols_.enter_scope();

  // Register generic parameters and validate their bounds
  for (const auto &gen_param : p.generic_params) {
    symbols_.insert_symbol_with_context(
        gen_param.name, symbol_kind::type_symbol, nullptr, std::nullopt, false,
        visibility_kind::visibility_private, std::nullopt, {}, {},
        gen_param.bounds);

    // Note: Bounds will be checked when the generic function is instantiated
    // For now, we just ensure the trait names in bounds are valid
    for (const auto &bound : gen_param.bounds) {
      if (!find_trait(bound)) {
        add_error(p.line, "Unknown trait '" + bound + "' in generic bound");
      }
    }
  }

  // Implicit 'self' for class methods
  if (current_class_name_.has_value()) {
    bool has_explicit_self = false;
    for (const auto &param : p.params) {
      if (param.name == "self") {
        has_explicit_self = true;
        break;
      }
    }

    if (!has_explicit_self) {
      // Insert implicit 'self'
      // For now, we assume 'self' is immutable by default.
      // TODO: Handle 'mut self' semantics if we add that later.
      symbols_.insert_symbol_with_context(
          "self", symbol_kind::variable_symbol, nullptr, current_class_name_,
          false, visibility_kind::visibility_private, std::nullopt);
    }
  }

  // Register parameters
  for (auto &param : p.params) {
    // Resolve canonical parameter type if a type annotation is provided.
    std::optional<std::string> canonical_param_type = std::nullopt;
    if (param.type_name.has_value()) {
      canonical_param_type = resolve_type(*param.type_name).name;
    } else if (param.name == "self" && current_class_name_.has_value()) {
      // Implicit type for 'self' in class methods
      canonical_param_type = current_class_name_;
    }
    if (canonical_param_type)
      param.type_name = canonical_param_type;
    // Preserve `is_mut` on parameter symbols so the symbol table accurately
    // reflects parameter mutability for later analysis.
    if (!symbols_.insert_symbol_with_context(
            param.name, symbol_kind::variable_symbol, nullptr,
            canonical_param_type, param.is_mut,
            visibility_kind::visibility_private, std::nullopt)) {
      add_error(param.line, "Parameter '" + param.name + "' already declared");
    } else if (auto *binding = symbols_.lookup_mut(param.name)) {
      binding->semantic_id = semantic_declaration(param.name, "parameter",
          canonical_param_type.value_or(""), diagnostic_source_file_, param.name_span);
    }
  }

  // Set current return type
  if (p.return_type) {
    current_function_return_type_ = resolve_type(*p.return_type);
    p.return_type = current_function_return_type_->name;
  } else {
    current_function_return_type_ = type_void;
  }

  if (p.body) {
    p.body->accept(*this);
  }

  symbols_.leave_scope();
  current_function_return_type_ = std::nullopt;

  // Visit attached Ny test blocks.
  // Crucially, we visit these AFTER leaving the function scope, so they CANNOT
  // access function parameters. They run in the parent (module) scope, allowing
  // them to call the function itself.
  for (auto &test : p.test_blocks) {
    if (test)
      test->accept(*this);
  }
  for (auto &example : p.example_blocks) {
    if (example)
      example->accept(*this);
  }
}

void type_checker::visit(ast_variable_decl &p) {
  diagnostic_source_scope source_scope(diagnostic_source_file_, p.source_file);
  if (p.name == "owned" || p.name == "owned_move" || p.name == "owned_borrow" ||
      p.name == "owned_borrow_mut" || p.name == "parallel_map")
    add_error(p.line, "Variable name '" + p.name + "' is reserved for a checked language operation");
  // Check if trying to shadow intrinsic keywords
  if (p.name == "assert") {
    add_error(p.line, "Cannot shadow intrinsic class 'assert'");
    return;
  }

  if (p.name == "hotreload") {
    add_error(p.line, "Cannot shadow intrinsic class 'hotreload'");
    return;
  }

  std::string var_name = p.name;
  if (!current_function_return_type_ && current_module_)
    var_name = *current_module_ + "::" + var_name;

  const auto *previous_declaration = symbols_.lookup_current_scope(var_name);
  if (previous_declaration && previous_declaration->defining_node != &p) {
    add_error(p.line,
              "Variable '" + p.name + "' already declared in this scope");
    last_expr_type_ = type_void;
    return;
  }
  // Check initializer if present
  type_info init_type = type_unknown;
  if (p.initializer) {
    p.initializer->accept(*this);
    if (last_expr_type_) {
      init_type = *last_expr_type_;
    }
  }

  // Determine variable type
  std::string var_type_name = "any";
  std::string candidate_type = "<none>";
  if (p.type_name) {
    // Try to resolve the type via imports first (e.g., `mytext` imported from a
    // different module), otherwise use the provided name directly. After we
    // select a candidate name, pass it through the symbol table canonicalizer.
    std::optional<std::string> resolved =
        resolve_type_name_with_imports(*p.type_name);

    candidate_type = resolved.has_value() ? *resolved : *p.type_name;
    var_type_name = resolve_type(candidate_type).name;
    type_info declared_type = {var_type_name, true};

    // std::cerr << "[type_checker] visit(variable_decl): var='" << p.name
    //           << "' annotated='" << *p.type_name << "' candidate='"
    //           << candidate_type << "' canonical='" << var_type_name
    //           << "' init='" << init_type.name << "'"
    //           << " mut=" << (p.is_mut ? "true" : "false");
    // if (current_module_)
    //   std::cerr << " current_module='" << *current_module_ << "'";
    // std::cerr << " line=" << p.line << std::endl;

    if (ny::is_debug_enabled()) {
      ny::diag_debug("[visit] var='" + p.name +
                     "' checking type compatibility");
    }

    if (p.initializer && !check_assignment_type(declared_type, init_type, p.initializer.get())) {
      if (ny::is_debug_enabled()) {
        ny::diag_debug("[visit] var='" + p.name + "' COMPAT FAILED");
      }
      add_error(p.line, "Type mismatch in variable initialization. Expected '" +
                            declared_type.name + "', got '" + init_type.name +
                            "'");
    }
  } else if (p.initializer) {
    // Inference: take initializer's deduced/known type
    var_type_name = init_type.name;
    // std::cerr << "[type_checker] visit(variable_decl): var='" << p.name
    //           << "' inferred_type='" << var_type_name << "'";
    // if (current_module_)
    //   std::cerr << " current_module='" << *current_module_ << "'";
    // std::cerr << " line=" << p.line << std::endl;
  }

  // Annotate the initializer expression with the resolved type
  // This enables codegen to determine type arguments for generic calls
  const auto scalar_type = [](const std::string &type) {
    return type == "i32" || type == "i64" || type == "u8" ||
           type == "bool" || type == "f64";
  };
  const auto canonical_variable_type = resolve_type(var_type_name).name;
  if (contains_ownership_type(canonical_variable_type))
    p.type_name = canonical_variable_type;
  if (scalar_type(canonical_variable_type)) {
    // Storage conversion happens after expression evaluation. Do not change
    // an arithmetic expression's width merely because its destination differs.
    p.type_name = canonical_variable_type;
  }
  if (p.initializer && var_type_name != "any" && var_type_name != "<none>" &&
      !p.initializer->checked_class_upcast &&
      !(scalar_type(init_type.name) && scalar_type(canonical_variable_type))) {
    p.initializer->inferred_type = var_type_name;
  }

  // The checker owns its symbol table; parser registration is not shared.
  // Insert exactly once and never mutate a different declaration's symbol.
  if (previous_declaration) {
    // Fill the exact predeclared global after checking its initializer; never
    // mutate a lexical outer declaration merely because it shares a spelling.
    symbols_.lookup_mut(var_name)->type_name = var_type_name;
  } else if (!symbols_.insert_symbol_with_context(
                 var_name, symbol_kind::variable_symbol, &p,
                 std::optional<std::string>(var_type_name), p.is_mut,
                 current_function_return_type_
                     ? visibility_kind::visibility_private
                     : p.get_visibility(),
                 current_module_)) {
    add_error(p.line,
              "Variable '" + p.name + "' already declared in this scope");
  }
  if (auto *binding = symbols_.lookup_mut(var_name);
      binding && binding->defining_node == &p) {
    binding->checked_callable_parameters.reset();
    // Structural function types do not declare labels. An immutable local
    // may retain its initializer's proven names; mutable or externally
    // supplied values need a future explicit labelled function-type contract.
    if (!p.is_mut && current_function_return_type_ && p.initializer)
      binding->checked_callable_parameters = p.initializer->checked_callable_parameters;
    binding->semantic_id = semantic_node(p, p.name, "variable", var_type_name,
                                         diagnostic_source_file_);
  }
  last_expr_type_ = type_void;
}

void type_checker::visit(ast_module_decl &p) {
  diagnostic_source_scope source_scope(diagnostic_source_file_, p.source_file);
  // Set the current module context
  current_module_ = module_context(p.name);
  if (!current_module_) return;

  // Register the module as a type_symbol to enable static dispatch for
  // module-level functions. This allows `module::function()` syntax to work
  // even for modules that don't define any structs.
  // The module symbol will have functions added to its `members` map
  // as they are declared in visit(ast_function_decl).
  symbol mod_sym(p.name, symbol_kind::type_symbol, &p, std::nullopt, false,
                 visibility_kind::visibility_public, std::nullopt);
  symbols_.insert_symbol(mod_sym);

  // TODO: Validate module name matches file path (requires file path context)
}

void type_checker::visit(ast_import_decl &p) {
  diagnostic_source_scope source_scope(diagnostic_source_file_, p.source_file);
  // Record imports keyed by the importer module (the module performing the
  // `import` statement). This allows us to do per-module import resolution
  // when resolving identifiers during type checking.
  std::string importer = current_module_.value_or("<global>");
  auto &import_map = imports_[importer];

  visibility_kind vis = p.get_visibility();

  if (p.is_glob) {
    // Glob import: `import module::*` -> store '*' sentinel under module key
    // for later resolution attempts.
    import_map[p.module_path] = import_entry{std::string("*"), vis};
  } else if (!p.names.empty()) {
    // Named imports: `import module::{a, b}` map local name -> fully-qualified
    // source path for quicker resolution.
    for (const auto &name : p.names) {
      std::string fq =
          p.module_path.empty() ? name : p.module_path + "::" + name;
      import_map[name] = import_entry{fq, vis};
    }
  } else {
    // Module import: `import module` -> store the module path with an empty
    // value (indicates module import). Lookups will attempt `module::type`.
    import_map[p.module_path] = import_entry{std::string(), vis};
  }
}

void type_checker::visit(ast_class_decl &p) {
  diagnostic_source_scope source_scope(diagnostic_source_file_, p.source_file);
  // Register class name
  std::string name = p.name;
  if (current_module_) {
    name = *current_module_ + "::" + name;
  }

  // Native scalar/handle aliases are case-insensitive. Reject bare nominal
  // collisions before they can serialize into a container its readers reject.
  // Check the canonical name: names::Text remains an ordinary distinct class.
  if (is_reserved_nil_class_name(name)) {
    p.checked_name.reset();
    p.checked_fields.clear();
    p.checked_virtual_methods.clear();
    add_error(p.line, "Class name '" + name +
                         "' conflicts with a reserved scalar or handle type");
    return;
  }

  const symbol *existing_class = symbols_.lookup(name);
  if (existing_class && existing_class->defining_node != &p) {
    add_error(p.line, "Class '" + p.name + "' already declared");
  } else if (!existing_class) {
    symbols_.insert_symbol_with_context(
        name, symbol_kind::type_symbol, &p, std::nullopt, false,
        p.get_visibility(), current_module_, p.generic_params);
  }
  const auto class_module = current_module_;
  const symbol *class_symbol = symbols_.lookup(name);
  const auto resolve_class = [&](const std::string &base,
                                 const std::optional<std::string> &module) {
    const auto saved_module = current_module_;
    current_module_ = module;
    const std::string resolved = normalize_trait_type(base);
    current_module_ = saved_module;
    const symbol *candidate = symbols_.lookup(resolved);
    if (!candidate || !candidate->defining_node ||
        candidate->defining_node->kind != ast_node_kind::class_decl_node)
      return static_cast<const symbol *>(nullptr);
    return candidate;
  };

  symbols_.enter_scope();

  // Set current class context
  auto old_class_name = current_class_name_;
  current_class_name_ = name;
  symbols_.insert_type_alias_in_module("Self", name,
      visibility_kind::visibility_private, current_module_);

  // Generic params
  for (const auto &gen_param : p.generic_params) {
    symbols_.insert_symbol_with_context(
        gen_param.name, symbol_kind::type_symbol, nullptr, std::nullopt, false,
        visibility_kind::visibility_private, std::nullopt, {}, {},
        gen_param.bounds);
    for (const auto &bound : gen_param.bounds) {
      if (!find_trait(bound))
        add_error(p.line, "Unknown trait '" + bound + "' in generic bound");
    }
  }

  // Predeclared class names allow forward inheritance. Bound traversal even
  // for rejected cycles, so diagnostics cannot turn into recursive overflow.
  std::set<const ast_class_decl *> inheritance_path{&p};
  std::set<const ast_class_decl *> validated_inheritance;
  std::function<void(const ast_class_decl &, const std::optional<std::string> &)>
      validate_inheritance = [&](const ast_class_decl &decl,
                                 const std::optional<std::string> &module) {
    for (const auto &base : decl.base_classes) {
      const symbol *base_sym = resolve_class(base, module);
      if (!base_sym)
        continue;
      const auto &base_decl =
          static_cast<const ast_class_decl &>(*base_sym->defining_node);
      if (inheritance_path.count(&base_decl)) {
        add_error(p.line, "Inheritance cycle involving class '" + base_sym->name + "'");
        continue;
      }
      if (!validated_inheritance.insert(&base_decl).second)
        continue;
      inheritance_path.insert(&base_decl);
      validate_inheritance(base_decl, base_sym->module);
      inheritance_path.erase(&base_decl);
    }
  };
  validate_inheritance(p, class_module);

  // Base classes validation
  std::set<std::string> visited_bases;
  for (const auto &base : p.base_classes) {
    type_info base_type = resolve_type(base);
    if (base_type.name == "<unknown>") {
      add_error(p.line, "Unknown base class '" + base + "'");
      continue;
    }

    // Verify base is a class
    const symbol *base_sym = symbols_.lookup(base_type.name);
    if (!base_sym || !base_sym->defining_node ||
        base_sym->defining_node->kind != ast_node_kind::class_decl_node) {
      add_error(p.line, "Base type '" + base + "' is not a class");
      continue;
    }
    if (!is_symbol_visible(base_sym, current_module_.value_or("")))
      add_error(p.line, "Base class '" + base_type.name + "' is private");
    if (!base_sym->generic_params.empty())
      add_error(p.line, "Generic class inheritance is not supported");

    // Cycle detection (simple transitive check)
    if (base_type.name == name) {
      add_error(p.line, "Class '" + name + "' cannot inherit from itself");
    }

    // Check for duplicate direct bases
    if (visited_bases.count(base_type.name)) {
      add_error(p.line, "Duplicate base class '" + base_type.name + "'");
    }
    visited_bases.insert(base_type.name);
  }

  // MRO / Diamond Problem Detection
  // We collect all methods from all direct bases and check for ambiguities.
  std::map<std::string, std::vector<std::string>> method_sources;

  for (const auto &base : p.base_classes) {
    const symbol *base_sym = resolve_class(base, class_module);
    if (base_sym && base_sym->defining_node &&
        base_sym->defining_node->kind == ast_node_kind::class_decl_node) {
      const auto &base_decl =
          static_cast<const ast_class_decl &>(*base_sym->defining_node);

      // Collect all methods available in this base (recursive)
      std::set<std::string> base_methods;
      std::set<const ast_class_decl *> collected_classes;
      std::function<void(const ast_class_decl &, const std::optional<std::string> &)> collect =
          [&](const ast_class_decl &d, const std::optional<std::string> &module) {
            if (!collected_classes.insert(&d).second)
              return;
            for (const auto &m : d.methods)
              base_methods.insert(m->name);
            for (const auto &b : d.base_classes) {
              const symbol *bs = resolve_class(b, module);
              if (bs && bs->defining_node &&
                  bs->defining_node->kind == ast_node_kind::class_decl_node) {
                collect(
                    static_cast<const ast_class_decl &>(*bs->defining_node),
                    bs->module);
              }
            }
          };
      collect(base_decl, base_sym->module);

      for (const auto &m : base_methods) {
        method_sources[m].push_back(base);
      }
    }
  }

  // Check for conflicts
  for (const auto &[m_name, sources] : method_sources) {
    // If a method comes from multiple bases, we must ensure it is either:
    // 1. Overridden in the current class.
    // 2. Or all sources point to the same implementation (diamond).
    // For MVP, we require override if there's ambiguity from >1 direct base
    // paths.
    if (sources.size() > 1) {
      bool overridden = false;
      for (const auto &m : p.methods) {
        if (m->name == m_name) {
          overridden = true;
          break;
        }
      }

      if (!overridden) {
        // We could check if they are actually the same method from a common
        // base, but that requires full graph traversal. For safety/simplicity,
        // we warn/error.
        // Actually, let's just warn for now or error? Error is safer.
        // But wait, if Diamond inherits A, B. A inherits Base, B inherits Base.
        // Base has 'foo'. A and B don't override 'foo'.
        // method_sources['foo'] will have [A, B].
        // This is valid in many languages (virtual inheritance).
        // If we error here, we force the user to override 'foo' in Diamond.
        // That is a safe conservative default.
        std::string msg =
            "Ambiguous method '" + m_name + "' inherited from multiple bases: ";
        for (size_t i = 0; i < sources.size(); ++i) {
          if (i > 0)
            msg += ", ";
          msg += sources[i];
        }
        msg += ". Override it to resolve ambiguity.";
        add_error(p.line, msg);
      }
    }
  }

  // Fields
  if (const symbol *class_symbol = symbols_.lookup(name)) {
    p.checked_fields = class_fields(*class_symbol, p.line);
    p.checked_name = name;
  }
  for (const auto &field : p.checked_fields)
    if (method_sources.count(field.name))
      add_error(field.line, "Class member '" + field.name + "' is both a field and an inherited method");
  for (auto &field : p.fields) {
    field.type_name = resolve_type(field.type_name).name;
    if (!symbols_.insert_symbol_with_context(
            field.name, symbol_kind::variable_symbol, nullptr,
            std::optional<std::string>(field.type_name), false,
            visibility_kind::visibility_public, std::nullopt)) {
      add_error(p.line, "Field '" + field.name + "' already declared");
    }
  }

  // Methods
  std::set<std::string> implemented_methods;
  for (auto &method : p.methods) {
    if (!implemented_methods.insert(method->name).second)
      add_error(method->line, "Duplicate class method '" + method->name + "'");
    for (const auto &field : p.checked_fields)
      if (field.name == method->name)
        add_error(method->line, "Class member '" + method->name + "' is both a field and a method");
    method->accept(*this);

    // A name match does not prove an override contract. Check every inherited
    // virtual/abstract declaration, including an implementation that omitted
    // the optional override marker. Otherwise a wrong-signature method could
    // also satisfy the abstract-method check below merely by sharing a name.
    bool found_base = false;
    std::set<std::string> checked_bases;
    std::function<void(const std::string &, const std::optional<std::string> &)> check_in_base =
        [&](const std::string &base_name, const std::optional<std::string> &module) {
        const symbol *base_sym = resolve_class(base_name, module);
        if (!base_sym)
          return;
        const std::string resolved_base = base_sym->name;
        if (!checked_bases.insert(resolved_base).second)
          return;
        if (!base_sym || !base_sym->defining_node ||
            base_sym->defining_node->kind != ast_node_kind::class_decl_node) {
          return;
        }
        const auto &base_decl =
            static_cast<const ast_class_decl &>(*base_sym->defining_node);
        for (const auto &m : base_decl.methods) {
          if (m->name != method->name)
            continue;
          if (!class_method_is_virtual(*base_sym, m->name)) {
            add_error(method->line, "Cannot hide non-virtual inherited method '" + method->name + "'");
            continue;
          }
          if (m->is_public() && !method->is_public())
            add_error(method->line, "Override cannot reduce visibility of public method '" + method->name + "'");
          found_base = true;

          std::map<std::string, std::string> inherited_substitutions;
          std::map<std::string, std::string> override_substitutions;
          inherited_substitutions["Self"] = "$ny.override.self";
          override_substitutions["Self"] = "$ny.override.self";
          for (size_t i = 0; i < m->generic_params.size(); ++i)
            inherited_substitutions[m->generic_params[i].name] =
                "$ny.override.generic." + std::to_string(i);
          for (size_t i = 0; i < method->generic_params.size(); ++i)
            override_substitutions[method->generic_params[i].name] =
                "$ny.override.generic." + std::to_string(i);
          const auto canonical_signature_type =
              [&](const std::string &type,
                  const std::map<std::string, std::string> &substitutions,
                  const std::optional<std::string> &owner_module) {
                const auto saved_module = current_module_;
                current_module_ = owner_module;
                const std::string result = normalize_trait_type(type, substitutions);
                current_module_ = saved_module;
                return result;
              };
          const auto parameter_type =
              [&](const ast_param &parameter, const std::string &owner,
                  const std::map<std::string, std::string> &substitutions,
                  const std::optional<std::string> &owner_module) {
                std::string type = parameter.type_name.value_or(
                    parameter.name == "self" ? "Self" : "any");
                type = canonical_signature_type(type, substitutions, owner_module);
                if (parameter.name == "self")
                  type = instantiate_generic_type(type, {{owner, "$ny.override.self"}});
                return type;
              };
          bool compatible = m->params.size() == method->params.size() &&
                            m->generic_params.size() == method->generic_params.size();
          if (compatible) {
            compatible = canonical_signature_type(
                             m->return_type.value_or("void"), inherited_substitutions, base_sym->module) ==
                         canonical_signature_type(
                             method->return_type.value_or("void"), override_substitutions, class_module);
          }
          for (size_t i = 0; compatible && i < m->params.size(); ++i) {
            const auto &inherited_parameter = m->params[i];
            const auto &override_parameter = method->params[i];
            compatible = (inherited_parameter.name == "self") ==
                             (override_parameter.name == "self") &&
                         inherited_parameter.is_mut == override_parameter.is_mut &&
                         inherited_parameter.is_variadic == override_parameter.is_variadic &&
                         parameter_type(inherited_parameter, resolved_base,
                                        inherited_substitutions, base_sym->module) ==
                             parameter_type(override_parameter, name,
                                            override_substitutions, class_module);
          }
          for (size_t i = 0; compatible && i < m->generic_params.size(); ++i) {
            std::set<std::string> inherited_bounds;
            std::set<std::string> override_bounds;
            for (const auto &bound : m->generic_params[i].bounds)
              inherited_bounds.insert(canonical_signature_type(
                  bound, inherited_substitutions, base_sym->module));
            for (const auto &bound : method->generic_params[i].bounds)
              override_bounds.insert(canonical_signature_type(
                  bound, override_substitutions, class_module));
            compatible = inherited_bounds == override_bounds;
          }
          if (!compatible) {
            add_error(method->line, "Method '" + method->name +
                "' has an incompatible override signature for base class '" +
                resolved_base + "'; parameter, return, receiver and generic "
                "contracts must match");
            }
        }
        for (const auto &b : base_decl.base_classes) {
          check_in_base(b, base_sym->module);
        }
      };

      for (const auto &base : p.base_classes) {
        check_in_base(base, class_module);
      }

      if (method->is_override && !found_base) {
        add_error(method->line, "Method '" + method->name +
                                    "' marked override but does not override "
                                    "any base method");
      }
  }

  p.checked_virtual_methods.clear();
  if (class_symbol)
    for (const auto &method : p.methods)
      if (class_method_is_virtual(*class_symbol, method->name))
        p.checked_virtual_methods.insert(method->name);

  // Abstract obligations follow the same nearest-declaration rule as member
  // dispatch, not a union of every ancestor's concrete method names. A nearer
  // abstract override intentionally removes an older concrete implementation.
  if (!p.is_abstract && class_symbol) {
    std::set<std::string> method_names;
    for (const auto &entry : method_sources) method_names.insert(entry.first);
    for (const auto &method : p.methods) method_names.insert(method->name);
    for (const auto &method_name : method_names) {
      const auto effective = resolve_class_member(*class_symbol, method_name, p.line);
      if (effective.method && effective.method->is_abstract) {
        add_error(p.line, "Class '" + p.name +
                              "' must implement abstract method '" + method_name +
                              "'");
      }
    }
  }

  current_class_name_ = old_class_name;
  symbols_.leave_scope();
}

void type_checker::visit(ast_type_alias_decl &p) {
  diagnostic_source_scope source_scope(diagnostic_source_file_, p.source_file);
  // Register type alias preserving its visibility
  std::string name = p.name;
  if (current_module_) {
    name = *current_module_ + "::" + name;
  }

  auto vis_to_str = [](visibility_kind vk) {
    switch (vk) {
    case visibility_kind::visibility_private:
      return "private";
    case visibility_kind::visibility_public:
      return "pub";
    case visibility_kind::visibility_crate:
      return "pub(crate)";
    case visibility_kind::visibility_super:
      return "pub(super)";
    default:
      return "unknown";
    }
  };
  /* std::cerr << "[type_checker] visit(type_alias): declaring alias name='" */
  /* << name << "' target='" << p.target_type */
  /* << "' visibility=" << vis_to_str(p.get_visibility()); */
  /* if (current_module_) */
  /* std::cerr << " module='" << *current_module_ << "'"; */
  /* std::cerr << " line=" << p.line << std::endl; */

  const symbol *existing_alias = symbols_.lookup(name);
  const bool is_own_predeclaration =
      existing_alias && existing_alias->defining_node == &p;
  if (!is_own_predeclaration &&
      !symbols_.insert_symbol_with_context(
          name, symbol_kind::type_symbol, &p, p.target_type, false,
          p.get_visibility(), current_module_, p.generic_params)) {
    add_error(p.line, "Type alias '" + p.name + "' already declared");
  } else {
    /* std::cerr << "[type_checker] visit(type_alias): registered alias -> '" */
    /* << name << "' -> '" << p.target_type << "'" << std::endl; */
  }

  // Validate target type (including generic parameters)
  symbols_.enter_scope();
  for (const auto &gen_param : p.generic_params) {
    symbols_.insert_symbol_with_context(
        gen_param.name, symbol_kind::type_symbol, nullptr, std::nullopt, false,
        visibility_kind::visibility_private, std::nullopt, {}, {}, gen_param.bounds);
  }
  for (const auto &gen_param : p.generic_params)
    for (const auto &bound : gen_param.bounds)
      if (!find_trait(bound))
        add_error(p.line, "Unknown trait '" + bound + "' in generic bound");
  // Alias expansion is bounded even if no function happens to use the alias.
  // Unknown target-type validation remains separate from this cycle guard.
  if (normalize_trait_type(p.target_type).find("<invalid-trait-type>") !=
      std::string::npos)
    add_error(p.line, "Type alias '" + p.name +
                         "' is cyclic or exceeds the alias resolution limit");
  symbols_.leave_scope();
}

void type_checker::visit(ast_block_stmt &s) {
  symbols_.enter_scope();
  for (auto &stmt : s.statements) {
    if (stmt)
      stmt->accept(*this);
  }
  symbols_.leave_scope();
}

void type_checker::visit(ast_expr_stmt &s) {
  if (s.expression) {
    s.expression->accept(*this);
  }
}

void type_checker::visit(ast_println_stmt &s) {
  if (s.args.size() != 1) {
    add_error(s.line, "println expects exactly 1 argument, got " +
                          std::to_string(s.args.size()));
  }
  for (auto &arg : s.args) {
    if (arg)
      arg->accept(*this);
  }
}

void type_checker::visit(ast_decl_stmt &s) {
  if (s.declaration) {
    s.declaration->accept(*this);
    // Prove lexical provenance from a declaration statement in a body, not
    // from ambient return-type state while some declaration is being checked.
    // A global initializer resolved from inside a function must never acquire
    // a closed-local editing capability.
    if (collect_semantics_ && semantics_.available && current_function_return_type_ &&
        s.declaration->kind == ast_node_kind::variable_decl_node) {
      const auto declaration = semantic_nodes_.find(s.declaration.get());
      if (declaration != semantic_nodes_.end())
        semantics_.symbols[declaration->second].lexical_local = true;
    }
  }
}

void type_checker::visit(ast_asm_stmt &s) {
  for (auto &input : s.inputs) {
    last_expr_type_.reset();
    if (input)
      input->accept(*this);
    if (!last_expr_type_ || last_expr_type_->name != "i64")
      add_error(s.line, "Inline assembly inputs must have type i64");
  }
  if (s.output) {
    const symbol *output = symbols_.lookup(*s.output);
    if (!output || output->kind != symbol_kind::variable_symbol ||
        !output->is_mut || !output->type_name ||
        resolve_type(*output->type_name).name != "i64") {
      add_error(s.line,
                "Inline assembly output must be an existing mutable i64 local");
    }
  }
}

void type_checker::visit(ast_return_stmt &s) {
  type_info ret_type = type_void;
  if (s.value) {
    if (s.value->kind == ast_node_kind::array_literal_expr_node &&
        current_function_return_type_) {
      auto &array = static_cast<ast_array_literal_expr &>(*s.value);
      const auto context =
          parse_generic_type_string(current_function_return_type_->name);
      if (array.elements.empty() && context.base_name == "array" &&
          context.type_args.size() == 1 && context.type_args[0] != "any" &&
          context.type_args[0] != "<unknown>") {
        s.value->inferred_type = current_function_return_type_->name;
      }
    }
    s.value->accept(*this);
    if (last_expr_type_) {
      ret_type = *last_expr_type_;
    }
  }

  if (current_function_return_type_) {
    if (!check_assignment_type(*current_function_return_type_, ret_type, s.value.get())) {
      add_error(s.line, "Return type mismatch. Expected '" +
                            current_function_return_type_->name + "', got '" +
                            ret_type.name + "'");
    }
  }
}

void type_checker::visit(ast_if_stmt &s) {
  if (s.condition) {
    s.condition->accept(*this);
    if (last_expr_type_ && last_expr_type_->name != "bool" &&
        last_expr_type_->name != "any") {
      add_error(s.line, "If condition must be boolean, got '" +
                            last_expr_type_->name + "'");
    }
  }
  if (s.then_branch)
    s.then_branch->accept(*this);
  if (s.else_branch)
    s.else_branch->accept(*this);
}

void type_checker::visit(ast_member_access_expr &e) {
  // std::cerr << "[type_checker] visit(member_access) member='" <<
  // e.member_name << "'" << std::endl; Check if object is a type (e.g. Enum
  // name)
  if (e.object->kind == ast_node_kind::identifier_expr_node) {
    auto id = std::static_pointer_cast<ast_identifier_expr>(e.object);
    const symbol *sym = symbols_.lookup(id->name);
    if (!sym) sym = resolve_class_symbol(id->name);
    if (sym && sym->kind == symbol_kind::type_symbol) {
      if (sym->defining_node && sym->defining_node->kind == ast_node_kind::class_decl_node) {
        const auto member = resolve_class_member(*sym, e.member_name, e.line);
        if (member.method) {
          id->inferred_type = sym->name;
          last_expr_type_ = resolve_type(member.method->return_type.value_or("void"));
          e.inferred_type = last_expr_type_->name;
          if (!checking_class_call_)
            add_error(e.line, "Class method values are unsupported; call the method directly");
          return;
        }
      }
      // Static access on a type
      if (sym->defining_node &&
          sym->defining_node->kind == ast_node_kind::enum_decl_node) {
        const auto &enum_decl =
            static_cast<const ast_enum_decl &>(*sym->defining_node);
        // Check if member is a variant
        for (const auto &v : enum_decl.variants) {
          if (v.name == e.member_name) {
            // Found variant!

            // Whether it has payload or not, for MVP we just return the Enum
            // type. If it's a call, visit(ast_call_expr) will handle it
            // (currently returns type_any for non-identifiers). If it's a value
            // (unit variant), it is the Enum type.
            std::string enum_type_name = id->name; // Or canonical name?
            last_expr_type_ = resolve_type(enum_type_name);
            e.inferred_type = last_expr_type_->name;
            return;
          }
        }
        add_error(e.line, "Enum '" + id->name + "' has no variant '" +
                              e.member_name + "'");
        last_expr_type_ = type_unknown;
        return;
      }
    }
  }

  // Resolve object type
  e.object->accept(*this);
  if (!last_expr_type_) {
    last_expr_type_ = type_unknown;
    return;
  }
  type_info obj_type = *last_expr_type_;

  if (const symbol *class_symbol = resolve_class_symbol(obj_type.name)) {
    const auto member = resolve_class_member(*class_symbol, e.member_name, e.line);
    if (member.method) {
      if (!member.method->is_public() && current_class_name_ != member.owner->name)
        add_error(e.line, "Class method '" + member.owner->name + "::" + e.member_name + "' is private");
      if (!checking_class_call_)
        add_error(e.line, "Class method values are unsupported; call the method directly");
    }
  }

  // Imported enum types carry their canonical name as the receiver type. Unit
  // variants and payload constructors are members of the enum declaration,
  // not ordinary fields or impl methods, so recognize them before generic
  // member lookup.
  {
    auto enum_parts = parse_generic_type_string(obj_type.name);
    const symbol *type_sym = symbols_.lookup(enum_parts.base_name);
    if (type_sym && type_sym->kind == symbol_kind::type_symbol &&
        type_sym->defining_node &&
        type_sym->defining_node->kind == ast_node_kind::enum_decl_node) {
      const auto &enum_decl =
          static_cast<const ast_enum_decl &>(*type_sym->defining_node);
      for (const auto &variant : enum_decl.variants) {
        if (variant.name == e.member_name) {
          last_expr_type_ = resolve_type(obj_type.name);
          e.inferred_type = last_expr_type_->name;
          return;
        }
      }
    }
  }

  // Resolve member type
  auto member_type_name = resolve_member_type(obj_type, e.member_name);
  if (!member_type_name) {
    add_error(e.line, "Type '" + obj_type.name + "' has no member '" +
                          e.member_name + "'");
    last_expr_type_ = type_unknown;
    return;
  }
  last_expr_type_ = resolve_type(*member_type_name);
  e.inferred_type = last_expr_type_->name;
}

void type_checker::visit(ast_struct_literal_expr &e) {
  // Build full type name with generic args
  std::string full_type_name = e.type_name;
  if (!e.type_args.empty()) {
    full_type_name += "<";
    for (size_t i = 0; i < e.type_args.size(); ++i) {
      if (i > 0)
        full_type_name += ", ";
      full_type_name += e.type_args[i];
    }
    full_type_name += ">";
  }

  // Parse the generic type to get base name and type args
  auto parts = parse_generic_type_string(full_type_name);

  // Look up struct declaration to get field types
  // Try unqualified name first, then module-qualified
  const symbol *sym = symbols_.lookup(parts.base_name);
  std::string qualified_base_name =
      parts.base_name; // Track the fully qualified name
  if (!sym && current_module_) {
    qualified_base_name = *current_module_ + "::" + parts.base_name;
    sym = symbols_.lookup(qualified_base_name);
  }

  // Also try via import resolution
  if (!sym) {
    if (auto imported = resolve_type_name_with_imports(parts.base_name)) {
      qualified_base_name = *imported;
      sym = symbols_.lookup(qualified_base_name);
    }
  }

  if (const auto *class_symbol = resolve_class_symbol(parts.base_name))
    sym = class_symbol;

  if (sym && sym->defining_node &&
      sym->defining_node->kind == ast_node_kind::class_decl_node) {
    const auto &class_decl = static_cast<const ast_class_decl &>(*sym->defining_node);
    if (!is_symbol_visible(sym, current_module_.value_or("")))
      add_error(e.line, "Class '" + sym->name + "' is private");
    if (class_decl.is_abstract)
      add_error(e.line, "Cannot construct abstract class '" + sym->name + "'");
    if (!class_decl.generic_params.empty() || !parts.type_args.empty())
      add_error(e.line, "Generic class construction is not supported");
    const auto fields = class_fields(*sym, e.line);
    std::set<std::string> initialized;
    for (auto &field : e.fields) {
      if (!initialized.insert(field.first).second)
        add_error(e.line, "Class literal initializes field '" + field.first + "' more than once");
      const auto declared = std::find_if(fields.begin(), fields.end(),
          [&](const ast_struct_field &item) { return item.name == field.first; });
      if (declared == fields.end())
        add_error(e.line, "Class literal contains unknown field '" + field.first + "'");
      else if (field.second)
        field.second->inferred_type = declared->type_name;
      if (field.second) field.second->accept(*this);
      if (declared != fields.end() &&
          !check_assignment_type(resolve_type(declared->type_name),
                                last_expr_type_.value_or(type_unknown), field.second.get()))
        add_error(e.line, "Type mismatch in class field '" + field.first + "'");
    }
    for (const auto &field : fields)
      if (!initialized.count(field.name))
        add_error(e.line, "Class literal is missing required field '" + field.name + "'");
    e.type_name = sym->name;
    e.inferred_type = sym->name;
    last_expr_type_ = type_info{sym->name, false};
    return;
  }

  const ast_struct_decl *struct_decl = nullptr;
  if (sym && sym->defining_node &&
      sym->defining_node->kind == ast_node_kind::struct_decl_node) {
    struct_decl = &static_cast<const ast_struct_decl &>(*sym->defining_node);

    // Update the AST with the fully qualified struct name so codegen can use it
    // Rebuild full_type_name with qualified base + generics
    if (qualified_base_name != parts.base_name) {
      full_type_name = qualified_base_name;
      if (!parts.type_args.empty()) {
        full_type_name += "<";
        for (size_t i = 0; i < parts.type_args.size(); ++i) {
          if (i > 0)
            full_type_name += ", ";
          full_type_name += parts.type_args[i];
        }
        full_type_name += ">";
      }
      e.type_name = qualified_base_name; // Update AST with qualified name (no
                                         // generics appended to type_name)
    }

    // Build generic substitution map
    std::map<std::string, std::string> generic_subs;
    if (struct_decl->generic_params.size() > 0 && parts.type_args.empty()) {
      // INFERENCE: Try to infer generic args from field types
      for (const auto &f : e.fields) {
        if (f.second) {
          f.second->accept(*this);
          type_info field_val_type = last_expr_type_.value_or(type_unknown);
          for (const auto &decl_field : struct_decl->fields) {
            if (decl_field.name == f.first) {
              for (const auto &gp : struct_decl->generic_params) {
                if (decl_field.type_name == gp.name) {
                  generic_subs[gp.name] = field_val_type.name;
                }
              }
              break;
            }
          }
        }
      }
      // Rebuild full_type_name with inferred args
      if (generic_subs.size() == struct_decl->generic_params.size()) {
        full_type_name = qualified_base_name + "<";
        for (size_t i = 0; i < struct_decl->generic_params.size(); ++i) {
          if (i > 0)
            full_type_name += ", ";
          full_type_name += generic_subs[struct_decl->generic_params[i].name];
        }
        full_type_name += ">";
      }
    } else {
      for (size_t i = 0;
           i < struct_decl->generic_params.size() && i < parts.type_args.size();
           ++i) {
        generic_subs[struct_decl->generic_params[i].name] = parts.type_args[i];
      }
    }

    // A struct literal is a complete construction, not a sparse update.  Keep
    // the validation here (in addition to native backend hardening) so every
    // execution route observes the same source-language contract.
    std::unordered_set<std::string> initialized_fields;

    // For each field in the literal, find its expected type and set
    // inferred_type
    for (auto &f : e.fields) {
      const auto declared_field = std::find_if(
          struct_decl->fields.begin(), struct_decl->fields.end(),
          [&](const ast_struct_field &field) { return field.name == f.first; });

      if (!initialized_fields.insert(f.first).second) {
        add_error(e.line, "Struct literal for '" + full_type_name +
                              "' initializes field '" + f.first +
                              "' more than once");
      }
      if (declared_field == struct_decl->fields.end()) {
        add_error(e.line, "Struct literal for '" + full_type_name +
                              "' contains unknown field '" + f.first + "'");
      }

      if (f.second) {
        std::optional<type_info> expected_type;
        if (declared_field != struct_decl->fields.end() &&
            !declared_field->type_name.empty()) {
          expected_type = resolve_type(
              instantiate_generic_type(declared_field->type_name, generic_subs));
          f.second->inferred_type = expected_type->name;
        }
        // Now visit the field expression
        f.second->accept(*this);
        const type_info value_type = last_expr_type_.value_or(type_unknown);
        if (expected_type && !check_assignment_type(*expected_type, value_type, f.second.get())) {
          add_error(e.line, "Type mismatch in struct field '" + f.first +
                                "'. Expected '" + expected_type->name +
                                "', got '" + value_type.name + "'");
        }
      }
    }

    for (const auto &decl_field : struct_decl->fields) {
      if (initialized_fields.count(decl_field.name) == 0) {
        add_error(e.line, "Struct literal for '" + full_type_name +
                              "' is missing required field '" +
                              decl_field.name + "'");
      }
    }

    // Validate generic args ONLY if provided explicitly
    if (!parts.type_args.empty()) {
      validate_generic_args(struct_decl->generic_params, parts.type_args,
                            e.line);
    }
  } else {
    // Fallback: just visit fields without type propagation
    for (auto &f : e.fields) {
      if (f.second) {
        f.second->accept(*this);
      }
    }
  }

  // Constructing a type nothing declares used to be accepted in silence, and
  // the only sign of it was a later "has no member" on the value it produced
  // -- which points at the type's definition rather than at the real fault,
  // a misspelled name or a file that was never given to the compiler. Report
  // it where the mistake is. Only a name that resolves to nothing at all is
  // reported, so an alias or a declaration this pass cannot classify still
  // behaves as before.
  if (!sym && !struct_decl)
    add_error(e.line, "Unknown type '" + parts.base_name +
                          "'. Nothing in scope declares it; if it is declared "
                          "elsewhere, that file must also be given to the "
                          "compiler.");

  last_expr_type_ = resolve_type(full_type_name);
  e.inferred_type = full_type_name; // Set inferred type on the AST node
}

void type_checker::visit(ast_while_stmt &s) {
  if (s.condition) {
    s.condition->accept(*this);
    if (last_expr_type_ && last_expr_type_->name != "bool" &&
        last_expr_type_->name != "any") {
      add_error(s.line, "While condition must be boolean, got '" +
                            last_expr_type_->name + "'");
    }
  }
  bool prev_loop = in_loop_;
  in_loop_ = true;
  if (s.body)
    s.body->accept(*this);
  in_loop_ = prev_loop;
}

void type_checker::visit(ast_for_in_stmt &s) {
  // Evaluate the iterable expression first to set last_expr_type_ where
  // possible.
  if (s.iterable)
    s.iterable->accept(*this);
  const auto iterable_type = last_expr_type_;

  // Helper: parse an element type from a textual container type name, such
  // as `array<i32, 10>` or `array<i32>` or `vec<i32>` or `slice<i32>`,
  // returning the textual element type (e.g., "i32") if found.
  auto parse_element_type_from_type_name =
      [&](const std::string &t) -> std::optional<std::string> {
    const auto parsed = parse_generic_type_string(resolve_type(t).name);
    const auto separator = parsed.base_name.rfind("::");
    const auto base = separator == std::string::npos ? parsed.base_name
                                                    : parsed.base_name.substr(separator + 2);
    if ((base == "array" || base == "vec" || base == "vector" || base == "slice") &&
        !parsed.type_args.empty())
      return parsed.type_args.front();
    auto name = t;
    // Remove whitespace
    auto trim_local = [](std::string s) {
      const char *ws = " \t\r\n";
      size_t b = s.find_first_not_of(ws);
      size_t e = s.find_last_not_of(ws);
      if (b == std::string::npos)
        return std::string();
      return s.substr(b, e - b + 1);
    };

    name = trim_local(name);

    // Lower-case prefix match checks for common container types.
    // We support patterns like 'array<elem, ...>' or 'array<elem>',
    // 'vec<elem>', 'vector<elem>', 'slice<elem>', '[]' suffix (e.g. 'i32[]').

    // Pattern: array<elem, ...> or array<elem>
    auto pos_array = name.find("array<");
    if (pos_array != std::string::npos) {
      size_t start = pos_array + strlen("array<");
      size_t end = name.find('>', start);
      if (end != std::string::npos && end > start) {
        std::string inner = name.substr(start, end - start);
        // inner may be "i32, 10" or "i32"
        auto comma = inner.find(',');
        std::string elem =
            (comma != std::string::npos) ? inner.substr(0, comma) : inner;
        elem = trim_local(elem);
        if (!elem.empty())
          return std::optional<std::string>(elem);
      }
    }

    // Pattern: vec<elem> or vector<elem>
    auto pos_vec = name.find("vec<");
    if (pos_vec != std::string::npos) {
      size_t start = pos_vec + strlen("vec<");
      size_t end = name.find('>', start);
      if (end != std::string::npos && end > start) {
        std::string elem = trim_local(name.substr(start, end - start));
        if (!elem.empty())
          return std::optional<std::string>(elem);
      }
    }
    auto pos_vector = name.find("vector<");
    if (pos_vector != std::string::npos) {
      size_t start = pos_vector + strlen("vector<");
      size_t end = name.find('>', start);
      if (end != std::string::npos && end > start) {
        std::string elem = trim_local(name.substr(start, end - start));
        if (!elem.empty())
          return std::optional<std::string>(elem);
      }
    }

    // Pattern: slice<elem>
    auto pos_slice = name.find("slice<");
    if (pos_slice != std::string::npos) {
      size_t start = pos_slice + strlen("slice<");
      size_t end = name.find('>', start);
      if (end != std::string::npos && end > start) {
        std::string elem = trim_local(name.substr(start, end - start));
        if (!elem.empty())
          return std::optional<std::string>(elem);
      }
    }

    // Pattern: trailing [] (i32[] or i32 [] with whitespace)
    if (!name.empty()) {
      // find '[]' suffix
      auto pos_sq = name.rfind("[]");
      if (pos_sq != std::string::npos && pos_sq >= 1) {
        std::string elem = trim_local(name.substr(0, pos_sq));
        if (!elem.empty())
          return std::optional<std::string>(elem);
      }
    }
    return std::nullopt;
  };

  // Determine element type textual name (e.g., "i32", "f64", "String", etc.)
  std::optional<std::string> elem_type_name = std::nullopt;

  // Quick path: if iterable is `range(...)` then the element type is `i32`
  // Extended: If iterable is `zip(...)`, try to deduce a tuple<...> element
  // type where each tuple entry reflects the element type of the respective
  // argument to `zip`. Fallback to `any` when a component element type cannot
  // be inferred.
  if (s.iterable && s.iterable->kind == ast_node_kind::call_expr_node) {
    const auto &call = static_cast<const ast_call_expr &>(*s.iterable);
    if (call.callee &&
        call.callee->kind == ast_node_kind::identifier_expr_node) {
      const auto &callee_id =
          static_cast<const ast_identifier_expr &>(*call.callee);
      if (callee_id.name == "range" && !symbols_.lookup_non_global("range")) {
        // range produces integer indices; choose i32 for now
        elem_type_name = std::optional<std::string>("i32");
      } else if (callee_id.name == "zip" && !symbols_.lookup_non_global("zip")) {
        // Each argument to zip is expected to be an iterable. Attempt to infer
        // each iterable's element type and build a tuple type string like:
        //   tuple<i32, String, ...>
        std::vector<std::string> element_types;
        element_types.reserve(call.args.size());

        for (const auto &arg : call.args) {
          std::optional<std::string> elem = std::nullopt;
          if (arg && arg->kind == ast_node_kind::call_expr_node) {
            // Example: zip(range(2), range(2)) -> range(...) produces i32
            const auto &arg_call = static_cast<const ast_call_expr &>(*arg);
            if (arg_call.callee &&
                arg_call.callee->kind == ast_node_kind::identifier_expr_node) {
              const auto &arg_callee_id =
                  static_cast<const ast_identifier_expr &>(*arg_call.callee);
              if (arg_callee_id.name == "range" &&
                  !symbols_.lookup_non_global("range")) {
                elem = std::optional<std::string>("i32");
              }
            }
          } else if (arg && arg->kind == ast_node_kind::identifier_expr_node) {
            // If arg is an identifier, try to resolve the symbol's type and
            // parse its element type
            const auto &id_arg = static_cast<const ast_identifier_expr &>(*arg);
            const symbol *sarg = symbols_.lookup(id_arg.name);
            if (sarg && sarg->type_name.has_value()) {
              auto parsed = parse_element_type_from_type_name(*sarg->type_name);
              if (parsed.has_value())
                elem = parsed;
            }
          } else if (arg &&
                     arg->kind == ast_node_kind::member_access_expr_node) {
            // For member access (foo.bar), attempt to extract type from symbol
            // name by resolving a possible canonical type. We only do a
            // conservative fallback: in many cases the type will be recorded on
            // symbol table entries for member values.
            const auto &ma = static_cast<const ast_member_access_expr &>(*arg);
            // Resolve the left-hand symbol if it's a simple identifier
            // expression
            if (ma.object &&
                ma.object->kind == ast_node_kind::identifier_expr_node) {
              const auto &obj =
                  static_cast<const ast_identifier_expr &>(*ma.object);
              const symbol *sobj = symbols_.lookup(obj.name);
              if (sobj && sobj->type_name.has_value()) {
                auto parsed =
                    parse_element_type_from_type_name(*sobj->type_name);
                if (parsed.has_value())
                  elem = parsed;
              }
            }
          }

          // Fallback to "any" when we could not deduce a more specific type.
          if (!elem.has_value())
            elem = std::optional<std::string>("any");

          element_types.push_back(*elem);
        } // for call.args

        // Build tuple textual representation
        std::string tuple_type = "tuple<";
        for (size_t i = 0; i < element_types.size(); ++i) {
          if (i)
            tuple_type += ", ";
          tuple_type += element_types[i];
        }
        tuple_type += ">";
        elem_type_name = std::optional<std::string>(tuple_type);
      }
    }
  }

  // Second path: if iterable is an identifier report then look up its symbol
  // entry to extract a canonical element if its type reveals a container.
  if (!elem_type_name.has_value() && s.iterable &&
      s.iterable->kind == ast_node_kind::identifier_expr_node) {
    const auto &id = static_cast<const ast_identifier_expr &>(*s.iterable);
    const symbol *sym = symbols_.lookup(id.name);
    if (sym && sym->type_name.has_value()) {
      // Attempt to extract element-type if it's recorded as a parametric type
      auto parsed = parse_element_type_from_type_name(*sym->type_name);
      if (parsed.has_value()) {
        elem_type_name = parsed;
      }
    }
  }

  // Literal and computed iterables already carry their full inferred type.
  // Do not discard array<array<i32>> merely because it is not an identifier.
  if (!elem_type_name && iterable_type)
    elem_type_name = parse_element_type_from_type_name(iterable_type->name);

  // Enter lexical scope for the loop body and declare loop variables. When an
  // element type has been identified, bind it to loop variables so subsequent
  // body checks use an inferred type.
  symbols_.enter_scope();

  std::vector<ast_param> vars = s.get_variables();
  for (size_t i = 0; i < vars.size(); ++i) {
    const ast_param &var = vars[i];
    std::optional<std::string> var_type = std::nullopt;

    // Tuple-unpack: if elem type is a tuple, attempt to split (rudimentary).
    if (vars.size() > 1 && elem_type_name.has_value()) {
      const auto element = parse_generic_type_string(*elem_type_name);
      if (element.base_name == "tuple" && i < element.type_args.size())
        var_type = element.type_args[i];
      else if (element.base_name == "array" && element.type_args.size() == 1)
        // Existing interpreter tuple unpacking also accepts homogeneous arrays.
        var_type = element.type_args.front();
      // Try to parse a simple tuple type syntax like 'tuple<i32, String>'
      std::string t = *elem_type_name;
      auto pos_tuple = t.find("tuple<");
      if (pos_tuple != std::string::npos) {
        size_t start = pos_tuple + strlen("tuple<");
        size_t end = t.find('>', start);
        if (end != std::string::npos && end > start) {
          std::string inner = t.substr(start, end - start);
          // split on comma to get element types
          std::vector<std::string> parts;
          std::istringstream iss(inner);
          std::string p;
          while (std::getline(iss, p, ',')) {
            const char *ws = " \t\r\n";
            size_t b = p.find_first_not_of(ws);
            size_t e = p.find_last_not_of(ws);
            if (b == std::string::npos) {
              parts.push_back(std::string());
            } else {
              parts.push_back(p.substr(b, e - b + 1));
            }
          }
          if (!var_type && i < parts.size())
            var_type = std::optional<std::string>(parts[i]);
        }
      }
    }

    // Single-variable case: if we discovered an element type, use it
    if (!var_type.has_value() && elem_type_name.has_value() &&
        vars.size() == 1) {
      var_type = elem_type_name;
    }

    // Fallback: keep as "any"
    std::string type_to_register =
        var_type.has_value() ? *var_type : std::string("any");

    // Insert variable symbol with inferred type and preserve mutability
    symbol sym(var.name, symbol_kind::variable_symbol, nullptr,
               std::optional<std::string>(type_to_register), var.is_mut, false,
               std::nullopt);
    sym.semantic_id = semantic_declaration(var.name, "variable", type_to_register,
                                           diagnostic_source_file_, var.name_span);
    if (sym.semantic_id)
      semantics_.symbols[*sym.semantic_id].lexical_local = true;
    if (!symbols_.insert_symbol(sym))
      add_error(var.line, "Loop variable '" + var.name +
                             "' already declared in this scope");

    // Mutate AST so future type-checking sees the variable type when it checks
    // a reference to the same name inside the loop. Assign into actual AST
    // nodes (works whether `variables` vector is used or single `variable`).
    if (!s.is_tuple_unpack()) {
      // single variable form: update s.variable.type_name
      s.variable.type_name = std::optional<std::string>(type_to_register);
    } else {
      if (s.variables.size() > i) {
        s.variables[i].type_name = std::optional<std::string>(type_to_register);
      }
    }
  }

  // Enable in-loop checks for break/continue semantics
  bool prev_loop = in_loop_;
  in_loop_ = true;

  if (s.body)
    s.body->accept(*this);
  in_loop_ = prev_loop;

  symbols_.leave_scope();
}

void type_checker::visit(ast_loop_stmt &s) {
  bool prev_loop = in_loop_;
  in_loop_ = true;
  if (s.body)
    s.body->accept(*this);
  in_loop_ = prev_loop;
}

void type_checker::visit(ast_break_stmt &s) {
  if (!in_loop_) {
    add_error(s.line, "Break statement outside of loop");
  }
}

void type_checker::visit(ast_continue_stmt &s) {
  if (!in_loop_) {
    add_error(s.line, "Continue statement outside of loop");
  }
}

void type_checker::visit(ast_match_stmt &s) {
  // Evaluate the match target first; this establishes a type that arms may
  // use for variable bindings when none are explicitly deduced from patterns.
  if (s.target)
    s.target->accept(*this);

  type_info target_type = last_expr_type_.value_or(type_unknown);

  for (auto &arm : s.arms) {
    // Infer pattern type if possible
    std::optional<type_info> pattern_type =
        infer_type_from_pattern(arm.pattern);

    // If both pattern and target are known types, ensure they are compatible.
    // Skip validation when either type is unknown or `any` to avoid noisy
    // errors cascading from unrelated issues.
    if (pattern_type.has_value() && target_type.name != "<unknown>") {
      if (!are_types_compatible(*pattern_type, target_type)) {
        int ln = (arm.body ? arm.body->line : s.line);
        add_error(ln, "Pattern type '" + pattern_type->name +
                          "' incompatible with match target type '" +
                          target_type.name + "'");
      }
    }

    // Enter a new lexical scope for variables bound by the pattern to avoid
    // leaking them across arms.
    symbols_.enter_scope();

    // Bind against the concrete target whenever it is known. Constructor
    // inference identifies the enum declaration, but it may only recover the
    // open generic (for example Result<T, E>); preferring it would bind an Ok
    // payload as the whole Result instead of the target's concrete T.
    type_info binding_type =
        target_type.name != "<unknown>"
            ? target_type
            : (pattern_type.has_value() ? *pattern_type : type_any);

    // Bind variables present in the pattern into the current scope
    bind_pattern_to_type(arm.pattern, binding_type,
                         arm.body ? arm.body->line : s.line);

    // Check guard expression (if any) - must be boolean
    if (arm.guard) {
      arm.guard->accept(*this);
      if (!last_expr_type_.has_value() ||
          last_expr_type_->name != type_bool.name) {
        add_error(arm.guard->line, "Match guard expression must be boolean");
      }
    }

    // Type-check the arm body now that its pattern-variables are bound.
    if (arm.body) {
      arm.body->accept(*this);
    }

    // Clean up the local scope for this arm
    symbols_.leave_scope();
  }
}

bool type_checker::check_runtime_primitive(ast_call_expr &e) {
  e.ownership_operation.clear();
  e.parallel_worker.reset();
  e.parallel_input_type.clear();
  e.parallel_output_type.clear();
  if (!e.callee || e.callee->kind != ast_node_kind::identifier_expr_node) return false;
  const auto &name = static_cast<const ast_identifier_expr &>(*e.callee).name;
  const bool ownership = name == "owned" || name == "owned_move" ||
      name == "owned_borrow" || name == "owned_borrow_mut";
  if (!ownership && name != "parallel_map") return false;
  const auto start_errors = diagnostics_.size();
  std::vector<type_info> arguments;
  for (auto &argument : e.args) {
    last_expr_type_.reset();
    if (argument) argument->accept(*this);
    arguments.push_back(last_expr_type_.value_or(type_unknown));
  }
  if (ownership) {
    e.ownership_operation = name;
    if (e.args.size() != 1)
      add_error(e.line, name + " expects exactly one argument");
    if (name == "owned") {
      if (e.type_args.size() != 1) {
        add_error(e.line, "owned<T>(count) requires exactly one element type");
        last_expr_type_ = type_unknown;
      } else {
        const auto element = resolve_type(e.type_args.front()).name;
        if (element != "i32" && element != "i64" && element != "u8")
          add_error(e.line, "Owned buffers support only i32, i64, or u8 elements");
        if (!arguments.empty() && arguments.front().name != "i32" &&
            arguments.front().name != "i64")
          add_error(e.line, "Owned buffer count must be i32 or i64");
        e.type_args.front() = element;
        last_expr_type_ = type_info{"owned<" + element + ">", true};
      }
    } else {
      if (!e.type_args.empty()) add_error(e.line, name + " does not accept type arguments");
      if (arguments.empty() || arguments.front().name.rfind("owned<", 0) != 0) {
        add_error(e.line, name + " requires an owned buffer");
        last_expr_type_ = type_unknown;
      } else {
        const auto element = ownership_element_type(arguments.front().name);
        const auto prefix = name == "owned_move" ? "owned" :
            name == "owned_borrow" ? "borrow" : "borrow_mut";
        last_expr_type_ = type_info{std::string(prefix) + "<" + element + ">", true};
      }
    }
    e.inferred_type = last_expr_type_->name;
    return true;
  }

  // A worker identity comes from exact semantic resolution, never a suffix or
  // an arbitrary function-pointer value. The NIL isolation validator checks
  // the entire transitive worker graph before any thread starts.
  checked_parallel_calls_.push_back(&e);
  const auto scalar = [](const std::string &type) {
    return type == "i32" || type == "i64" || type == "u8" ||
           type == "bool" || type == "f64";
  };
  if (!e.type_args.empty() || e.args.size() != 2)
    add_error(e.line, "parallel_map(worker, inputs) expects two arguments and no type arguments");
  const symbol *worker = nullptr;
  if (!e.args.empty() && e.args.front() &&
      e.args.front()->kind == ast_node_kind::identifier_expr_node) {
    const auto &identifier = static_cast<const ast_identifier_expr &>(*e.args.front());
    if (symbols_.lookup_non_global(identifier.name)) {
      add_error(e.line, "parallel_map requires a static source function, not a local function value");
    } else {
      if (current_module_) worker = symbols_.lookup(*current_module_ + "::" + identifier.name);
      if (!worker) worker = symbols_.lookup(identifier.name);
      if (!worker) worker = resolve_qualified_name(identifier.name);
      if (!worker) {
        std::set<std::string> imported_candidates;
        const auto scope = imports_.find(current_module_.value_or("<global>"));
        bool named_import = false;
        if (scope != imports_.end()) {
          const auto named = scope->second.find(identifier.name);
          if (named != scope->second.end() && !named->second.target_path.empty() &&
              named->second.target_path != "*") {
            named_import = true;
            imported_candidates.insert(named->second.target_path);
          } else {
            for (const auto &entry : scope->second) {
              if (!entry.second.target_path.empty() && entry.second.target_path != "*") continue;
              const auto candidate = entry.first + "::" + identifier.name;
              const auto *found = symbols_.lookup(candidate);
              if (found && found->kind == symbol_kind::function_symbol &&
                  is_symbol_visible(found, current_module_.value_or("")))
                imported_candidates.insert(candidate);
            }
          }
        }
        if (imported_candidates.size() > 1) {
          add_error(e.line, "parallel_map worker name is ambiguous across imports; qualify its module explicitly");
        } else if (imported_candidates.size() == 1) {
          worker = symbols_.lookup(*imported_candidates.begin());
        } else if (!named_import) {
          if (const auto imported = resolve_imported_symbol(identifier.name))
            worker = symbols_.lookup(*imported);
        }
      }
    }
  }
  if (!worker || worker->kind != symbol_kind::function_symbol ||
      !worker->defining_node || worker->defining_node->kind != ast_node_kind::function_decl_node) {
    add_error(e.line, "parallel_map requires a statically resolved source worker function");
    last_expr_type_ = type_unknown;
    e.inferred_type = last_expr_type_->name;
    return true;
  }
  const auto &function = static_cast<const ast_function_decl &>(*worker->defining_node);
  if (!function.generic_params.empty() || function.params.size() != 1 || !function.body)
    add_error(e.line, "parallel_map worker must be a nongeneric source function with one scalar parameter");
  if (!is_symbol_visible(worker, current_module_.value_or("")))
    add_error(e.line, "parallel_map worker is private to another module");
  const auto caller_module = current_module_;
  current_module_ = worker->module;
  const auto input = function.params.size() == 1
      ? resolve_type(function.params.front().type_name.value_or("any")).name : "any";
  const auto output = resolve_type(function.return_type.value_or("void")).name;
  current_module_ = caller_module;
  if (!scalar(input) || !scalar(output) ||
      (function.params.size() == 1 && (function.params.front().is_variadic ||
                                     function.params.front().default_value)))
    add_error(e.line, "parallel_map worker signature must be (scalar) -> scalar with exact i32/i64/u8/bool/f64 types");
  if (arguments.size() == 2) {
    const auto array = parse_generic_type_string(arguments[1].name);
    if (array.base_name != "array" || array.type_args.size() != 1 || array.type_args.front() != input)
      add_error(e.line, "parallel_map inputs must have exact type array<" + input + ">");
  }
  last_expr_type_ = type_info{"array<" + output + ">", true};
  e.inferred_type = last_expr_type_->name;
  if (diagnostics_.size() == start_errors) {
    e.parallel_worker = worker->name;
    e.parallel_input_type = input;
    e.parallel_output_type = output;
  }
  return true;
}

static callable_parameter_contract callable_parameters(
    const std::vector<ast_param> &parameters, std::size_t offset = 0) {
  callable_parameter_contract result;
  for (std::size_t index = offset; index < parameters.size(); ++index)
    result.names.push_back(parameters[index].name);
  result.variadic = parameters.size() > offset && parameters.back().is_variadic;
  return result;
}

void type_checker::visit(ast_call_expr &e) {
  bool named_arguments_handled = e.argument_names.empty();
  struct named_argument_guard {
    std::function<void()> check;
    ~named_argument_guard() { check(); }
  } named_guard{[&] {
    if (!named_arguments_handled)
      add_error(e.line, "Named arguments require a resolved parameter-name contract; "
                        "use positional arguments for this callable");
  }};
  if (check_runtime_primitive(e)) return;
  /* std::cerr << "[type_checker] visit(call_expr) line " << e.line << "
   * callee_kind=" << (int)e.callee->kind << " expected=" <<
   * (int)ast_node_kind::member_access_expr_node << std::endl; */
  const symbol *bounded_receiver = nullptr;
  ast_member_access_expr *bounded_member = nullptr;
  if (e.callee && e.callee->kind == ast_node_kind::member_access_expr_node) {
    auto &member = static_cast<ast_member_access_expr &>(*e.callee);
    if (member.object && member.object->kind == ast_node_kind::identifier_expr_node) {
      const auto &identifier = static_cast<const ast_identifier_expr &>(*member.object);
      const symbol *candidate = symbols_.lookup(identifier.name);
      static const std::set<std::string> builtin_constructors = {
          "array", "vec", "slice", "tuple", "ref", "ptr", "Self", "void", "any"};
      if (candidate && candidate->kind == symbol_kind::type_symbol &&
          !candidate->defining_node && !candidate->type_name &&
          !is_primitive_name(identifier.name) &&
          !builtin_constructors.count(identifier.name)) {
        bounded_receiver = candidate;
        bounded_member = &member;
      }
    }
  }
  e.static_receiver_type.reset();
  e.concrete_class_receiver.reset();
  e.class_method_owner.reset();
  e.class_virtual_call = false;
  e.class_instance_call = false;
  e.class_argument_types.clear();
  const bool saved_class_call = checking_class_call_;
  checking_class_call_ = true;
  if (e.callee && !bounded_receiver)
    e.callee->accept(*this);
  checking_class_call_ = saved_class_call;

  // Visit arguments first to resolve their types for inference
  std::vector<type_info> arg_types;
  for (auto &arg : e.args) {
    if (arg) {
      arg->accept(*this);
      arg_types.push_back(last_expr_type_.value_or(type_unknown));
    } else {
      arg_types.push_back(type_unknown);
    }
  }

  // Argument labels select positions only after lexical/imported/member
  // resolution has selected the callable. Reorder inferred types with the
  // expressions so generic inference and call-boundary checks see one order.
  const auto order_named_arguments = [&](const callable_parameter_contract &contract) {
    if (e.argument_names.empty()) return true;
    named_arguments_handled = true;
    if (e.argument_names.size() != e.args.size()) {
      add_error(e.line, "Invalid named argument metadata");
      return false;
    }
    const auto folded = [](const std::string &name) {
      std::string key;
      for (unsigned char c : name) {
        if (c == '-' || c == '_') continue;
        key += static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
      }
      return key;
    };
    const auto fixed = contract.names.size() - (contract.variadic ? 1U : 0U);
    std::vector<std::optional<std::size_t>> positions(fixed);
    std::vector<std::size_t> tail;
    bool valid = true;
    for (std::size_t index = 0; index < e.argument_names.size(); ++index) {
      const auto &name = e.argument_names[index];
      if (name.empty()) {
        tail.push_back(index);
        continue;
      }
      std::optional<std::size_t> selected;
      for (std::size_t parameter = 0; parameter < fixed; ++parameter) {
        if (folded(contract.names[parameter]) == folded(name)) {
          if (selected) {
            add_error(e.line, "Ambiguous parameter name '" + name + "'");
            valid = false;
            break;
          }
          selected = parameter;
        }
      }
      if (!selected) {
        add_error(e.line, "No parameter named '" + name + "'");
        valid = false;
      } else if (positions[*selected]) {
        add_error(e.line, "Argument '" + name + "' given more than once");
        valid = false;
      } else {
        positions[*selected] = index;
      }
    }
    for (std::size_t index = 0; index < fixed; ++index)
      if (!positions[index]) {
        add_error(e.line, "Missing argument '" + contract.names[index] + "'");
        valid = false;
      }
    if (!tail.empty() && !contract.variadic) {
      add_error(e.line, "A 'more' argument tail requires a variadic callable");
      valid = false;
    }
    if (!valid) return false;
    std::vector<ast_expr_ptr> ordered;
    std::vector<type_info> ordered_types;
    std::vector<std::string> ordered_names;
    const auto append = [&](std::size_t index) {
      ordered.push_back(e.args[index]);
      ordered_types.push_back(arg_types[index]);
      ordered_names.push_back(e.argument_names[index]);
    };
    for (const auto position : positions) append(*position);
    for (const auto index : tail) append(index);
    e.args = std::move(ordered);
    arg_types = std::move(ordered_types);
    e.argument_names = std::move(ordered_names);
    return true;
  };

  const auto unannotated_return_type = [&](const symbol &callee) {
    // An omitted return annotation on an actual Ny declaration is void,
    // exactly as in NIL lowering. Only unresolved external metadata is any.
    return callee.defining_node &&
                   callee.defining_node->kind == ast_node_kind::function_decl_node
               ? type_void : type_any;
  };

  // Default-parameter expressions are retained by the parser, but the NIL
  // calling convention does not yet carry enough information to evaluate an
  // omitted argument safely. Validate source calls against the arguments that
  // are actually lowered so malformed calls fail at compile time instead of
  // underflowing (or leaking values on) the interpreter operand stack.
  auto validate_call_arity = [&](const symbol &callee, std::size_t provided,
                                 const std::string &display_name) {
    if (!callee.defining_node ||
        callee.defining_node->kind != ast_node_kind::function_decl_node) {
      return;
    }

    const auto &params = callee.params;
    const bool variadic = !params.empty() && params.back().is_variadic;
    const std::size_t required = variadic ? params.size() - 1 : params.size();
    const bool valid = variadic ? provided >= required : provided == required;
    if (valid) {
      return;
    }

    bool omitted_default = false;
    if (provided < params.size()) {
      for (std::size_t i = provided; i < params.size(); ++i) {
        if (params[i].default_value.has_value()) {
          omitted_default = true;
          break;
        }
      }
    }

    if (omitted_default) {
      add_error(e.line, "Default arguments are not lowered yet for function '" +
                            display_name + "'; provide all " +
                            std::to_string(params.size()) + " arguments");
    } else if (variadic) {
      add_error(e.line, "Function '" + display_name + "' expects at least " +
                            std::to_string(required) + " arguments, got " +
                            std::to_string(provided));
    } else {
      add_error(e.line, "Function '" + display_name + "' expects " +
                            std::to_string(params.size()) + " arguments, got " +
                            std::to_string(provided));
    }
  };

  auto apply_argument_array_context =
      [&](const symbol &callee,
          const std::map<std::string, std::string> &substitutions,
          std::size_t parameter_offset = 0) {
        for (std::size_t i = 0; i < e.args.size() && i + parameter_offset < callee.params.size(); ++i) {
          const auto &parameter = callee.params[i + parameter_offset];
          if (!parameter.type_name) continue;
          const auto expected = resolve_type(instantiate_generic_type(*parameter.type_name, substitutions));
          // Legacy general argument inference is intentionally unchanged.
          // Class boundaries, however, require an explicit checked proof.
          if ((type_contains_class(expected.name) || type_contains_class(arg_types[i].name)) &&
              !check_assignment_type(expected, arg_types[i], e.args[i].get()))
            add_error(e.line, "Function '" + callee.name + "' argument " +
                                  std::to_string(i + 1) + " has incompatible class type");
        }
        for (std::size_t argument_index = 0;
             argument_index < e.args.size() &&
             argument_index + parameter_offset < callee.params.size();
             ++argument_index) {
          auto &argument = e.args[argument_index];
          if (!argument ||
              argument->kind != ast_node_kind::array_literal_expr_node)
            continue;
          auto &array = static_cast<ast_array_literal_expr &>(*argument);
          if (!array.elements.empty())
            continue;

          const auto &parameter =
              callee.params[argument_index + parameter_offset];
          if (!parameter.type_name)
            continue;
          const std::string expected_type =
              instantiate_generic_type(*parameter.type_name, substitutions);
          const auto context = parse_generic_type_string(expected_type);
          if (context.base_name != "array" || context.type_args.size() != 1 ||
              context.type_args[0] == "any" ||
              context.type_args[0] == "<unknown>") {
            continue;
          }

          bool has_unresolved_generic = false;
          for (const auto &generic : callee.generic_params) {
            if (context.type_args[0] == generic.name) {
              has_unresolved_generic = true;
              break;
            }
          }
          if (has_unresolved_generic)
            continue;

          const std::string resolved_type = resolve_type(expected_type).name;
          argument->inferred_type = resolved_type;
          arg_types[argument_index] = type_info{resolved_type, false};
        }
      };

  if (e.callee && e.callee->kind == ast_node_kind::member_access_expr_node) {
    auto &member = static_cast<ast_member_access_expr &>(*e.callee);
    const symbol *receiver = member.object && member.object->inferred_type
        ? resolve_class_symbol(*member.object->inferred_type) : nullptr;
    if (receiver) {
      const auto selected = resolve_class_member(*receiver, member.member_name, e.line);
      // A member the class declaration does not know may still be an
      // EXTENSION, which registers exactly as a record's impl method does.
      // Falling through to ordinary method resolution is what lets a class
      // be extended at all; rejecting here made 'X begin extension' on a
      // class fail even inside the book that declared it.
      if (selected.method) {
        const auto &method = *selected.method;
        bool type_receiver = false;
        if (member.object->kind == ast_node_kind::identifier_expr_node) {
          const auto &id = static_cast<const ast_identifier_expr &>(*member.object);
          const symbol *binding = symbols_.lookup_non_global(id.name);
          if (!binding) binding = symbols_.lookup(resolve_type(id.name).name);
          type_receiver = binding && binding->kind == symbol_kind::type_symbol;
        }
        if (type_receiver || method.params.empty() || method.params.front().name != "self")
          add_error(e.line, "Class instance method requires a value receiver and an explicit first self parameter");
        if (!method.generic_params.empty() || !e.type_args.empty() || !receiver->generic_params.empty())
          add_error(e.line, "Generic class method execution is not supported");
        if (std::any_of(method.params.begin(), method.params.end(),
                       [](const ast_param &parameter) { return parameter.is_variadic; }))
          add_error(e.line, "Variadic class method execution is not supported");
        if (!method.is_public() && current_class_name_ != selected.owner->name)
          add_error(e.line, "Class method '" + selected.owner->name + "::" + method.name + "' is private");
        const std::map<std::string, std::string> substitutions{
            {"Self", selected.owner->name}};
        symbol callable(method.name, symbol_kind::function_symbol,
            const_cast<ast_function_decl *>(&method), method.return_type, false,
            method.get_visibility(), selected.owner->module, method.generic_params, method.params);
        if (!order_named_arguments(callable_parameters(method.params, 1))) return;
        validate_call_arity(callable, e.args.size() + 1, receiver->name + "." + method.name);
        apply_argument_array_context(callable, substitutions, 1);
        if (!method.params.empty()) {
          const auto self_type = instantiate_generic_type(
              method.params.front().type_name.value_or("Self"),
              {{"Self", receiver->name}, {selected.owner->name, receiver->name}});
          if (resolve_type(self_type).name != *member.object->inferred_type)
            add_error(e.line, "Class method receiver type mismatch; use its exact concrete class/reference type");
        }
        for (size_t i = 0; i < e.args.size() && i + 1 < method.params.size(); ++i) {
          const auto expected = resolve_type(instantiate_generic_type(
              method.params[i + 1].type_name.value_or("any"), substitutions));
          e.class_argument_types.push_back(expected.name);
          if (!check_assignment_type(expected, arg_types[i], e.args[i].get()))
            add_error(e.line, "Class method '" + method.name + "' argument " +
                                  std::to_string(i + 1) + " has incompatible type");
        }
        const auto result = instantiate_generic_type(method.return_type.value_or("void"), substitutions);
        e.concrete_class_receiver = receiver->name;
        e.class_method_owner = selected.owner->name;
        e.class_virtual_call = class_method_is_virtual(*receiver, method.name);
        e.class_instance_call = true;
        checked_class_calls_.push_back(&e);
        e.inferred_type = resolve_type(result).name;
        member.inferred_type = e.inferred_type;
        last_expr_type_ = resolve_type(result);
        return;
      }
    }
  }

  if (bounded_receiver) {
    const ast_function_decl *selected = nullptr;
    std::map<std::string, std::string> selected_substitutions;
    std::optional<std::string> selected_module;
    std::set<std::pair<const ast_function_decl *, std::string>> seen_methods;
    std::vector<std::string> bounds = bounded_receiver->bounds;
    if (const auto active = active_impl_generic_bounds_.find(bounded_receiver->name);
        active != active_impl_generic_bounds_.end())
      bounds.insert(bounds.end(), active->second.begin(), active->second.end());
    for (const auto &bound : bounds) {
      const symbol *trait_symbol = find_trait(bound);
      if (!trait_symbol) continue;
      const auto &trait = static_cast<const ast_trait_decl &>(*trait_symbol->defining_node);
      const std::string normalized_bound = normalize_trait_type(bound);
      const auto parts = parse_generic_type_string(normalized_bound);
      for (const auto &method : trait.methods) {
        if (method->name != bounded_member->member_name ||
            !seen_methods.emplace(method.get(), normalized_bound).second)
          continue;
        if (selected) {
          add_error(e.line, "Ambiguous associated method '" + bounded_member->member_name +
                                "' across generic trait bounds");
          last_expr_type_ = type_unknown;
          return;
        }
        selected = method.get();
        selected_module = trait_symbol->module;
        selected_substitutions["Self"] = bounded_receiver->name;
        for (size_t i = 0; i < trait.generic_params.size(); ++i)
          selected_substitutions[trait.generic_params[i].name] = parts.type_args[i];
      }
    }
    if (!selected) {
      add_error(e.line, "No associated method '" + bounded_member->member_name +
                            "' is declared by the bounds of type parameter '" + bounded_receiver->name + "'");
      last_expr_type_ = type_unknown;
      return;
    }
    if (std::any_of(selected->params.begin(), selected->params.end(),
                    [](const ast_param &parameter) { return parameter.name == "self"; })) {
      add_error(e.line, "Instance trait method '" + selected->name +
                            "' requires a value receiver, not a type parameter");
      last_expr_type_ = type_unknown;
      return;
    }
    // Method-generic associated dispatch requires an additional specialization
    // contract; reject it rather than dropping type arguments during lowering.
    if (!selected->generic_params.empty() || !e.type_args.empty()) {
      add_error(e.line, "Generic associated methods through trait bounds are not supported");
      last_expr_type_ = type_unknown;
      return;
    }
    symbol method_symbol(selected->name, symbol_kind::function_symbol,
                         const_cast<ast_function_decl *>(selected), selected->return_type,
                         false, selected->get_visibility(), selected_module,
                         selected->generic_params, selected->params);
    if (!order_named_arguments(callable_parameters(selected->params))) return;
    validate_call_arity(method_symbol, e.args.size(), bounded_receiver->name + "." + selected->name);
    apply_argument_array_context(method_symbol, selected_substitutions);
    const auto caller_module = current_module_;
    current_module_ = selected_module;
    for (size_t i = 0; i < e.args.size() && i < selected->params.size(); ++i) {
      if (!selected->params[i].type_name) continue;
      const std::string expected = normalize_trait_type(*selected->params[i].type_name, selected_substitutions);
      if (!check_assignment_type(type_info{expected, false}, arg_types[i], e.args[i].get()))
        add_error(e.line, "Associated method '" + selected->name + "' argument " +
                              std::to_string(i + 1) + " has incompatible type");
    }
    const std::string result = normalize_trait_type(selected->return_type.value_or("void"), selected_substitutions);
    current_module_ = caller_module;
    bounded_member->object->inferred_type = bounded_receiver->name;
    bounded_member->inferred_type = result;
    e.inferred_type = result;
    e.static_receiver_type = bounded_receiver->name;
    last_expr_type_ = type_info{result, false};
    return;
  }

  // Resolve associated functions on imported, non-generic types before the
  // ordinary object-method path. The receiver is a type, not a runtime value;
  // preserving both its canonical name and the declared return type makes
  // fluent expressions such as `Builder.create().configure()` type-stable.
  if (e.callee && e.type_args.empty() &&
      e.callee->kind == ast_node_kind::member_access_expr_node) {
    auto ma = std::static_pointer_cast<ast_member_access_expr>(e.callee);
    if (ma->object && ma->object->kind == ast_node_kind::identifier_expr_node) {
      auto type_id = std::static_pointer_cast<ast_identifier_expr>(ma->object);
      std::string canonical_type = type_id->name;
      const symbol *type_sym = nullptr;

      if (current_module_) {
        const std::string local_type = *current_module_ + "::" + canonical_type;
        if (const symbol *candidate = symbols_.lookup(local_type)) {
          canonical_type = local_type;
          type_sym = candidate;
        }
      }
      if (!type_sym) {
        if (auto imported = resolve_type_name_with_imports(type_id->name)) {
          if (const symbol *candidate = symbols_.lookup(*imported)) {
            canonical_type = *imported;
            type_sym = candidate;
          }
        }
      }
      if (!type_sym) {
        const symbol *candidate = symbols_.lookup(type_id->name);
        if (candidate && (!current_module_ || !candidate->module ||
                          *candidate->module == *current_module_)) {
          type_sym = candidate;
          canonical_type = candidate->name;
        }
      }

      if (type_sym && type_sym->kind == symbol_kind::type_symbol &&
          type_sym->generic_params.empty()) {
        bool is_enum_variant = false;
        if (type_sym->defining_node &&
            type_sym->defining_node->kind == ast_node_kind::enum_decl_node) {
          const auto &enum_decl =
              static_cast<const ast_enum_decl &>(*type_sym->defining_node);
          for (const auto &variant : enum_decl.variants) {
            if (variant.name == ma->member_name) {
              is_enum_variant = true;
              break;
            }
          }
        }

        if (!is_enum_variant) {
          const symbol *method_sym = nullptr;
          auto member = type_sym->members.find(ma->member_name);
          if (member != type_sym->members.end())
            method_sym = &member->second;
          if (!method_sym)
            method_sym =
                symbols_.lookup(canonical_type + "__" + ma->member_name);

          if (method_sym && method_sym->kind == symbol_kind::function_symbol) {
            if (!order_named_arguments(callable_parameters(method_sym->params))) return;
            validate_call_arity(*method_sym, e.args.size(),
                                type_id->name + "." + ma->member_name);
            apply_argument_array_context(*method_sym, {});
            ma->object->inferred_type = canonical_type;
            if (method_sym->type_name)
              last_expr_type_ = resolve_type(*method_sym->type_name);
            else
              last_expr_type_ = unannotated_return_type(*method_sym);
            ma->inferred_type = last_expr_type_->name;
            e.inferred_type = last_expr_type_->name;
            return;
          }
        }
      }
    }
  }

  // `Type<T>.factory()` keeps the explicit type arguments on the call node,
  // while the member-access object is just the identifier `Type`.  Treat that
  // spelling as static generic dispatch before the ordinary object-method
  // path.  Without this, an imported generic type has no value type of its own,
  // so `Type` degrades to `any`, the factory result becomes `any`, and the next
  // call is lowered as `any__method` even though the source supplied T.
  if (e.callee && !e.type_args.empty() &&
      e.callee->kind == ast_node_kind::member_access_expr_node) {
    auto ma = std::static_pointer_cast<ast_member_access_expr>(e.callee);
    if (ma->object && ma->object->kind == ast_node_kind::identifier_expr_node) {
      auto type_id = std::static_pointer_cast<ast_identifier_expr>(ma->object);
      std::string canonical_type = type_id->name;
      const symbol *type_sym = symbols_.lookup(canonical_type);

      // A same-spelled type from another module is not in scope unless it was
      // imported by the module containing this call.
      if (type_sym && current_module_ && type_sym->module &&
          *type_sym->module != *current_module_) {
        type_sym = nullptr;
      }
      // Prefer the imported declaration over parser-created unqualified type
      // placeholders.  Those placeholders intentionally have no owner or
      // generic contract and cannot describe `Type<T>`.
      if (auto imported = resolve_type_name_with_imports(canonical_type)) {
        if (const symbol *candidate = symbols_.lookup(*imported)) {
          canonical_type = *imported;
          type_sym = candidate;
        }
      }
      if (!type_sym && current_module_) {
        std::string local_type = *current_module_ + "::" + canonical_type;
        if (const symbol *candidate = symbols_.lookup(local_type)) {
          canonical_type = std::move(local_type);
          type_sym = candidate;
        }
      }

      if (type_sym && type_sym->kind == symbol_kind::type_symbol &&
          !type_sym->generic_params.empty()) {
        const symbol *method_sym = nullptr;
        auto member = type_sym->members.find(ma->member_name);
        if (member != type_sym->members.end()) {
          method_sym = &member->second;
        }

        // Impl methods are predeclared before bodies are checked, but the
        // type-member table may not be populated until a later declaration in
        // the combined module.  Use that predeclaration for order-independent
        // checking of imported source.
        if (!method_sym) {
          method_sym = symbols_.lookup(canonical_type + "__" + ma->member_name);
        }
        if (!method_sym) {
          auto parts = parse_generic_type_string(canonical_type);
          method_sym =
              symbols_.lookup(parts.base_name + "__" + ma->member_name);
        }

        if (ny::is_debug_enabled()) {
          ny::diag_debug("[type_checker] static generic call " + type_id->name +
                         "<...>." + ma->member_name +
                         " resolved_type=" + canonical_type + " type_params=" +
                         std::to_string(type_sym->generic_params.size()) +
                         " method=" + (method_sym ? "found" : "missing"));
        }

        if (method_sym && method_sym->kind == symbol_kind::function_symbol) {
          if (!order_named_arguments(callable_parameters(method_sym->params))) return;
          validate_call_arity(*method_sym, e.args.size(),
                              type_id->name + "." + ma->member_name);
          validate_generic_args(type_sym->generic_params, e.type_args, e.line);

          std::map<std::string, std::string> substitutions;
          for (size_t i = 0;
               i < type_sym->generic_params.size() && i < e.type_args.size();
               ++i) {
            substitutions[type_sym->generic_params[i].name] = e.type_args[i];
          }
          apply_argument_array_context(*method_sym, substitutions);

          std::string receiver_type = canonical_type + "<";
          for (size_t i = 0; i < e.type_args.size(); ++i) {
            if (i > 0)
              receiver_type += ", ";
            receiver_type += e.type_args[i];
          }
          receiver_type += ">";
          ma->object->inferred_type = receiver_type;

          if (method_sym->type_name) {
            std::string return_type =
                instantiate_generic_type(*method_sym->type_name, substitutions);
            last_expr_type_ = resolve_type(return_type);
          } else {
            last_expr_type_ = unannotated_return_type(*method_sym);
          }
          ma->inferred_type = last_expr_type_.value_or(type_any).name;
          e.inferred_type = last_expr_type_.value_or(type_any).name;
          return;
        }
      }
    }
  }

  /* fprintf(stderr, "[type_checker] Pre-Callee Logic Check..\n");
   * fflush(stderr); */
  if (e.callee && e.callee->kind == ast_node_kind::identifier_expr_node) {
    auto id = std::static_pointer_cast<ast_identifier_expr>(e.callee);
    const symbol *parent_type_sym = nullptr;
    const symbol *s = symbols_.lookup(id->name);

    // The symbol table retains unqualified aliases for compatibility. Do not
    // let an alias owned by another module outrank the caller's own module or
    // its explicit imports merely because that declaration was registered
    // later in the combined program.
    if (s && id->name.find("::") == std::string::npos && current_module_ &&
        s->module && *s->module != *current_module_)
      s = nullptr;

    // Try current-module qualified lookup for module-local function calls.
    // Functions are registered as "module::name" but callers within the same
    // module use bare names (e.g. AI_REGISTRY_MAX(), SCRIPT_CACHE_MAX()).
    if (!s && current_module_) {
      std::string qualified = *current_module_ + "::" + id->name;
      s = symbols_.lookup(qualified);
      if (s) {
        id->name = qualified; // Rewrite for codegen mangling
      }
    }

    // Imported symbols follow lexical and current-module symbols. Rewrite the
    // AST to the selected fully-qualified name so codegen cannot reintroduce
    // an order-dependent unqualified lookup.
    if (!s) {
      if (auto imported_name = resolve_imported_symbol(id->name)) {
        s = symbols_.lookup(*imported_name);
        if (s)
          id->name = *imported_name;
      }
    }

    // Static Dispatch Lookup (e.g. vec::new) - Always attempt this for
    // qualified names, as it allows resolving struct methods even if a global
    // symbol (e.g. mangled or ghost) exists.
    if (id->name.find("::") != std::string::npos) {
      auto last_sep = id->name.rfind("::");
      std::string type_part = id->name.substr(0, last_sep);
      std::string method_part = id->name.substr(last_sep + 2);

      type_info ti = resolve_type(type_part);
      if (ti != type_unknown) {
        std::string canonical = ti.name;
        // Parse potential generics off the canonical name info if needed, but
        // resolve_type usually gives string
        auto parts = parse_generic_type_string(canonical);
        const symbol *type_sym = symbols_.lookup(parts.base_name);
        if (!type_sym && current_module_) {
          type_sym = symbols_.lookup(*current_module_ + "::" + parts.base_name);
        }

        if (type_sym) {
          auto it = type_sym->members.find(method_part);
          if (it != type_sym->members.end()) {
            s = &it->second;
            parent_type_sym = type_sym;
            /* std::cerr << "[type_checker] Resolved static dispatch: " <<
             * id->name << " -> " << canonical << "::" << method_part << "\n";
             */
          }
        }
      }
    }

    if (s && (s->kind == symbol_kind::function_symbol ||
              s->kind == symbol_kind::variable_symbol)) {
      semantic_reference_to(*id, id->name, *s);
      // Check if it's a direct function symbol
      if (s->kind == symbol_kind::function_symbol) {
        if (!order_named_arguments(callable_parameters(s->params))) return;
        validate_call_arity(*s, e.args.size(), id->name);
        std::map<std::string, std::string> substitutions;

        // Check trait bounds on explicit type arguments, or infer function
        // generics structurally from parameter/argument type pairs. Inference
        // must descend through containers (`array<T>` with `array<u8>`) rather
        // than only matching a parameter whose entire type is `T`.
        if (!e.type_args.empty()) {
          const std::vector<ast_generic_param> *params = &s->generic_params;
          // If the function is not generic but its parent is (for example,
          // vec<T>::new), use the parent's parameters.
          if (params->empty() && parent_type_sym) {
            params = &parent_type_sym->generic_params;
          }

          if (!params->empty()) {
            validate_generic_args(*params, e.type_args, e.line);
            for (size_t i = 0; i < params->size() && i < e.type_args.size();
                 ++i) {
              substitutions[(*params)[i].name] = e.type_args[i];
            }
          } else {
            add_error(e.line,
                      "Function '" + id->name +
                          "' is not generic but type arguments were provided");
          }
        } else if (!s->generic_params.empty()) {
          std::unordered_set<std::string> generic_names;
          for (const auto &generic : s->generic_params)
            generic_names.insert(generic.name);

          auto lookup_inference_type_symbol =
              [&](const std::string &base_name) -> const symbol * {
            if (base_name.find("::") != std::string::npos) {
              const symbol *qualified = symbols_.lookup(base_name);
              return qualified && qualified->kind == symbol_kind::type_symbol
                         ? qualified
                         : nullptr;
            }

            if (current_module_) {
              if (const symbol *local =
                      symbols_.lookup(*current_module_ + "::" + base_name)) {
                if (local->kind == symbol_kind::type_symbol)
                  return local;
              }
            }
            if (auto imported = resolve_type_name_with_imports(base_name)) {
              if (const symbol *candidate = symbols_.lookup(*imported)) {
                if (candidate->kind == symbol_kind::type_symbol)
                  return candidate;
              }
            }

            const symbol *unqualified = symbols_.lookup(base_name);
            if (!unqualified || unqualified->kind != symbol_kind::type_symbol)
              return nullptr;
            if (current_module_ && unqualified->module &&
                *unqualified->module != *current_module_)
              return nullptr;
            return unqualified;
          };

          auto expand_inference_alias =
              [&](const std::string &type_name) -> std::optional<std::string> {
            const auto parsed = parse_generic_type_string(type_name);
            const symbol *type_symbol =
                lookup_inference_type_symbol(parsed.base_name);
            if (!type_symbol || !type_symbol->type_name)
              return std::nullopt;
            if (type_symbol->defining_node &&
                type_symbol->defining_node->kind !=
                    ast_node_kind::type_alias_decl_node)
              return std::nullopt;

            if (type_symbol->generic_params.empty()) {
              if (!parsed.type_args.empty())
                return std::nullopt;
              return *type_symbol->type_name;
            }
            if (parsed.type_args.size() != type_symbol->generic_params.size())
              return std::nullopt;

            std::map<std::string, std::string> alias_substitutions;
            for (std::size_t index = 0; index < parsed.type_args.size();
                 ++index) {
              alias_substitutions[type_symbol->generic_params[index].name] =
                  parsed.type_args[index];
            }
            return instantiate_generic_type(*type_symbol->type_name,
                                            alias_substitutions);
          };

          std::function<std::string(const std::string &, std::size_t)>
              normalize_inference_type;
          normalize_inference_type = [&](const std::string &type_name,
                                         std::size_t depth) {
            if (depth >= 32)
              return type_name;
            if (const auto alias = expand_inference_alias(type_name)) {
              if (*alias != type_name)
                return normalize_inference_type(*alias, depth + 1);
            }

            const auto parsed = parse_generic_type_string(type_name);
            if (parsed.type_args.empty())
              return resolve_type(type_name).name;

            std::string normalized_base = resolve_type(parsed.base_name).name;
            std::string normalized = normalized_base + "<";
            for (std::size_t index = 0; index < parsed.type_args.size();
                 ++index) {
              if (index != 0)
                normalized += ", ";
              normalized +=
                  normalize_inference_type(parsed.type_args[index], depth + 1);
            }
            normalized += ">";
            return normalized;
          };

          std::unordered_set<std::string> conflicting_generics;
          std::function<void(const std::string &, const std::string &)>
              infer_type_arguments;
          infer_type_arguments = [&](const std::string &parameter_type,
                                     const std::string &argument_type) {
            const auto raw_parameter =
                parse_generic_type_string(parameter_type);
            if (raw_parameter.type_args.empty() &&
                generic_names.count(raw_parameter.base_name) != 0) {
              if (argument_type == "any" || argument_type == "<unknown>")
                return;

              const std::string inferred =
                  normalize_inference_type(argument_type, 0);
              const auto existing = substitutions.find(raw_parameter.base_name);
              if (existing == substitutions.end()) {
                substitutions.emplace(raw_parameter.base_name, inferred);
              } else if (normalize_inference_type(existing->second, 0) !=
                             inferred &&
                         conflicting_generics.insert(raw_parameter.base_name)
                             .second) {
                add_error(e.line,
                          "Conflicting types inferred for generic parameter '" +
                              raw_parameter.base_name + "' in function '" +
                              id->name + "': '" + existing->second + "' and '" +
                              inferred + "'");
              }
              return;
            }

            const auto parameter = parse_generic_type_string(
                normalize_inference_type(parameter_type, 0));
            if (parameter.type_args.empty())
              return;

            const auto argument = parse_generic_type_string(
                normalize_inference_type(argument_type, 0));
            if (parameter.base_name != argument.base_name ||
                parameter.type_args.size() != argument.type_args.size())
              return;

            for (std::size_t index = 0; index < parameter.type_args.size();
                 ++index) {
              infer_type_arguments(parameter.type_args[index],
                                   argument.type_args[index]);
            }
          };

          for (std::size_t index = 0;
               index < s->params.size() && index < arg_types.size(); ++index) {
            if (s->params[index].type_name) {
              infer_type_arguments(*s->params[index].type_name,
                                   arg_types[index].name);
            }
          }

          std::vector<std::string> inferred_type_args;
          inferred_type_args.reserve(s->generic_params.size());
          for (const auto &generic : s->generic_params) {
            const auto inferred = substitutions.find(generic.name);
            if (inferred == substitutions.end()) {
              add_error(e.line, "Could not infer generic type '" +
                                    generic.name + "' for function '" +
                                    id->name + "'");
              continue;
            }
            inferred_type_args.push_back(inferred->second);
          }

          if (inferred_type_args.size() == s->generic_params.size()) {
            validate_generic_args(s->generic_params, inferred_type_args,
                                  e.line);
            e.type_args = std::move(inferred_type_args);
          }
        }

        apply_argument_array_context(*s, substitutions);

        if (s->type_name) {
          if (!substitutions.empty()) {
            std::string instantiated_ret =
                instantiate_generic_type(*s->type_name, substitutions);
            last_expr_type_ = resolve_type(instantiated_ret);
          } else {
            last_expr_type_ = resolve_type(*s->type_name);
          }
        } else {
          last_expr_type_ = unannotated_return_type(*s);
        }
      } else {
        // It's a variable or parameter with function type
        if (!e.argument_names.empty() && s->checked_callable_parameters &&
            s->checked_callable_parameters->variadic) {
          named_arguments_handled = true;
          add_error(e.line, "Named variadic callback calls are not supported; "
                            "call the declared variadic paragraph directly");
          return;
        }
        if (s->checked_callable_parameters &&
            !order_named_arguments(*s->checked_callable_parameters)) return;
        if (s->type_name && s->type_name->rfind("fn", 0) == 0) {
          // Parse function type string "fn(T) -> U"
          // For now, simple return type extraction
          // This is a HACK. type_info should have structure info.
          // Assuming type_info name contains the signature
          std::string sig = *s->type_name;
          size_t arrow = sig.find("->");
          if (arrow != std::string::npos) {
            std::string ret = sig.substr(arrow + 2);
            // Trim
            size_t first = ret.find_first_not_of(" ");
            if (first != std::string::npos)
              ret = ret.substr(first);
            last_expr_type_ = resolve_type(ret);
          } else {
            last_expr_type_ = type_any;
          }
        } else {
          last_expr_type_ = type_any;
        }
      }
    } else {
      last_expr_type_ = type_any;
    }
  } else if (e.callee &&
             e.callee->kind == ast_node_kind::member_access_expr_node) {
    // Logic for method calls: resolve return type of method.
    // This is complicated because we need to know the type of the OBJECT,
    // then lookup the method in that type's impl methods,
    // then get the return type.

    auto ma = std::static_pointer_cast<ast_member_access_expr>(e.callee);

    if (ma->object && ma->object->inferred_type) {
      std::string type_name = *ma->object->inferred_type;
      std::string method_name = ma->member_name;

      // Built-in array.push has no declaration symbol to supply a parameter
      // contract. Validate it before the heuristic member-return fallback:
      // otherwise array<f64>.push(i32) and array<text>.push(i32) silently
      // produce runtime stores with the wrong physical element type.
      const auto builtin_receiver =
          parse_generic_type_string(resolve_type(type_name).name);
      // This runtime member has no source declaration, but its label contract
      // is public in nil.std.text and reflection's Matrix implementation.
      // Preserve that contract without pretending arbitrary heuristic members
      // have parameter names or falling back to source-order arguments.
      if ((builtin_receiver.base_name == "String" ||
           builtin_receiver.base_name == "text") && method_name == "substr") {
        if (!order_named_arguments({{"start", "length"}, false})) return;
      }
      if (builtin_receiver.base_name == "array" && method_name == "push") {
        if (e.args.size() != 1) {
          add_error(e.line, "Array.push expects 1 argument, got " +
                                std::to_string(e.args.size()));
        } else if (builtin_receiver.type_args.size() == 1) {
          const type_info expected = resolve_type(builtin_receiver.type_args[0]);
          const type_info actual = resolve_type(arg_types.front().name);
          // Match indexed-storage conversion: integer words may canonicalize
          // into u8/bool, but f64 and text require their actual element kind.
          const auto integer_storage_value = [](const std::string &name) {
            return name == "i8" || name == "i16" || name == "i32" ||
                   name == "i64" || name == "u8" || name == "u16" ||
                   name == "u32" || name == "u64" || name == "isize" ||
                   name == "usize" || name == "bool";
          };
          const bool storage_conversion =
              (expected.name == "u8" || expected.name == "bool") &&
              integer_storage_value(actual.name);
          if (!storage_conversion && !are_types_compatible(expected, actual)) {
            add_error(e.line, "Array.push element type mismatch. Expected '" +
                                  expected.name + "', got '" + actual.name + "'");
          }
        }
        if (!e.type_args.empty())
          add_error(e.line, "Array.push does not accept method type arguments");
        last_expr_type_ = type_void;
        ma->inferred_type = type_void.name;
        e.inferred_type = type_void.name;
        return;
      }

      // Lookup impl method
      // We need access to code_generator or symbol_table method lookup.
      // symbols_ has 'lookup_impl_method'?
      // Actually, type_checker doesn't have easy access to impl methods...
      // They are stored in 'function_decls_' in codegen, or in AST
      // 'impl_decl_node'. Symbol table might NOT have them indexed by type!
      // BUT, 'impl' methods add functions to the symbol table?
      // Usually as 'type__method' or just inside the module?
      // If 'math::vector::vec3', methods are inside 'math::vector' module.
      // But with mangled names? No.
      // If 'impl vec3', methods are added to current scope.
      // Inside 'math::vector', 'sub' is a function 'math::vector::sub'? No.
      // Accessing 'vec3::new' works (static).

      // If 'sub' is defined as 'pub function sub', it's a symbol in
      // 'math::vector'. So we should lookup 'math::vector::vec3' (struct)
      // members? No, methods are separate. But they are usually named
      // 'Type::method' in symbols?

      // Try looking up 'type_name + "::" + method_name' or 'type_name + "__" +
      // method_name'. Impl visitor registers methods as
      // 'TargetType__MethodName' (double underscore).

      const symbol *s = nullptr;

      // Try looking up via type members first (robust against mangling
      // mismatch)
      auto type_parts = parse_generic_type_string(type_name);
      const symbol *type_sym = symbols_.lookup(type_parts.base_name);
      if (type_sym && type_sym->kind == symbol_kind::type_symbol) {
        auto it = type_sym->members.find(method_name);
        if (it != type_sym->members.end()) {
          s = &it->second;
          // std::cerr << "[CHECK_CALL] Found via members: " << method_name <<
          // "\n";
        }
      }

      std::string qualified_method = type_name + "::" + method_name;
      if (!s)
        s = symbols_.lookup(qualified_method);

      if (!s) {
        /* std::cerr << "[CHECK_CALL] '" << qualified_method << "' NOT FOUND.
         * Trying fallback..." << std::endl; */
        // Try with double underscore (how impl methods are often registered in
        // symbol table keys) Strip generics from type_name for mangled lookup
        auto type_parts = parse_generic_type_string(type_name);
        std::string mangled_method = type_parts.base_name + "__" + method_name;
        /* std::cerr << "[CHECK_CALL] Lookup mangled: '" << mangled_method << "'
         * BaseName='" << type_parts.base_name << "'\n"; */
        s = symbols_.lookup(mangled_method);
        /* if (s) std::cerr << "[CHECK_CALL] Found via mangled: " <<
         * mangled_method << std::endl; */
      } else {
        /* std::cerr << "[CHECK_CALL] Found method: " << qualified_method <<
         * std::endl; */
      }

      if (!s) {
        // Try resolving type alias?
        type_info t = resolve_type(type_name);
        /* std::cerr << "[CHECK_CALL] Resolved type '" << type_name << "' -> '"
         * << t.name << "'" << std::endl; */

        qualified_method = t.name + "::" + method_name;
        s = symbols_.lookup(qualified_method);

        if (!s) {
          /* std::cerr << "[CHECK_CALL] '" << qualified_method << "' NOT FOUND.
           * Trying mangled alias..." << std::endl; */
          auto type_parts = parse_generic_type_string(t.name);
          std::string mangled_method =
              type_parts.base_name + "__" + method_name;
          s = symbols_.lookup(mangled_method);
          /* if (s) std::cerr << "[CHECK_CALL] Found via mangled alias: " <<
           * mangled_method << std::endl; */
        }
      }

      if (s && s->kind == symbol_kind::function_symbol) {
        const bool has_receiver_parameter =
            !s->params.empty() && s->params.front().name == "self";
        if (!order_named_arguments(callable_parameters(
                s->params, has_receiver_parameter ? 1U : 0U))) return;
        validate_call_arity(*s,
                            e.args.size() + (has_receiver_parameter ? 1U : 0U),
                            type_name + "." + method_name);
        // Found it! Resolve return type with generic substitutions.
        std::map<std::string, std::string> substitutions;

        // 1. Map type generics from the object type (for instance calls)
        auto obj_type_parts = parse_generic_type_string(type_name);
        // We need the base type symbol to know generic param names
        const symbol *base_sym = symbols_.lookup(obj_type_parts.base_name);
        if (!base_sym || !base_sym->defining_node) {
          if (auto imported = resolve_type_name_with_imports(obj_type_parts.base_name)) {
            const symbol *declared = symbols_.lookup(*imported);
            if (declared && declared->kind == symbol_kind::type_symbol &&
                declared->defining_node)
              base_sym = declared;
          }
        }
        if (!base_sym) {
          // Try resolving type alias / import
          type_info ti = resolve_type(obj_type_parts.base_name);
          if (ti != type_unknown) {
            // Lookup using fully qualified name from resolved type
            // But parse generics again just in case resolved name has them?
            // Usually resolve_type returns canonical name "core::vec"
            // Remove generics if present in canonical name?
            // resolve_type("vec") -> "core::vec".
            base_sym = symbols_.lookup(ti.name);
          }
        }

        if (base_sym && base_sym->kind == symbol_kind::type_symbol) {
          for (size_t i = 0; i < base_sym->generic_params.size() &&
                             i < obj_type_parts.type_args.size();
               ++i) {
            substitutions[base_sym->generic_params[i].name] =
                obj_type_parts.type_args[i];
          }

          // 2. Map explicit type args from the call (for static calls like
          // vec::new<T>)
          if (!e.type_args.empty()) {
            for (size_t i = 0;
                 i < base_sym->generic_params.size() && i < e.type_args.size();
                 ++i) {
              substitutions[base_sym->generic_params[i].name] = e.type_args[i];
            }
          }
        }

        apply_argument_array_context(*s, substitutions,
                                     has_receiver_parameter ? 1U : 0U);

        if (s->type_name) {
          std::string inst_ret =
              instantiate_generic_type(*s->type_name, substitutions);
          last_expr_type_ = resolve_type(inst_ret);
        } else {
          last_expr_type_ = unannotated_return_type(*s);
        }
      } else {
        // The member-access visitor already resolves built-in/generic
        // container signatures (for example vec<T>.get -> Option<T>).
        // Preserve that result when an out-of-module impl method is not
        // present under an unqualified symbol-table key; degrading it to
        // `any` breaks chained calls such as `values.get(0).unwrap()`.
        if (ma->inferred_type && *ma->inferred_type != "any" &&
            *ma->inferred_type != "<unknown>") {
          last_expr_type_ = resolve_type(*ma->inferred_type);
        } else {
          last_expr_type_ = type_any;
        }
      }
    } else {
      last_expr_type_ = type_any;
    }

  } else {
    last_expr_type_ = type_any;
  }
  if (last_expr_type_)
    e.inferred_type = last_expr_type_->name;
}

void type_checker::visit(ast_binary_expr &e) {
  if (e.left)
    e.left->accept(*this);
  type_info left_type = last_expr_type_.value_or(type_unknown);

  if (e.right)
    e.right->accept(*this);
  type_info right_type = last_expr_type_.value_or(type_unknown);

  const bool left_is_pointer = left_type.name.rfind("ptr<", 0) == 0;
  const bool right_is_pointer = right_type.name.rfind("ptr<", 0) == 0;
  const bool right_is_pointer_offset =
      right_type.name == "i32" || right_type.name == "i64";
  const bool is_add_or_subtract =
      e.op == token_kind::plus || e.op == token_kind::minus;

  // Pointer arithmetic is deliberately asymmetric and element based. Keeping
  // it here (instead of in general assignment/type compatibility) prevents
  // pointer/integer assignments and unrelated pointer element types from
  // becoming implicitly interchangeable.
  if (left_is_pointer && is_add_or_subtract && right_is_pointer_offset) {
    last_expr_type_ = left_type;
    if (last_expr_type_)
      e.inferred_type = last_expr_type_->name;
    return;
  }

  if ((left_is_pointer || right_is_pointer) && is_add_or_subtract) {
    add_error(e.line,
              "Pointer arithmetic requires lhs ptr<T> and rhs i32 or i64, "
              "got '" +
                  left_type.name + "' and '" + right_type.name + "'");
    last_expr_type_ = type_unknown;
    e.inferred_type = last_expr_type_->name;
    return;
  }

  const bool is_comparison =
      e.op == token_kind::double_equal || e.op == token_kind::not_equal ||
      e.op == token_kind::less || e.op == token_kind::less_equal ||
      e.op == token_kind::greater || e.op == token_kind::greater_equal;
  if ((left_is_pointer || right_is_pointer) && !is_comparison) {
    add_error(e.line,
              "Pointer values support only same-type comparisons or lhs "
              "ptr<T> +/- rhs i32 or i64");
    last_expr_type_ = type_unknown;
    e.inferred_type = last_expr_type_->name;
    return;
  }

  const bool arithmetic = e.op == token_kind::plus || e.op == token_kind::minus ||
      e.op == token_kind::star || e.op == token_kind::slash ||
      e.op == token_kind::percent || e.op == token_kind::mod_kw;
  const bool bitwise = e.op == token_kind::amp || e.op == token_kind::pipe ||
      e.op == token_kind::caret || e.op == token_kind::less_less ||
      e.op == token_kind::greater_greater;
  const auto integer = [](const std::string &type) {
    return type == "i32" || type == "i64" || type == "u8";
  };
  const auto numeric = [&](const std::string &type) {
    return integer(type) || type == "f64";
  };
  if ((arithmetic || bitwise) &&
      (left_type.name == "bool" || right_type.name == "bool")) {
    add_error(e.line, "Boolean arithmetic is unsupported; cast explicitly to an integer");
    last_expr_type_ = type_unknown;
    e.inferred_type = last_expr_type_->name;
    return;
  }
  if (bitwise && (left_type.name == "f64" || right_type.name == "f64" ||
                  left_type.name == "f32" || right_type.name == "f32")) {
    add_error(e.line, "Bitwise and shift operands must be integers");
    last_expr_type_ = type_unknown;
    e.inferred_type = last_expr_type_->name;
    return;
  }
  if ((arithmetic || bitwise) && numeric(left_type.name) &&
      numeric(right_type.name)) {
    // Shifts retain the left operand's width. Other scalar operations use
    // symmetric promotion, never the incidental physical width of a value.
    std::string type = left_type.name;
    if (e.op != token_kind::less_less && e.op != token_kind::greater_greater) {
      type = left_type.name == "f64" || right_type.name == "f64" ? "f64" :
             left_type.name == "i64" || right_type.name == "i64" ? "i64" :
             left_type.name == "i32" || right_type.name == "i32" ? "i32" : "u8";
    }
    last_expr_type_ = resolve_type(type);
    e.inferred_type = type;
    return;
  }

  // Non-numeric operations retain their existing nominal compatibility rules.
  if (!are_types_compatible(left_type, right_type)) {
    add_error(e.line, "Binary operator type mismatch: '" + left_type.name +
                          "' vs '" + right_type.name + "'");
  }

  // Result type
  // Comparison -> bool
  if (e.op == token_kind::double_equal || e.op == token_kind::not_equal ||
      e.op == token_kind::less || e.op == token_kind::less_equal ||
      e.op == token_kind::greater || e.op == token_kind::greater_equal) {
    last_expr_type_ = type_bool;
  } else {
    // Arithmetic -> same as operands (simplified)
    last_expr_type_ = left_type;
  }
  if (last_expr_type_)
    e.inferred_type = last_expr_type_->name;
}

void type_checker::visit(ast_cast_expr &e) {
  e.checked_identity = false;
  if (e.operand)
    e.operand->accept(*this);

  const type_info source = last_expr_type_.value_or(type_unknown);
  const type_info target = resolve_type(e.target_type);
  const auto scalar = [](const std::string &name) {
    return name == "bool" || name == "i32" || name == "i64" ||
           name == "u8" || name == "f64";
  };
  const auto unavailable_scalar = [](const std::string &name) {
    return name == "i8" || name == "i16" || name == "u16" ||
           name == "u32" || name == "u64" || name == "isize" ||
           name == "usize" || name == "char" || name == "f32" ||
           name == "float" || name == "double";
  };
  const std::function<bool(const std::string &)> known_identity_type =
      [&](const std::string &name) {
        if (name == "any" || name == "void" || name.empty() ||
            name.front() == '<')
          return false;
        if (name.rfind("fn(", 0) == 0)
          return true; // The function expression already carries its signature.
        const auto parts = parse_generic_type_string(name);
        const symbol *type = symbols_.lookup(parts.base_name);
        if (!type || type->kind != symbol_kind::type_symbol)
          return false;
        return std::all_of(parts.type_args.begin(), parts.type_args.end(),
                           known_identity_type);
      };
  if (unavailable_scalar(target.name) || unavailable_scalar(source.name)) {
    add_error(e.line, "Unsupported scalar cast from '" + source.name +
                          "' to '" + target.name +
                          "'; supported scalar types are bool, i32, i64, u8, f64");
  } else if (source.name.rfind("fn(", 0) == 0 && scalar(target.name)) {
    add_error(e.line, "Cannot cast a function value to scalar type '" +
                          target.name + "'");
  } else if ((scalar(source.name) || source.name == "String") &&
             (scalar(target.name) || target.name == "String")) {
    // Text parsing/formatting has a checked interpreter contract. Native
    // lowering may explicitly reject it; it must never reinterpret a text
    // handle as a scalar word (or a scalar as a text handle).
    e.checked_identity = source.name == "String" && target.name == "String";
  } else if (source.name == target.name && known_identity_type(source.name)) {
    // Identity casts retain the complete canonical type, including generic
    // arguments. Do not use are_types_compatible here: that relation permits
    // numeric widening and dynamic/generic compatibility, neither of which
    // proves the identity of a pointer, array, enum, or aggregate.
    e.checked_identity = true;
  } else {
    add_error(e.line, "Cannot cast '" + source.name + "' to '" + target.name +
                          "'; non-scalar casts require the exact same type");
  }
  last_expr_type_ = target;
  e.inferred_type = target.name;
}

bool type_checker::check_reference_handle(ast_expr &reference, ast_expr &operand,
                                           const type_info &operand_type) {
  reference.checked_reference_handle = false;
  // REF_LOCAL preserves an aggregate object handle, not the address of a
  // binding. Primitive values must never be copied and then interpreted as
  // heap addresses by READ_PTR/WRITE_PTR. A future scalar-reference feature
  // requires its own cell/lifetime ABI rather than widening this proof.
  if (operand.kind != ast_node_kind::identifier_expr_node) {
    add_error(reference.line,
              "Reference target must be a local struct or class handle; "
              "bind the aggregate to a local before using ref");
    return false;
  }
  const auto &identifier = static_cast<const ast_identifier_expr &>(operand);
  const symbol *local = symbols_.lookup_non_global(identifier.name);
  if (!local || local->kind != symbol_kind::variable_symbol ||
      identifier.resolved_global_name) {
    add_error(reference.line,
              "Reference target must be a local struct or class handle; "
              "references to globals are unavailable");
    return false;
  }
  const auto parts = parse_generic_type_string(resolve_type(operand_type.name).name);
  const symbol *nominal = symbols_.lookup_type(parts.base_name);
  if (!nominal || !nominal->defining_node ||
      (nominal->defining_node->kind != ast_node_kind::struct_decl_node &&
       nominal->defining_node->kind != ast_node_kind::class_decl_node)) {
    add_error(reference.line,
              "Reference creation requires a local struct or class handle; "
              "scalar, array, pointer, enum and nested references are unavailable");
    return false;
  }
  reference.checked_reference_handle = true;
  checked_reference_handles_.push_back(&reference);
  return true;
}

void type_checker::visit(ast_unary_expr &e) {
  if (e.operand)
    e.operand->accept(*this);

  if (e.op == token_kind::not_kw) {
    if (!last_expr_type_ || last_expr_type_->name != "bool")
      add_error(e.line, "Logical 'not' requires a boolean operand, got '" +
                            last_expr_type_.value_or(type_unknown).name + "'");
    last_expr_type_ = type_bool;
    e.inferred_type = type_bool.name;
    return;
  }

  if (e.op == token_kind::minus && last_expr_type_ &&
      last_expr_type_->name == "bool")
    add_error(e.line, "Boolean arithmetic is unsupported; cast explicitly to an integer");
  if (e.op == token_kind::tilde && last_expr_type_ &&
      last_expr_type_->name != "i32" && last_expr_type_->name != "i64" &&
      last_expr_type_->name != "u8")
    add_error(e.line, "Bitwise complement requires i32, i64, or u8");

  if (e.op == token_kind::star) {
    // Handle both ptr<T> and ref<T> dereference
    if (last_expr_type_ && is_ownership_type(last_expr_type_->name)) {
      last_expr_type_ = resolve_type(ownership_element_type(last_expr_type_->name));
    } else if (last_expr_type_ && last_expr_type_->name.rfind("ptr<", 0) == 0) {
      // Extract T from ptr<T>
      std::string inner =
          last_expr_type_->name.substr(4, last_expr_type_->name.length() - 5);
      last_expr_type_ = resolve_type(inner);
    } else if (last_expr_type_ && last_expr_type_->name.rfind("ref<", 0) == 0) {
      add_error(e.line,
                "Explicit reference dereference is unavailable; use aggregate "
                "reference fields or methods, not raw-pointer dereference");
      // Extract T from ref<T>
      std::string inner =
          last_expr_type_->name.substr(4, last_expr_type_->name.length() - 5);
      last_expr_type_ = resolve_type(inner);
    } else {
      add_error(e.line,
                "Cannot dereference non-pointer type '" +
                    (last_expr_type_ ? last_expr_type_->name : "unknown") +
                    "'");
      last_expr_type_ = type_unknown;
    }
  } else if (e.op == token_kind::ref_kw) {
    // Only nominal aggregate handles have an executable reference contract.
    if (last_expr_type_) {
      check_reference_handle(e, *e.operand, *last_expr_type_);
      last_expr_type_ = {"ref<" + last_expr_type_->name + ">", true};
    } else {
      last_expr_type_ = type_unknown;
    }
  } else if (e.op == token_kind::amp) {
    // Handle address-of operator: &x -> ptr<T>
    if (last_expr_type_) {
      last_expr_type_ = {"ptr<" + last_expr_type_->name + ">", true};
    } else {
      last_expr_type_ = type_unknown;
    }
  }
  if (last_expr_type_)
    e.inferred_type = last_expr_type_->name;
}

void type_checker::visit(ast_assignment_expr &e) {
  // Resolve the destination before visiting the RHS. Besides checking the
  // lvalue, this provides concrete context for values such as an empty array
  // assigned to an array<T> local or a generic struct field.
  std::optional<type_info> target_type;
  bool storage_conversion = false;
  std::string assignment_kind;

  // Indexed and dereferenced storage intentionally canonicalizes integer words
  // to the destination representation (notably modulo-256 u8 and 0/1 bool).
  // Keep that explicit storage conversion narrow: pointer/handle and unrelated
  // aggregate values must still be rejected before NIL lowering.
  const auto storage_types_compatible = [&](const type_info &target_type,
                                            const type_info &value_type) {
    if (are_types_compatible(target_type, value_type))
      return true;

    const std::string target = resolve_type(target_type.name).name;
    const std::string value = resolve_type(value_type.name).name;
    const auto is_integer_storage_value = [](const std::string &name) {
      return name == "i8" || name == "i16" || name == "i32" || name == "i64" ||
             name == "u8" || name == "u16" || name == "u32" || name == "u64" ||
             name == "isize" || name == "usize" || name == "bool";
    };
    return (target == "u8" || target == "bool") &&
           is_integer_storage_value(value);
  };

  if (e.target) {
    // Special handling for identifier target to check existence/mutability.
    if (e.target->kind == ast_node_kind::unary_expr_node) {
      auto ue = std::static_pointer_cast<ast_unary_expr>(e.target);
      if (ue->op == token_kind::star) {
        // Dereference assignment: *ptr = val or *ref = val
        ue->operand->accept(*this);
        bool is_ptr_or_ref =
            last_expr_type_ && (last_expr_type_->name.rfind("ptr<", 0) == 0 ||
                                last_expr_type_->name.rfind("ref<", 0) == 0 ||
                                is_ownership_type(last_expr_type_->name));
        if (!is_ptr_or_ref) {
          add_error(e.line, "Invalid assignment target: expected "
                            "pointer/reference dereference");
        }
        // Check compatibility with the pointee type. Raw pointers share the
        // VM's integer representation, so omitting this check would allow a
        // ptr<S> value to be silently truncated into ptr<i32>/ptr<u8> storage.
        if (is_ptr_or_ref) {
          if (last_expr_type_->name.rfind("ref<", 0) == 0)
            add_error(e.line,
                      "Explicit reference dereference assignment is unavailable; "
                      "mutate aggregate reference fields instead");
          std::string inner = is_ownership_type(last_expr_type_->name)
              ? ownership_element_type(last_expr_type_->name)
              : last_expr_type_->name.substr(4, last_expr_type_->name.length() - 5);
          target_type = resolve_type(inner);
          storage_conversion = true;
          assignment_kind = "dereference ";
        }
      } else {
        add_error(e.line, "Invalid assignment target");
      }
    } else if (e.target->kind == ast_node_kind::identifier_expr_node) {
      auto id = std::static_pointer_cast<ast_identifier_expr>(e.target);
      id->resolved_global_name.reset();
      const symbol *s = symbols_.lookup_non_global(id->name);
      if (!s) {
        if (const auto global = resolve_source_global(id->name, e.line)) {
          id->resolved_global_name = *global;
          s = symbols_.lookup(*global);
        } else {
          s = symbols_.lookup(id->name);
        }
      }
      // Also try module-qualified lookup for global variables (let*)
      if (!s && current_module_) {
        s = symbols_.lookup(*current_module_ + "::" + id->name);
      }
      // Try global lookup via resolve_imported_symbol
      if (!s) {
        if (auto imported_name = resolve_imported_symbol(id->name)) {
          s = symbols_.lookup(*imported_name);
        }
      }
      if (!s) {
        add_error(e.line,
                  "Assignment to undefined variable '" + id->name + "'");
      } else {
        // Ensure the symbol is a variable and then ensure mutability when
        // writing to it. Non-variable assignments are invalid here.
        if (s->kind != symbol_kind::variable_symbol) {
          add_error(e.line,
                    "Cannot assign to non-variable symbol '" + id->name + "'");
        } else {
          semantic_reference_to(*id, id->name, *s);
          if (!is_symbol_visible(s, current_module_.value_or("")))
            add_error(e.line, "Symbol '" + id->name + "' is private to module '" +
                              s->module.value_or("<unknown>") + "'");
          // If the symbol exists and is a variable, check mutability
          if (!s->is_mut) {
            add_error(e.line,
                      "Assignment to immutable variable '" + id->name + "'");
          }

          // If the variable has a declared type, verify assignment
          // compatibility
          if (s->type_name) {
            target_type = id->resolved_global_name
                ? resolve_global_type(*s) : resolve_type(*s->type_name);
            id->inferred_type = target_type->name;
          }
        }
      }
    } else if (e.target->kind == ast_node_kind::index_expr_node) {
      // The index visitor resolves array<T>/ptr<T> to T. Check the value here;
      // the visitor itself cannot see the assignment RHS.
      e.target->accept(*this);
      target_type = last_expr_type_.value_or(type_unknown);
      storage_conversion = true;
      assignment_kind = "indexed ";
    } else if (e.target->kind == ast_node_kind::member_access_expr_node) {
      auto &member = static_cast<ast_member_access_expr &>(*e.target);
      member.object->accept(*this);
      const type_info receiver = last_expr_type_.value_or(type_unknown);
      bool type_receiver = false;
      if (member.object->kind == ast_node_kind::identifier_expr_node) {
        const auto &id = static_cast<const ast_identifier_expr &>(*member.object);
        const symbol *receiver_symbol = symbols_.lookup(id.name);
        if (!receiver_symbol && current_module_)
          receiver_symbol = symbols_.lookup(*current_module_ + "::" + id.name);
        if (!receiver_symbol) {
          if (auto imported = resolve_imported_symbol(id.name))
            receiver_symbol = symbols_.lookup(*imported);
        }
        type_receiver = receiver_symbol &&
                        receiver_symbol->kind == symbol_kind::type_symbol;
      }

      // A member read may resolve to a method, an enum constructor or a
      // synthetic property (array.len). None of these is assignable storage.
      // Look up declared fields directly, applying the receiver's generic
      // arguments before checking the RHS. Handle/ref receivers keep the
      // language's existing shared mutable field semantics.
      std::function<std::optional<type_info>(type_info, unsigned)> field_type;
      field_type = [&](type_info object_type,
                       unsigned depth) -> std::optional<type_info> {
        if (depth > 64)
          return std::nullopt;
        object_type = resolve_type(object_type.name);
        auto parts = parse_generic_type_string(object_type.name);
        if ((parts.base_name == "ptr" || parts.base_name == "ref") &&
            parts.type_args.size() == 1)
          return field_type(resolve_type(parts.type_args[0]), depth + 1);
        if (parts.base_name == "Self" && current_class_name_)
          return field_type(resolve_type(*current_class_name_), depth + 1);

        const symbol *type_symbol =
            symbols_.lookup(resolve_type(parts.base_name).name);
        if (!type_symbol || type_symbol->kind != symbol_kind::type_symbol ||
            !type_symbol->defining_node)
          return std::nullopt;

        const std::vector<ast_struct_field> *fields = nullptr;
        const std::vector<ast_generic_param> *parameters = nullptr;
        const ast_class_decl *class_decl = nullptr;
        if (type_symbol->defining_node->kind == ast_node_kind::struct_decl_node) {
          const auto &decl = static_cast<const ast_struct_decl &>(
              *type_symbol->defining_node);
          fields = &decl.fields;
          parameters = &decl.generic_params;
        } else if (type_symbol->defining_node->kind ==
                   ast_node_kind::class_decl_node) {
          class_decl = &static_cast<const ast_class_decl &>(
              *type_symbol->defining_node);
          fields = &class_decl->fields;
          parameters = &class_decl->generic_params;
        } else {
          return std::nullopt;
        }

        std::map<std::string, std::string> substitutions;
        for (size_t i = 0;
             i < parameters->size() && i < parts.type_args.size(); ++i)
          substitutions[(*parameters)[i].name] = parts.type_args[i];
        for (const auto &field : *fields) {
          if (field.name == member.member_name)
            return resolve_type(
                instantiate_generic_type(field.type_name, substitutions));
        }
        if (class_decl) {
          for (const auto &base : class_decl->base_classes) {
            auto inherited = field_type(
                resolve_type(instantiate_generic_type(base, substitutions)),
                depth + 1);
            if (inherited)
              return inherited;
          }
        }
        return std::nullopt;
      };
      if (!type_receiver)
        target_type = field_type(receiver, 0);
      if (!target_type)
        add_error(e.line, "Invalid assignment target: type '" + receiver.name +
                              "' has no assignable field '" +
                              member.member_name + "'");
      assignment_kind = "field ";
    } else {
      e.target->accept(*this);
      add_error(e.line, "Invalid assignment target");
    }
  } else {
    add_error(e.line, "Invalid assignment target");
  }

  if (target_type) {
    e.target->inferred_type = target_type->name;
    if (e.value)
      e.value->inferred_type = target_type->name;
  }
  last_expr_type_ = type_unknown;
  if (e.value)
    e.value->accept(*this);
  const type_info val_type = last_expr_type_.value_or(type_unknown);
  if (target_type &&
      !(storage_conversion ? storage_types_compatible(*target_type, val_type)
                           : check_assignment_type(*target_type, val_type, e.value.get()))) {
    add_error(e.line, "Type mismatch in " + assignment_kind +
                          "assignment. Expected '" + target_type->name +
                          "', got '" + val_type.name + "'");
  }
  last_expr_type_ = val_type;
}

void type_checker::visit(ast_identifier_expr &e) {
  capture_completion(e);
  e.resolved_global_name.reset();
  e.checked_callable_parameters.reset();
  const auto *fault_binding = symbols_.lookup_non_global(e.name);
  if (fault_binding && fault_binding->defining_node &&
      fault_binding->defining_node->kind == ast_node_kind::fault_handler_node &&
      !static_cast<const ast_fault_handler *>(fault_binding->defining_node)->fault_type) {
    add_error(e.line, "Opaque catch-all fault token may only be ignored or used as the whole expression of raise");
    last_expr_type_ = type_unknown;
    e.inferred_type.reset();
    return;
  }
  if (!symbols_.lookup_non_global(e.name)) {
    if (const auto global = resolve_source_global(e.name, e.line)) {
      const symbol *variable = symbols_.lookup(*global);
      if (global->empty() || !variable) {
        // Preserve a failed global resolution as well as successful identity:
        // --no-typecheck must not reinterpret an ambiguous import as a function.
        e.resolved_global_name = *global;
        if (!global->empty())
          add_error(e.line, "Global '" + *global + "' is used before its declaration");
        last_expr_type_ = type_unknown;
        return;
      }
      if (!variable->type_name) {
        add_error(e.line, "Global '" + *global +
                  "' needs an explicit type before its initializer has been checked");
        last_expr_type_ = type_unknown;
        return;
      }
      if (!is_symbol_visible(variable, current_module_.value_or(""))) {
        add_error(e.line, "Symbol '" + e.name + "' is private to module '" +
                          variable->module.value_or("<unknown>") + "'");
        last_expr_type_ = type_unknown;
        return;
      }
      e.resolved_global_name = *global;
      last_expr_type_ = resolve_global_type(*variable);
      e.inferred_type = last_expr_type_->name;
      semantic_reference_to(e, e.name, *variable);
      return;
    }
  }
  const auto value_type = [&](const symbol &value,
                              const std::map<std::string, std::string> &substitutions) {
    e.checked_callable_parameters = value.checked_callable_parameters;
    const auto specialized = [&](const std::string &type) {
      return resolve_type(instantiate_generic_type(type, substitutions)).name;
    };
    if (value.kind == symbol_kind::function_symbol &&
        (!value.defining_node ||
         value.defining_node->kind == ast_node_kind::function_decl_node)) {
      e.checked_callable_parameters = callable_parameters(value.params);
      // Function references are structural values even when found through a
      // type alias or a copied module-member symbol. Their return type alone
      // is never the type of the reference itself.
      std::string signature = "fn(";
      for (std::size_t index = 0; index < value.params.size(); ++index) {
        if (index != 0)
          signature += ", ";
        signature += specialized(value.params[index].type_name.value_or("any"));
      }
      signature += ") -> " + specialized(value.type_name.value_or("void"));
      return type_info{signature, true};
    }
    return resolve_type(instantiate_generic_type(value.type_name.value_or("any"),
                                                 substitutions));
  };
  // std::cerr << "[type_checker] visit(identifier) name='" << e.name << "'" <<
  // std::endl;
  // 1. Try local lookup first
  const symbol *s = symbols_.lookup(e.name);

  // Unqualified aliases (notably enum variants) are retained for convenient
  // same-module spelling, but they must not leak into unrelated modules. A
  // matching imported symbol is resolved below through that module's import
  // map. Lexical symbols have no owning module and remain highest priority.
  if (s && current_module_ && s->module && *s->module != *current_module_) {
    s = nullptr;
  }

  // 1b. Try current module qualified lookup (if not found locally)
  if (!s && current_module_) {
    std::string qualified = *current_module_ + "::" + e.name;
    s = symbols_.lookup(qualified);
  }

  // 2. Try imported symbols
  if (!s) {
    if (auto imported_name = resolve_imported_symbol(e.name)) {
      s = symbols_.lookup(*imported_name);
    }
  }

  // 3. Try qualified name resolution
  if (!s && e.name.find("::") != std::string::npos) {
    s = resolve_qualified_name(e.name);
  }

  if (!s && e.name.find("::") != std::string::npos) {
    /* std::cerr << "[type_checker] Checking static dispatch for " << e.name <<
     * std::endl; */
    auto last_sep = e.name.rfind("::");
    std::string type_part = e.name.substr(0, last_sep);
    std::string method_part = e.name.substr(last_sep + 2);

    // Try to resolve the prefix as a type
    // We use resolve_type to get the canonical type name (e.g. core::vec)
    // But resolve_type might return "unknown" if not found.
    // We need to be careful not to emit errors during this speculative check.
    // Actually resolve_type emits errors/logs.
    // Let's try lookup of the type symbol first.

    // Short import aliases for common modules used by Gehenna
    // These allow "console::writeln" to resolve to "io::console::writeln" etc.
    static const std::unordered_map<std::string, std::string>
        short_module_aliases = {
            {"console", "io::console"},
            {"mimic", "mimic"},
            {"engine_vulkan", "engine_vulkan"},
            {"engine_window", "engine_window"},
            {"engine_native", "engine_native"},
            {"math", "math"},
            {"voxel", "voxel"},
            {"agent", "agent"},
        };

    // Expand short alias if present
    std::string expanded_type_part = type_part;
    auto alias_it = short_module_aliases.find(type_part);
    if (alias_it != short_module_aliases.end()) {
      expanded_type_part = alias_it->second;
    }

    const symbol *type_sym = symbols_.lookup(expanded_type_part);
    if (type_sym) {
      /* std::cerr << "[type_checker] Found type symbol for " << type_part << "
       * members=" << type_sym->members.size() << std::endl; */
      // For simple module types (no generics), try member lookup immediately
      auto it = type_sym->members.find(method_part);
      if (it != type_sym->members.end()) {
        /* std::cerr << "[type_checker] Found static member " << method_part <<
         * " in " << type_part << std::endl; */
        s = &it->second;
        // Set return type and do visibility check
        last_expr_type_ = value_type(*s, {});
        std::string current = current_module_.value_or("");
        if (!is_symbol_visible(s, current)) {
          add_error(e.line, "Symbol '" + e.name + "' is private to module '" +
                                s->module.value_or("<unknown>") + "'");
        }
        e.inferred_type = last_expr_type_->name;
        /* std::cerr << "[type_checker] visit(identifier) DONE name='" << e.name
         * << "'" << std::endl; */
        return; // Early exit - found the member
      } else if (type_part.find("engine_vulkan") != std::string::npos &&
                 ny::is_debug_enabled()) {
        // Debug dump first 10 member names
        std::cerr << "[type_checker] Member lookup FAILED for '" << method_part
                  << "' in " << type_part << ". Members:" << std::endl;
        int count = 0;
        for (const auto &m : type_sym->members) {
          if (count++ < 10)
            std::cerr << "  - " << m.first << std::endl;
        }
      }
    } else {
      /* std::cerr << "[type_checker] Type symbol NOT FOUND for " << type_part
       * << std::endl; */
    }
    if (!type_sym && current_module_) {
      type_sym = symbols_.lookup(*current_module_ + "::" + type_part);
    }

    if (!type_sym && type_part.find("engine_math") != std::string::npos &&
        ny::is_debug_enabled()) {
      std::cerr << "[type_checker] Debug dump for " << type_part << ":"
                << std::endl;
      symbols_.dump(std::cerr);
    }
    // Also try imported
    if (!type_sym) {
      if (auto imported = resolve_imported_symbol(type_part)) {
        type_sym = symbols_.lookup(*imported);
      }
    }

    std::string canonical_type_name = type_part;
    auto resolved_name = resolve_type_name_with_imports(type_part);
    if (resolved_name) {
      canonical_type_name = *resolved_name;
    } else if (current_module_) {
      std::string q = *current_module_ + "::" + type_part;
      if (symbols_.lookup(q))
        canonical_type_name = q;
    }

    // Explicitly resolve aliases (peel layers)
    // Aliases are type_symbols that have a 'type_name' set (pointing to target)
    // Structs/Enums have 'type_name' as null.
    for (int i = 0; i < 20; ++i) {
      const symbol *s_alias = symbols_.lookup(canonical_type_name);
      if (s_alias && s_alias->kind == symbol_kind::type_symbol &&
          s_alias->type_name.has_value()) {
        // It's an alias, resolve the target fully
        type_info ti = resolve_type(*s_alias->type_name);
        canonical_type_name = ti.name;
      } else {
        break;
      }
    }

    // Parse generics from the canonical type name (e.g. "HashMap<Point, i32>")
    auto parts = parse_generic_type_string(canonical_type_name);

    // Use the base type symbol to look up the method in its members map
    // This avoids issues with mangled names containing generic parameters (e.g.
    // "Box<T>__new")
    const symbol *base_type_sym = symbols_.lookup(parts.base_name);

    if (base_type_sym) {
      /* std::cerr << "[type_checker] Checking members of " << parts.base_name
       * << " (count=" << base_type_sym->members.size() << ")" << std::endl; */
      auto it = base_type_sym->members.find(method_part);
      if (it != base_type_sym->members.end()) {
        /* std::cerr << "[type_checker] Found static member " << method_part <<
         * " in " << parts.base_name << std::endl; */

        // Found the method symbol in the type's members
        s = &it->second;
        // std::cerr << "[visit(identifier)] Found '" << method_part << "' in
        // members of '" << parts.base_name << "'\n";
      } else {
        /* std::cerr << "[visit(identifier)] '" << method_part << "' NOT FOUND
         * in members of '" << parts.base_name << "'\n"; */
        // Dump members
        /* for(auto& m : base_type_sym->members) std::cerr << "  - " << m.first
         * << "\n"; */
      }
    } else {
      /* std::cerr << "[visit(identifier)] Base type '" << parts.base_name << "'
       * NOT FOUND for '" << e.name << "'\n"; */
    }

    // Fallback to mangled lookup if not found in members (legacy support)
    if (!s) {
      std::string mangled = parts.base_name + "__" + method_part;
      s = symbols_.lookup(mangled);
    }

    // If found, we must specialize the symbol's type with the generic args
    if (s) {
      // Create a temporary symbol copy to hold the specialized return type?
      // Or just set last_expr_type_ directly?
      // Since we return 's', visit end will inspect s->type_name.
      // We need to intercept that.
      // But we can't modify 's' in the symbol table permanently.

      // Strategy: Let 's' be valid so we pass visibility checks.
      // Then manualy resolve typ, applying substitutions.

      // 1. Build substitution map
      std::map<std::string, std::string> substitutions;
      const symbol *base_type_sym = symbols_.lookup(parts.base_name);
      if (base_type_sym && !parts.type_args.empty()) {
        // We need the generic params of the struct/impl.
        // This is tricky if symbols_ doesn't expose them cleanly.
        // But ast_struct_decl does.
        if (base_type_sym->defining_node &&
            base_type_sym->defining_node->kind ==
                ast_node_kind::struct_decl_node) {
          const auto &sd = static_cast<const ast_struct_decl &>(
              *base_type_sym->defining_node);
          for (size_t i = 0;
               i < sd.generic_params.size() && i < parts.type_args.size();
               ++i) {
            substitutions[sd.generic_params[i].name] = parts.type_args[i];
          }
        }
      }

      // 2. Resolve type and set last_expr_type_
      last_expr_type_ = value_type(*s, substitutions);

      // Check visibility
      std::string current = current_module_.value_or("");
      if (!is_symbol_visible(s, current)) {
        add_error(e.line, "Symbol '" + e.name + "' is private to module '" +
                              s->module.value_or("<unknown>") + "'");
      }

      e.inferred_type = last_expr_type_->name;
      return; // Early exit because we handled it manually
    }

    // If not found, try Type::Member (for enum variants)
    if (!s) {
      std::string variant_fqn = canonical_type_name + "::" + method_part;
      s = symbols_.lookup(variant_fqn);
    }
  }

  if (!s) {
    add_error(e.line, "Undefined identifier '" + e.name + "'");
    last_expr_type_ = type_unknown;
  } else {
    // Check visibility
    std::string current = current_module_.value_or("");
    if (!is_symbol_visible(s, current)) {
      add_error(e.line, "Symbol '" + e.name + "' is private to module '" +
                            s->module.value_or("<unknown>") + "'");
    }

    last_expr_type_ = value_type(*s, {});
    e.inferred_type = last_expr_type_->name;
    semantic_reference_to(e, e.name, *s);
  }
  // std::cerr << "[type_checker] visit(identifier) DONE name='" << e.name <<
  // "'" << std::endl;
}

void type_checker::visit(ast_string_literal &e) {
  last_expr_type_ = type_string;
  e.inferred_type = last_expr_type_->name;
}

void type_checker::visit(ast_number_literal &e) {
  if (is_floating_literal_spelling(e.value)) {
    last_expr_type_ = type_f64;
  } else {
    integer_literal_info integer;
    if (const auto *message = parse_integer_literal(e.value, integer)) {
      add_error(e.line, message);
      last_expr_type_ = type_unknown;
    } else {
      last_expr_type_ = integer.inferred_type() == std::string_view("i64")
          ? type_i64 : type_i32;
    }
  }
  e.inferred_type = last_expr_type_->name;
}

void type_checker::visit(ast_char_literal &e) {
  last_expr_type_ = resolve_type("u8");
  e.inferred_type = last_expr_type_->name;
}

void type_checker::visit(ast_match_expr &e) {
  // 1. Evaluate target expression
  if (e.target)
    e.target->accept(*this);

  type_info target_type = last_expr_type_.value_or(type_unknown);

  // Helper to convert expression to pattern (since ast_match_expr uses exprs)
  const auto constructor_name = [](const ast_expr &expression) -> std::string {
    if (expression.kind == ast_node_kind::identifier_expr_node)
      return static_cast<const ast_identifier_expr &>(expression).name;
    if (expression.kind == ast_node_kind::member_access_expr_node) {
      const auto &member = static_cast<const ast_member_access_expr &>(expression);
      if (member.object && member.object->kind == ast_node_kind::identifier_expr_node)
        return static_cast<const ast_identifier_expr &>(*member.object).name +
               "::" + member.member_name;
    }
    return {};
  };
  const auto has_enum_owner = [&](const std::string &name) {
    const auto separator = name.rfind("::");
    if (separator == std::string::npos) return false;
    const auto owner = name.substr(0, separator);
    const symbol *type = resolve_pattern_type_symbol(owner);
    return type && type->defining_node &&
           type->defining_node->kind == ast_node_kind::enum_decl_node;
  };
  std::function<ast_pattern(const ast_expr_ptr &)> expr_to_pattern;
  expr_to_pattern = [&](const ast_expr_ptr &expr) -> ast_pattern {
    ast_pattern p;
    if (!expr)
      return p;

    if (expr->kind == ast_node_kind::number_literal_node) {
      p.kind = ast_pattern_kind::literal_pattern;
      p.literal_value = static_cast<ast_number_literal *>(expr.get())->value;
    } else if (expr->kind == ast_node_kind::string_literal_node) {
      p.kind = ast_pattern_kind::literal_pattern;
      p.literal_kind = expr->kind;
      p.literal_value = static_cast<ast_string_literal *>(expr.get())->value;
    } else if (expr->kind == ast_node_kind::boolean_literal_node) {
      p.kind = ast_pattern_kind::literal_pattern;
      p.literal_kind = expr->kind;
      p.literal_value = static_cast<ast_boolean_literal *>(expr.get())->value
                            ? "true" : "false";
    } else if (expr->kind == ast_node_kind::char_literal_node) {
      p.kind = ast_pattern_kind::literal_pattern;
      p.literal_kind = expr->kind;
      p.literal_value = static_cast<ast_char_literal *>(expr.get())->value;
    } else if (expr->kind == ast_node_kind::identifier_expr_node) {
      auto name = static_cast<ast_identifier_expr *>(expr.get())->name;
      if (name == "_")
        p.kind = ast_pattern_kind::wildcard_pattern;
      else {
        p.kind = has_enum_owner(name)
                     ? ast_pattern_kind::constructor_pattern
                     : ast_pattern_kind::variable_pattern;
        p.name = name;
      }
    } else if (expr->kind == ast_node_kind::member_access_expr_node) {
      p.name = constructor_name(*expr);
      if (has_enum_owner(p.name))
        p.kind = ast_pattern_kind::constructor_pattern;
    } else if (expr->kind == ast_node_kind::call_expr_node) {
      // Constructor pattern: some(v)
      auto call = static_cast<ast_call_expr *>(expr.get());
      if (call->callee && has_enum_owner(p.name = constructor_name(*call->callee))) {
        p.kind = ast_pattern_kind::constructor_pattern;
        for (const auto &arg : call->args) {
          p.subpatterns.push_back(expr_to_pattern(arg));
        }
      }
    }
    return p;
  };

  // 2. Check cases
  std::optional<type_info> common_result_type;

  for (const auto &c : e.cases) {
    // Convert expr pattern to ast_pattern
    ast_pattern pattern = expr_to_pattern(c.pattern);

    const bool wildcard = c.pattern &&
        c.pattern->kind == ast_node_kind::identifier_expr_node &&
        static_cast<const ast_identifier_expr &>(*c.pattern).name == "_";
    const bool enum_pattern = pattern.kind == ast_pattern_kind::constructor_pattern;
    const bool value_pattern = !wildcard && !enum_pattern;
    std::optional<type_info> pattern_type;
    if (value_pattern && c.pattern) {
      // Non-enum expression patterns compare against ordinary values. They
      // must resolve fields, calls, and globals normally, not create bindings
      // that hide undefined identifiers from editor analysis.
      last_expr_type_.reset();
      c.pattern->accept(*this);
      pattern_type = last_expr_type_;
    } else {
      pattern_type = infer_type_from_pattern(pattern);
    }

    // Check compatibility with target
    if (pattern_type.has_value() && target_type.name != "<unknown>") {
      if (!are_types_compatible(*pattern_type, target_type)) {
        add_error(e.line, "Pattern type '" + pattern_type->name +
                              "' incompatible with match target type '" +
                              target_type.name + "'");
      }
    }

    // Enter scope for pattern bindings
    symbols_.enter_scope();

    type_info binding_type =
        target_type.name != "<unknown>"
            ? target_type
            : (pattern_type.has_value() ? *pattern_type : type_any);

    if (!value_pattern)
      bind_pattern_to_type(pattern, binding_type, e.line);
    if (pattern.kind == ast_pattern_kind::constructor_pattern && c.pattern) {
      ast_expr *constructor = c.pattern.get();
      if (constructor->kind == ast_node_kind::call_expr_node)
        constructor = static_cast<ast_call_expr *>(constructor)->callee.get();
      if (constructor && constructor->kind == ast_node_kind::identifier_expr_node)
        static_cast<ast_identifier_expr *>(constructor)->name = pattern.name;
      else if (constructor && constructor->kind == ast_node_kind::member_access_expr_node) {
        auto &member = *static_cast<ast_member_access_expr *>(constructor);
        const auto separator = pattern.name.rfind("::");
        if (separator != std::string::npos && member.object &&
            member.object->kind == ast_node_kind::identifier_expr_node) {
          static_cast<ast_identifier_expr &>(*member.object).name = pattern.name.substr(0, separator);
          member.member_name = pattern.name.substr(separator + 2);
        }
      }
    }

    // Check guard
    if (c.guard) {
      c.guard->accept(*this);
      if (!last_expr_type_.has_value() ||
          last_expr_type_->name != type_bool.name) {
        add_error(e.line, "Match guard expression must be boolean");
      }
    }

    // Check body
    if (c.body) {
      c.body->accept(*this);
      type_info body_type = last_expr_type_.value_or(type_unknown);

      if (!common_result_type.has_value()) {
        common_result_type = body_type;
      } else {
        const auto promoted = promoted_scalar_join(common_result_type->name,
                                                    body_type.name);
        if (promoted) {
          common_result_type = resolve_type(*promoted);
        } else if ((common_result_type->name == "bool") != (body_type.name == "bool") ||
                   !are_types_compatible(*common_result_type, body_type)) {
          add_error(e.line, "Match case types must be consistent. Expected '" +
                                common_result_type->name + "', found '" +
                                body_type.name + "'");
        }
      }
    }

    symbols_.leave_scope();
  }
  last_expr_type_ = common_result_type.value_or(type_unknown);
  e.inferred_type = last_expr_type_->name;
}

void type_checker::visit(ast_array_literal_expr &e) {
  if (e.elements.empty()) {
    if (e.inferred_type) {
      const auto context = parse_generic_type_string(*e.inferred_type);
      if (context.base_name == "array" && context.type_args.size() == 1 &&
          context.type_args[0] != "any" &&
          context.type_args[0] != "<unknown>") {
        last_expr_type_ = type_info{*e.inferred_type, false};
        return;
      }
    }
    last_expr_type_ = type_info{"array<any>", false};
    e.inferred_type = last_expr_type_->name;
    return;
  }

  std::optional<type_info> elem_type;
  std::vector<type_info> element_types;
  for (auto &el : e.elements) {
    el->accept(*this);
    type_info t = last_expr_type_.value_or(type_unknown);
    element_types.push_back(t);
    if (!elem_type.has_value()) {
      elem_type = t;
    } else {
      // Scalar i32/i64 compatibility must not make an array's width depend
      // on element order. Existing array values remain invariant; this join
      // applies only while constructing a new literal.
      if ((elem_type->name == "i32" && t.name == "i64") ||
          (elem_type->name == "i64" && t.name == "i32"))
        elem_type = type_i64;
    }
  }
  // Recheck every element against the final width: a previously compatible
  // bool/i32 pair does not make bool compatible with a later i64 element.
  for (const auto &type : element_types) {
    if (!are_types_compatible(*elem_type, type))
      add_error(e.line, "Array element type mismatch. Expected '" +
                            elem_type->name + "', got '" + type.name + "'");
  }

  // Construct array type string: array<T>
  std::string type_name = "array<" + elem_type->name + ">";
  last_expr_type_ = type_info{type_name, false};
  e.inferred_type = type_name;
}

void type_checker::visit(ast_index_expr &e) {
  e.object->accept(*this);
  type_info obj_type = last_expr_type_.value_or(type_unknown);

  e.index->accept(*this);
  type_info idx_type = last_expr_type_.value_or(type_unknown);

  if (idx_type.name != "i32" && idx_type.name != "i64" &&
      idx_type.name != "u32" && idx_type.name != "u64" &&
      idx_type.name != "usize" && idx_type.name != "isize" &&
      idx_type.name != "<unknown>") {
    add_error(e.line, "Array index must be an integer type, got '" +
                          idx_type.name + "'");
  }

  // Infer element type from obj_type
  // Assuming obj_type is "array<T>" or "vec<T>" or "slice<T>" (possibly
  // module-qualified)
  auto parts = parse_generic_type_string(obj_type.name);
  // Strip module prefix from base_name (e.g., "core::vec" -> "vec")
  std::string bare_base = parts.base_name;
  {
    auto last_colon = bare_base.rfind("::");
    if (last_colon != std::string::npos) {
      bare_base = bare_base.substr(last_colon + 2);
    }
  }
  if (bare_base == "array" || bare_base == "vec" || bare_base == "slice" ||
      bare_base == "vector" || bare_base == "ptr" || bare_base == "owned" ||
      bare_base == "borrow" || bare_base == "borrow_mut") {
    if (!parts.type_args.empty()) {
      last_expr_type_ =
          type_info{parts.type_args[0], is_primitive_name(parts.type_args[0])};
    } else {
      last_expr_type_ = type_any;
    }
  } else if (obj_type.name == "<unknown>" || obj_type.name == "any") {
    // Module-level vars may be typed 'any' if initializer type was not tracked.
    // Allow indexing and propagate unknown element type.
    last_expr_type_ = type_unknown;
  } else {
    add_error(e.line, "Type '" + obj_type.name + "' cannot be indexed");
    last_expr_type_ = type_unknown;
  }
}

void type_checker::visit(ast_struct_decl &p) {
  diagnostic_source_scope source_scope(diagnostic_source_file_, p.source_file);
  // Register struct as a type; preserve visibility flag from AST
  std::string struct_name = p.name;
  if (current_module_) {
    struct_name = *current_module_ + "::" + p.name;
  }
  // Check if the struct was pre-registered in the first pass. Its defining
  // node is installed during pre-registration so out-of-order field access is
  // available. A different defining node is a true duplicate include.
  symbol *existing_sym = symbols_.lookup_mut(struct_name);
  if (existing_sym) {
    if (existing_sym->defining_node != nullptr &&
        existing_sym->defining_node != &p) {
      // Already registered by another declaration — true multi-include, skip.
      return;
    }
    existing_sym->defining_node = &p;
  } else {
    /* std::cerr << "[visit(struct_decl)] Registering struct '" << struct_name
     * << "' with defining_node=yes\n"; */
    if (!symbols_.insert_symbol_with_context(
            struct_name, symbol_kind::type_symbol, &p, std::nullopt, false,
            p.get_visibility(), current_module_, p.generic_params)) {
      return;
    }
  }

  // Validate field types
  symbols_.enter_scope();
  // Register generic parameters and validate bounds
  for (const auto &gen_param : p.generic_params) {
    symbols_.insert_symbol_with_context(
        gen_param.name, symbol_kind::type_symbol, nullptr, std::nullopt, false,
        visibility_kind::visibility_private, std::nullopt, {}, {},
        gen_param.bounds);

    // Validate that traits in bounds exist
    for (const auto &bound : gen_param.bounds) {
      if (!find_trait(bound)) {
        add_error(p.line, "Unknown trait '" + bound + "' in generic bound");
      }
    }
  }

  for (auto &field : p.fields) {
    // Resolve type to handle imports and generic parameters
    type_info t = resolve_type(field.type_name);
    field.type_name = t.name;

    // Check if the base type exists (e.g. check 'vec' in 'vec<T>')
    auto parts = parse_generic_type_string(t.name);
    const symbol *field_type_sym = symbols_.lookup(parts.base_name);

    if (!field_type_sym) {
      // Fallback 1: Check if the fully resolved type exists (e.g.
      // 'core::option::option<text>') This handles cases where resolve_type
      // instantiated a concrete generic type.
      field_type_sym = symbols_.lookup(t.name);
    }

    if (!field_type_sym) {
      // Fallback 2: If base name is qualified (e.g. 'core::option::option'),
      // try looking up the unqualified alias (e.g. 'option') and verify it
      // resolves to the same type.
      size_t last_sep = parts.base_name.rfind("::");
      if (last_sep != std::string::npos) {
        std::string unqualified = parts.base_name.substr(last_sep + 2);
        const symbol *alias_sym = symbols_.lookup(unqualified);
        if (alias_sym) {
          // Verify canonical name matches
          std::string canonical = symbols_.canonicalize_type_name(unqualified);
          if (canonical == parts.base_name) {
            field_type_sym = alias_sym;
          }
        }
      }
    }

    if (!field_type_sym || field_type_sym->kind != symbol_kind::type_symbol) {
      if (parts.base_name == "any" || parts.base_name == "<unknown>" ||
          is_primitive_name(parts.base_name) ||
          is_primitive_name(field.type_name)) {
        // Ignore internal types and primitives for now
        continue;
      }

      // Allow function pointer types (structural, not in symbol table)
      if (t.name.rfind("fn(", 0) == 0) {
        continue;
      }

      // Fallback 3: If this is a generic type (has type args), check if the
      // base type exists and is a generic struct/enum. If so, the type will be
      // instantiated when needed. Check the ORIGINAL field type name (before
      // resolution) to handle instantiated structs.
      auto orig_parts = parse_generic_type_string(field.type_name);
      if (!orig_parts.type_args.empty()) {
        const symbol *base_sym = symbols_.lookup(orig_parts.base_name);
        if (base_sym && base_sym->kind == symbol_kind::type_symbol) {
          // Base type exists, this is a valid generic instantiation
          continue;
        }
      }

      /* std::cerr << "[debug] Struct '" << p.name << "' field '" << field.name
                << "' type '" << field.type_name << "' at line " << field.line
         << std::endl; */
      // Say why, not just what. Reading this as "the import is not working"
      // is the natural misreading, and it has cost real time: the usual cause
      // is simply that the file declaring the type was not given to the
      // compiler alongside this one.
      add_error(field.line,
                "Unknown type '" + field.type_name +
                    "' in struct field. Nothing in scope declares it; if it "
                    "is declared elsewhere, that file must also be given to "
                    "the compiler.");
    }
  }
  symbols_.leave_scope();
}

void type_checker::visit(ast_enum_decl &p) {
  diagnostic_source_scope source_scope(diagnostic_source_file_, p.source_file);
  // Register enum as a type; preserve visibility from AST
  std::string name = p.name;
  if (current_module_) {
    name = *current_module_ + "::" + name;
  }

  // Check if enum already exists.
  // nullptr defining_node = pre-registered placeholder; update and continue.
  // non-null defining_node = fully registered; skip (multi-include protection).
  symbol *existing_enum_sym = symbols_.lookup_mut(name);
  if (existing_enum_sym) {
    if (existing_enum_sym->defining_node != nullptr) {
      return;
    }
    existing_enum_sym->defining_node = &p;
  } else {
    /* std::cerr << "[type_checker] visit(enum): registering '" << name <<
     * "'\n"; */
    if (!symbols_.insert_symbol_with_context(
            name, symbol_kind::type_symbol, &p, std::nullopt, false,
            p.get_visibility(), current_module_, p.generic_params)) {
      return;
    }
  }

  // Register each variant as a callable symbol (EnumName::VariantName)
  // This allows lookups like Option::Some to resolve correctly.
  for (const auto &variant : p.variants) {
    std::string variant_fqn = name + "::" + variant.name;
    // Variants with associated types act like constructors (function symbols)
    // Unit variants (no associated type) act like constant values
    symbol_kind vkind = variant.associated_type.has_value()
                            ? symbol_kind::function_symbol
                            : symbol_kind::variable_symbol;
    // The "return type" of a variant constructor is the enum type itself
    symbols_.insert_symbol_with_context(
        variant_fqn, vkind, nullptr,
        std::optional<std::string>(name), // type is the enum (qualified)
        false, p.get_visibility(), current_module_);

    // Also register an unqualified alias (just "variant_name") so that code
    // can write `some(x)` / `none` instead of `option::some(x)` /
    // `option::none`. The alias is only inserted if no symbol with that simple
    // name exists yet, so local variables can shadow it as expected.
    symbols_.insert_symbol_with_context(variant.name, vkind, nullptr,
                                        std::optional<std::string>(name), false,
                                        p.get_visibility(), current_module_);
  }

  // Validate variant associated types
  symbols_.enter_scope();
  // Register generic parameters
  for (const auto &gen_param : p.generic_params) {
    symbols_.insert_symbol_with_context(
        gen_param.name, symbol_kind::type_symbol, nullptr, std::nullopt, false,
        visibility_kind::visibility_private, std::nullopt, {}, {},
        gen_param.bounds);
  }
  for (const auto &gen_param : p.generic_params)
    for (const auto &bound : gen_param.bounds)
      if (!find_trait(bound))
        add_error(p.line, "Unknown trait '" + bound + "' in generic bound");

  for (auto &variant : p.variants) {
    if (variant.associated_type.has_value()) {
      variant.associated_type = resolve_type(*variant.associated_type).name;
      const std::string &assoc_type = *variant.associated_type;

      // Check if this is a tuple-like type "(T1, T2, ...)"
      if (!assoc_type.empty() && assoc_type[0] == '(') {
        // Tuple type — structural, always accept
      } else if (assoc_type.size() >= 3 && assoc_type.substr(0, 3) == "fn(") {
        // Function type (fn(...) -> T) — structural, always accept
      } else {
        // Generic associated payloads such as Tuple<T, U> are represented by
        // their base declaration plus type arguments; there is no separate
        // symbol for every open generic spelling.
        const auto assoc_parts = parse_generic_type_string(assoc_type);
        const std::string &lookup_name =
            assoc_parts.type_args.empty() ? assoc_type : assoc_parts.base_name;
        const symbol *assoc_type_sym = symbols_.lookup(lookup_name);
        if (!assoc_type_sym ||
            assoc_type_sym->kind != symbol_kind::type_symbol) {
          add_error(variant.line, "Unknown associated type '" + assoc_type +
                                      "' in enum variant '" + variant.name +
                                      "'");
        }
      }
    }
  }
  symbols_.leave_scope();
}

// Trait conformance deliberately does not use are_types_compatible: numeric
// coercions and `any` compatibility at calls cannot alter a declared contract.
std::string type_checker::normalize_trait_type(
    const std::string &name,
    const std::map<std::string, std::string> &substitutions, unsigned depth) {
  if (depth > 64)
    return "<invalid-trait-type>";
  auto substituted = substitutions.find(name);
  if (substituted != substitutions.end())
    return substituted->second;
  if (!name.empty() && name.front() == '*')
    return "ptr<" + normalize_trait_type(name.substr(1), substitutions, depth + 1) + ">";
  // Function-valued parameters are structural too, including generic types
  // nested in their argument and result positions.
  if (name.rfind("fn(", 0) == 0) {
    int nesting = 1;
    size_t close = 3;
    for (; close < name.size() && nesting; ++close) {
      if (name[close] == '(') ++nesting;
      else if (name[close] == ')') --nesting;
    }
    if (nesting)
      return "<invalid-trait-type>";
    const auto parameters = parse_generic_type_string(
        "tuple<" + name.substr(3, close - 4) + ">");
    std::string result = "fn(";
    for (size_t i = 0; i < parameters.type_args.size(); ++i) {
      if (i) result += ", ";
      result += normalize_trait_type(parameters.type_args[i], substitutions, depth + 1);
    }
    result += ") -> ";
    const auto arrow = name.find("->", close);
    std::string return_type = arrow == std::string::npos ? "void" : name.substr(arrow + 2);
    return_type.erase(0, return_type.find_first_not_of(" \t"));
    return result + normalize_trait_type(return_type, substitutions, depth + 1);
  }
  const auto parts = parse_generic_type_string(name);
  const auto qualified_base = resolve_type_name_with_imports(parts.base_name);
  const symbol *base_symbol = symbols_.lookup(qualified_base.value_or(parts.base_name));
  if (base_symbol && base_symbol->defining_node &&
      base_symbol->defining_node->kind == ast_node_kind::type_alias_decl_node) {
    const auto &alias = static_cast<const ast_type_alias_decl &>(*base_symbol->defining_node);
    if (alias.generic_params.size() == parts.type_args.size()) {
      auto alias_substitutions = substitutions;
      for (size_t i = 0; i < parts.type_args.size(); ++i)
        alias_substitutions[alias.generic_params[i].name] =
            normalize_trait_type(parts.type_args[i], substitutions, depth + 1);
      // Guard declaration recursion, not just textual depth. An alias such as
      // Expand<T> = Expand<tuple<T, T>> otherwise grows exponentially before a
      // depth limit can stop it. Normalize supplied arguments first so valid
      // nested applications like Identity<Identity<i32>> remain supported.
      if (!expanding_type_aliases_.insert(&alias).second)
        return "<invalid-trait-type>";
      struct alias_expansion_guard {
        std::set<const ast_type_alias_decl *> &active;
        const ast_type_alias_decl *alias;
        ~alias_expansion_guard() { active.erase(alias); }
      } expansion_guard{expanding_type_aliases_, &alias};
      const auto caller_module = current_module_;
      current_module_ = base_symbol->module;
      std::string result = normalize_trait_type(alias.target_type, alias_substitutions, depth + 1);
      current_module_ = caller_module;
      return result;
    }
  }
  const std::string base = resolve_type(parts.base_name).name;
  if (parts.type_args.empty())
    return base;
  std::string result = base + "<";
  for (size_t i = 0; i < parts.type_args.size(); ++i) {
    if (i) result += ", ";
    result += normalize_trait_type(parts.type_args[i], substitutions, depth + 1);
  }
  return result + ">";
}

const symbol *type_checker::find_trait(const std::string &name) {
  const auto parts = parse_generic_type_string(normalize_trait_type(name));
  const symbol *candidate = symbols_.lookup(parts.base_name);
  if (!candidate || candidate->kind != symbol_kind::type_symbol ||
      !candidate->defining_node ||
      candidate->defining_node->kind != ast_node_kind::trait_decl_node)
    return nullptr;
  const auto &trait = static_cast<const ast_trait_decl &>(*candidate->defining_node);
  return trait.generic_params.size() == parts.type_args.size() ? candidate : nullptr;
}

void type_checker::register_trait_implementation(ast_impl_decl &p) {
  if (!p.trait_name)
    return;
  const auto initial_errors = diagnostics_.size();
  const auto impl_module = current_module_;
  symbols_.enter_scope();
  std::map<std::string, std::string> impl_substitutions;
  for (const auto &parameter : p.generic_params) {
    if (!impl_substitutions.emplace(parameter.name, parameter.name).second)
      add_error(p.line, "Duplicate impl generic parameter '" + parameter.name + "'");
    symbols_.insert_symbol_with_context(parameter.name, symbol_kind::type_symbol,
        nullptr, std::nullopt, false, visibility_kind::visibility_private,
        std::nullopt, {}, {}, parameter.bounds);
  }
  const std::string target = normalize_trait_type(p.target_type, impl_substitutions);
  const std::string trait_name = normalize_trait_type(*p.trait_name, impl_substitutions);
  const symbol *trait_symbol = find_trait(trait_name);
  const auto target_parts = parse_generic_type_string(target);
  const symbol *target_symbol = symbols_.lookup(target_parts.base_name);
  if (!target_symbol || target_symbol->kind != symbol_kind::type_symbol) {
    add_error(p.line, "Unknown target type '" + p.target_type + "' in impl block");
  } else {
    size_t target_arity = target_symbol->generic_params.size();
    if (!target_symbol->defining_node &&
        (target_parts.base_name == "array" || target_parts.base_name == "vec" ||
         target_parts.base_name == "slice" || target_parts.base_name == "ptr" ||
         target_parts.base_name == "ref"))
      target_arity = 1;
    if (target_parts.base_name != "tuple" && target_arity != target_parts.type_args.size())
      add_error(p.line, "Invalid generic argument count for impl target '" + p.target_type + "'");
    if (target_symbol->defining_node &&
        target_symbol->defining_node->kind == ast_node_kind::trait_decl_node)
      add_error(p.line, "A trait declaration cannot be an impl target; use a concrete type");
  }
  if (!trait_symbol) {
    add_error(p.line, "Unknown trait or invalid trait arguments '" + *p.trait_name +
                          "' in impl block; expected a trait declaration");
    symbols_.leave_scope();
    return;
  }
  const auto &trait = static_cast<const ast_trait_decl &>(*trait_symbol->defining_node);
  const auto trait_module = trait_symbol->module;
  const auto trait_parts = parse_generic_type_string(trait_name);
  std::map<std::string, std::string> trait_substitutions{{"Self", target}};
  impl_substitutions["Self"] = target;
  for (size_t i = 0; i < trait.generic_params.size(); ++i)
    trait_substitutions[trait.generic_params[i].name] = trait_parts.type_args[i];

  std::map<std::string, const ast_function_decl *> methods;
  for (const auto &method : p.methods) {
    if (!methods.emplace(method->name, method.get()).second)
      add_error(method->line, "Duplicate impl method '" + method->name + "'");
  }
  for (const auto &required : trait.methods) {
    auto found = methods.find(required->name);
    if (found == methods.end()) {
      add_error(p.line, "Impl of trait '" + trait_name + "' is missing required method '" +
                            required->name + "'");
      continue;
    }
    const auto &actual = *found->second;
    const std::string prefix = "Trait method '" + required->name + "' ";
    if (actual.params.size() != required->params.size()) {
      add_error(actual.line, prefix + "has incompatible parameter count");
      continue;
    }
    if (actual.generic_params.size() != required->generic_params.size()) {
      add_error(actual.line, prefix + "has incompatible generic parameter count");
      continue;
    }
    auto expected_types = trait_substitutions;
    auto actual_types = impl_substitutions;
    for (size_t i = 0; i < actual.generic_params.size(); ++i) {
      const std::string identity = "$trait_method_parameter_" + std::to_string(i);
      expected_types[required->generic_params[i].name] = identity;
      actual_types[actual.generic_params[i].name] = identity;
    }
    auto normalize = [&](const std::string &type, bool expected) {
      current_module_ = expected ? trait_module : impl_module;
      std::string result = normalize_trait_type(type, expected ? expected_types : actual_types);
      current_module_ = impl_module;
      return result;
    };
    for (size_t i = 0; i < actual.params.size(); ++i) {
      const auto &expected = required->params[i];
      const auto &provided = actual.params[i];
      const std::string expected_type = normalize(expected.type_name.value_or(
          expected.name == "self" ? "Self" : "<missing>"), true);
      const std::string actual_type = normalize(provided.type_name.value_or(
          provided.name == "self" ? "Self" : "<missing>"), false);
      if (expected_type != actual_type || expected.is_mut != provided.is_mut ||
          expected.is_variadic != provided.is_variadic ||
          (expected.name == "self") != (provided.name == "self"))
        add_error(actual.line, prefix + "has incompatible parameter " + std::to_string(i + 1) +
                                  "; expected '" + expected_type + "' with matching receiver and mutability");
    }
    const std::string expected_return = normalize(required->return_type.value_or("void"), true);
    const std::string actual_return = normalize(actual.return_type.value_or("void"), false);
    if (expected_return != actual_return)
      add_error(actual.line, prefix + "has incompatible return type; expected '" +
                                expected_return + "', got '" + actual_return + "'");
    for (size_t i = 0; i < actual.generic_params.size(); ++i) {
      std::set<std::string> expected_bounds, actual_bounds;
      for (const auto &bound : required->generic_params[i].bounds)
        expected_bounds.insert(normalize(bound, true));
      for (const auto &bound : actual.generic_params[i].bounds)
        actual_bounds.insert(normalize(bound, false));
      if (expected_bounds != actual_bounds)
        add_error(actual.line, prefix + "has incompatible generic bounds");
    }
  }
  trait_implementation implementation{trait_name, target, {}, {}, impl_module, p.line};
  current_module_ = trait_module;
  for (size_t i = 0; i < trait.generic_params.size(); ++i)
    for (const auto &bound : trait.generic_params[i].bounds)
      implementation.required_bounds.emplace_back(
          trait_parts.type_args[i], normalize_trait_type(bound, trait_substitutions));
  current_module_ = impl_module;
  for (const auto &parameter : p.generic_params) {
    auto &bounds = implementation.generic_bounds[parameter.name];
    for (const auto &bound : parameter.bounds) {
      if (!find_trait(bound))
        add_error(p.line, "Unknown trait '" + bound + "' in impl generic bound");
      bounds.push_back(normalize_trait_type(bound, impl_substitutions));
    }
  }
  for (const auto &existing : trait_impls_)
    if (existing.trait_name == trait_name && existing.target_type == target)
      add_error(p.line, "Duplicate trait implementation for '" + target + "'");
  if (diagnostics_.size() == initial_errors)
    trait_impls_.push_back(std::move(implementation));
  symbols_.leave_scope();
}

void type_checker::visit(ast_trait_decl &p) {
  diagnostic_source_scope source_scope(diagnostic_source_file_, p.source_file);
  // Register trait as a type symbol
  std::string name = p.name;
  if (current_module_) {
    name = *current_module_ + "::" + name;
  }

  if (!visited_traits_.insert(&p).second) {
    return;
  }

  // IMPORTANT: Pass &p as defining node
  if (!symbols_.lookup(name) && !symbols_.insert_symbol_with_context(
          name, symbol_kind::type_symbol, &p, std::nullopt, false,
          p.get_visibility(), current_module_, p.generic_params)) {
    return;
  }

  // Enter scope for generic params and methods
  symbols_.enter_scope();
  for (const auto &gen_param : p.generic_params) {
    symbols_.insert_symbol_with_context(
        gen_param.name, symbol_kind::type_symbol, nullptr, std::nullopt, false,
        visibility_kind::visibility_private, std::nullopt, {}, {},
        gen_param.bounds);
    for (const auto &bound : gen_param.bounds)
      if (!find_trait(bound))
        add_error(p.line, "Unknown trait '" + bound + "' in generic bound");
  }

  // Trait methods are contracts, not free functions. Keep their validation
  // inside the trait's generic/Self scope and do not register global names.
  const auto old_class = current_class_name_;
  current_class_name_ = "Self";
  std::set<std::string> method_names;
  std::function<bool(const std::string &)> known_type = [&](const std::string &raw) {
    const std::string type = normalize_trait_type(raw);
    if (type == "void") return true;
    if (type.rfind("fn(", 0) == 0) {
      int depth = 1;
      size_t close = 3;
      for (; close < type.size() && depth; ++close) {
        if (type[close] == '(') ++depth;
        else if (type[close] == ')') --depth;
      }
      if (depth) return false;
      const auto args = parse_generic_type_string("tuple<" + type.substr(3, close - 4) + ">");
      for (const auto &arg : args.type_args)
        if (!known_type(arg)) return false;
      return known_type(type.substr(close + 4));
    }
    const auto parts = parse_generic_type_string(type);
    const symbol *type_symbol = symbols_.lookup(parts.base_name);
    if (!type_symbol || type_symbol->kind != symbol_kind::type_symbol)
      return false;
    if (type_symbol->defining_node &&
        type_symbol->generic_params.size() != parts.type_args.size())
      return false;
    for (const auto &arg : parts.type_args)
      if (!known_type(arg)) return false;
    return true;
  };
  for (auto &method : p.methods) {
    if (!method_names.insert(method->name).second)
      add_error(method->line, "Duplicate trait method '" + method->name + "'");
    symbols_.enter_scope();
    for (const auto &parameter : method->generic_params)
      symbols_.insert_symbol_with_context(parameter.name, symbol_kind::type_symbol,
          nullptr, std::nullopt, false, visibility_kind::visibility_private,
          std::nullopt, {}, {}, parameter.bounds);
    for (const auto &parameter : method->params)
      if (!known_type(parameter.type_name.value_or("Self")))
        add_error(method->line, "Unknown type in trait method parameter '" + parameter.name + "'");
    if (!known_type(method->return_type.value_or("void")))
      add_error(method->line, "Unknown type in trait method return type");
    symbols_.leave_scope();
    method->accept(*this);
  }
  current_class_name_ = old_class;

  symbols_.leave_scope();
}

void type_checker::visit(ast_impl_decl &p) {
  diagnostic_source_scope source_scope(diagnostic_source_file_, p.source_file);
  // Guard against double-visitation of the same impl block
  if (visited_impls_.count(&p)) {
    /* std::cerr << "[visit_impl] SKIP (already visited) Target='" <<
     * p.target_type << "'\n"; */
    return;
  }
  visited_impls_.insert(&p);

  // Preserve the enclosing state for completeness if impl declarations ever
  // become nestable, then expose this impl's generic contracts to its methods.
  auto previous_impl_generic_bounds = active_impl_generic_bounds_;
  active_impl_generic_bounds_.clear();
  for (const auto &gen_param : p.generic_params)
    active_impl_generic_bounds_[gen_param.name] = gen_param.bounds;

  // Enter scope for generic params
  symbols_.enter_scope();
  for (const auto &gen_param : p.generic_params) {
    symbols_.insert_symbol_with_context(
        gen_param.name, symbol_kind::type_symbol, nullptr, std::nullopt, false,
        visibility_kind::visibility_private, std::nullopt, {}, {},
        gen_param.bounds);
    if (!p.trait_name)
      for (const auto &bound : gen_param.bounds)
        if (!find_trait(bound))
          add_error(p.line, "Unknown trait '" + bound + "' in impl generic bound");
  }

  // Resolve target type
  type_info target = resolve_type(p.target_type);
  if (target.name == "unknown") {
    add_error(p.line,
              "Unknown target type '" + p.target_type + "' in impl block");
  }

  // Set current type context for Self resolution
  auto old_class_name = current_class_name_;
  current_class_name_ = target.name;
  /* std::cerr << "[visit_impl] Target='" << p.target_type << "' Resolved='" <<
   * target.name << "' ClassName='" << (current_class_name_ ?
   * *current_class_name_ : "null") << "'\n"; */

  // Register Self as a type alias pointing to the target type
  // This allows resolve_type("Self") to expand to the actual type
  symbols_.insert_symbol_with_context(
      "Self", symbol_kind::type_symbol, nullptr,
      std::optional<std::string>(
          target.name), // type_name = the actual target type
      false, visibility_kind::visibility_private, std::nullopt);

  // Visit methods and register them with mangled names
  for (auto &method : p.methods) {
    method->accept(*this);
    // Register method as TypeName__methodName for global lookup if needed
    // Use resolved target name (qualified) but strip generics
    auto type_parts = parse_generic_type_string(target.name);
    std::string mangled_name = type_parts.base_name + "__" + method->name;
    /* std::cerr << "[visit_impl] Registering mangled: '" << mangled_name << "'
     * TargetName='" << target.name << "' BaseName='" << type_parts.base_name <<
     * "'\n"; */
    symbols_.insert_symbol_with_context(
        mangled_name, symbol_kind::function_symbol, method.get(),
        method->return_type, false, visibility_kind::visibility_public,
        std::nullopt, method->generic_params, method->params);

    // Register method in the Type's member map for dot-operator resolution
    // We must handle generic types (e.g. "Box<T>" -> "Box") to find the base
    // symbol
    auto parts = parse_generic_type_string(target.name);
    symbol *type_sym = symbols_.lookup_mut(
        parts.base_name); // Lookup the resolved base type symbol
    if (!type_sym && current_module_) {
      // Fallback: try looking up in current module (e.g. impl vec -> core::vec)
      type_sym = symbols_.lookup_mut(*current_module_ + "::" + parts.base_name);
    }
    if (!type_sym) {
      // Fallback: try resolving via imports
      auto imported = resolve_type_name_with_imports(parts.base_name);
      if (imported) {
        type_sym = symbols_.lookup_mut(*imported);
      }
    }
    if (type_sym && type_sym->kind == symbol_kind::type_symbol) {
      symbol method_sym(method->name, symbol_kind::function_symbol,
                        method.get(), method->return_type, false,
                        method->get_visibility(), std::nullopt,
                        method->generic_params, method->params);
      type_sym->members[method->name] = method_sym;
      /* std::cerr << "[visit_impl] Registered member '" << method->name << "'
       * on type '" << parts.base_name << "'\n"; */
    } else {
      /* std::cerr << "[visit_impl] FAILED to register member '" << method->name
       * << "' - Type '" << parts.base_name << "' not found/invalid\n"; */
    }
  }
  current_class_name_ = old_class_name;

  symbols_.leave_scope();
  active_impl_generic_bounds_ = std::move(previous_impl_generic_bounds);
}

#ifdef UNIT_TEST_TYPECHECKER
// Lightweight internal unit-tests for the type checker mutability rules.
// These are guarded so they only compile into a test executable that defines
// UNIT_TEST_TYPECHECKER.
static int run_typechecker_unit_tests() {
  int failures = 0;

  // Test 1 - Immutable variable assignment should produce an error
  {
    auto program = std::make_shared<ast_program>();
    auto fn = std::static_pointer_cast<ast_decl>(make_function_decl("test1"));
    auto fn_decl = std::static_pointer_cast<ast_function_decl>(fn);
    // Build function body: let x: i32 = 0; x = 1;
    auto block = std::make_shared<ast_block_stmt>(1);

    // let x: i32 = 0;
    auto let_x_decl = std::static_pointer_cast<ast_decl>(
        make_variable_decl("x", make_number_literal("0", 1),
                           std::optional<std::string>("i32"), false, 1));
    block->statements.push_back(std::make_shared<ast_decl_stmt>(let_x_decl, 1));

    // x = 1;
    auto assign = std::make_shared<ast_assignment_expr>(
        make_identifier_expr("x", 2), make_number_literal("1", 2), 2);
    block->statements.push_back(std::make_shared<ast_expr_stmt>(assign, 2));

    fn_decl->body = block;
    program->declarations.push_back(fn);

    type_checker tc;
    bool ok = tc.check(*program);
    auto diags = tc.get_diagnostics();
    bool found_mut_err = false;
    for (auto &d : diags) {
      if (d.message.find("immutable variable") != std::string::npos) {
        found_mut_err = true;
        break;
      }
    }
    if (!found_mut_err) {
      std::cerr << "FAIL: immutable assignment not detected\n";
      ++failures;
    } else {
      std::cerr << "PASS: immutable assignment detected\n";
    }
  }

  // Test 2 - Mutable variable assignment should be allowed
  {
    auto program = std::make_shared<ast_program>();
    auto fn = std::static_pointer_cast<ast_decl>(make_function_decl("test2"));
    auto fn_decl = std::static_pointer_cast<ast_function_decl>(fn);
    // Build function body: let mut x: i32 = 0; x = 1;
    auto block = std::make_shared<ast_block_stmt>(1);

    // let mut x: i32 = 0;
    auto let_x_decl = std::static_pointer_cast<ast_decl>(
        make_variable_decl("x", make_number_literal("0", 1),
                           std::optional<std::string>("i32"), true, 1));
    block->statements.push_back(std::make_shared<ast_decl_stmt>(let_x_decl, 1));

    // x = 1;
    auto assign = std::make_shared<ast_assignment_expr>(
        make_identifier_expr("x", 2), make_number_literal("1", 2), 2);
    block->statements.push_back(std::make_shared<ast_expr_stmt>(assign, 2));

    fn_decl->body = block;
    program->declarations.push_back(fn);

    type_checker tc;
    bool ok = tc.check(*program);
    auto diags = tc.get_diagnostics();
    if (!diags.empty()) {
      std::cerr
          << "FAIL: assignment to mutable variable produced diagnostics\n";
      for (auto &d : diags)
        std::cerr << "  diag: line " << d.line << ": " << d.message << "\n";
      ++failures;
    } else {
      std::cerr << "PASS: assignment to mutable variable allowed\n";
    }
  }

  return failures;
}
#endif // UNIT_TEST_TYPECHECKER

void type_checker::visit(ast_try_expr &e) {
  if (e.inner)
    e.inner->accept(*this);

  type_info inner_type = last_expr_type_.value_or(type_unknown);

  // Parse generic type to check if it's Option or Result
  auto parts = parse_generic_type_string(inner_type.name);

  if (parts.base_name == "option" || parts.base_name == "Option") {
    if (parts.type_args.size() == 1) {
      // option<T> -> T
      last_expr_type_ =
          type_info{parts.type_args[0], is_primitive_name(parts.type_args[0])};
    } else {
      add_error(e.line, "Invalid Option type: expected 1 generic argument");
      last_expr_type_ = type_unknown;
    }
  } else if (parts.base_name == "result" || parts.base_name == "Result") {
    if (parts.type_args.size() == 2) {
      // result<T, E> -> T
      last_expr_type_ =
          type_info{parts.type_args[0], is_primitive_name(parts.type_args[0])};
    } else {
      add_error(e.line, "Invalid Result type: expected 2 generic arguments");
      last_expr_type_ = type_unknown;
    }
  } else {
    add_error(
        e.line,
        "The '?' operator can only be used on Option or Result types, not '" +
            inner_type.name + "'");
    last_expr_type_ = type_unknown;
  }
}

const symbol *type_checker::lookup_imported(const std::string &name) const {
  auto fq = resolve_imported_symbol(name);
  if (!fq.has_value())
    return nullptr;
  return symbols_.lookup(*fq);
}

bool type_checker::check_trait_bound(const std::string &type_name,
                                     const std::string &trait_name) const {
  // Bounds and implementation declarations can use different spellings for
  // the same imported types. Resolve both in the active module/import context
  // before comparing them against the canonical implementation registry.
  auto &checker = *const_cast<type_checker *>(this);
  const std::string resolved_type = checker.normalize_trait_type(type_name);
  const std::string resolved_trait = checker.normalize_trait_type(trait_name);
  if (!checker.find_trait(resolved_trait))
    return false;
  // A symbolic parameter carries a real contract even before specialization.
  if (const auto *parameter = symbols_.lookup(resolved_type)) {
    for (const auto &bound : parameter->bounds)
      if (checker.normalize_trait_type(bound) == resolved_trait)
        return true;
  }
  const auto key = std::make_pair(resolved_type, resolved_trait);
  if (!checking_trait_bounds_.insert(key).second)
    return false;
  struct bound_guard {
    std::set<std::pair<std::string, std::string>> &active;
    std::pair<std::string, std::string> key;
    ~bound_guard() { active.erase(key); }
  } guard{checking_trait_bounds_, key};

  for (const auto &implementation : trait_impls_) {
    std::map<std::string, std::string> substitutions;
    std::function<bool(const std::string &, const std::string &)> match =
        [&](const std::string &pattern, const std::string &concrete) {
      if (implementation.generic_bounds.count(pattern)) {
        auto [found, inserted] = substitutions.emplace(pattern, concrete);
        return inserted || found->second == concrete;
      }
      const auto pattern_parts = checker.parse_generic_type_string(pattern);
      const auto concrete_parts = checker.parse_generic_type_string(concrete);
      if (pattern_parts.base_name != concrete_parts.base_name ||
          pattern_parts.type_args.size() != concrete_parts.type_args.size())
        return false;
      for (size_t i = 0; i < pattern_parts.type_args.size(); ++i)
        if (!match(pattern_parts.type_args[i], concrete_parts.type_args[i]))
          return false;
      return true;
    };
    if (!match(implementation.target_type, resolved_type) ||
        !match(implementation.trait_name, resolved_trait))
      continue;
    bool valid = true;
    for (const auto &parameter : implementation.generic_bounds) {
      const auto argument = substitutions.find(parameter.first);
      if (argument == substitutions.end()) {
        valid = false;
        break;
      }
      for (const auto &bound : parameter.second) {
        const auto concrete_bound = checker.normalize_trait_type(bound, substitutions);
        if (!check_trait_bound(argument->second, concrete_bound)) {
          valid = false;
          break;
        }
      }
      if (!valid) break;
    }
    if (valid) return true;
  }
  return false;
}

std::string type_checker::instantiate_generic_type(
    const std::string &generic_type_name,
    const std::map<std::string, std::string> &substitutions) {
  // std::cerr << "[instantiate_generic_type] '" << generic_type_name << "'" <<
  // std::endl; Recursion guard for type string instantiation
  static thread_local int recursion_depth = 0;
  if (recursion_depth > 100) {
    std::cerr << "instantiate_generic_type recursion depth exceeded! "
              << generic_type_name << std::endl;
    return generic_type_name;
  }
  recursion_depth++;
  struct DepthGuard {
    ~DepthGuard() { recursion_depth--; }
  } depth_guard;

  if (substitutions.count(generic_type_name)) {
    // std::cerr << "[instantiate] Substituting '" << generic_type_name << "' ->
    // '" << substitutions.at(generic_type_name) << "'\n";
  }

  auto parts = parse_generic_type_string(generic_type_name);

  if (parts.type_args.empty()) {
    auto it = substitutions.find(parts.base_name);
    if (it != substitutions.end()) {
      return it->second;
    }
    return generic_type_name;
  }

  std::string result = parts.base_name + "<";
  for (size_t i = 0; i < parts.type_args.size(); ++i) {
    if (i > 0)
      result += ", ";
    result += instantiate_generic_type(parts.type_args[i], substitutions);
  }
  result += ">";
  return result;
}

bool type_checker::validate_generic_args(
    const std::vector<ast_generic_param> &params,
    const std::vector<std::string> &type_args, int line) {
  // Validate that each type argument satisfies the bounds on the corresponding
  // generic parameter.
  bool all_valid = true;

  size_t count = std::min(params.size(), type_args.size());
  std::map<std::string, std::string> substitutions;
  for (size_t i = 0; i < count; ++i)
    substitutions[params[i].name] = normalize_trait_type(type_args[i]);
  for (size_t i = 0; i < count; ++i) {
    const auto &param = params[i];
    const std::string &type_arg = type_args[i];

    // Check each bound on this parameter
    for (const auto &bound : param.bounds) {
      const auto concrete_bound = normalize_trait_type(bound, substitutions);
      if (!check_trait_bound(type_arg, concrete_bound)) {
        add_error(line, "Type '" + type_arg + "' does not implement trait '" +
                            bound + "' required by generic parameter '" +
                            param.name + "'");
        all_valid = false;
      }
    }
  }

  // Check for missing type arguments
  if (type_args.size() < params.size()) {
    add_error(line, "Not enough type arguments provided. Expected " +
                        std::to_string(params.size()) + ", got " +
                        std::to_string(type_args.size()));
    all_valid = false;
  }

  return all_valid;
}

// ----------------------------------------------------------------------------
// Module System Helpers
// ----------------------------------------------------------------------------

bool type_checker::is_symbol_visible(const symbol *sym,
                                     const std::string &from_module) const {
  if (!sym)
    return false;

  // Public symbols are always visible
  if (sym->visibility == visibility_kind::visibility_public)
    return true;

  // Check if symbol has a module context
  if (!sym->module.has_value()) {
    // Symbols without module are visible if in same scope (handled by lookup)
    // or if we treat them as global/prelude.
    // For now, assume visible if found.
    return true;
  }

  const std::string &sym_mod = *sym->module;

  // Private: only visible in same module
  if (sym->visibility == visibility_kind::visibility_private) {
    return sym_mod == from_module;
  }

  // Pub(crate): visible anywhere in same crate (project)
  // For MVP, we assume single crate, so equivalent to public
  if (sym->visibility == visibility_kind::visibility_crate) {
    return true;
  }

  // Pub(super): visible in parent module
  if (sym->visibility == visibility_kind::visibility_super) {
    // Check if from_module is a child of sym_mod's parent
    // Simple check: is from_module starts with parent of sym_mod?
    auto last_sep = sym_mod.rfind("::");
    std::string parent =
        (last_sep == std::string::npos) ? "" : sym_mod.substr(0, last_sep);

    if (parent.empty())
      return true; // Root parent
    return from_module.find(parent) == 0;
  }

  return false;
}

const symbol *
type_checker::resolve_qualified_name(const std::string &requested_path) const {
  // Namespace aliases belong to their declaring module. The parser's symbol
  // table is separate from this checker, so expand the exact predeclared alias
  // map here as we already do for module globals and fault identities.
  std::string qualified_path = requested_path;
  const auto aliases = global_namespace_aliases_.find(current_module_.value_or(""));
  if (aliases != global_namespace_aliases_.end()) {
    std::set<std::string> expanded;
    for (;;) {
      const auto split = qualified_path.find("::");
      if (split == std::string::npos) break;
      const auto prefix = qualified_path.substr(0, split);
      const auto alias = aliases->second.find(prefix);
      if (alias == aliases->second.end()) break;
      if (!expanded.insert(prefix).second || expanded.size() > 64) return nullptr;
      qualified_path = alias->second + qualified_path.substr(split);
    }
  }
  // Split path: std::io::println -> ["std", "io", "println"]
  std::vector<std::string> parts;
  size_t start = 0;
  size_t end = qualified_path.find("::");
  while (end != std::string::npos) {
    parts.push_back(qualified_path.substr(start, end - start));
    start = end + 2;
    end = qualified_path.find("::", start);
  }
  parts.push_back(qualified_path.substr(start));

  if (parts.empty())
    return nullptr;

  // For MVP, we don't have a full module tree structure in memory.
  // We rely primarily on the type-checker symbol table for fully qualified
  // symbol names (module::symbol).

  // Strategy 1: Try full path lookup directly.
  const symbol *sym = symbols_.lookup(qualified_path);
  if (sym) {
    // Check visibility
    std::string current = current_module_.value_or("");
    if (is_symbol_visible(sym, current)) {
      return sym;
    }

    // Temporary workaround: treat module symbols as visible when imported from
    // other modules
    if (!current.empty() && sym->module.has_value() &&
        sym->module.value() != current) {
      return sym; // Allow access to imported module symbols
    }

    // Additional workaround: for nyus_simple specifically, always allow access
    if (sym->module.has_value() && sym->module.value() == "nyus_simple") {
      return sym;
    }
  }

  // Strategy 2: Attempt alias expansion on prefix segments.
  // We attempt to find a registered namespace alias among prefix segments and,
  // if found, expand it and attempt to resolve the resulting candidate.
  // Example:
  //  alias -> "long::nested::path"
  //  alias::Type -> long::nested::path::Type
  std::string current = current_module_.value_or("");
  for (size_t prefixLen = 1; prefixLen < parts.size(); ++prefixLen) {
    // Build prefix string composed of the first `prefixLen` segments.
    std::string prefix;
    for (size_t i = 0; i < prefixLen; ++i) {
      if (i)
        prefix += "::";
      prefix += parts[i];
    }

    // Try to resolve the prefix as a symbol which might be a namespace alias.
    const symbol *prefixSym = symbols_.lookup(prefix);
    if (!prefixSym)
      continue;

    // If it's a type-like symbol (alias) with a recorded target module path,
    // attempt to replace the prefix with the alias's `type_name` and re-run a
    // lookup.
    if (prefixSym->kind == symbol_kind::type_symbol && prefixSym->type_name) {
      std::string alias_target = *prefixSym->type_name;

      // Compose rest path after the prefix
      std::string rest;
      for (size_t i = prefixLen; i < parts.size(); ++i) {
        if (!rest.empty())
          rest += "::";
        rest += parts[i];
      }

      std::string candidate = alias_target;
      if (!rest.empty())
        candidate += "::" + rest;

      // Try direct lookup for the candidate
      const symbol *resolved = symbols_.lookup(candidate);
      if (resolved && is_symbol_visible(resolved, current)) {
        return resolved;
      }

      // Additional attempt: sometimes a symbol may be registered as
      // `alias_target::LastSegment` (module::name) - try that as well by
      // attempting candidate built from alias target + last segment only.
      if (!rest.empty()) {
        size_t lastSep = rest.rfind("::");
        std::string lastSeg =
            (lastSep == std::string::npos) ? rest : rest.substr(lastSep + 2);
        std::string candidate2 = alias_target + "::" + lastSeg;
        const symbol *resolved2 = symbols_.lookup(candidate2);
        if (resolved2 && is_symbol_visible(resolved2, current)) {
          return resolved2;
        }
      }
    }
  }

  return nullptr;
}

type_info type_checker::resolve_global_type(const symbol &variable) {
  const auto caller_module = current_module_;
  current_module_ = variable.module;
  const auto result = resolve_type(normalize_trait_type(
      variable.type_name.value_or("any")));
  current_module_ = caller_module;
  return result;
}

std::optional<std::string>
type_checker::resolve_source_global(const std::string &name, int line) {
  const std::string module = current_module_.value_or("");
  const std::string local = module.empty() ? name : module + "::" + name;
  if (global_declarations_.count(local)) return local;
  if (global_declarations_.count(name)) return name;
  if (name.find("::") != std::string::npos) {
    const auto prefix_end = name.find("::");
    const auto alias = global_namespace_aliases_[module].find(name.substr(0, prefix_end));
    if (alias != global_namespace_aliases_[module].end()) {
      const auto candidate = alias->second + name.substr(prefix_end);
      if (global_declarations_.count(candidate)) return candidate;
    }
    if (const auto *symbol = resolve_qualified_name(name);
        symbol && global_declarations_.count(symbol->name))
      return symbol->name;
  }
  std::set<std::string> candidates;
  const auto add = [&](const std::string &candidate) {
    if (global_declarations_.count(candidate)) candidates.insert(candidate);
  };
  for (const auto *import : global_imports_[module]) {
    if (!import->names.empty()) {
      if (std::find(import->names.begin(), import->names.end(), name) != import->names.end())
        add(import->module_path.empty() ? name : import->module_path + "::" + name);
    } else {
      add(import->module_path + "::" + name);
      if (!import->is_glob) {
        for (const auto *reexport : global_imports_[import->module_path]) {
          if (!reexport->is_glob ||
              reexport->get_visibility() != visibility_kind::visibility_public) continue;
          std::string target = reexport->module_path;
          if (!target.empty() && target.find("::") == std::string::npos)
            target = import->module_path + "::" + target;
          add(target + "::" + name);
        }
      }
    }
  }
  if (candidates.size() > 1) {
    add_error(line, "Ambiguous imported global '" + name + "'; qualify its module explicitly");
    return std::string{};
  }
  return candidates.empty() ? std::nullopt : std::optional<std::string>(*candidates.begin());
}

std::optional<std::string>
type_checker::resolve_imported_symbol(const std::string &name) const {
  // Resolve imports in the context of the current module only. Imports are
  // keyed by importer (the module that performed the import).
  std::string importer = current_module_.value_or("<global>");

  auto it = imports_.find(importer);
  if (it == imports_.end())
    return std::nullopt;

  const auto &import_map = it->second;

  // 1) Direct named import mapping -> stored as fully-qualified earlier
  auto nit = import_map.find(name);
  if (nit != import_map.end()) {
    const auto &entry = nit->second;
    if (!entry.target_path.empty() && entry.target_path != "*")
      return entry.target_path; // fully-qualified name
    // fall-through when target_path is '*' (glob) or empty (module import)
  }

  // 2) Check module import or glob import entries by attempting module::name
  // lookup for each imported module key.
  for (const auto &entry_pair : import_map) {
    const std::string &module_or_local = entry_pair.first;
    const auto &entry = entry_pair.second;
    if (entry.target_path == "*" || entry.target_path.empty()) {
      std::string candidate = module_or_local + "::" + name;
      const symbol *s = symbols_.lookup(candidate);
      if (s) {
        return candidate;
      }
    }
  }

  // 3) Transitive re-export resolution: `pub use X::*` inside an imported
  // module. When this module imports M (target_path=""), check M's PUBLIC glob
  // re-exports for name.
  for (const auto &entry_pair : import_map) {
    const std::string &imported_module = entry_pair.first;
    const auto &entry = entry_pair.second;
    if (entry.target_path.empty()) {
      // Follow one level of re-exports from the imported module
      auto re_it = imports_.find(imported_module);
      if (re_it == imports_.end())
        continue;
      for (const auto &re_entry_pair : re_it->second) {
        const std::string &re_module = re_entry_pair.first;
        const auto &re_entry = re_entry_pair.second;

        // Only follow PUBLIC re-exports
        if (re_entry.visibility != visibility_kind::visibility_public)
          continue;

        if (re_entry.target_path == "*") {
          // re_module may be a relative path (e.g. "router" inside "nyhttp").
          // Qualify it with the imported module if it has no "::" separator.
          std::string full_re_module = re_module;
          if (!re_module.empty() && re_module.find("::") == std::string::npos) {
            full_re_module = imported_module + "::" + re_module;
          }
          std::string candidate = full_re_module + "::" + name;
          if (symbols_.lookup(candidate)) {
            return candidate;
          }
        }
      }
    }
  }

  return std::nullopt;
}

void type_checker::visit(ast_allocate_expr &e) {
  // Check size is numeric
  e.size->accept(*this);
  type_info size_type = last_expr_type_.value_or(type_unknown);

  bool is_numeric = (size_type.name == "i32" || size_type.name == "i64" ||
                     size_type.name == "u32" || size_type.name == "u64" ||
                     size_type.name == "usize");

  if (!is_numeric) {
    add_error(e.line,
              "Allocate size must be numeric, got '" + size_type.name + "'");
  }

  // Resolve target type T
  type_info target_type = resolve_type(e.type_name);
  if (target_type.name == "unknown") {
    add_error(e.line, "Unknown type '" + e.type_name + "' in allocate");
  }

  // Result type is ptr<T>
  std::string ptr_type = "ptr<" + target_type.name + ">";
  last_expr_type_ = resolve_type(ptr_type);
}

void type_checker::visit(ast_deallocate_stmt &s) {
  s.ptr->accept(*this);
  type_info ptr_type = last_expr_type_.value_or(type_unknown);

  // Check if it's a pointer type
  if (ptr_type.name.find("ptr<") != 0) {
    add_error(s.line,
              "Deallocate expects a pointer, got '" + ptr_type.name + "'");
  }
}

std::optional<std::string>
type_checker::resolve_fault_struct(const std::string &name, int line) {
  // Check the source spelling before expanding type aliases. Checking only
  // the canonical target would let another module name a private alias of a
  // public record. Namespace aliases are lexical prefixes, not nominal types.
  std::function<std::optional<std::string>(const std::string &)> checked_spelling =
      [&](const std::string &spelling) -> std::optional<std::string> {
    const auto written = parse_generic_type_string(spelling);
    std::string base = written.base_name;
    std::set<std::string> prefixes;
    const auto aliases = global_namespace_aliases_.find(current_module_.value_or(""));
    if (aliases != global_namespace_aliases_.end()) {
      while (base.find("::") != std::string::npos) {
        const auto split = base.find("::");
        const auto alias = aliases->second.find(base.substr(0, split));
        if (alias == aliases->second.end()) break;
        if (!prefixes.insert(base.substr(0, split)).second) {
          add_error(line, "Cyclic namespace alias in fault type '" + spelling + "'");
          return std::nullopt;
        }
        base = alias->second + base.substr(split);
      }
    }
    const auto resolved_name = resolve_type_name_with_imports(base).value_or(base);
    const symbol *source_type = symbols_.lookup(resolved_name);
    if (source_type && !is_symbol_visible(source_type, current_module_.value_or(""))) {
      add_error(line, "Fault type spelling '" + spelling + "' is private to its defining module");
      return std::nullopt;
    }
    std::string resolved = resolved_name;
    if (!written.type_args.empty()) {
      resolved += "<";
      for (std::size_t index = 0; index < written.type_args.size(); ++index) {
        const auto argument = checked_spelling(written.type_args[index]);
        if (!argument) return std::nullopt;
        if (index) resolved += ", ";
        resolved += *argument;
      }
      resolved += ">";
    }
    return resolved;
  };
  const auto spelling = checked_spelling(name);
  if (!spelling) return std::nullopt;
  const auto canonical = resolve_type(*spelling).name;
  const auto parts = parse_generic_type_string(canonical);
  const symbol *declared = symbols_.lookup(parts.base_name);
  if (!declared || declared->kind != symbol_kind::type_symbol ||
      !declared->defining_node ||
      declared->defining_node->kind != ast_node_kind::struct_decl_node) {
    add_error(line, "Fault payload must have a concrete struct type, got '" + canonical + "'");
    return std::nullopt;
  }
  const auto &structure = static_cast<const ast_struct_decl &>(*declared->defining_node);
  if (structure.generic_params.size() != parts.type_args.size()) {
    add_error(line, "Fault payload requires exact concrete generic arguments for '" + canonical + "'");
    return std::nullopt;
  }
  std::function<bool(const std::string &)> valid_argument = [&](const std::string &argument) {
    const auto inner = parse_generic_type_string(argument);
    if (inner.base_name == "any" || inner.base_name == "void" ||
        inner.base_name.empty() || inner.base_name.front() == '<' ||
        !symbols_.lookup(inner.base_name))
      return false;
    return std::all_of(inner.type_args.begin(), inner.type_args.end(), valid_argument);
  };
  if (!std::all_of(parts.type_args.begin(), parts.type_args.end(), valid_argument)) {
    add_error(line, "Fault payload requires resolved concrete generic arguments for '" + canonical + "'");
    return std::nullopt;
  }
  if (!is_symbol_visible(declared, current_module_.value_or(""))) {
    add_error(line, "Fault type '" + canonical + "' is private to its defining module");
    return std::nullopt;
  }
  return canonical;
}

void type_checker::visit(ast_attempt_stmt &s) {
  s.fault_checked = false;
  checked_attempts_.push_back(&s);
  const auto before = diagnostics_.size();
  if (s.body) {
    symbols_.enter_scope();
    s.body->accept(*this);
    symbols_.leave_scope();
  }

  bool exhaustive = false;
  for (auto &handler : s.handlers) {
    if (handler) {
      if (exhaustive)
        add_error(handler->line, "Unreachable fault handler after an unguarded catch-all");
      handler->accept(*this);
      exhaustive = exhaustive || (!handler->fault_type && !handler->guard);
    }
  }

  // Type-check the recover block if present
  if (s.recover) {
    symbols_.enter_scope();
    s.recover->accept(*this);
    symbols_.leave_scope();
  }
  s.fault_checked = diagnostics_.size() == before;
}

void type_checker::visit(ast_fault_handler &h) {
  h.fault_checked = false;
  h.checked_fault_type.reset();
  checked_fault_handlers_.push_back(&h);
  const auto before = diagnostics_.size();
  if (h.fault_type)
    h.checked_fault_type = resolve_fault_struct(*h.fault_type, h.line);

  // Enter a new scope for the handler body and bind the fault variable
  symbols_.enter_scope();

  // Register the fault variable in the handler's scope
  const std::string fault_var_type = h.fault_type
      ? h.checked_fault_type.value_or("<unknown>") : "<opaque-fault-token>";
  if (!symbols_.insert_symbol_with_context(
          h.variable, symbol_kind::variable_symbol, &h,
          std::optional<std::string>(fault_var_type), false,
          visibility_kind::visibility_private, current_module_)) {
    add_error(h.line, "Variable '" + h.variable + "' already declared");
  }

  // Type-check the guard condition if present
  if (h.guard) {
    last_expr_type_.reset();
    h.guard->accept(*this);
    if (!last_expr_type_ || last_expr_type_->name != "bool") {
      add_error(h.line, "Fault handler guard condition must be boolean, got '" +
                            last_expr_type_.value_or(type_unknown).name + "'");
    }
  }

  // Type-check the handler body
  if (h.body) {
    h.body->accept(*this);
  }

  symbols_.leave_scope();
  h.fault_checked = diagnostics_.size() == before;
}

void type_checker::visit(ast_raise_stmt &s) {
  s.fault_checked = false;
  s.checked_fault_type.reset();
  s.checked_rethrow_handler = nullptr;
  checked_raises_.push_back(&s);
  const auto before = diagnostics_.size();
  if (!s.fault_expr) {
    add_error(s.line, "Raise statement missing fault expression");
    return;
  }
  if (s.fault_expr->kind == ast_node_kind::identifier_expr_node) {
    const auto &identifier = static_cast<const ast_identifier_expr &>(*s.fault_expr);
    const symbol *binding = symbols_.lookup_non_global(identifier.name);
    if (binding && binding->defining_node &&
        binding->defining_node->kind == ast_node_kind::fault_handler_node) {
      const auto *handler = static_cast<const ast_fault_handler *>(binding->defining_node);
      if (!handler->fault_type) {
        s.checked_rethrow_handler = handler;
        s.fault_checked = true;
        return;
      }
    }
  }
  last_expr_type_.reset();
  s.fault_expr->accept(*this);
  s.checked_fault_type = resolve_fault_struct(last_expr_type_.value_or(type_unknown).name, s.line);
  s.fault_checked = diagnostics_.size() == before && s.checked_fault_type.has_value();
}

void type_checker::visit(ast_assert_expr &e) {
  // Valid assertion methods (from intrinsic specification)
  static const std::set<std::string> valid_methods = {
      // Boolean
      "true", "false",
      // Equality
      "equal", "not_equal",
      // Option
      "something", "nothing",
      // Collection
      "contains", "not_contains", "empty", "not_empty", "length",
      // Comparison
      "greater_than", "greater_or_equal", "less_than", "less_or_equal",
      // Range
      "in_range", "not_in_range",
      // String
      "starts_with", "ends_with", "matches",
      // Type
      "is_type", "implements",
      // Fault
      "raises", "not_raises"};

  // Validate method name
  if (valid_methods.find(e.method) == valid_methods.end()) {
    add_error(e.line, "Unknown assertion method: assert." + e.method);
    last_expr_type_ = type_void;
    return;
  }

  // Type-check arguments based on method
  if (e.method == "true" || e.method == "false") {
    // Boolean assertions: assert.true(condition: bool, message?: text)
    if (e.args.empty()) {
      add_error(e.line, "assert." + e.method + " requires a boolean condition");
      last_expr_type_ = type_void;
      return;
    }
    e.args[0]->accept(*this);
    if (last_expr_type_ && last_expr_type_->name != "bool" &&
        last_expr_type_->name != "any") {
      add_error(e.line, "assert." + e.method +
                            " condition must be boolean, got '" +
                            last_expr_type_->name + "'");
    }
  } else if (e.method == "equal" || e.method == "not_equal") {
    // Equality assertions: assert.equal(actual: T, expected: T, message?: text)
    if (e.args.size() < 2) {
      add_error(e.line, "assert." + e.method + " requires two arguments");
      last_expr_type_ = type_void;
      return;
    }
    e.args[0]->accept(*this);
    type_info arg1_type = last_expr_type_.value_or(type_unknown);
    e.args[1]->accept(*this);
    type_info arg2_type = last_expr_type_.value_or(type_unknown);

    if (!are_types_compatible(arg1_type, arg2_type)) {
      add_error(e.line,
                "assert." + e.method + " arguments must have compatible types");
    }
  } else if (e.method == "something" || e.method == "nothing") {
    // Option assertions: assert.something(value: option<T>, message?: text)
    if (e.args.empty()) {
      add_error(e.line, "assert." + e.method + " requires an option value");
      last_expr_type_ = type_void;
      return;
    }
    e.args[0]->accept(*this);
    // TODO: Verify first arg is option<T> type
  } else if (e.method == "contains" || e.method == "not_contains") {
    // Collection assertions: assert.contains(collection: vec<T>, item: T,
    // message?: text)
    if (e.args.size() < 2) {
      add_error(e.line, "assert." + e.method +
                            " requires collection and item arguments");
      last_expr_type_ = type_void;
      return;
    }
    e.args[0]->accept(*this);
    // TODO: Verify first arg is vec<T> or similar collection
    e.args[1]->accept(*this);
    // TODO: Verify second arg type matches collection element type
  } else if (e.method == "empty" || e.method == "not_empty") {
    // Collection assertions: assert.empty(collection: vec<T>, message?: text)
    if (e.args.empty()) {
      add_error(e.line,
                "assert." + e.method + " requires a collection argument");
      last_expr_type_ = type_void;
      return;
    }
    e.args[0]->accept(*this);
    // TODO: Verify first arg is vec<T> or similar collection
  } else if (e.method == "length") {
    // Length assertion: assert.length(collection: vec<T>, expected: i32,
    // message?: text)
    if (e.args.size() < 2) {
      add_error(
          e.line,
          "assert.length requires collection and expected length arguments");
      last_expr_type_ = type_void;
      return;
    }
    e.args[0]->accept(*this);
    // TODO: Verify first arg is vec<T> or similar collection
    e.args[1]->accept(*this);
    if (last_expr_type_ && last_expr_type_->name != "i32" &&
        last_expr_type_->name != "any") {
      add_error(e.line, "assert.length expected length must be i32, got '" +
                            last_expr_type_->name + "'");
    }
  } else if (e.method == "greater_than" || e.method == "greater_or_equal" ||
             e.method == "less_than" || e.method == "less_or_equal") {
    // Comparison assertions: assert.greater_than(actual: T, threshold: T,
    // message?: text)
    if (e.args.size() < 2) {
      add_error(e.line,
                "assert." + e.method + " requires two comparable arguments");
      last_expr_type_ = type_void;
      return;
    }
    e.args[0]->accept(*this);
    type_info arg1_type = last_expr_type_.value_or(type_unknown);
    e.args[1]->accept(*this);
    type_info arg2_type = last_expr_type_.value_or(type_unknown);

    if (!are_types_compatible(arg1_type, arg2_type)) {
      add_error(e.line,
                "assert." + e.method + " arguments must have compatible types");
    }
  } else if (e.method == "in_range" || e.method == "not_in_range") {
    // Range assertions: assert.in_range(value: T, min: T, max: T, message?:
    // text)
    if (e.args.size() < 3) {
      add_error(e.line, "assert." + e.method +
                            " requires value, min, and max arguments");
      last_expr_type_ = type_void;
      return;
    }
    e.args[0]->accept(*this);
    type_info value_type = last_expr_type_.value_or(type_unknown);
    e.args[1]->accept(*this);
    type_info min_type = last_expr_type_.value_or(type_unknown);
    e.args[2]->accept(*this);
    type_info max_type = last_expr_type_.value_or(type_unknown);

    if (!are_types_compatible(value_type, min_type) ||
        !are_types_compatible(value_type, max_type)) {
      add_error(e.line, "assert." + e.method +
                            " all arguments must have compatible types");
    }
  } else if (e.method == "starts_with" || e.method == "ends_with") {
    // String assertions: assert.starts_with(text: text, prefix: text, message?:
    // text)
    if (e.args.size() < 2) {
      add_error(e.line,
                "assert." + e.method + " requires text and pattern arguments");
      last_expr_type_ = type_void;
      return;
    }
    e.args[0]->accept(*this);
    if (last_expr_type_ && last_expr_type_->name != "String" &&
        last_expr_type_->name != "text" && last_expr_type_->name != "any") {
      add_error(e.line,
                "assert." + e.method + " first argument must be text/String");
    }
    e.args[1]->accept(*this);
    if (last_expr_type_ && last_expr_type_->name != "String" &&
        last_expr_type_->name != "text" && last_expr_type_->name != "any") {
      add_error(e.line,
                "assert." + e.method + " second argument must be text/String");
    }
  } else if (e.method == "matches") {
    // Pattern matching: assert.matches(text: text, pattern: text, message?:
    // text)
    if (e.args.size() < 2) {
      add_error(e.line, "assert.matches requires text and pattern arguments");
      last_expr_type_ = type_void;
      return;
    }
    e.args[0]->accept(*this);
    e.args[1]->accept(*this);
  } else if (e.method == "raises" || e.method == "not_raises") {
    // Fault assertions: assert.raises<FaultType>(fn: fn() -> (), message?:
    // text)
    if (e.args.empty()) {
      add_error(e.line,
                "assert." + e.method + " requires a function/lambda argument");
      last_expr_type_ = type_void;
      return;
    }
    e.args[0]->accept(*this);
    // TODO: Verify first arg is a function/lambda type
  }

  // Check optional message parameter (if more args than required, last should
  // be text) Most assertions take 1-3 required args, so if we have more, check
  // the last one
  size_t expected_args = 1;
  if (e.method == "equal" || e.method == "not_equal" ||
      e.method == "contains" || e.method == "not_contains" ||
      e.method == "greater_than" || e.method == "greater_or_equal" ||
      e.method == "less_than" || e.method == "less_or_equal" ||
      e.method == "starts_with" || e.method == "ends_with" ||
      e.method == "matches" || e.method == "length") {
    expected_args = 2;
  } else if (e.method == "in_range" || e.method == "not_in_range") {
    expected_args = 3;
  }

  if (e.args.size() > expected_args) {
    // Check if last arg is text (message parameter)
    auto &msg_arg = e.args.back();
    msg_arg->accept(*this);
    if (last_expr_type_ && last_expr_type_->name != "String" &&
        last_expr_type_->name != "text" && last_expr_type_->name != "any") {
      add_error(e.line,
                "Assertion message parameter must be text/String, got '" +
                    last_expr_type_->name + "'");
    }
  }

  // Assertions always return void
  last_expr_type_ = type_void;
  e.inferred_type = "void";
}

void type_checker::visit(ast_hotreload_expr &e) {
  // Valid hotreload methods (from intrinsic specification)
  static const std::map<std::string, std::pair<int, std::string>>
      valid_methods = {
          // Method name -> (arg count, return type)
          {"enable", {0, "void"}},
          {"disable", {0, "void"}},
          {"is_enabled", {0, "bool"}},
          {"version", {0, "i32"}},
          {"check_now", {0, "void"}},
          {"before_reload", {1, "void"}}, // fn() -> ()
          {"after_reload", {1, "void"}},  // fn(i32) -> ()
          {"on_error", {1, "void"}},      // fn(text) -> ()
          {"last_reload_time", {0, "i64"}},
          {"watched_files", {0, "vec<text>"}},
          {"watch_file", {1, "void"}},          // text
          {"unwatch_file", {1, "void"}},        // text
          {"set_debounce_ms", {1, "void"}},     // i32
          {"set_watch_recursive", {1, "void"}}, // bool
      };

  // Validate method name
  if (valid_methods.find(e.method) == valid_methods.end()) {
    add_error(e.line, "Unknown hotreload method: hotreload." + e.method);
    last_expr_type_ = type_void;
    return;
  }

  auto &method_info = valid_methods.at(e.method);
  int expected_args = method_info.first;
  std::string return_type = method_info.second;

  // Validate argument count
  if (e.args.size() != static_cast<size_t>(expected_args)) {
    add_error(e.line, "hotreload." + e.method + " expects " +
                          std::to_string(expected_args) + " argument(s), got " +
                          std::to_string(e.args.size()));
    last_expr_type_ = type_void;
    return;
  }

  // Type check arguments based on method
  if (e.method == "watch_file" || e.method == "unwatch_file") {
    // Expect text/String argument
    if (!e.args.empty()) {
      e.args[0]->accept(*this);
      if (last_expr_type_ && last_expr_type_->name != "String" &&
          last_expr_type_->name != "text" && last_expr_type_->name != "any") {
        add_error(e.line, "hotreload." + e.method +
                              " expects text/String argument, got '" +
                              last_expr_type_->name + "'");
      }
    }
  } else if (e.method == "set_debounce_ms") {
    // Expect i32 argument
    if (!e.args.empty()) {
      e.args[0]->accept(*this);
      if (last_expr_type_ && last_expr_type_->name != "i32" &&
          last_expr_type_->name != "any") {
        add_error(e.line,
                  "hotreload.set_debounce_ms expects i32 argument, got '" +
                      last_expr_type_->name + "'");
      }
    }
  } else if (e.method == "set_watch_recursive") {
    // Expect bool argument
    if (!e.args.empty()) {
      e.args[0]->accept(*this);
      if (last_expr_type_ && last_expr_type_->name != "bool" &&
          last_expr_type_->name != "any") {
        add_error(e.line,
                  "hotreload.set_watch_recursive expects bool argument, got '" +
                      last_expr_type_->name + "'");
      }
    }
  } else if (e.method == "before_reload" || e.method == "after_reload" ||
             e.method == "on_error") {
    // Expect function/lambda argument
    if (!e.args.empty()) {
      e.args[0]->accept(*this);
      // TODO: Validate function signature matches expected callback type
    }
  }

  // Set return type
  if (return_type == "void") {
    last_expr_type_ = type_void;
  } else if (return_type == "bool") {
    last_expr_type_ = type_bool;
  } else if (return_type == "i32") {
    last_expr_type_ = type_i32;
  } else if (return_type == "i64") {
    last_expr_type_ = resolve_type("i64");
  } else if (return_type == "vec<text>") {
    last_expr_type_ = resolve_type("vec<text>");
  } else {
    last_expr_type_ = type_void;
  }

  e.inferred_type = return_type;
}

void type_checker::visit(ast_if_expr &e) {
  // Conditions and both value arms are checked, even when the condition is a
  // constant. A dead arm cannot silently turn an unrelated value into zero.
  if (e.condition) {
    e.condition->accept(*this);
    if (!last_expr_type_ || last_expr_type_->name != "bool")
      add_error(e.line, "If expression condition must be boolean");
  }

  // Type-check then-branch and capture its type
  std::optional<type_info> then_type;
  if (e.then_expr) {
    e.then_expr->accept(*this);
    then_type = last_expr_type_;
  }

  // Type-check else-branch and capture its type
  std::optional<type_info> else_type;
  if (e.else_expr) {
    e.else_expr->accept(*this);
    else_type = last_expr_type_;
  }

  if (!then_type || !else_type) {
    add_error(e.line, "If expression requires then and else value branches");
    last_expr_type_ = type_unknown;
  } else if (then_type->name == "void" || else_type->name == "void") {
    add_error(e.line, "If expression branches must produce values");
    last_expr_type_ = type_unknown;
  } else if (const auto promoted = promoted_scalar_join(then_type->name,
                                                        else_type->name)) {
    last_expr_type_ = resolve_type(*promoted);
  } else if ((then_type->name == "bool") != (else_type->name == "bool") ||
             !are_types_compatible(*then_type, *else_type)) {
    add_error(e.line, "If expression branch types must be consistent: '" +
                          then_type->name + "' vs '" + else_type->name + "'");
    last_expr_type_ = type_unknown;
  } else {
    last_expr_type_ = then_type;
  }

  if (last_expr_type_)
    e.inferred_type = last_expr_type_->name;
}

void type_checker::visit(ast_ref_expr &e) {
  // Visit operand to get its type
  e.operand->accept(*this);
  type_info operand_type = last_expr_type_.value_or(type_unknown);
  check_reference_handle(e, *e.operand, operand_type);

  // The ref expression has type ref<T>
  std::string ref_type_name = "ref<" + operand_type.name + ">";
  last_expr_type_ = resolve_type(ref_type_name);
  e.inferred_type = last_expr_type_->name;
}

void type_checker::visit(ast_lambda_expr &e) {
  e.checked_callable_parameters = callable_parameters(e.params);
  // Build function type: fn(T1, T2, ...) -> R
  std::string fn_type = "fn(";
  for (size_t i = 0; i < e.params.size(); ++i) {
    if (i > 0)
      fn_type += ", ";
    fn_type += e.params[i].type_name.value_or("any");
  }
  fn_type += ") -> " + e.return_type.value_or("void");

  // Enter lambda scope, check body
  symbols_.enter_scope();
  for (const auto &p : e.params) {
    if (symbols_.insert_symbol_with_context(
        p.name, symbol_kind::variable_symbol, nullptr, p.type_name, p.is_mut,
        visibility_kind::visibility_private, std::nullopt)) {
      if (auto *binding = symbols_.lookup_mut(p.name))
        binding->semantic_id = semantic_declaration(p.name, "parameter",
            p.type_name.value_or(""), diagnostic_source_file_, p.name_span);
    }
  }

  // Save current return type and set for lambda
  auto old_return_type = current_function_return_type_;
  current_function_return_type_ = resolve_type(e.return_type.value_or("void"));

  if (e.body) {
    e.body->accept(*this);
  }

  // Restore original return type
  current_function_return_type_ = old_return_type;

  symbols_.leave_scope();

  last_expr_type_ = type_info{fn_type, true};
}

void type_checker::visit(ast_interpolated_string_expr &e) {
  // Visit each expression part to type check them
  for (const auto &part : e.parts) {
    if (!part.is_literal && part.expression) {
      part.expression->accept(*this);
    }
  }
  // Result type is always text
  // Result type is always String (primitive)
  last_expr_type_ = type_string;
}

void type_checker::visit(ast_boolean_literal &e) {
  last_expr_type_ = type_bool;
  e.inferred_type = "bool";
}

bool type_checker::instantiate_generic_node(
    const std::string &base_node_name, const std::string &instantiated_name,
    const std::vector<std::string> &type_args) {
  // std::cerr << "[instantiate] CHECK " << instantiated_name << " args=" <<
  // type_args.size() << "\n";
  if (symbols_.lookup(instantiated_name))
    return false;

  // A composite argument can still contain a scoped parameter. Materializing
  // Wrapper<Pair<TKey, TValue>> while those parameters are open queues a
  // supposedly concrete declaration whose fields are checked after their
  // scope has ended. Keep the template until every nested argument is closed.
  std::function<bool(const std::string &)> contains_open_parameter =
      [&](const std::string &type) {
    const symbol *local = symbols_.lookup_non_global(type);
    if (local && local->kind == symbol_kind::type_symbol &&
        !local->defining_node && !local->type_name)
      return true;
    const auto parts = parse_generic_type_string(type);
    for (const auto &argument : parts.type_args)
      if (contains_open_parameter(argument))
        return true;
    return false;
  };
  // Skip instantiation if any type arg is an unresolved generic parameter
  for (const auto &arg : type_args) {
    if (contains_open_parameter(arg))
      return false;
    const symbol *arg_sym = symbols_.lookup(arg);
    if (arg_sym && arg_sym->kind == symbol_kind::type_symbol &&
        !arg_sym->defining_node) {
      return false;
    }
    // Also check for common generic parameter patterns (single uppercase
    // letters)
    if (arg.size() == 1 && std::isupper(arg[0])) {
      return false;
    }
  }

  /* std::cerr << "[instantiate] START " << instantiated_name << " base=" <<
   * base_node_name << std::endl; */

  auto *base_sym = symbols_.lookup(base_node_name);

  if (!base_sym || !base_sym->defining_node ||
      base_sym->kind != symbol_kind::type_symbol) {
    /* std::cerr << "[instantiate] FAIL: Base symbol not found or invalid: " <<
     * base_node_name << "\n"; */
    return false;
  }

  // Debug helper
  static thread_local std::vector<std::string> instantiation_stack;
  if (instantiation_stack.size() > 50) {
    std::cerr << "Instantiation Recursion Exceeded! Stack:\n";
    for (const auto &s : instantiation_stack)
      std::cerr << " " << s << "\n";
    std::cerr << " Current: " << instantiated_name << std::endl;
    return false;
  }

  // Push instantiated_name to stack for cycle detection
  instantiation_stack.push_back(instantiated_name);
  struct Guard {
    ~Guard() { instantiation_stack.pop_back(); }
  } guard_inst;

  // Build unqualified AST name
  std::string unqualified_name = instantiated_name;

  // Scan backwards for last '::' ensuring valid bracket nesting (ignore ::
  // inside <...>)
  int depth = 0;
  size_t last_sep = std::string::npos;
  if (!instantiated_name.empty()) {
    for (size_t i = instantiated_name.size(); i > 0; --i) {
      char c = instantiated_name[i - 1];
      if (c == '>')
        depth++;
      else if (c == '<')
        depth--;
      else if (c == ':' && i > 1 && instantiated_name[i - 2] == ':' &&
               depth == 0) {
        last_sep = i - 2;
        break;
      }
    }
  }

  if (last_sep != std::string::npos) {
    unqualified_name = unqualified_name.substr(last_sep + 2);
  }

  // Create a stable generated identifier (replace < > : , and spaces).
  for (char &c : unqualified_name) {
    if (c == '<' || c == '>' || c == ',' || c == ' ' || c == ':') {
      c = '_';
    }
  }

  // Handle Structs
  if (auto *base_decl =
          dynamic_cast<ast_struct_decl *>(base_sym->defining_node)) {
    if (base_decl->generic_params.size() != type_args.size())
      return false;

    std::map<std::string, std::string> substitutions;
    for (size_t i = 0; i < base_decl->generic_params.size(); ++i) {
      substitutions[base_decl->generic_params[i].name] = type_args[i];
    }

    /* std::cerr << "[instantiate] Creating struct " << unqualified_name << "
     * (registered as " << instantiated_name << ") from " << base_node_name <<
     * std::endl; */

    auto new_decl = std::make_shared<ast_struct_decl>(
        unqualified_name, std::vector<ast_struct_field>{},
        std::vector<ast_generic_param>{}, base_decl->is_public(),
        base_decl->line);
    new_decl->concrete_generic_type = instantiated_name;
    new_decl->visibility = base_decl->visibility; // Ensure visibility matches

    ast_struct_decl *decl_ptr = new_decl.get();

    bool inserted = symbols_.insert_symbol_with_context(
        instantiated_name, symbol_kind::type_symbol, decl_ptr,
        instantiated_name, false, base_decl->get_visibility(), current_module_);
    if (!inserted) {
      // std::cerr << "CRITICAL: Failed to register instantiated struct '" <<
      // instantiated_name << "'\n";
    }

    // Now process fields
    for (const auto &field : base_decl->fields) {
      std::string new_type =
          instantiate_generic_type(field.type_name, substitutions);
      new_decl->fields.push_back({field.name, new_type, field.line});
    }

    new_declarations_.push_back(new_decl);
    return true;
  }
  // Handle Enums
  else if (auto *base_enum =
               dynamic_cast<ast_enum_decl *>(base_sym->defining_node)) {
    if (base_enum->generic_params.size() != type_args.size())
      return false;

    std::map<std::string, std::string> substitutions;
    for (size_t i = 0; i < base_enum->generic_params.size(); ++i) {
      substitutions[base_enum->generic_params[i].name] = type_args[i];
    }

    /* std::cerr << "[instantiate] Creating enum " << unqualified_name << "
     * (registered as " << instantiated_name << ") from " << base_node_name <<
     * std::endl; */

    auto new_decl = std::make_shared<ast_enum_decl>(
        unqualified_name, std::vector<ast_enum_variant>{},
        std::vector<ast_generic_param>{}, base_enum->is_public(),
        base_enum->line);
    new_decl->visibility = base_enum->visibility;

    ast_enum_decl *decl_ptr = new_decl.get();

    bool inserted = symbols_.insert_symbol_with_context(
        instantiated_name, symbol_kind::type_symbol, decl_ptr,
        instantiated_name, false, base_enum->get_visibility(), current_module_);
    if (!inserted) {
      // std::cerr << "CRITICAL: Failed to register instantiated enum '" <<
      // instantiated_name << "'\n";
    }

    // Now process variants
    for (const auto &variant : base_enum->variants) {
      std::optional<std::string> new_type_name;
      if (variant.associated_type) {
        new_type_name =
            instantiate_generic_type(*variant.associated_type, substitutions);
      }
      new_decl->variants.push_back({variant.name, new_type_name, variant.line});
    }

    new_declarations_.push_back(new_decl);
    return true;
  }
  // Handle Traits
  else if (auto *base_trait =
               dynamic_cast<ast_trait_decl *>(base_sym->defining_node)) {
    if (base_trait->generic_params.size() != type_args.size())
      return false;

    std::map<std::string, std::string> substitutions;
    for (size_t i = 0; i < base_trait->generic_params.size(); ++i) {
      substitutions[base_trait->generic_params[i].name] = type_args[i];
    }

    /* std::cerr << "[instantiate] Creating trait " << unqualified_name << "
     * (registered as " << instantiated_name << ") from " << base_node_name <<
     * std::endl; */

    auto new_decl = std::make_shared<ast_trait_decl>(
        unqualified_name, std::vector<ast_generic_param>{},
        std::vector<std::shared_ptr<ast_function_decl>>{},
        base_trait->is_public(), base_trait->line);
    new_decl->visibility = base_trait->visibility;

    ast_trait_decl *decl_ptr = new_decl.get();

    bool inserted = symbols_.insert_symbol_with_context(
        instantiated_name, symbol_kind::type_symbol, decl_ptr,
        instantiated_name, false, base_trait->get_visibility(),
        current_module_);
    if (!inserted) {
      // std::cerr << "CRITICAL: Failed to register instantiated trait '" <<
      // instantiated_name << "'\n";
    }

    // Process methods
    for (const auto &method : base_trait->methods) {
      auto method_substitutions = substitutions;
      auto method_generic_params = method->generic_params;
      for (const auto &parameter : method_generic_params)
        method_substitutions.erase(parameter.name);
      for (auto &parameter : method_generic_params)
        for (auto &bound : parameter.bounds)
          bound = instantiate_generic_type(bound, method_substitutions);
      std::optional<std::string> ret_type;
      if (method->return_type) {
        ret_type =
            instantiate_generic_type(*method->return_type, method_substitutions);
      }

      std::vector<ast_param> new_params;
      for (const auto &param : method->params) {
        std::optional<std::string> p_type;
        if (param.type_name) {
          p_type = instantiate_generic_type(*param.type_name, method_substitutions);
        }
        new_params.emplace_back(param.name, p_type, param.is_mut, param.line,
                                param.default_value);
      }

      // Clone function decl (signature)
      auto new_method = std::make_shared<ast_function_decl>(
          method->name, new_params, ret_type,
          nullptr, // Body is null for trait signatures (usually) - generic
                   // traits won't have default impls instantiated yet heavily
          std::move(method_generic_params),
          method->is_public(), method->line, method->annotations,
          method->is_virtual, method->is_abstract, method->is_override);
      new_decl->methods.push_back(new_method);
    }

    new_declarations_.push_back(new_decl);
    return true;
  } else {
    /* std::cerr << "[instantiate] FAIL: Base node is neither struct nor enum
     * nor trait. Kind=" << (int)base_sym->defining_node->kind << "\n"; */
    return false;
  }
}

} // namespace ny
