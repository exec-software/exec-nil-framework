/*
 * Copyright (C) 2008 - 2026 Execute Software.  All rights reserved.
 * SPDX-License-Identifier: MIT
 * See the LICENSE file in the project root for license terms.
 */
#include "codegen.hpp"
#include "array_intrinsic.hpp"
#include "binding_reference.hpp"
#include "inline_asm.hpp"
#include "diagnostics.hpp"
#include "dynamic_type.hpp"
#include "nil_contract.hpp"
#include "numeric_literal.hpp"
#include "scalar_text.hpp"
#include "ownership_check.hpp"
#include "concurrency_contract.hpp"
#include "owned_contract.hpp"
#include "standard_namespace.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>

namespace ny {

static void append_implicit_function_return(nil_function &function,
                                            int debug_line) {
  if (!function.body.empty() && function.body.back().opname == "RET")
    return;

  nil_instruction instruction;
  instruction.opname = "RET";
  instruction.debug_line = debug_line;
  if (!function.return_type.empty() && function.return_type != "void")
    instruction.args.push_back(nil_nonvoid_fallthrough_operand);
  function.body.push_back(std::move(instruction));
}

// Escape every control character recognized by the textual NIL loader. In
// particular, a Ny `\0` literal must not place a raw NUL into a textual NIL
// artifact: downstream line-oriented tools are entitled to treat it as text.
static std::string escape_string(const std::string &s) {
  std::string out;
  out.reserve(s.size() + 2);
  for (char c : s) {
    if (c == '\\')
      out += "\\\\";
    else if (c == '"')
      out += "\\\"";
    else if (c == '\n')
      out += "\\n";
    else if (c == '\r')
      out += "\\r";
    else if (c == '\t')
      out += "\\t";
    else if (c == '\v')
      out += "\\v";
    else if (c == '\f')
      out += "\\f";
    else if (c == '\0')
      out += "\\0";
    else
      out.push_back(c);
  }
  return out;
}

static std::size_t
native_dispatch_annotation_count(const ast_function_decl &function) {
  return static_cast<std::size_t>(std::count_if(
      function.annotations.begin(), function.annotations.end(),
      [](const ast_annotation &annotation) {
        return annotation.name == "intrinsic" ||
               annotation.name == "@intrinsic" ||
               annotation.name == "interop" ||
               annotation.name == "@interop";
      }));
}

static std::string mangle_fully(std::string name) {
  std::string original = name;
  size_t pos = 0;
  while ((pos = name.find("::", pos)) != std::string::npos) {
    name.replace(pos, 2, "__");
    pos += 2;
  }
  // Handle generics
  for (char &c : name) {
    if (c == '<' || c == '>' || c == ',' || c == ' ') {
      c = '_';
    }
  }
  ny::diag_debug("[codegen] mangle " + original + " -> " + name);
  return name;
}

static std::string canonicalize_variant_name(std::string name) {
  // The parser accepts both Enum.Variant and Enum::Variant. Emit a single NIL
  // spelling so every execution backend observes the same tag.
  for (size_t dot = name.find('.'); dot != std::string::npos;
       dot = name.find('.', dot + 2)) {
    name.replace(dot, 1, "::");
  }

  const auto variant_separator = name.rfind("::");
  if (variant_separator == std::string::npos)
    return name;
  const auto enum_separator = variant_separator == 0
                                  ? std::string::npos
                                  : name.rfind("::", variant_separator - 1);
  const std::size_t enum_start =
      enum_separator == std::string::npos ? 0 : enum_separator + 2;
  std::string enum_name =
      name.substr(enum_start, variant_separator - enum_start);
  std::string variant_name = name.substr(variant_separator + 2);
  std::transform(
      enum_name.begin(), enum_name.end(), enum_name.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  std::transform(
      variant_name.begin(), variant_name.end(), variant_name.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

  std::string canonical_enum;
  std::string canonical_variant;
  if (enum_name == "option") {
    canonical_enum = "Option";
    if (variant_name == "some")
      canonical_variant = "Some";
    else if (variant_name == "none")
      canonical_variant = "None";
  } else if (enum_name == "result") {
    canonical_enum = "Result";
    if (variant_name == "ok")
      canonical_variant = "Ok";
    else if (variant_name == "error" || variant_name == "err")
      canonical_variant = "Error";
  }

  if (canonical_variant.empty())
    return name;
  name.replace(enum_start, variant_separator - enum_start, canonical_enum);
  const auto new_variant_separator = name.rfind("::");
  name.replace(new_variant_separator + 2, std::string::npos, canonical_variant);
  return name;
}

static std::string normalized_receiver_base_name(std::string type_name) {
  // Imported built-ins retain their source spelling and may be qualified.
  // Intrinsic dispatch is semantic, so compare one generic-free,
  // unqualified, case-normalized base name.
  const size_t generic_start = type_name.find('<');
  if (generic_start != std::string::npos)
    type_name = type_name.substr(0, generic_start);
  const size_t module_separator = type_name.rfind("::");
  if (module_separator != std::string::npos)
    type_name = type_name.substr(module_separator + 2);
  std::transform(type_name.begin(), type_name.end(), type_name.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return type_name;
}

static std::optional<std::string>
array_element_type_name(const std::string &type_name) {
  const std::size_t open = type_name.find('<');
  if (open == std::string::npos || type_name.empty() ||
      type_name.back() != '>') {
    return std::nullopt;
  }

  std::string base = type_name.substr(0, open);
  const std::size_t module_separator = base.rfind("::");
  if (module_separator != std::string::npos)
    base = base.substr(module_separator + 2);
  std::transform(base.begin(), base.end(), base.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  if (base != "array")
    return std::nullopt;

  std::string element = type_name.substr(open + 1, type_name.size() - open - 2);
  const auto first = element.find_first_not_of(" \t\r\n");
  if (first == std::string::npos)
    return std::nullopt;
  const auto last = element.find_last_not_of(" \t\r\n");
  return element.substr(first, last - first + 1);
}

static std::string trim_type_name(const std::string &value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos)
    return {};
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

struct parsed_generic_type_name {
  std::string base_name;
  std::vector<std::string> type_args;
};

static std::optional<parsed_generic_type_name>
parse_generic_type_name(const std::string &raw_type_name) {
  const std::string type_name = trim_type_name(raw_type_name);
  const std::size_t open = type_name.find('<');
  if (open == std::string::npos || type_name.empty() ||
      type_name.back() != '>') {
    return std::nullopt;
  }

  parsed_generic_type_name parsed;
  parsed.base_name = trim_type_name(type_name.substr(0, open));
  if (parsed.base_name.empty())
    return std::nullopt;

  int angle_depth = 0;
  int parenthesis_depth = 0;
  int bracket_depth = 0;
  std::string argument;
  for (std::size_t index = open + 1; index + 1 < type_name.size(); ++index) {
    const char character = type_name[index];
    if (character == '<') {
      ++angle_depth;
      argument.push_back(character);
    } else if (character == '>') {
      if (angle_depth == 0)
        return std::nullopt;
      --angle_depth;
      argument.push_back(character);
    } else if (character == '(') {
      ++parenthesis_depth;
      argument.push_back(character);
    } else if (character == ')') {
      if (parenthesis_depth == 0)
        return std::nullopt;
      --parenthesis_depth;
      argument.push_back(character);
    } else if (character == '[') {
      ++bracket_depth;
      argument.push_back(character);
    } else if (character == ']') {
      if (bracket_depth == 0)
        return std::nullopt;
      --bracket_depth;
      argument.push_back(character);
    } else if (character == ',' && angle_depth == 0 && parenthesis_depth == 0 &&
               bracket_depth == 0) {
      argument = trim_type_name(argument);
      if (argument.empty())
        return std::nullopt;
      parsed.type_args.push_back(argument);
      argument.clear();
    } else {
      argument.push_back(character);
    }
  }
  if (angle_depth != 0 || parenthesis_depth != 0 || bracket_depth != 0)
    return std::nullopt;
  argument = trim_type_name(argument);
  if (argument.empty())
    return std::nullopt;
  parsed.type_args.push_back(argument);
  return parsed;
}

static std::string canonical_nil_type_name(const std::string &raw_type_name) {
  const std::string type_name = trim_type_name(raw_type_name);
  const auto parsed = parse_generic_type_name(type_name);
  if (!parsed) {
    // NIL uses the language-facing ABI spelling for primitive aliases so an
    // initializer annotated with `text` and the same local later inferred as
    // `String` cannot create two schemas for one concrete enum instance.
    if (type_name == "String" || type_name == "string")
      return "text";
    if (type_name == "int")
      return "i32";
    if (type_name == "boolean" || type_name == "flag")
      return "bool";
    return type_name;
  }

  std::string canonical = parsed->base_name + "<";
  for (std::size_t index = 0; index < parsed->type_args.size(); ++index) {
    if (index != 0)
      canonical += ",";
    canonical += canonical_nil_type_name(parsed->type_args[index]);
  }
  canonical += ">";
  return canonical;
}

static bool is_type_identifier_character(char character) {
  return std::isalnum(static_cast<unsigned char>(character)) ||
         character == '_';
}

static bool contains_type_identifier(const std::string &type_name,
                                     const std::string &identifier) {
  std::size_t position = 0;
  while ((position = type_name.find(identifier, position)) !=
         std::string::npos) {
    const bool before_is_boundary =
        position == 0 || !is_type_identifier_character(type_name[position - 1]);
    const std::size_t after = position + identifier.size();
    const bool after_is_boundary =
        after == type_name.size() ||
        !is_type_identifier_character(type_name[after]);
    if (before_is_boundary && after_is_boundary)
      return true;
    position = after;
  }
  return false;
}

static std::string substitute_nil_type_name(
    const std::string &raw_type_name,
    const std::map<std::string, std::string> &substitutions) {
  const std::string type_name = trim_type_name(raw_type_name);
  const auto exact = substitutions.find(type_name);
  if (exact != substitutions.end())
    return canonical_nil_type_name(exact->second);

  if (const auto parsed = parse_generic_type_name(type_name)) {
    std::string substituted = parsed->base_name + "<";
    for (std::size_t index = 0; index < parsed->type_args.size(); ++index) {
      if (index != 0)
        substituted += ",";
      substituted +=
          substitute_nil_type_name(parsed->type_args[index], substitutions);
    }
    substituted += ">";
    return substituted;
  }

  // Associated types may also use tuple or function syntax. Replace complete
  // parameter identifiers there without treating substrings as type names.
  std::string substituted = type_name;
  for (const auto &entry : substitutions) {
    std::size_t position = 0;
    while ((position = substituted.find(entry.first, position)) !=
           std::string::npos) {
      const bool before_is_boundary =
          position == 0 ||
          !is_type_identifier_character(substituted[position - 1]);
      const std::size_t after = position + entry.first.size();
      const bool after_is_boundary =
          after == substituted.size() ||
          !is_type_identifier_character(substituted[after]);
      if (before_is_boundary && after_is_boundary) {
        substituted.replace(position, entry.first.size(), entry.second);
        position += entry.second.size();
      } else {
        position = after;
      }
    }
  }
  return canonical_nil_type_name(substituted);
}

static std::vector<std::string> specialization_type_arguments(
    const std::string &raw_type_name,
    const std::map<std::string, std::string> &substitutions) {
  const auto parsed =
      parse_generic_type_name(substitute_nil_type_name(raw_type_name,
                                                       substitutions));
  if (!parsed)
    return {};
  return parsed->type_args;
}

// Binding references are a calling convention, not a new source value type.
// The reserved schema name cannot collide with a Ny/Matrix declaration. Its
// handle is only exposed to another checked reference parameter; the source
// binding continues to have the field's scalar type.
static std::string binding_cell_name(const std::string &type) {
  return std::string(nil_binding_cell_prefix) + canonical_nil_type_name(type);
}

static std::optional<std::string>
binding_parameter_type(const std::string &type) {
  return binding_reference_value_type(canonical_nil_type_name(type));
}

static bool supported_binding_scalar(const std::string &type) {
  return is_binding_scalar_type(type);
}

static bool append_serialized_nil_parameter(
    nil_function &function, const ast_param &parameter,
    const std::map<std::string, std::string> &substitutions = {},
    const std::string &implicit_self_type = {}) {
  std::string type;
  if (parameter.type_name.has_value())
    type = *parameter.type_name;
  else if (parameter.name == "self")
    type = implicit_self_type;
  if (type.empty())
    return false;

  type = substitute_nil_type_name(type, substitutions);
  if (type.empty())
    return false;
  if (parameter.is_ref && !parameter.checked_binding_reference)
    return false;
  if (parameter.is_ref)
    type = "ref<" + binding_cell_name(type) + ">";
  function.params.push_back(parameter.name + ": " + type);
  return true;
}

// Use lexical NIL slots rather than source spellings: two nested declarations
// called "amount" must never share storage. Every reference marker is collected
// before any access is rewritten, including markers in a later branch. Cells
// are initialized once per invocation, so two arguments addressing one binding
// share it immediately and forwarding a reference never performs copy-back.
static void lower_binding_references(nil_module &module) {
  struct cell_binding {
    std::string type;
    std::string storage;
    bool reference_parameter = false;
    bool value_parameter = false;
  };
  for (auto &function : module.functions) {
    std::map<std::string, cell_binding> cells;
    std::set<std::string> parameters;
    for (const auto &parameter : function.params) {
      const auto colon = parameter.find(':');
      if (colon == std::string::npos) continue;
      const auto name = parameter.substr(0, colon);
      parameters.insert(name);
      if (const auto type = binding_parameter_type(parameter.substr(colon + 1)))
        cells.emplace(name, cell_binding{*type, name, true, false});
    }
    for (const auto &instruction : function.body) {
      if (instruction.opname != "BINDING_REF_LOCAL") continue;
      if (instruction.args.size() != 2)
        throw std::runtime_error("Invalid internal binding-reference marker");
      auto [where, inserted] = cells.emplace(instruction.args[0],
          cell_binding{instruction.args[1], {}, false,
                       parameters.count(instruction.args[0]) != 0});
      if (!inserted && where->second.type != instruction.args[1])
        throw std::runtime_error("Binding reference has conflicting storage types");
    }
    if (cells.empty()) continue;
    std::size_t next_storage = 0;
    for (auto &[slot, cell] : cells) {
      if (!supported_binding_scalar(cell.type))
        throw std::runtime_error("Binding reference requires i32, i64, u8, bool, f64, text, or anything");
      if (!cell.reference_parameter)
        cell.storage = "$nil.binding.storage." + std::to_string(next_storage++);
      const auto schema_name = binding_cell_name(cell.type);
      const auto existing = std::find_if(module.structs.begin(), module.structs.end(),
          [&](const auto &schema) { return schema.name == schema_name; });
      if (existing == module.structs.end())
        module.structs.push_back({schema_name, {{"value", cell.type}}, {"@nil_binding_cell"}});
      else if (existing->fields != std::vector<std::pair<std::string, std::string>>{{"value", cell.type}})
        throw std::runtime_error("Conflicting internal binding-reference schema");
    }

    const auto parameter_count = function.params.size();
    if (function.body.size() < parameter_count)
      throw std::runtime_error("Binding-reference function lacks its parameter prologue");
    for (std::size_t index = 0; index < parameter_count; ++index) {
      const auto &parameter = function.params[parameter_count - index - 1];
      const auto name = parameter.substr(0, parameter.find(':'));
      const auto &instruction = function.body[index];
      if (instruction.opname != "STORE_LOCAL" || instruction.args != std::vector<std::string>{name})
        throw std::runtime_error("Binding-reference function has an invalid parameter prologue");
    }

    std::vector<nil_instruction> body;
    const auto append = [&](const nil_instruction &original, const std::string &op,
                            std::vector<std::string> args) {
      auto generated = original;
      generated.opname = op;
      generated.args = std::move(args);
      body.push_back(std::move(generated));
    };
    for (std::size_t index = 0; index < parameter_count; ++index)
      body.push_back(function.body[index]);
    // No user statement is attributed to the compiler's storage setup. The
    // original parameter slots remain valid until each cell is initialized.
    nil_instruction setup;
    setup.source_file = function.source_file;
    std::set<std::string> pending_dynamic_cells;
    for (const auto &[slot, cell] : cells) {
      if (cell.reference_parameter) continue;
      // Anything has no default value. Build a local cell at its initializer,
      // after evaluating that actual value, instead of manufacturing a box.
      if (cell.type == "anything" && !cell.value_parameter) {
        pending_dynamic_cells.insert(slot);
        continue;
      }
      append(setup, "NEW_STRUCT", {binding_cell_name(cell.type)});
      if (cell.value_parameter) {
        append(setup, "LOAD_LOCAL", {slot});
      } else if (cell.type == "text") {
        const std::string empty = "$nil.binding.empty-text";
        if (std::none_of(module.string_constants.begin(), module.string_constants.end(),
                        [&](const auto &entry) { return entry.first == empty; }))
          module.string_constants.emplace_back(empty, "");
        append(setup, "PUSH_CONST_STR", {empty});
      } else {
        append(setup, "PUSH_CONST", {cell.type == "f64" ? "0.0" : "0"});
      }
      if (cell.type != "text" && cell.type != "anything") append(setup, "CAST", {cell.type});
      append(setup, "SET_FIELD", {"value"});
      append(setup, "STORE_LOCAL", {cell.storage});
    }

    std::size_t scratch = 0;
    for (std::size_t index = parameter_count; index < function.body.size(); ++index) {
      auto instruction = function.body[index];
      for (auto &debug : instruction.debug_locals) {
        const auto cell = cells.find(debug.storage);
        if (cell == cells.end()) continue;
        debug.storage = cell->second.storage;
        debug.type = "binding_ref<" + cell->second.type + ">";
      }
      const auto cell = instruction.args.empty() ? cells.end() : cells.find(instruction.args[0]);
      if (instruction.opname == "BINDING_REF_LOCAL") {
        if (cell == cells.end())
          throw std::runtime_error("Binding reference has no storage cell");
        if (pending_dynamic_cells.count(cell->first))
          throw std::runtime_error("Anything reference requires an initialized local binding");
        append(instruction, cell->second.reference_parameter ? "LOAD_LOCAL" : "REF_LOCAL",
               {cell->second.storage});
      } else if (instruction.opname == "LOAD_LOCAL" && cell != cells.end()) {
        if (pending_dynamic_cells.count(cell->first))
          throw std::runtime_error("Anything reference requires an initialized local binding");
        append(instruction, "LOAD_LOCAL", {cell->second.storage});
        append(instruction, "GET_FIELD", {"value"});
      } else if (instruction.opname == "STORE_LOCAL" && cell != cells.end()) {
        const auto temporary = "$nil.binding.value." + std::to_string(scratch++);
        // Re-establish the checked source storage type even when the ordinary
        // producer (for example an array index) has no precise CFG category.
        // Anything already carries a checked box: CAST cannot fabricate or
        // erase that provenance. Preserve the box exactly through the cell.
        if (cell->second.type != "anything")
          append(instruction, "CAST", {cell->second.type});
        append(instruction, "STORE_LOCAL", {temporary});
        if (pending_dynamic_cells.erase(cell->first))
          append(instruction, "NEW_STRUCT", {binding_cell_name(cell->second.type)});
        else
          append(instruction, "LOAD_LOCAL", {cell->second.storage});
        append(instruction, "LOAD_LOCAL", {temporary});
        append(instruction, "SET_FIELD", {"value"});
        append(instruction, "STORE_LOCAL", {cell->second.storage});
      } else {
        body.push_back(std::move(instruction));
      }
    }
    function.body = std::move(body);
  }
}

static std::string canonical_builtin_enum_base(std::string base_name) {
  const std::size_t separator = base_name.rfind("::");
  const std::size_t leaf_start =
      separator == std::string::npos ? 0 : separator + 2;
  std::string leaf = base_name.substr(leaf_start);
  std::transform(leaf.begin(), leaf.end(), leaf.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  if (leaf == "result")
    base_name.replace(leaf_start, std::string::npos, "Result");
  else if (leaf == "option")
    base_name.replace(leaf_start, std::string::npos, "Option");
  return base_name;
}

static std::string variant_leaf_name(const std::string &variant_name) {
  std::string canonical = canonicalize_variant_name(variant_name);
  const std::size_t separator = canonical.rfind("::");
  return separator == std::string::npos ? canonical
                                        : canonical.substr(separator + 2);
}

static bool equal_ascii_case_insensitive(const std::string &left,
                                         const std::string &right) {
  return left.size() == right.size() &&
         std::equal(left.begin(), left.end(), right.begin(),
                    [](unsigned char lhs, unsigned char rhs) {
                      return std::tolower(lhs) == std::tolower(rhs);
                    });
}

struct concrete_enum_variant_metadata {
  std::string enum_type;
  std::string payload_type;
};

static std::optional<concrete_enum_variant_metadata>
resolve_concrete_enum_variant(codegen &cg, nil_module &module,
                              const std::string &raw_enum_type,
                              const std::string &raw_variant_name) {
  const auto parsed = parse_generic_type_name(raw_enum_type);
  if (!parsed) {
    // Nominal enums need the same exact payload metadata as instantiated
    // generic enums when different variants have distinct native layouts.
    std::string enum_name = cg.lookup_enum_fqn(raw_enum_type);
    const ast_enum_decl *declaration = cg.lookup_enum_decl(enum_name);
    if (!declaration || !declaration->generic_params.empty())
      return std::nullopt;
    const std::string qualified_variant = canonicalize_variant_name(raw_variant_name);
    const auto owner_end = qualified_variant.rfind("::");
    if (owner_end != std::string::npos &&
        cg.lookup_enum_fqn(qualified_variant.substr(0, owner_end)) != enum_name)
      return std::nullopt;
    const std::string selected_name = variant_leaf_name(raw_variant_name);
    for (const auto &variant : declaration->variants) {
      if (variant.name == selected_name)
        return concrete_enum_variant_metadata{
            enum_name,
            variant.associated_type
                ? *variant.associated_type
                : std::string("void")};
    }
    return std::nullopt;
  }
  if (parsed->type_args.empty())
    return std::nullopt;

  const std::string canonical_base =
      canonical_builtin_enum_base(parsed->base_name);
  const ast_enum_decl *enum_decl = cg.lookup_enum_decl(canonical_base);
  std::string enum_base = canonical_base;
  if (!enum_decl) {
    enum_base = cg.lookup_enum_fqn(canonical_base);
    enum_decl = cg.lookup_enum_decl(enum_base);
  }
  if (!enum_decl && canonical_base.find("::") != std::string::npos) {
    const std::string unqualified =
        canonical_base.substr(canonical_base.rfind("::") + 2);
    enum_decl = cg.lookup_enum_decl(unqualified);
    if (enum_decl)
      enum_base = cg.lookup_enum_fqn(unqualified);
  }
  if (!enum_decl || enum_decl->generic_params.empty() ||
      enum_decl->generic_params.size() != parsed->type_args.size()) {
    return std::nullopt;
  }

  std::map<std::string, std::string> substitutions;
  std::vector<std::string> concrete_args;
  concrete_args.reserve(parsed->type_args.size());
  for (std::size_t index = 0; index < parsed->type_args.size(); ++index) {
    const std::string concrete =
        canonical_nil_type_name(parsed->type_args[index]);
    if (concrete.empty() || concrete == "any" || concrete == "<unknown>" ||
        concrete == "<error>" || concrete == "__local") {
      return std::nullopt;
    }
    for (const auto &generic_param : enum_decl->generic_params) {
      if (contains_type_identifier(concrete, generic_param.name))
        return std::nullopt;
    }
    concrete_args.push_back(concrete);
    substitutions[enum_decl->generic_params[index].name] = concrete;
  }

  // Use a single schema key for built-ins regardless of Result/result and
  // Option/option source spelling, and prefer the registered qualified name.
  enum_base = canonical_builtin_enum_base(enum_base);
  if (enum_base.find("::") == std::string::npos)
    enum_base = cg.lookup_enum_fqn(enum_base);
  std::string concrete_enum_type = enum_base + "<";
  for (std::size_t index = 0; index < concrete_args.size(); ++index) {
    if (index != 0)
      concrete_enum_type += ",";
    concrete_enum_type += concrete_args[index];
  }
  concrete_enum_type += ">";

  const std::string requested_variant = variant_leaf_name(raw_variant_name);
  const ast_enum_variant *selected_variant = nullptr;
  for (const auto &variant : enum_decl->variants) {
    if (variant.name == requested_variant ||
        equal_ascii_case_insensitive(variant.name, requested_variant)) {
      selected_variant = &variant;
      break;
    }
  }
  if (!selected_variant)
    return std::nullopt;

  const auto existing =
      std::find_if(module.enums.begin(), module.enums.end(),
                   [&](const nil_enum_def &definition) {
                     return definition.name == concrete_enum_type;
                   });
  if (existing == module.enums.end()) {
    nil_enum_def definition;
    definition.name = concrete_enum_type;
    for (const auto &variant : enum_decl->variants) {
      nil_enum_def::variant concrete_variant;
      concrete_variant.name = variant.name;
      if (variant.associated_type) {
        concrete_variant.associated_type =
            substitute_nil_type_name(*variant.associated_type, substitutions);
      }
      definition.variants.push_back(std::move(concrete_variant));
    }
    module.enums.push_back(std::move(definition));
  }

  return concrete_enum_variant_metadata{
      concrete_enum_type,
      selected_variant->associated_type
          ? substitute_nil_type_name(*selected_variant->associated_type,
                                     substitutions)
          : std::string("void")};
}

/*
 * nil_writer Implementation
 */
nil_writer::nil_writer(std::ostream &out, const codegen_options &opts)
    : out_(out), options_(opts) {}

void nil_writer::emit_text_instruction(std::ostream &os,
                                       const nil_instruction &instr) {
  // Write opname and arguments; quote string-like arguments if necessary
  os << std::setw(12) << std::left << instr.opname;
  if (!instr.args.empty()) {
    os << " "; // Ensure separation between opcode and args
    bool first = true;
    for (const auto &a : instr.args) {
      if (!first)
        os << " ";
      first = false;
      // If argument has whitespace or punctuation, put it in quotes
      bool needs_quote =
          a.find(' ') != std::string::npos || a.find('"') != std::string::npos;
      if (needs_quote) {
        os << '"' << escape_string(a) << '"';
      } else {
        os << a;
      }
    }
  }
  if (instr.debug_line > 0) {
    os << "  ; line " << instr.debug_line;
  }
  os << "\n";
}

// Emit function-level annotations (lines like `@inline` or `@test`) for the
// provided function into the textual output stream. The method prints any
// annotations in `fn.annotations` (raw textual form) and also outputs a
// `@inline` annotation when `fn.is_inline` is true. This helper is used by
// `emit_text_module` when outputting function declarations.
// Write annotation lines for any declaration. Shared with the function path so
// the two cannot drift: one spelling on disk, one set of limits.
void nil_writer::emit_declaration_annotations(std::ostream &os,
                                              const std::vector<std::string> &annotations) {
  if (annotations.size() > 65536U)
    throw std::runtime_error("Textual NIL declaration annotation count exceeds safety limit");
  for (const auto &a : annotations) {
    if (a.empty() || a.front() != '@' || a.find('\0') != std::string::npos ||
        a.size() > 16U * 1024U * 1024U)
      throw std::runtime_error("Textual NIL contains an invalid declaration annotation");
    if (a.find_first_of("\n\r\t") != std::string::npos || a.back() == ' ') {
      const auto escaped = escape_string(a);
      if (escaped.size() > 16U * 1024U * 1024U - 28U)
        throw std::runtime_error("Textual NIL declaration annotation exceeds line safety limit");
      os << "# declaration_annotation \"" << escaped << "\"\n";
    } else os << a << "\n";
  }
}

void nil_writer::emit_function_annotations(std::ostream &os,
                                           const nil_function &fn) {
  if (fn.annotations.size() > 65536U)
    throw std::runtime_error("Textual NIL function annotation count exceeds safety limit");
  const bool explicit_inline = std::any_of(fn.annotations.begin(), fn.annotations.end(),
      [](const auto &annotation) { return annotation_name_matches(annotation, "inline"); });
  if (fn.is_inline && !explicit_inline) {
    os << "@inline\n";
  }
  for (const auto &a : fn.annotations) {
    if (a.empty() || a.front() != '@' || a.find('\0') != std::string::npos ||
        a.size() > 16U * 1024U * 1024U) {
      throw std::runtime_error("Textual NIL contains an invalid function annotation");
    }
    // Preserve the complete explicit metadata sequence, including inline
    // properties and repeated annotations. Only a flag without an explicit
    // inline annotation requires a synthesized marker above.
    // Source annotation strings are already decoded. Preserve embedded line
    // controls (and trailing whitespace) without making them physical NIL
    // records; ordinary annotations retain their legacy textual spelling.
    if (a.find_first_of("\n\r\t") != std::string::npos || a.back() == ' ') {
      const auto escaped = escape_string(a);
      if (escaped.size() > 16U * 1024U * 1024U - 24U)
        throw std::runtime_error("Textual NIL function annotation exceeds line safety limit");
      os << "# function_annotation \"" << escaped << "\"\n";
    } else {
      os << a << "\n";
    }
  }
}

void nil_writer::emit_text_module(const nil_module &mod) {
  // Header
  out_ << "# NIL Textual Module\n";
  out_ << "module " << (mod.module_name.empty() ? "root" : mod.module_name)
       << "\n\n";

  // String constants
  if (!mod.string_constants.empty()) {
    out_ << "# String constants\n";
    for (const auto &kv : mod.string_constants) {
      out_ << "const " << kv.first << " = \"" << escape_string(kv.second)
           << "\"\n";
    }
    out_ << "\n";
  }

  // Struct definitions
  if (!mod.structs.empty()) {
    out_ << "# Struct definitions\n";
    for (const auto &s : mod.structs) {
      emit_declaration_annotations(out_, s.annotations);
      out_ << "struct " << s.name << "\n";
      for (const auto &f : s.fields) {
        out_ << "    " << f.first << ": " << f.second << "\n";
      }
      out_ << "end struct\n\n";
    }
  }

  // Explicit class identity is separate from flattened field schemas.
  for (const auto &klass : mod.classes) {
    emit_declaration_annotations(out_, klass.annotations);
    out_ << "class " << klass.name << " " << (klass.is_abstract ? 1 : 0) << "\n";
    for (const auto &base : klass.bases) out_ << "    base " << base << "\n";
    for (const auto &method : klass.methods) {
      out_ << "    method " << method.name << " "
           << (method.implementation.empty() ? "-" : method.implementation) << " "
           << '"' << escape_string(method.receiver_type) << "\" \""
           << escape_string(method.return_type) << "\" "
           << (method.is_virtual ? 1 : 0) << " " << (method.is_abstract ? 1 : 0)
           << " " << method.parameter_types.size();
      for (const auto &type : method.parameter_types) out_ << " \"" << escape_string(type) << '"';
      out_ << "\n";
    }
    out_ << "end class\n\n";
  }

  // Enum definitions
  if (!mod.enums.empty()) {
    out_ << "# Enum definitions\n";
    for (const auto &e : mod.enums) {
      emit_declaration_annotations(out_, e.annotations);
      out_ << "enum " << e.name << "\n";
      for (const auto &variant : e.variants) {
        out_ << "    " << variant.name;
        if (!variant.associated_type.empty())
          out_ << ": " << variant.associated_type;
        out_ << "\n";
      }
      out_ << "end enum\n\n";
    }
  }

  // Functions
  for (const auto &fn : mod.functions) {
    if (fn.source_line < 0 ||
        static_cast<std::uint64_t>(fn.source_line) > 2147483647ULL) {
      throw std::runtime_error(
          "Textual NIL function declaration line exceeds signed 32-bit range");
    }
    // Emit function-level annotations (if any) before the function header,
    // so tools and readers know about things like @inline, @deprecated, etc.
    emit_function_annotations(out_, fn);

    // Older readers ignore this comment. Keep the header itself unchanged for
    // tools that recognize complete function-header lines.
    out_ << "# declaration_line " << fn.source_line << '\n';
    out_ << "function " << fn.name;
    if (!fn.params.empty()) {
      out_ << "(";
      for (size_t i = 0; i < fn.params.size(); ++i) {
        if (i > 0)
          out_ << ", ";
        out_ << fn.params[i];
      }
      out_ << ")";
    }
    if (!fn.return_type.empty())
      out_ << " -> " << fn.return_type;
    out_ << ":\n";
    std::size_t debug_index = 0;
    for (const auto &instr : fn.body) {
      validate_source_debug(instr.source_file, instr.debug_locals);
      if (!instr.source_file.empty()) {
        out_ << "    # source_debug " << debug_index << " \""
             << escape_string(instr.source_file) << "\" " << instr.debug_locals.size();
        for (const auto &local : instr.debug_locals)
          out_ << " \"" << escape_string(local.name) << "\" \""
               << escape_string(local.storage) << "\" \"" << escape_string(local.type)
               << "\" " << local.scope_depth;
        out_ << '\n';
      }
      out_ << "    ";
      emit_text_instruction(out_, instr);
      ++debug_index;
    }
    out_ << "end function\n\n";
  }

  // Tests section - provides an explicit registry that can be used by test
  // harnesses / tools. We emit @test entries that list test function names
  // discovered during compilation. This complements per-function `@test`
  // annotations found in `fn.annotations` by providing a central registry.
  if (!mod.tests.empty()) {
    out_ << "# Tests\n";
    for (const auto &t : mod.tests) {
      out_ << "@test " << t << "\n";
    }
    out_ << "\n";
  }
}

void nil_writer::emit_binary_module(const nil_module &mod) {
  nil_binary_writer writer(out_, options_);
  writer.emit_binary_module(mod);
}

/*
 * Codegen Implementation
 */
codegen::codegen(const codegen_options &opts)
    : options_(opts), string_const_map_() {}

codegen::~codegen() = default;

// Helper to find a function declaration by name (for generics)
const ast_function_decl *
codegen::lookup_function_decl(const std::string &name) {
  // We need to scan the program declarations.
  // Since we don't store the program in the codegen class, we might need to
  // pass it or store a map. For now, let's assume we can build a map during
  // assemble_module or store the program reference. Actually, assemble_module
  // iterates over the program. We can build a map there. BUT,
  // generate_specialization needs it. Let's add a map to the codegen class and
  // populate it in assemble_module.
  auto it = function_decls_.find(name);
  if (it != function_decls_.end()) {
    return it->second;
  }
  return nullptr;
}

// Look up the generic impl that owns a method on a target type (e.g., "vec").
// One generic target can have multiple trait and inherent impl declarations.
// An impl block on a BUILTIN type must not be module-qualified.
//
// A method's NIL symbol is built from the impl's target type, while the call
// site builds it from the receiver's INFERRED type. For a user-declared type
// both are the module-qualified declaration name, so they agree. For a
// builtin, the inferred type is the bare canonical name ("String", "i64"), so
// prefixing the declaration with the current module made the two sides
// disagree: `impl i64` in module demo.prim emitted
// `demo__prim__i64__doubled` while the call emitted `i64__doubled`, and the
// interpreter reported an unknown function. The same happened for
// `impl text` / `impl String`.
//
// Returns the canonical bare name for a builtin target, or nullopt for a
// user-declared type. Container builtins (array, ptr, ref, vec, slice) are
// deliberately excluded: they have their own dispatch paths and are not
// covered by the fixtures that motivate this.
static std::optional<std::string>
builtin_impl_target(const std::string &name) {
  if (name == "text" || name == "string" || name == "String")
    return std::string("String");
  if (name == "int")
    return std::string("i32");
  if (name == "i8" || name == "i16" || name == "i32" || name == "i64" ||
      name == "u8" || name == "u16" || name == "u32" || name == "u64" ||
      name == "f32" || name == "f64" || name == "bool" || name == "char")
    return name;
  return std::nullopt;
}

const ast_impl_decl *
codegen::lookup_generic_impl(const std::string &target_type,
                             const std::string &method_name) {
  const auto find_method_impl = [&](const std::string &lookup_name) {
    auto found = generic_impl_decls_.find(lookup_name);
    if (found == generic_impl_decls_.end())
      return static_cast<const ast_impl_decl *>(nullptr);
    // Prefer the most recently declared matching impl, preserving the old
    // behavior when a project deliberately repeats a method.
    for (auto impl = found->second.rbegin(); impl != found->second.rend();
         ++impl) {
      if (lookup_impl_method(*impl, method_name))
        return *impl;
    }
    return static_cast<const ast_impl_decl *>(nullptr);
  };

  if (const auto *impl = find_method_impl(target_type))
    return impl;

  // Try stripping type parameters (e.g., "vec<i32>" -> "vec")
  std::string base = target_type;
  size_t lt = base.find('<');
  if (lt != std::string::npos) {
    base = base.substr(0, lt);
    if (const auto *impl = find_method_impl(base))
      return impl;
  }
  // Try stripping module qualifier (e.g., "core::vec" -> "vec")
  size_t colon = base.rfind("::");
  if (colon != std::string::npos) {
    std::string unqualified = base.substr(colon + 2);
    if (const auto *impl = find_method_impl(unqualified))
      return impl;
  }
  return nullptr;
}

// Look up a method in a generic impl
const ast_function_decl *
codegen::lookup_impl_method(const ast_impl_decl *impl,
                            const std::string &method_name) {
  if (!impl)
    return nullptr;
  for (const auto &method : impl->methods) {
    if (method && method->name == method_name) {
      return method.get();
    }
  }
  return nullptr;
}

std::string codegen::sanitize_identifier(const std::string &name) {
  std::string out = name;
  for (size_t i = 0; i < out.size(); ++i) {
    char &c = out[i];
    if (c == ':' || c == '<' || c == '>' || c == ',' || c == ' ' || c == '-')
      c = '_';
  }
  return out;
}

std::string codegen::materialize_struct_schema(const std::string &raw_type,
                                               nil_module &module, int line,
                                               unsigned depth,
                                               bool require_concrete) {
  const std::string type = canonical_nil_type_name(raw_type);
  const auto parsed = parse_generic_type_name(type);
  if (parsed && parsed->base_name == "tuple") {
    if (depth >= 64 || parsed->type_args.empty()) {
      reject_codegen_construct(line, "invalid or excessively nested tuple shape", "use a finite nonempty tuple");
      return type;
    }
    nil_struct_def definition;
    definition.name = type;
    definition.annotations.push_back("@nil_tuple");
    for (size_t index = 0; index < parsed->type_args.size(); ++index)
      definition.fields.emplace_back(std::to_string(index), parsed->type_args[index]);
    for (const auto &existing : module.structs) {
      if (canonical_nil_type_name(existing.name) != type) continue;
      if (existing.fields != definition.fields)
        reject_codegen_construct(line, "conflicting tuple shape '" + type + "'", "preserve ordered tuple element types");
      return type;
    }
    module.structs.push_back(definition);
    for (const auto &field : definition.fields)
      materialize_struct_schema(field.second, module, line, depth + 1, require_concrete);
    return type;
  }
  const auto *declaration =
      lookup_struct_decl(parsed ? parsed->base_name : type);
  if (!declaration || declaration->generic_params.empty()) {
    // Preserve exact handle element metadata, including schemas behind a
    // pointer/reference. The native layout validator still decides whether a
    // reachable handle or nested aggregate has an implemented representation.
    if (parsed && depth < 64) {
      for (const auto &argument : parsed->type_args)
        materialize_struct_schema(argument, module, line, depth + 1,
                                  require_concrete);
    }
    return type;
  }
  if (!parsed || parsed->type_args.size() != declaration->generic_params.size()) {
    if (require_concrete)
      reject_codegen_construct(line,
          "generic struct '" + type + "' requires concrete type arguments",
          "provide one concrete argument for each declared generic parameter");
    return type;
  }
  if (depth >= 64) {
    reject_codegen_construct(line,
        "generic struct schema expansion exceeds the nesting limit for '" + type + "'",
        "use a finite concrete aggregate layout");
    return type;
  }

  std::map<std::string, std::string> field_substitutions;
  std::string concrete_name = struct_qualified_names_.at(declaration) + "<";
  for (std::size_t index = 0; index < parsed->type_args.size(); ++index) {
    const std::string argument = canonical_nil_type_name(parsed->type_args[index]);
    bool unresolved = argument.empty() || argument == "any" ||
                      argument == "<unknown>" || argument == "<error>" ||
                      argument == "__local";
    for (const auto &parameter : declaration->generic_params)
      unresolved = unresolved || contains_type_identifier(argument, parameter.name);
    if (unresolved) {
      if (require_concrete)
        reject_codegen_construct(line,
            "generic struct '" + type + "' has unresolved type argument '" + argument + "'",
            "instantiate the struct with concrete types before emitting NIL");
      return type;
    }
    if (index != 0)
      concrete_name += ",";
    concrete_name += argument;
    field_substitutions[declaration->generic_params[index].name] = argument;
  }
  concrete_name += ">";

  nil_struct_def definition;
  definition.name = concrete_name;
  for (const auto &field : declaration->fields) {
    definition.fields.emplace_back(
        field.name, substitute_nil_type_name(field.type_name, field_substitutions));
  }
  for (const auto &existing : module.structs) {
    if (canonical_nil_type_name(existing.name) != concrete_name)
      continue;
    if (existing.fields != definition.fields)
      reject_codegen_construct(line,
          "conflicting concrete struct schemas for '" + concrete_name + "'",
          "use one unambiguous declared field layout for each concrete struct type");
    return concrete_name;
  }

  // Register before descending so recursive nominal fields are preserved as
  // recursive metadata, not infinitely expanded or replaced with word types.
  // Native lowering rejects unsupported nested/recursive layouts explicitly.
  module.structs.push_back(definition);
  for (const auto &field : definition.fields)
    materialize_struct_schema(field.second, module, line, depth + 1,
                              require_concrete);
  return concrete_name;
}

std::string codegen::mangle_function_name(const std::string &fqn) {
  // Simple mangle: replace spaces/:: with underscores
  std::string mangled = sanitize_identifier(fqn);
  return mangled;
}

std::string codegen::make_const_string_name(const std::string &value) {
  auto it = string_const_map_.find(value);
  if (it != string_const_map_.end())
    return it->second;
  // Registration strings are collected separately from each emitter's module
  // pool. Keep their names disjoint until both pools are joined below.
  std::string name = "__ny_registration_string_" +
                     std::to_string(string_const_map_.size() + 1);
  string_const_map_.emplace(value, name);
  return name;
}

/*
 * A small emitter that converts AST nodes into nil_instructions in a stack
 * machine style using simple instructions. It also performs a pragmatic
 * 'for' desugaring into an index-based loop for ranges or iterables.
 */
// Helper to calculate native size for FFI return types
static size_t get_native_size(codegen &cg, const std::string &type_name) {
  if (type_name.empty() || type_name == "void")
    return 0;
  if (type_name == "i32")
    return 4;
  if (type_name == "i64" || type_name == "f64" || type_name == "ptr" ||
      type_name == "text" || type_name == "String")
    return 8;
  if (type_name == "bool")
    return 1;

  // Generic check: vec/array handles are 8 bytes
  if (type_name.find("vec<") == 0 || type_name.find("array<") == 0 ||
      type_name.find("slice<") == 0)
    return 8;

  if (auto *decl = cg.lookup_struct_decl(type_name)) {
    size_t sum = 0;
    for (const auto &f : decl->fields) {
      sum += get_native_size(cg, f.type_name);
    }
    return sum; // Should add padding logic eventually
  }
  // If unknown, default to 8 (handle/ptr)
  return 8;
}

static bool is_declared_enum_type(codegen &cg, const std::string &type) {
  const auto parsed = parse_generic_type_name(type);
  const auto base = canonical_builtin_enum_base(parsed ? parsed->base_name : type);
  return cg.lookup_enum_decl(base) ||
         cg.lookup_enum_decl(cg.lookup_enum_fqn(base));
}

class emitter {
public:
  emitter(nil_function &out, nil_module &mod, codegen &cg,
          std::map<std::string, std::string> subs = {},
          const std::string &current_module = "")
      : out_fn(out), mod_(mod), cg_(cg), substitutions(std::move(subs)),
        label_id(0), tmp_id(0), current_module_(current_module) {}

  struct LoopContext {
    std::string start_label;
    std::string end_label;
    std::size_t cleanup_depth = 0;
    std::size_t ownership_depth = 0;
    // Empty start_label denotes a switch, which continue must skip.
    std::string result_slot;
    std::optional<std::string> result_type;
  };
  std::vector<LoopContext> loop_stack;
  // Physical expression emission remains active while recover temporarily
  // restores an outer lexical control context.
  std::size_t value_switch_emission_depth = 0;
  std::vector<std::set<std::string>> value_switch_outer_targets;
  bool exits_physical_value_switch(const LoopContext &target) const {
    return std::any_of(value_switch_outer_targets.begin(), value_switch_outer_targets.end(),
        [&](const auto &outer) { return outer.count(target.end_label) != 0; });
  }
  std::size_t ownership_depth = 0;
  bool ownership_checked = false;
  struct CleanupContext {
    std::string region;
    const ast_block_stmt *recover = nullptr;
    std::vector<std::string> fault_slots;
    std::unordered_map<std::string, std::string> types;
    std::unordered_map<std::string, std::string> bindings;
    std::map<const ast_fault_handler *, std::string> fault_bindings;
    std::vector<LoopContext> loops;
    std::size_t ownership_depth = 0;
    std::unordered_map<std::string, int> local_depths;
    int lexical_depth = 0;
  };
  std::vector<CleanupContext> cleanup_stack;
  std::map<const ast_fault_handler *, std::string> fault_bindings;
  void emit_member_receiver(const ast_expr &receiver) {
    emit_expr(receiver);
  }

  void emit_checked_reference(const ast_expr &expression,
                              const ast_expr_ptr &operand) {
    if (!operand || operand->kind != ast_node_kind::identifier_expr_node) {
      cg_.reject_codegen_construct(
          expression.line,
          "reference target is unavailable: NIL only lowers ref applied to "
          "a local identifier",
          "bind the value to a local before taking its reference");
      return;
    }
    if (!expression.checked_reference_handle &&
        !expression.checked_binding_reference) {
      cg_.reject_codegen_construct(
          expression.line,
          "reference creation requires a checked aggregate handle",
          "take ref of a checked local struct or class value; scalar, global, "
          "and nested references are unavailable");
      return;
    }
    const auto &identifier =
        static_cast<const ast_identifier_expr &>(*operand);
    if (!local_bindings.count(identifier.name)) {
      reject_escaped_local(identifier.name, expression.line);
      cg_.reject_codegen_construct(
          expression.line,
          "reference target '" + identifier.name + "' is not a local binding",
          "bind the aggregate value to a local before taking its reference");
      return;
    }
    if (expression.checked_binding_reference) {
      const auto type = substitute_nil_type_name(
          operand->inferred_type.value_or(""), substitutions);
      if (!supported_binding_scalar(type)) {
        cg_.reject_codegen_construct(expression.line,
            "binding reference requires an exact supported scalar type",
            "use i32, i64, u8, bool, f64, or text");
        return;
      }
      // This compiler-only marker is removed after lexical slots have been
      // assigned throughout the function. That pass sees references inside
      // branches before rewriting the declaration and every earlier access.
      push_instr("BINDING_REF_LOCAL", {identifier.name, type}, expression.line);
      return;
    }
    push_instr("REF_LOCAL", {identifier.name}, expression.line);
  }

  bool reject_reference_dereference(const ast_expr &operand, int line) {
    std::string type = operand.inferred_type.value_or("");
    if (type.empty() && operand.kind == ast_node_kind::identifier_expr_node) {
      const auto &identifier =
          static_cast<const ast_identifier_expr &>(operand);
      const auto local = local_var_types.find(identifier.name);
      if (local != local_var_types.end())
        type = local->second;
    }
    const auto parsed = parse_generic_type_name(
        substitute_nil_type_name(type, substitutions));
    if (!parsed || parsed->base_name != "ref")
      return false;
    // ref<T> is an aggregate handle, not an address with the raw pointer ABI.
    // Keep this boundary even when diagnostic continuation is requested.
    cg_.reject_codegen_construct(
        line, "explicit ref dereference is unavailable",
        "access aggregate reference fields or methods directly; only ptr<T> "
        "supports explicit pointer dereference");
    return true;
  }

  // Lexical visibility is separate from the function-wide NIL storage map.
  // Preserve the readable source name for its first binding and give every
  // subsequent binding an unspellable, deterministic storage identity.
  struct lexical_scope {
    emitter &owner;
    std::unordered_map<std::string, std::string> types;
    std::unordered_map<std::string, std::string> bindings;
    std::map<const ast_fault_handler *, std::string> faults;
    std::unordered_map<std::string, int> depths;
    explicit lexical_scope(emitter &value)
        : owner(value), types(value.local_var_types),
          bindings(value.local_bindings), faults(value.fault_bindings),
          depths(value.local_depths) { ++owner.lexical_depth; }
    ~lexical_scope() {
      owner.local_var_types = std::move(types);
      owner.local_bindings = std::move(bindings);
      owner.fault_bindings = std::move(faults);
      owner.local_depths = std::move(depths);
      --owner.lexical_depth;
    }
  };

  CleanupContext cleanup_context(const std::string &region,
                                const ast_block_stmt *recover,
                                std::vector<std::string> slots = {}) const {
    return {region, recover, std::move(slots), local_var_types, local_bindings,
            fault_bindings, loop_stack, ownership_depth, local_depths, lexical_depth};
  }

  void emit_recover(const CleanupContext &context) {
    if (!context.recover) return;
    // Inline cleanup still has its lexical environment: a body-local shadow
    // or loop is not visible merely because this copy precedes its return.
    lexical_scope scope(*this);
    const auto saved_loops = loop_stack;
    const auto saved_depth = lexical_depth;
    local_var_types = context.types;
    local_bindings = context.bindings;
    fault_bindings = context.fault_bindings;
    local_depths = context.local_depths;
    lexical_depth = context.lexical_depth;
    loop_stack = context.loops;
    emit_stmt(*context.recover);
    lexical_depth = saved_depth;
    loop_stack = saved_loops;
  }

  void emit_exited_cleanups(std::size_t retained_depth, int line,
                            std::size_t retained_ownership = 0) {
    const auto saved = cleanup_stack;
    const auto saved_owned_depth = ownership_depth;
    while (cleanup_stack.size() > retained_depth) {
      const auto context = cleanup_stack.back();
      cleanup_stack.pop_back();
      while (ownership_depth > context.ownership_depth) {
        push_instr("OWN_LEAVE", {}, line);
        --ownership_depth;
      }
      push_instr("FAULT_LEAVE", {context.region}, line);
      for (const auto &slot : context.fault_slots)
        push_instr("FAULT_DROP", {slot}, line);
      // The current context is already retired, so a return/raise/loop
      // transfer from recover replaces this exit and only runs outer cleanup.
      emit_recover(context);
    }
    while (ownership_depth > retained_ownership) {
      push_instr("OWN_LEAVE", {}, line);
      --ownership_depth;
    }
    ownership_depth = saved_owned_depth;
    cleanup_stack = saved;
  }

  void emit_recover_and_resume(const CleanupContext &context,
                               const std::string &slot, int line) {
    if (!context.recover) {
      push_instr("FAULT_RESUME", {slot}, line);
      return;
    }
    const auto region = new_label("fault_recover_region");
    const auto landing = new_label("fault_recover_landing");
    const auto replacement = new_temp("fault_recover_replacement");
    push_instr("FAULT_ENTER", {region, landing}, line);
    // This region exists solely to release the suspended fault if recover
    // exits abruptly; it must never execute the same recover recursively.
    cleanup_stack.push_back(cleanup_context(region, nullptr, {slot}));
    emit_recover(context);
    cleanup_stack.pop_back();
    push_instr("FAULT_LEAVE", {region}, line);
    push_instr("FAULT_RESUME", {slot}, line);
    push_instr("LABEL", {landing}, line);
    push_instr("FAULT_SAVE", {replacement}, line);
    push_instr("FAULT_DROP", {slot}, line);
    push_instr("FAULT_RESUME", {replacement}, line);
  }

  void emit_attempt(const ast_attempt_stmt &attempt) {
    if (!attempt.fault_checked) {
      cg_.reject_codegen_construct(attempt.line,
          "fault lowering requires checked fault control flow",
          "correct all semantic errors before compiling attempt/fault/recover");
      return;
    }
    const auto body_region = new_label("fault_body_region");
    const auto body_landing = new_label("fault_body_landing");
    const auto dispatch_region = new_label("fault_dispatch_region");
    const auto dispatch_landing = new_label("fault_dispatch_landing");
    const auto end = new_label("fault_end");
    const auto original = new_temp("fault_original");
    const auto replacement = new_temp("fault_replacement");
    const auto body_context = cleanup_context(body_region, attempt.recover.get());

    push_instr("FAULT_ENTER", {body_region, body_landing}, attempt.line);
    cleanup_stack.push_back(body_context);
    if (attempt.body) emit_stmt(*attempt.body);
    cleanup_stack.pop_back();
    push_instr("FAULT_LEAVE", {body_region}, attempt.line);
    emit_recover(body_context);
    push_instr("JMP", {end}, attempt.line);

    push_instr("LABEL", {body_landing}, attempt.line);
    push_instr("FAULT_SAVE", {original}, attempt.line);
    push_instr("FAULT_ENTER", {dispatch_region, dispatch_landing}, attempt.line);
    const auto dispatch_context =
        cleanup_context(dispatch_region, attempt.recover.get(), {original});
    cleanup_stack.push_back(dispatch_context);
    for (const auto &handler : attempt.handlers) {
      if (!handler || !handler->fault_checked) {
        cg_.reject_codegen_construct(attempt.line,
            "fault lowering requires checked fault handler identity",
            "correct the fault filter and handler before compiling");
        continue;
      }
      lexical_scope handler_scope(*this);
      const auto next = new_label("fault_next_handler");
      if (handler->checked_fault_type) {
        const auto type = cg_.materialize_struct_schema(
            substitute_nil_type_name(*handler->checked_fault_type, substitutions),
            mod_, handler->line, 0, true);
        push_instr("FAULT_MATCH", {original, type}, handler->line);
        push_instr("JMP_IF_NOT", {next}, handler->line);
        push_instr("FAULT_BIND", {original, type}, handler->line);
        declare_local(handler->variable, type);
        push_instr("STORE_LOCAL", {handler->variable}, handler->line);
      } else {
        fault_bindings[handler.get()] = original;
      }
      if (handler->guard) {
        emit_expr(*handler->guard);
        push_instr("JMP_IF_NOT", {next}, handler->line);
      }
      if (handler->body) emit_stmt(*handler->body);
      cleanup_stack.pop_back();
      push_instr("FAULT_LEAVE", {dispatch_region}, handler->line);
      push_instr("FAULT_DROP", {original}, handler->line);
      emit_recover(body_context);
      push_instr("JMP", {end}, handler->line);
      cleanup_stack.push_back(dispatch_context);
      push_instr("LABEL", {next}, handler->line);
    }
    cleanup_stack.pop_back();
    push_instr("FAULT_LEAVE", {dispatch_region}, attempt.line);
    emit_recover_and_resume(body_context, original, attempt.line);

    push_instr("LABEL", {dispatch_landing}, attempt.line);
    push_instr("FAULT_SAVE", {replacement}, attempt.line);
    push_instr("FAULT_DROP", {original}, attempt.line);
    emit_recover_and_resume(body_context, replacement, attempt.line);
    push_instr("LABEL", {end}, attempt.line);
  }

  // Register parameter names and their textual types from the AST function
  // declaration so codegen can perform type-based optimizations (e.g., for
  // arrays/vecs). The emitter stores these in `local_var_types` for later
  // lookups when optimizing for-range loops.
  void register_function_params(const ast_function_decl &decl) {
    register_function_params(decl.params);
    ownership_checked = decl.checked_ownership;
    if (contains_ownership_type(decl.return_type.value_or("")))
      cg_.reject_codegen_construct(decl.line, "owned values and borrows cannot be function return values", "keep ownership local and return a scalar value");
  }

  void register_function_params(const std::vector<ast_param> &params) {
    local_var_types.clear();
    local_bindings.clear();
    local_depths.clear();
    for (const auto &p : params) {
      if (contains_ownership_type(p.type_name.value_or("")))
        cg_.reject_codegen_construct(p.line, "owned values and borrows cannot be function parameters", "pass copied scalar values");
      // Keep signature/prologue names unchanged; native ABI matching and
      // handwritten NIL continue to use their existing parameter contract.
      local_bindings[p.name] = p.name;
      used_local_names.insert(p.name);
      local_var_types[p.name] = p.type_name.value_or("__local");
      local_depths[p.name] = 0;
    }
  }

  void emit_scalar_boundary(const std::optional<std::string> &type, int line) {
    if (!type)
      return;
    auto resolved = substitute_nil_type_name(*type, substitutions);
    if (resolved == "int")
      resolved = "i32";
    if (resolved == "i32" || resolved == "i64" || resolved == "u8" ||
        resolved == "bool" || resolved == "f64" || resolved == "f32")
      push_instr("CAST", {resolved}, line);
  }

  bool emit_array_intrinsic(const ast_function_decl &declaration,
                            const std::string &name,
                            const std::vector<std::string> &type_arguments,
                            std::size_t argument_count, int line) {
    if (!is_array_intrinsic(name))
      return false;
    const auto reject = [&](const std::string &reason) {
      cg_.reject_codegen_construct(line,
          "invalid array intrinsic declaration: " + reason,
          "Use an exact supported array element, i32 size/index, and matching result type.");
      return true;
    };
    auto concrete = substitutions;
    if (declaration.generic_params.size() != type_arguments.size())
      return reject("array intrinsic requires concrete generic arguments");
    for (std::size_t index = 0; index < type_arguments.size(); ++index)
      concrete[declaration.generic_params[index].name] = type_arguments[index];
    const auto result = substitute_nil_type_name(
        declaration.return_type.value_or("void"), concrete);
    const bool creates = name.rfind("array.new", 0) == 0;
    const auto array_type = creates ? result
        : declaration.params.empty() ? std::string{}
        : substitute_nil_type_name(declaration.params[0].type_name.value_or(""), concrete);
    const auto parsed = parse_generic_type_name(array_type);
    if (!parsed || parsed->base_name != "array" || parsed->type_args.size() != 1)
      return reject("array intrinsic declaration requires an exact array type");
    std::vector<std::string> operands = {
        "intrinsic", name, std::to_string(argument_count), result,
        parsed->type_args[0]};
    array_intrinsic_contract contract;
    try {
      operands[3] = canonical_array_type(operands[3]);
      operands[4] = canonical_array_type(operands[4]);
      contract = parse_array_intrinsic(operands);
    } catch (const std::runtime_error &error) {
      return reject(error.what());
    }
    std::vector<std::string> expected;
    if (creates)
      expected = {"i32", contract.element};
    else {
      expected = {"array<" + contract.element + ">"};
      if (contract.operation != array_intrinsic_operation::length)
        expected.push_back("i32");
      if (contract.operation == array_intrinsic_operation::set)
        expected.push_back(contract.element);
    }
    if (declaration.params.size() != expected.size())
      return reject("array intrinsic declaration has an invalid parameter count");
    for (std::size_t index = 0; index < expected.size(); ++index) {
      std::string actual;
      try {
        actual = canonical_array_type(substitute_nil_type_name(
            declaration.params[index].type_name.value_or(""), concrete));
      } catch (const std::runtime_error &error) {
        return reject(error.what());
      }
      if (actual != expected[index])
        return reject("array intrinsic declaration has an invalid parameter type");
    }
    if (is_array_tuple_type(contract.element))
      cg_.materialize_struct_schema(contract.element, mod_, line, 0, true);
    push_instr("INTEROP_CALL", operands, line);
    return true;
  }

  std::string struct_construction_type(
      const std::string &raw_type,
      const std::optional<std::string> &contextual_type) {
    std::string type = substitute_nil_type_name(raw_type, substitutions);
    if (parse_generic_type_name(type) || !contextual_type)
      return type;
    const auto context = substitute_nil_type_name(*contextual_type, substitutions);
    const auto parsed_context = parse_generic_type_name(context);
    const auto *declaration = cg_.lookup_struct_decl(type);
    if (parsed_context && declaration && !declaration->generic_params.empty() &&
        cg_.lookup_struct_decl(parsed_context->base_name) == declaration)
      return context;
    return type;
  }

  std::optional<std::string> struct_field_type(const std::string &raw_type,
                                               const std::string &field_name) {
    const auto type = substitute_nil_type_name(raw_type, substitutions);
    const auto generic = parse_generic_type_name(type);
    if (generic && generic->base_name == "tuple") {
      for (size_t index = 0; index < generic->type_args.size(); ++index)
        if (field_name == std::to_string(index)) return generic->type_args[index];
      return std::nullopt;
    }
    const auto *declaration = cg_.lookup_struct_decl(
        generic ? generic->base_name : type);
    if (!declaration) {
      if (const auto *class_decl = cg_.lookup_class_decl(type))
        for (const auto &field : class_decl->checked_fields)
          if (field.name == field_name)
            return substitute_nil_type_name(field.type_name, substitutions);
      return std::nullopt;
    }
    auto field_substitutions = substitutions;
    if (generic && generic->type_args.size() == declaration->generic_params.size()) {
      for (std::size_t index = 0; index < generic->type_args.size(); ++index)
        field_substitutions[declaration->generic_params[index].name] =
            generic->type_args[index];
    }
    for (const auto &field : declaration->fields) {
      if (field.name == field_name)
        return substitute_nil_type_name(field.type_name, field_substitutions);
    }
    return std::nullopt;
  }

  bool dynamic_default_requires_initializer(const std::string &written) {
    std::set<std::string> active;
    std::function<bool(const std::string &)> contains = [&](const std::string &name) {
      const auto type = canonical_dynamic_type(substitute_nil_type_name(name, substitutions));
      if (type == "anything") return true;
      if (!active.insert(type).second || type.rfind("fn(", 0) == 0) return false;
      const auto parsed = parse_generic_type_name(type);
      const auto base = parsed ? parsed->base_name : type;
      if (base == "ptr" || base == "ref") return false;
      if (parsed)
        for (const auto &argument : parsed->type_args)
          if (contains(argument)) return true;
      if (const auto *declaration = cg_.lookup_struct_decl(base)) {
        for (const auto &field : declaration->fields) {
          const auto actual = struct_field_type(type, field.name);
          if (actual && contains(*actual)) return true;
        }
      } else if (const auto *declaration = cg_.lookup_class_decl(base)) {
        for (const auto &field : declaration->checked_fields)
          if (contains(field.type_name)) return true;
      } else if (const auto *declaration = cg_.lookup_enum_decl(base)) {
        auto concrete = substitutions;
        if (parsed)
          for (std::size_t i = 0; i < declaration->generic_params.size() && i < parsed->type_args.size(); ++i)
            concrete[declaration->generic_params[i].name] = parsed->type_args[i];
        for (const auto &variant : declaration->variants)
          if (variant.associated_type) {
            auto payload = substitute_nil_type_name(*variant.associated_type, concrete);
            if (!payload.empty() && payload.front() == '(' && payload.back() == ')')
              payload = "tuple<" + payload.substr(1, payload.size() - 2) + ">";
            if (contains(payload)) return true;
          }
      }
      return false;
    };
    return contains(written);
  }

  void emit_arithmetic(const std::string &opcode, const ast_expr &expression) {
    if (!expression.inferred_type) {
      // Explicit --no-typecheck retains legacy numeric-word NIL semantics.
      push_instr(opcode, {}, expression.line);
      return;
    }
    const auto type = substitute_nil_type_name(*expression.inferred_type,
                                               substitutions);
    const bool integer_only = opcode == "BIT_AND" || opcode == "BIT_OR" ||
        opcode == "BIT_XOR" || opcode == "SHL" || opcode == "SHR";
    if (type != "i32" && type != "i64" && type != "u8" &&
        !(type == "f64" && !integer_only)) {
      static const std::set<std::string> scalar_names = {
          "bool", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
          "isize", "usize", "f32", "f64"};
      const std::string diagnostic = scalar_names.count(type)
          ? "unsupported scalar arithmetic type '" + type + "'"
          : opcode + (opcode == "NEG" ? " operand must be a numeric scalar"
                                        : " operands must be numeric scalars");
      cg_.reject_codegen_construct(expression.line,
          diagnostic,
          "use i32, i64, u8, or f64 arithmetic; cast booleans explicitly");
      return;
    }
    push_instr(opcode, {type}, expression.line);
  }

  // Recover only unambiguous expression types for direct AST clients that do
  // not run the checker. This supplies array element metadata, without adding
  // typed arithmetic or implicit scalar storage conversions to that path.
  std::string unambiguous_expression_type(const ast_expr &expression) const {
    if (expression.inferred_type)
      return substitute_nil_type_name(*expression.inferred_type, substitutions);
    switch (expression.kind) {
    case ast_node_kind::number_literal_node: {
      const auto &number = static_cast<const ast_number_literal &>(expression);
      if (is_floating_literal_spelling(number.value)) return "f64";
      integer_literal_info info;
      return parse_integer_literal(number.value, info) ? "" : info.inferred_type();
    }
    case ast_node_kind::boolean_literal_node: return "bool";
    case ast_node_kind::char_literal_node: return "u8";
    case ast_node_kind::string_literal_node: return "text";
    case ast_node_kind::cast_expr_node:
      return substitute_nil_type_name(
          static_cast<const ast_cast_expr &>(expression).target_type, substitutions);
    case ast_node_kind::identifier_expr_node: {
      const auto &name = static_cast<const ast_identifier_expr &>(expression).name;
      const auto local = local_var_types.find(name);
      return local == local_var_types.end() ? "" :
          substitute_nil_type_name(local->second, substitutions);
    }
    case ast_node_kind::unary_expr_node: {
      const auto &unary = static_cast<const ast_unary_expr &>(expression);
      if (unary.operand && (unary.op == token_kind::minus || unary.op == token_kind::tilde))
        return unambiguous_expression_type(*unary.operand);
      return {};
    }
    case ast_node_kind::array_literal_expr_node: {
      const auto &array = static_cast<const ast_array_literal_expr &>(expression);
      const auto element = unambiguous_array_element_type(array);
      return element == "any" ? "" : "array<" + element + ">";
    }
    default: return {};
    }
  }

  std::string unambiguous_array_element_type(const ast_array_literal_expr &array) const {
    std::string result;
    for (const auto &element : array.elements) {
      if (!element) return "any";
      auto type = unambiguous_expression_type(*element);
      if (type.empty() || type == "any" || type == "__local" || type == "<unknown>")
        return "any";
      if (type == "int") type = "i32";
      if (result.empty()) result = type;
      else if ((result == "i32" && type == "i64") ||
               (result == "i64" && type == "i32")) result = "i64";
      else if (result != type) return "any";
    }
    return result.empty() ? "any" : result;
  }

  void emit_switch(const ast_expr_ptr &target,
                   const std::vector<ast_switch_arm> &arms,
                   bool checked, int line,
                   const std::optional<std::string> &result_type) {
    if (!checked || !target) {
      cg_.reject_codegen_construct(line,
          "switch requires checked selector, case labels, and control flow",
          "enable type checking and correct switch diagnostics");
      return;
    }
    struct expression_scope {
      emitter &owner;
      bool active;
      expression_scope(emitter &e, bool a) : owner(e), active(a) {
        if (!active) return;
        ++owner.value_switch_emission_depth;
        std::set<std::string> outer;
        for (const auto &context : owner.loop_stack) outer.insert(context.end_label);
        owner.value_switch_outer_targets.push_back(std::move(outer));
      }
      ~expression_scope() {
        if (!active) return;
        --owner.value_switch_emission_depth;
        owner.value_switch_outer_targets.pop_back();
      }
    } expression_context(*this, result_type.has_value());
    lexical_scope scope(*this);
    const auto selector = new_temp("switch_target");
    const auto target_type = substitute_nil_type_name(target->inferred_type.value_or(""), substitutions);
    local_var_types[selector] = target_type;
    emit_expr(*target);
    emit_scalar_boundary(target_type, line);
    push_instr("STORE_LOCAL", {selector}, line);
    const auto end = new_label("L_switch_end");
    std::string result;
    if (result_type) {
      result = new_temp("switch_result");
      local_var_types[result] = substitute_nil_type_name(*result_type, substitutions);
    }
    std::vector<std::string> labels;
    labels.reserve(arms.size());
    for (std::size_t index = 0; index < arms.size(); ++index)
      labels.push_back(new_label("L_switch_case"));
    std::string fallback = end;
    const auto enum_type = enum_type_for_expression(*target);
    // Dispatch precedes the bodies. Falling out of a body therefore executes
    // the next body without testing its label again, including a default arm.
    for (std::size_t index = 0; index < arms.size(); ++index) {
      const auto &arm = arms[index];
      if (!arm.label) { fallback = labels[index]; continue; }
      push_instr("LOAD_LOCAL", {selector}, arm.line);
      if (!arm.enum_variant.empty()) {
        emit_is_variant(arm.enum_variant, enum_type, arm.line);
      } else {
        // Use i64 for integral dispatch so signed i32 and unsigned u8 labels
        // have the same comparison interpretation on every execution route.
        const bool integral = target_type == "i32" || target_type == "i64" || target_type == "u8";
        if (integral) push_instr("CAST", {"i64"}, arm.line);
        emit_expr(*arm.label);
        emit_scalar_boundary(arm.label->inferred_type, arm.line);
        if (integral) {
          push_instr("CAST", {"i64"}, arm.line);
        }
        push_instr("CMP_EQ", {}, arm.line);
      }
      push_instr("JMP_IF", {labels[index]}, arm.line);
    }
    push_instr("JMP", {fallback}, line);
    loop_stack.push_back({"", end, cleanup_stack.size(), ownership_depth, result, result_type});
    for (std::size_t index = 0; index < arms.size(); ++index) {
      push_instr("LABEL", {labels[index]}, arms[index].line);
      lexical_scope arm_scope(*this);
      if (arms[index].body) emit_stmt(*arms[index].body);
    }
    loop_stack.pop_back();
    push_instr("LABEL", {end}, line);
    if (result_type) push_instr("LOAD_LOCAL", {result}, line);
  }

  void emit_stmt(const ast_stmt &s) {
    switch (s.kind) {
    case ast_node_kind::inline_test_stmt_node: {
      if (!cg_.test_mode()) break;
      const auto &region = static_cast<const ast_inline_test_stmt &>(s);
      if (!region.checked_test) {
        cg_.reject_codegen_construct(s.line, "inline tests require checked lexical and control-flow boundaries",
            "fix the test diagnostics before generating NIL");
        break;
      }
      if (region.test && region.test->body) emit_stmt(*region.test->body);
      break;
    }
    case ast_node_kind::block_stmt_node: {
      const auto &blk = static_cast<const ast_block_stmt &>(s);
      lexical_scope scope(*this);
      const bool owned_scope = std::any_of(blk.statements.begin(), blk.statements.end(),
          [](const auto &statement) {
            if (!statement || statement->kind != ast_node_kind::decl_stmt_node) return false;
            const auto &declaration = static_cast<const ast_decl_stmt &>(*statement).declaration;
            if (!declaration || declaration->kind != ast_node_kind::variable_decl_node) return false;
            const auto &variable = static_cast<const ast_variable_decl &>(*declaration);
            const auto type = variable.type_name.value_or(variable.initializer ? variable.initializer->inferred_type.value_or("") : "");
            return type.rfind("owned<", 0) == 0;
          });
      if (owned_scope) { push_instr("OWN_ENTER", {}, s.line); ++ownership_depth; }
      for (const auto &st : blk.statements) {
        if (st)
          emit_stmt(*st);
      }
      if (owned_scope) { push_instr("OWN_LEAVE", {}, s.line); --ownership_depth; }
      break;
    }
    case ast_node_kind::decl_stmt_node: {
      const auto &ds = static_cast<const ast_decl_stmt &>(s);
      if (ds.declaration &&
          ds.declaration->kind == ast_node_kind::variable_decl_node) {
        const auto &vd =
            static_cast<const ast_variable_decl &>(*ds.declaration);
        if (!vd.tuple_bindings.empty()) {
          if (!vd.initializer || !vd.type_name) {
            cg_.reject_codegen_construct(ds.line, "tuple destructuring requires checked tuple types", "enable type checking");
            break;
          }
          const auto tuple_type = substitute_nil_type_name(*vd.type_name, substitutions);
          cg_.materialize_struct_schema(tuple_type, mod_, ds.line);
          emit_expr(*vd.initializer, tuple_type);
          const auto temporary = new_temp("tuple");
          declare_local(temporary, tuple_type);
          push_instr("STORE_LOCAL", {temporary}, ds.line);
          for (size_t index = 0; index < vd.tuple_bindings.size(); ++index) {
            const auto &binding = vd.tuple_bindings[index];
            if (binding->name == "_") continue;
            push_instr("LOAD_LOCAL", {temporary}, ds.line);
            push_instr("GET_FIELD", {std::to_string(index)}, ds.line);
            declare_local(binding->name, substitute_nil_type_name(binding->type_name.value_or("any"), substitutions));
            push_instr("STORE_LOCAL", {binding->name}, ds.line);
          }
          break;
        }
        const auto fallback_type = vd.initializer
            ? unambiguous_expression_type(*vd.initializer) : std::string{};
        const auto local_type = vd.type_name.value_or(
            fallback_type.empty() ? "__local" : fallback_type);
        if (vd.initializer) {
          emit_expr(*vd.initializer, vd.type_name);
          // NY-001: When a type annotation is present (e.g. `let x: f64 =
          // fn()`) and the typechecker did not run (--no-typecheck), the stack
          // value may have the wrong runtime type. Emit CAST to coerce
          // primitives. Only applies to scalar types — structs/arrays can't be
          // cast this way.
          emit_scalar_boundary(vd.type_name ? vd.type_name :
                               vd.initializer->inferred_type, ds.line);
          // The initializer resolves before introducing the new binding:
          // `let value = value + 1` reads the visible outer declaration.
          declare_local(vd.name,
                        local_type);
          nil_instruction instr;
          instr.opname = "STORE_LOCAL";
          instr.args.push_back(vd.name);
          instr.debug_line = ds.line;
          push_instr(instr);
          if (local_var_types[vd.name].rfind("owned<", 0) == 0)
            push_instr("OWN_BIND", {vd.name}, ds.line);
        } else {
          if (vd.rejected_dynamic_initializer ||
              (vd.type_name && dynamic_default_requires_initializer(*vd.type_name))) {
            cg_.reject_codegen_construct(vd.line, "an anything binding requires an initial value",
                "initialize it or use Option<anything> for absence");
            break;
          }
          declare_local(vd.name, vd.type_name.value_or("__local"));
        }
        // Record declared/annotated type for local variable if available.
        // This allows later for-in optimizations to inspect the container's
        // element type for index-based loops.
        // Always mark as a known local (even without type) so the identifier
        // handler can distinguish locals from fn-pointer references.
        local_var_types[vd.name] =
            local_type;
      }
      break;
    }
    case ast_node_kind::asm_stmt_node: {
      const auto &assembly = static_cast<const ast_asm_stmt &>(s);
      const auto error = inline_asm_contract_error(
          assembly.target, assembly.assembly_code, assembly.inputs.size(),
          assembly.output ? 1 : 0);
      if (!error.empty()) {
        cg_.reject_codegen_construct(s.line, error,
            "use asm \"x86_64\", \"template\" (i64 inputs) -> local end asm");
        break;
      }
      if (assembly.output &&
          (local_var_types.find(*assembly.output) == local_var_types.end() ||
           local_var_types.at(*assembly.output) != "i64")) {
        cg_.reject_codegen_construct(s.line,
            "inline assembly output must be an explicitly typed i64 local",
            "declare let mut result: i64 = 0 before the asm block");
        break;
      }
      for (const auto &input : assembly.inputs)
        emit_expr(*input);
      const auto constant = get_or_add_const_string_name(assembly.assembly_code);
      push_instr("INLINE_ASM", {assembly.target, constant,
          std::to_string(assembly.inputs.size()), assembly.output ? "1" : "0"},
          s.line);
      if (assembly.output)
        push_instr("STORE_LOCAL", {*assembly.output}, s.line);
      break;
    }
    case ast_node_kind::println_stmt_node: {
      const auto &p = static_cast<const ast_println_stmt &>(s);
      // Keep diagnostic continuation from reopening the historical VM/native
      // arity disagreement or lowering side effects from an invalid statement.
      if (p.args.size() != 1) {
        cg_.reject_codegen_construct(
            p.line, "println expects exactly 1 argument, got " +
                        std::to_string(p.args.size()),
            "use separate println statements; println(\"\") prints a blank line");
        break;
      }
      emit_expr(*p.args.front());
      // Emit call - println is a built-in runtime function
      nil_instruction instr;
      instr.opname = "CALL";
      instr.args.push_back("println");
      instr.args.push_back(std::to_string(p.args.size()));
      instr.debug_line = p.line;
      push_instr(instr);
      break;
    }
    case ast_node_kind::return_stmt_node: {
      const auto &r = static_cast<const ast_return_stmt &>(s);
      std::optional<std::string> saved_return;
      if (r.value) {
        const std::optional<std::string> return_type =
            out_fn.return_type.empty() || out_fn.return_type == "void"
                ? std::nullopt
                : std::optional<std::string>(out_fn.return_type);
        emit_expr(*r.value, return_type);
        emit_scalar_boundary(return_type, r.line);
        if (!cleanup_stack.empty()) {
          saved_return = new_temp("fault_saved_return");
          push_instr("STORE_LOCAL", {*saved_return}, r.line);
        }
      }
      emit_exited_cleanups(0, r.line);
      if (saved_return)
        push_instr("LOAD_LOCAL", {*saved_return}, r.line);
      nil_instruction instr;
      instr.opname = "RET";
      if (value_switch_emission_depth)
        instr.args = {nil_switch_return_marker};
      instr.debug_line = r.line;
      push_instr(instr);
      break;
    }
    case ast_node_kind::if_stmt_node: {
      const auto &ifs = static_cast<const ast_if_stmt &>(s);
      std::string label_end = new_label("L_if_end");
      std::string label_else = new_label("L_if_else");

      emit_expr(*ifs.condition);

      // If else branch exists, jump to else label on false
      // Otherwise jump to end
      std::string jump_target = ifs.else_branch ? label_else : label_end;
      push_instr("JMP_IF_NOT", {jump_target}, ifs.line);

      if (ifs.then_branch)
        emit_stmt(*ifs.then_branch);

      if (ifs.else_branch) {
        push_instr("JMP", {label_end}, ifs.line);
        push_instr("LABEL", {label_else}, ifs.line);
        emit_stmt(*ifs.else_branch);
      }

      push_instr("LABEL", {label_end}, ifs.line);
      break;
    }

    case ast_node_kind::break_stmt_node: {
      const auto &statement = static_cast<const ast_break_stmt &>(s);
      auto destination = loop_stack.rbegin();
      for (; destination != loop_stack.rend(); ++destination) {
        const bool is_loop = !destination->start_label.empty();
        if (statement.target == break_target_kind::nearest ||
            (statement.target == break_target_kind::loop && is_loop) ||
            (statement.target == break_target_kind::switch_ && !is_loop)) break;
      }
      if (destination == loop_stack.rend()) {
        cg_.reject_codegen_construct(s.line,
            "break has no enclosing target loop or switch",
            "put the break inside its targeted construct");
        break;
      }
      if (exits_physical_value_switch(*destination) ||
          std::any_of(loop_stack.rbegin(), destination,
          [](const LoopContext &context) { return !context.result_slot.empty(); })) {
        cg_.reject_codegen_construct(s.line,
            "control exit cannot cross a switch expression with suspended operands",
            "produce a switch result and exit the loop afterward");
        break;
      }
      // Expressions and cleanup may push control contexts; retain by value.
      const auto target = *destination;
      if (statement.value) {
        if (target.result_slot.empty() || !target.result_type) {
          cg_.reject_codegen_construct(s.line,
              "a break value requires a switch expression target",
              "yield a value only from a switch expression");
          break;
        }
        const auto first_instruction = out_fn.body.size();
        emit_expr(*statement.value, target.result_type);
        if (!expression_statement_produces_value(*statement.value, first_instruction)) {
          cg_.reject_codegen_construct(s.line,
              "switch break expression does not produce a value",
              "provide a non-void result");
          break;
        }
        emit_scalar_boundary(target.result_type, s.line);
        // Capture before recover, as with return: cleanup cannot change the
        // already evaluated result unless it explicitly replaces this exit.
        push_instr("STORE_LOCAL", {target.result_slot}, s.line);
      } else if (!target.result_slot.empty()) {
        cg_.reject_codegen_construct(s.line,
            "switch expression break is missing its value",
            "provide a value when leaving this switch expression");
        break;
      }
      emit_exited_cleanups(target.cleanup_depth, s.line, target.ownership_depth);
      push_instr("JMP", {target.end_label}, s.line);
      break;
    }
    case ast_node_kind::continue_stmt_node: {
      const auto destination = std::find_if(loop_stack.rbegin(), loop_stack.rend(),
          [](const LoopContext &context) { return !context.start_label.empty(); });
      if (destination != loop_stack.rend()) {
        if (exits_physical_value_switch(*destination) ||
            std::any_of(loop_stack.rbegin(), destination,
            [](const LoopContext &context) { return !context.result_slot.empty(); })) {
          cg_.reject_codegen_construct(s.line,
              "continue cannot cross a switch expression with suspended operands",
              "produce a switch result and continue the loop afterward");
          break;
        }
        const auto target = *destination;
        emit_exited_cleanups(target.cleanup_depth, s.line, target.ownership_depth);
        push_instr("JMP", {target.start_label}, s.line);
      } else {
        cg_.reject_codegen_construct(
            s.line, "continue cannot be lowered outside a loop",
            "move this continue statement inside a loop");
      }
      break;
    }
    case ast_node_kind::switch_stmt_node: {
      const auto &statement = static_cast<const ast_switch_stmt &>(s);
      emit_switch(statement.target, statement.arms, statement.checked_switch,
                  statement.line, std::nullopt);
      break;
    }
    case ast_node_kind::while_stmt_node: {
      const auto &while_s = static_cast<const ast_while_stmt &>(s);
      std::string start = new_label("L_while_start");
      std::string end = new_label("L_while_end");
      // start label
      push_instr("LABEL", {start}, while_s.line);
      // condition
      emit_expr(*while_s.condition);
      // if false -> jump end
      push_instr("JMP_IF_NOT", {end}, while_s.line);
      // body
      // push loop context (for break/continue)
      loop_stack.push_back({start, end, cleanup_stack.size(), ownership_depth});
      if (while_s.body)
        emit_stmt(*while_s.body);
      loop_stack.pop_back();
      // jump back to start
      push_instr("JMP", {start}, while_s.line);
      // end label
      push_instr("LABEL", {end}, while_s.line);
      break;
    }
    case ast_node_kind::for_in_stmt_node: {
      const auto &for_s = static_cast<const ast_for_in_stmt &>(s);
      if (for_s.iterable && for_s.iterable->rejected_tuple_operation) {
        cg_.reject_codegen_construct(for_s.line, *for_s.iterable->rejected_tuple_operation,
            "match the number of bindings to the tuple element's arity");
        break;
      }
      lexical_scope scope(*this);

      // Helper: extract tuple inner non-nested parts from textual "tuple<T1,
      // T2>" used by some heuristics in tuple unpacking cases. This is
      // intentionally simple and only splits at top-level commas.
      auto extract_tuple_parts = [&](const std::string &tuple_type) {
        const auto parsed = parse_generic_type_name(tuple_type);
        return parsed && parsed->base_name == "tuple" ? parsed->type_args
                                                     : std::vector<std::string>{};
      };
      // Desugaring for a `range(N)` call specially, else fallback to
      // iterator-based approach.
      bool handled = false;

      // If iterable is a call to function 'range' with 1 arg, implement index
      // based
      if (for_s.iterable &&
          for_s.iterable->kind == ast_node_kind::call_expr_node) {
        const auto &call = static_cast<const ast_call_expr &>(*for_s.iterable);
        if (call.callee &&
            call.callee->kind == ast_node_kind::identifier_expr_node) {
          const auto &callee_id =
              static_cast<const ast_identifier_expr &>(*call.callee);
          // Only perform the range -> index optimization for single-target
          // loops. If this `for` contains tuple-unpacking variables (e.g., `for
          // (a, b) in ...`) we fall back to iterator-based codegen so tuple
          // unpacking can be handled.
          if (callee_id.name == "range" && !scope.bindings.count("range") &&
              call.args.size() == 2 &&
              for_s.variables.size() == 1) {
            // range(start, end) -> index loop from start to end
            std::string tmp_idx = new_temp("idx");
            std::string tmp_len = new_temp("len");

            // idx = arg[0] (start)
            emit_expr(*call.args[0]);
            push_instr("STORE_LOCAL", {tmp_idx}, for_s.line);

            // len = arg[1] (end)
            emit_expr(*call.args[1]);
            push_instr("STORE_LOCAL", {tmp_len}, for_s.line);

            std::string start = new_label("L_iter_start");
            std::string next = new_label("L_iter_continue");
            std::string end = new_label("L_iter_end");

            push_instr("LABEL", {start}, for_s.line);

            // cmp: idx < len
            push_instr("LOAD_LOCAL", {tmp_idx}, for_s.line);
            push_instr("LOAD_LOCAL", {tmp_len}, for_s.line);
            push_instr("CMP_LT", {}, for_s.line);
            push_instr("JMP_IF_NOT", {end}, for_s.line);

            // assign loop var = idx
            push_instr("LOAD_LOCAL", {tmp_idx}, for_s.line);
            emit_scalar_boundary(std::string("i32"), for_s.line);
            declare_local(for_s.variables[0].name);
            push_instr("STORE_LOCAL", {for_s.variables[0].name}, for_s.line);
            local_var_types[for_s.variables[0].name] = std::string("i32");

            // push loop context
            loop_stack.push_back({next, end, cleanup_stack.size(), ownership_depth});

            // body
            if (for_s.body)
              emit_stmt(*for_s.body);

            loop_stack.pop_back();

            // idx = idx + 1
            push_instr("LABEL", {next}, for_s.line);
            push_instr("LOAD_LOCAL", {tmp_idx}, for_s.line);
            push_instr("PUSH_CONST", {"1"}, for_s.line);
            push_instr("ADD", {}, for_s.line);
            push_instr("STORE_LOCAL", {tmp_idx}, for_s.line);

            push_instr("JMP", {start}, for_s.line);
            push_instr("LABEL", {end}, for_s.line);

            handled = true;
          } else if (callee_id.name == "range" &&
                     !scope.bindings.count("range") && call.args.size() == 1 &&
                     for_s.variables.size() == 1) {
            // We will evaluate the arg -> N
            std::string tmp_len = new_temp("len");
            std::string tmp_idx = new_temp("idx");

            // len = <arg>
            emit_expr(*call.args[0]);
            push_instr("STORE_LOCAL", {tmp_len}, for_s.line);

            // idx = 0
            push_instr("PUSH_CONST", {"0"}, for_s.line);
            push_instr("STORE_LOCAL", {tmp_idx}, for_s.line);

            std::string start = new_label("L_for_start");
            std::string next = new_label("L_for_continue");
            std::string end = new_label("L_for_end");

            push_instr("LABEL", {start}, for_s.line);

            // cmp: idx < len
            push_instr("LOAD_LOCAL", {tmp_idx}, for_s.line);
            push_instr("LOAD_LOCAL", {tmp_len}, for_s.line);
            push_instr("CMP_LT", {}, for_s.line);
            // if not true -> goto end
            push_instr("JMP_IF_NOT", {end}, for_s.line);

            // assign param var = idx
            push_instr("LOAD_LOCAL", {tmp_idx}, for_s.line);
            // If the loop targets a single variable, store directly; otherwise,
            // treat the index result as a 'value' and attempt to unpack it into
            // tuple elements.
            if (for_s.variables.size() == 1 && !for_s.tuple_pattern) {
              emit_scalar_boundary(std::string("i32"), for_s.line);
              declare_local(for_s.variables[0].name);
              push_instr("STORE_LOCAL", {for_s.variables[0].name}, for_s.line);
              // range() yields an i32 index; reflect that in local_var_types so
              // subsequent expressions can optimize around it.
              local_var_types[for_s.variables[0].name] = std::string("i32");
            } else {
              // store the index value into a temporary local
              std::string tmp_val = new_temp("val");
              push_instr("STORE_LOCAL", {tmp_val}, for_s.line);

              // Unpack elements out of the stored value into the loop variables
              // using the `__ritual_tuple_get` intrinsic that accepts (value,
              // index).
              for (size_t ii = 0; ii < for_s.variables.size(); ++ii) {
                const auto &pv = for_s.variables[ii];
                push_instr("LOAD_LOCAL", {tmp_val}, for_s.line);
                push_instr("PUSH_CONST", {std::to_string(static_cast<int>(ii))},
                           for_s.line);
                push_instr("CALL", {"__ritual_tuple_get", "2"}, for_s.line);
                declare_local(pv.name);
                push_instr("STORE_LOCAL", {pv.name}, for_s.line);
              }
            }

            // push loop context
            loop_stack.push_back({next, end, cleanup_stack.size(), ownership_depth});

            // body
            if (for_s.body)
              emit_stmt(*for_s.body);

            loop_stack.pop_back();

            // idx = idx + 1
            push_instr("LABEL", {next}, for_s.line);
            push_instr("LOAD_LOCAL", {tmp_idx}, for_s.line);
            push_instr("PUSH_CONST", {"1"}, for_s.line);
            push_instr("ADD", {}, for_s.line);
            push_instr("STORE_LOCAL", {tmp_idx}, for_s.line);

            // jump to start
            push_instr("JMP", {start}, for_s.line);

            // end label
            push_instr("LABEL", {end}, for_s.line);

            handled = true;
          }
        }
      }

      // Typed arrays use the same index-based lowering for names, literals,
      // calls, and member expressions. Evaluate the array once before binding
      // loop variables, so side effects and a binding that shadows its source
      // cannot change which container the loop traverses. Other containers
      // retain their existing named-local optimization.
      if (!handled && for_s.iterable) {
        std::string var_type = unambiguous_expression_type(*for_s.iterable);
        std::string container_local;
        const bool named = for_s.iterable->kind == ast_node_kind::identifier_expr_node;
        if (named) {
          const auto &id = static_cast<const ast_identifier_expr &>(*for_s.iterable);
          const auto local = local_var_types.find(id.name);
          if (var_type.empty() && local != local_var_types.end())
            var_type = substitute_nil_type_name(local->second, substitutions);
          if (local != local_var_types.end()) container_local = id.name;
        }
        const auto iterable_container = parse_generic_type_name(var_type);
        const bool array_expression =
            (iterable_container && iterable_container->base_name == "array" &&
             !iterable_container->type_args.empty()) ||
            (var_type.size() > 2 && var_type.compare(var_type.size() - 2, 2, "[]") == 0);
        if (array_expression || !container_local.empty()) {

          // Helper lambdas for trimming and numeric detection
          auto trim_local = [](std::string s) {
            const char *ws = " \t\r\n";
            size_t b = s.find_first_not_of(ws);
            size_t e = s.find_last_not_of(ws);
            if (b == std::string::npos)
              return std::string();
            return s.substr(b, e - b + 1);
          };
          auto is_digits = [](const std::string &s) {
            if (s.empty())
              return false;
            for (char c : s)
              if (!std::isdigit(static_cast<unsigned char>(c)))
                return false;
            return true;
          };

          bool do_index_loop = false;
          std::optional<std::string> elem_type;
          std::optional<std::string> known_len;

          // Parse nested element shapes before interpreting a fixed-size tail.
          const auto container = parse_generic_type_name(var_type);
          const size_t pos_array = container && container->base_name == "array"
              ? 0 : std::string::npos;
          if (pos_array != std::string::npos && !container->type_args.empty()) {
            elem_type = container->type_args.front();
            if (container->type_args.size() == 2 && is_digits(container->type_args[1]))
              known_len = container->type_args[1];
            do_index_loop = true;
          }

          // vec<Elem> / vector<Elem> / slice<Elem>
          size_t pos_vec = var_type.find("vec<");
          size_t pos_vector = var_type.find("vector<");
          size_t pos_slice = var_type.find("slice<");
          if (!do_index_loop && (pos_vec != std::string::npos ||
                                 pos_vector != std::string::npos ||
                                 pos_slice != std::string::npos)) {
            size_t pos = (pos_vec != std::string::npos)
                             ? pos_vec
                             : (pos_vector != std::string::npos ? pos_vector
                                                                : pos_slice);
            size_t start = pos + (pos_vec != std::string::npos
                                      ? strlen("vec<")
                                      : (pos_vector != std::string::npos
                                             ? strlen("vector<")
                                             : strlen("slice<")));
            size_t end = var_type.rfind('>');
            if (end != std::string::npos && end > start) {
              std::string elem =
                  trim_local(var_type.substr(start, end - start));
              elem_type = elem;
              // length obtained at runtime using __ritual_len
              do_index_loop = true;
              known_len = std::nullopt;
            }
          }

          // i32[] trailing array notation
          if (!do_index_loop) {
            auto pos_sq = var_type.rfind("[]");
            if (pos_sq != std::string::npos && pos_sq >= 1) {
              std::string elem = trim_local(var_type.substr(0, pos_sq));
              elem_type = elem;
              // dynamic length call at runtime
              do_index_loop = true;
            }
          }

          if (do_index_loop && elem_type.has_value()) {
            // Implement index-based iteration for this container
            const bool typed_array = pos_array != std::string::npos ||
                                     var_type.rfind("[]") != std::string::npos;
            if (typed_array) {
              container_local = new_temp("iterable");
              emit_expr(*for_s.iterable);
              push_instr("STORE_LOCAL", {container_local}, for_s.line);
              local_var_types[container_local] = var_type;
            }
            std::string tmp_len = new_temp("len");
            std::string tmp_idx = new_temp("idx");

            if (known_len.has_value()) {
              // Use a constant length known at compile time
              push_instr("PUSH_CONST", {*known_len}, for_s.line);
              push_instr("STORE_LOCAL", {tmp_len}, for_s.line);
            } else {
              // Query runtime length: __ritual_len(container)
              push_instr("LOAD_LOCAL", {container_local}, for_s.line);
              if (typed_array)
                push_instr("LEN", {}, for_s.line);
              else
                push_instr("CALL", {"__ritual_len", "1"}, for_s.line);
              push_instr("STORE_LOCAL", {tmp_len}, for_s.line);
            }

            // idx = 0
            push_instr("PUSH_CONST", {"0"}, for_s.line);
            push_instr("STORE_LOCAL", {tmp_idx}, for_s.line);

            std::string start = new_label("L_for_start");
            std::string next = new_label("L_for_continue");
            std::string end = new_label("L_for_end");
            push_instr("LABEL", {start}, for_s.line);

            // cmp: idx < len
            push_instr("LOAD_LOCAL", {tmp_idx}, for_s.line);
            push_instr("LOAD_LOCAL", {tmp_len}, for_s.line);
            push_instr("CMP_LT", {}, for_s.line);
            // if not true -> goto end
            push_instr("JMP_IF_NOT", {end}, for_s.line);

            // value = __ritual_index(container, idx)
            push_instr("LOAD_LOCAL", {container_local}, for_s.line);
            push_instr("LOAD_LOCAL", {tmp_idx}, for_s.line);
            if (typed_array)
              push_instr("GET_INDEX", {}, for_s.line);
            else
              push_instr("CALL", {"__ritual_index", "2"}, for_s.line);

            // If single loop variable store element directly; else store tuple
            // and unpack into variables.
            if (for_s.variables.size() == 1 && !for_s.tuple_pattern) {
              declare_local(for_s.variables[0].name);
              push_instr("STORE_LOCAL", {for_s.variables[0].name}, for_s.line);
              // Reflect element type for loop variable locally
              local_var_types[for_s.variables[0].name] = *elem_type;
            } else {
              std::string tmp_val = new_temp("val");
              push_instr("STORE_LOCAL", {tmp_val}, for_s.line);
              for (size_t ii = 0; ii < for_s.variables.size(); ++ii) {
                const auto &pv = for_s.variables[ii];
                if (pv.name == "_") continue;
                push_instr("LOAD_LOCAL", {tmp_val}, for_s.line);
                if (elem_type && !extract_tuple_parts(*elem_type).empty()) {
                  cg_.materialize_struct_schema(substitute_nil_type_name(*elem_type, substitutions), mod_, for_s.line);
                  push_instr("GET_FIELD", {std::to_string(ii)}, for_s.line);
                } else {
                  push_instr("PUSH_CONST", {std::to_string(ii)}, for_s.line);
                  push_instr("CALL", {"__ritual_tuple_get", "2"}, for_s.line);
                }
                declare_local(pv.name);
                push_instr("STORE_LOCAL", {pv.name}, for_s.line);
                // If we can determine the element tuple type for the container
                // element (e.g., elem_type == "tuple<i32, String>"), register
                // per- element types for local use inside the body. Fall back
                // to the raw element type or 'any' if unknown.
                if (elem_type.has_value()) {
                  auto parts = extract_tuple_parts(*elem_type);
                  if (parts.size() > ii)
                    local_var_types[pv.name] = parts[ii];
                  else
                    local_var_types[pv.name] = *elem_type;
                } else {
                  local_var_types[pv.name] = std::string("any");
                }
              }
            }

            // push loop context
            loop_stack.push_back({next, end, cleanup_stack.size(), ownership_depth});

            // body
            if (for_s.body)
              emit_stmt(*for_s.body);

            loop_stack.pop_back();

            // idx = idx + 1
            push_instr("LABEL", {next}, for_s.line);
            push_instr("LOAD_LOCAL", {tmp_idx}, for_s.line);
            push_instr("PUSH_CONST", {"1"}, for_s.line);
            push_instr("ADD", {}, for_s.line);
            push_instr("STORE_LOCAL", {tmp_idx}, for_s.line);

            // jump to start
            push_instr("JMP", {start}, for_s.line);

            // end label
            push_instr("LABEL", {end}, for_s.line);

            handled = true;
          }
        }
      }

      if (!handled) {
        // Fallback: iterator protocol via ritual intrinsics:
        // iter = __ritual_iter(iterable)
        // loop:
        //    opt = __ritual_next(iter)
        //    if opt.@ritual_is_done -> break
        //    var = opt.@ritual_value()
        //    ... body ...
        // Note: We will model these as calls and store locals.
        std::string tmp_iter = new_temp("iter");
        std::string tmp_opt = new_temp("opt");
        // create iterator
        // evaluate iterable and call ritual_iter
        emit_expr(*for_s.iterable);
        push_instr("CALL", {"__ritual_iter", "1"}, for_s.line); // returns iter
        push_instr("STORE_LOCAL", {tmp_iter}, for_s.line);

        std::string start = new_label("L_iter_start");
        std::string end = new_label("L_iter_end");

        push_instr("LABEL", {start}, for_s.line);

        // opt = __ritual_next(iter)
        push_instr("LOAD_LOCAL", {tmp_iter}, for_s.line);
        push_instr("CALL", {"__ritual_next", "1"}, for_s.line);
        push_instr("STORE_LOCAL", {tmp_opt}, for_s.line);

        // is_done = __ritual_is_done(opt)
        push_instr("LOAD_LOCAL", {tmp_opt}, for_s.line);
        push_instr("CALL", {"__ritual_is_done", "1"}, for_s.line);
        // If true -> jump end
        push_instr("JMP_IF", {end},
                   for_s.line); // Note: assuming CALL returned boolean on
                                // stack; we pop and check

        // value = __ritual_value(opt)
        push_instr("LOAD_LOCAL", {tmp_opt}, for_s.line);
        push_instr("CALL", {"__ritual_value", "1"}, for_s.line);

        // If the loop uses a single target variable, store the result into it.
        // Otherwise, store the returned value into a temp and unpack tuple
        // elements by index.
        if (for_s.variables.size() == 1 && !for_s.tuple_pattern) {
          declare_local(for_s.variables[0].name);
          push_instr("STORE_LOCAL", {for_s.variables[0].name}, for_s.line);
          // Try to derive a usable element type for the variable:
          // - range(...) -> i32
          // - zip(...) -> tuple<...> (compose parts from args)
          // - identifier -> check local_var_types for its element type
          std::string deduced_type = "any";
          if (for_s.iterable &&
              for_s.iterable->kind == ast_node_kind::call_expr_node) {
            const auto &call =
                static_cast<const ast_call_expr &>(*for_s.iterable);
            if (call.callee &&
                call.callee->kind == ast_node_kind::identifier_expr_node) {
              const auto &callee_id =
                  static_cast<const ast_identifier_expr &>(*call.callee);
              if (callee_id.name == "range" && !scope.bindings.count("range")) {
                deduced_type = "i32";
              } else if (callee_id.name == "zip" && !scope.bindings.count("zip")) {
                // Try to derive a tuple<...> textual form from args
                std::ostringstream tss;
                tss << "tuple<";
                bool first = true;
                for (auto &arg : call.args) {
                  if (!first)
                    tss << ", ";
                  first = false;
                  if (arg->kind == ast_node_kind::call_expr_node) {
                    const auto &ac = static_cast<const ast_call_expr &>(*arg);
                    if (ac.callee && ac.callee->kind ==
                                         ast_node_kind::identifier_expr_node) {
                      const auto &aid =
                          static_cast<const ast_identifier_expr &>(*ac.callee);
                      if (aid.name == "range" && !scope.bindings.count("range"))
                        tss << "i32";
                      else
                        tss << "any";
                    } else {
                      tss << "any";
                    }
                  } else if (arg->kind == ast_node_kind::identifier_expr_node) {
                    const auto &idarg =
                        static_cast<const ast_identifier_expr &>(*arg);
                    auto it = scope.types.find(idarg.name);
                    if (it != scope.types.end()) {
                      // Attempt to use the element type of the container
                      // (naive)
                      std::string raw = it->second;
                      // If it's vec<E> or array<E,...> use that inner E; else
                      // use raw We'll keep it simple and just use the recorded
                      // local type
                      tss << raw;
                    } else {
                      tss << "any";
                    }
                  } else {
                    tss << "any";
                  }
                }
                tss << ">";
                deduced_type = tss.str();
              }
            }
          } else if (for_s.iterable &&
                     for_s.iterable->kind ==
                         ast_node_kind::identifier_expr_node) {
            const auto &id =
                static_cast<const ast_identifier_expr &>(*for_s.iterable);
            auto it = scope.types.find(id.name);
            if (it != scope.types.end()) {
              // Use the known container type; assume the emitter can interpret
              // it in later passes; otherwise, fallback to 'any'
              deduced_type = it->second;
            }
          }
          local_var_types[for_s.variables[0].name] = deduced_type;
        } else {
          // Save the tuple-like value in a temporary
          std::string tmp_val = new_temp("val");
          push_instr("STORE_LOCAL", {tmp_val}, for_s.line);

          // Extract each element by index via an intrinsic and assign to the
          // loop variables.
          for (size_t ii = 0; ii < for_s.variables.size(); ++ii) {
            const auto &pv = for_s.variables[ii];
            if (pv.name == "_") continue;
            push_instr("LOAD_LOCAL", {tmp_val}, for_s.line);
            const auto container = parse_generic_type_name(
                substitute_nil_type_name(for_s.iterable->inferred_type.value_or(""), substitutions));
            const auto element = container && container->base_name == "array" && !container->type_args.empty()
                ? parse_generic_type_name(container->type_args.front()) : std::nullopt;
            if (element && element->base_name == "tuple") {
              cg_.materialize_struct_schema(container->type_args.front(), mod_, for_s.line);
              push_instr("GET_FIELD", {std::to_string(ii)}, for_s.line);
            } else {
              push_instr("PUSH_CONST", {std::to_string(ii)}, for_s.line);
              push_instr("CALL", {"__ritual_tuple_get", "2"}, for_s.line);
            }
            declare_local(pv.name, substitute_nil_type_name(pv.type_name.value_or("any"), substitutions));
            push_instr("STORE_LOCAL", {pv.name}, for_s.line);

            // try to deduce tuple element parts from a zip(...) or known
            // container
            std::string deduced = "any";
            if (for_s.iterable &&
                for_s.iterable->kind == ast_node_kind::call_expr_node) {
              const auto &call =
                  static_cast<const ast_call_expr &>(*for_s.iterable);
              if (call.callee &&
                  call.callee->kind == ast_node_kind::identifier_expr_node) {
                const auto &idc =
                    static_cast<const ast_identifier_expr &>(*call.callee);
                if (idc.name == "zip" && !scope.bindings.count("zip")) {
                  // Compose tuple parts similar to above
                  std::vector<std::string> parts;
                  for (auto &arg : call.args) {
                    if (arg->kind == ast_node_kind::call_expr_node) {
                      const auto &c2 = static_cast<const ast_call_expr &>(*arg);
                      if (c2.callee &&
                          c2.callee->kind ==
                              ast_node_kind::identifier_expr_node) {
                        const auto &c2id =
                            static_cast<const ast_identifier_expr &>(
                                *c2.callee);
                        if (c2id.name == "range" && !scope.bindings.count("range"))
                          parts.push_back("i32");
                        else
                          parts.push_back("any");
                      } else
                        parts.push_back("any");
                    } else if (arg->kind ==
                               ast_node_kind::identifier_expr_node) {
                      const auto &idarg =
                          static_cast<const ast_identifier_expr &>(*arg);
                      auto it = scope.types.find(idarg.name);
                      if (it != scope.types.end())
                        parts.push_back(it->second);
                      else
                        parts.push_back("any");
                    } else
                      parts.push_back("any");
                  }
                  if (ii < parts.size())
                    deduced = parts[ii];
                }
              }
            }
            local_var_types[pv.name] = pv.type_name.value_or(deduced);
          }
        }

        // push loop context
        loop_stack.push_back({start, end, cleanup_stack.size(), ownership_depth});

        // body
        if (for_s.body)
          emit_stmt(*for_s.body);

        loop_stack.pop_back();

        // jump to start
        push_instr("JMP", {start}, for_s.line);

        // end label
        push_instr("LABEL", {end}, for_s.line);
      }
      break;
    }
    case ast_node_kind::match_stmt_node: {
      const auto &match_s = static_cast<const ast_match_stmt &>(s);
      // Evaluate the match target once, store into a temporary local.
      std::string tmp_target = new_temp("match_target");
      if (match_s.target) {
        emit_expr(*match_s.target);
      } else {
        // No target expression - treat as wildcard match only.
        push_instr("PUSH_CONST", {"0"}, match_s.line);
      }
      push_instr("STORE_LOCAL", {tmp_target}, match_s.line);
      const std::optional<std::string> match_enum_type =
          match_s.target ? enum_type_for_expression(*match_s.target)
                         : std::nullopt;

      std::string label_end = new_label("L_match_end");

      // small helper: detect numeric literal strings vs string literals
      auto is_numeric_literal = [](const std::string &text) -> bool {
        if (text.empty())
          return false;
        bool seen_dot = false;
        for (size_t ii = 0; ii < text.size(); ++ii) {
          char c = text[ii];
          if (ii == 0 && (c == '+' || c == '-'))
            continue;
          if (c == '.') {
            if (seen_dot)
              return false;
            seen_dot = true;
            continue;
          }
          if (!std::isdigit(static_cast<unsigned char>(c)))
            return false;
        }
        return true;
      };

      for (size_t i = 0; i < match_s.arms.size(); ++i) {
        const auto &arm = match_s.arms[i];
        lexical_scope scope(*this);
        std::string label_next = (i + 1 < match_s.arms.size())
                                     ? new_label("L_match_case")
                                     : label_end;

        if (arm.pattern.kind == ast_pattern_kind::constructor_pattern && !arm.pattern.name.empty()) {
          const bool is_qualified =
              arm.pattern.name.find("::") != std::string::npos ||
              arm.pattern.name.find('.') != std::string::npos;
          const bool supported_payload =
              arm.pattern.subpatterns.empty() ||
              (arm.pattern.subpatterns.size() == 1 &&
               (arm.pattern.subpatterns[0].kind ==
                    ast_pattern_kind::variable_pattern ||
                arm.pattern.subpatterns[0].kind ==
                    ast_pattern_kind::wildcard_pattern));
          if (!is_qualified || !supported_payload) {
            cg_.reject_codegen_construct(
                match_s.line,
                "match pattern is unavailable: NIL only lowers qualified "
                "variants with zero payloads or one variable/wildcard payload",
                "split or simplify this constructor pattern until structured "
                "payload matching is implemented");
            continue;
          }
        }

        bool is_wildcard =
            (arm.pattern.kind == ast_pattern_kind::wildcard_pattern);

        // If the pattern is not a simple wildcard, check the matcher
        if (!is_wildcard) {
          switch (arm.pattern.kind) {
          case ast_pattern_kind::literal_pattern: {
            // Load target, push literal, compare for equality
            push_instr("LOAD_LOCAL", {tmp_target}, match_s.line);
            if (arm.pattern.literal_kind == ast_node_kind::number_literal_node) {
              integer_literal_info integer;
              const bool integral = !is_floating_literal_spelling(arm.pattern.literal_value);
              if (integral) {
                if (const auto *message = parse_integer_literal(arm.pattern.literal_value, integer)) {
                  cg_.reject_codegen_construct(match_s.line, message,
                      "use a signed i64 value or a supported floating-point literal");
                  break;
                }
              }
              push_instr("PUSH_CONST", {arm.pattern.literal_value},
                         match_s.line);
              // Newly accepted signed hexadecimal patterns must match the
              // existing unary NEG expression, including 32-bit mask wrap.
              if (integral && integer.negative && integer.hexadecimal &&
                  integer.inferred_type() == std::string_view("i32"))
                push_instr("CAST", {"i32"}, match_s.line);
            } else if (arm.pattern.literal_kind == ast_node_kind::boolean_literal_node) {
              push_instr("PUSH_CONST", {arm.pattern.literal_value == "true" ? "1" : "0"},
                         match_s.line);
            } else if (arm.pattern.literal_kind == ast_node_kind::char_literal_node) {
              ast_char_literal literal(arm.pattern.literal_value, match_s.line);
              emit_expr(literal);
            } else {
              // Insert string constant and compare
              std::string sname =
                  get_or_add_const_string_name(arm.pattern.literal_value);
              push_instr("PUSH_CONST_STR", {sname}, match_s.line);
            }
            push_instr("CMP_EQ", {}, match_s.line);
            // If compare is false -> check next case
            push_instr("JMP_IF_NOT", {label_next}, match_s.line);
            break;
          }
          case ast_pattern_kind::variable_pattern: {
            // Bind the target to the variable name by storing it
            push_instr("LOAD_LOCAL", {tmp_target}, match_s.line);
            declare_local(arm.pattern.name,
                          match_s.target
                              ? match_s.target->inferred_type.value_or("__local")
                              : "__local");
            push_instr("STORE_LOCAL", {arm.pattern.name}, match_s.line);
            break;
          }
          case ast_pattern_kind::constructor_pattern: {
            if (arm.pattern.name.empty()) {
              emit_tuple_pattern(arm.pattern, tmp_target,
                  match_s.target->inferred_type.value_or(""), label_next, match_s.line);
              break;
            }
            // Enum variant check
            std::string variant_fqn =
                canonicalize_variant_name(arm.pattern.name);

            push_instr("LOAD_LOCAL", {tmp_target}, match_s.line);
            emit_is_variant(variant_fqn, match_enum_type, match_s.line);
            push_instr("JMP_IF_NOT", {label_next}, match_s.line);

            // Bind subpatterns
            if (!arm.pattern.subpatterns.empty()) {
              // Assuming single argument for now (e.g. Some(v))
              const auto &sub = arm.pattern.subpatterns[0];
              if (sub.kind == ast_pattern_kind::variable_pattern) {
                push_instr("LOAD_LOCAL", {tmp_target}, match_s.line);
                emit_unwrap_variant(arm.pattern.subpatterns.size(), variant_fqn,
                                    match_enum_type, match_s.line);
                declare_local(sub.name);
                push_instr("STORE_LOCAL", {sub.name}, match_s.line);
              }
            }
            break;
          }
          default: {
            // Not implemented/comprehensive: fallback to not-matching for now
            push_instr("JMP", {label_next}, match_s.line);
            break;
          }
          }
        }

        // If guard exists, evaluate it and jump to next case on false
        if (arm.guard) {
          emit_expr(*arm.guard);
          push_instr("JMP_IF_NOT", {label_next}, match_s.line);
        }

        // Execute the body
        if (arm.body) {
          emit_stmt(*arm.body);
        }

        // After a successful arm, jump to end to skip the rest
        push_instr("JMP", {label_end}, match_s.line);

        // Emit label for next case (if not last)
        if (i + 1 < match_s.arms.size()) {
          push_instr("LABEL", {label_next}, match_s.line);
        }
      }

      // End label for the entire match
      push_instr("LABEL", {label_end}, match_s.line);
      break;
    }
    case ast_node_kind::loop_stmt_node: {
      const auto &lp = static_cast<const ast_loop_stmt &>(s);
      std::string start = new_label("L_loop_start");
      std::string end = new_label("L_loop_end");
      push_instr("LABEL", {start}, lp.line);

      loop_stack.push_back({start, end, cleanup_stack.size(), ownership_depth});
      if (lp.body)
        emit_stmt(*lp.body);
      loop_stack.pop_back();

      push_instr("JMP", {start}, lp.line);
      push_instr("LABEL", {end}, lp.line);
      break;
    }
    case ast_node_kind::deallocate_stmt_node: {
      const auto &ds = static_cast<const ast_deallocate_stmt &>(s);
      emit_expr(*ds.ptr);
      push_instr("FREE", {}, ds.line);
      break;
    }

    case ast_node_kind::expr_stmt_node: {
      const auto &es = static_cast<const ast_expr_stmt &>(s);
      if (es.expression) {
        const std::size_t first_instruction = out_fn.body.size();
        emit_expr(*es.expression);
        // A statement discards exactly the value its expression produces.
        // Void calls and assignments produce no value; ordinary calls and
        // pure expressions produce one. Never rely on an empty POP being a
        // no-op: it can consume an enclosing expression's suspended operand.
        if (expression_statement_produces_value(*es.expression,
                                                first_instruction)) {
          push_instr("POP", {}, es.expression->line);
        }
      }
      break;
    }
    case ast_node_kind::attempt_stmt_node: {
      emit_attempt(static_cast<const ast_attempt_stmt &>(s));
      break;
    }
    case ast_node_kind::raise_stmt_node: {
      const auto &raise = static_cast<const ast_raise_stmt &>(s);
      if (!raise.fault_checked) {
        cg_.reject_codegen_construct(s.line,
            "fault lowering requires checked fault payload identity",
            "raise a checked concrete struct or a scoped opaque catch-all token");
        break;
      }
      if (raise.checked_rethrow_handler) {
        const auto saved = fault_bindings.find(raise.checked_rethrow_handler);
        if (saved == fault_bindings.end()) {
          cg_.reject_codegen_construct(s.line,
              "opaque fault token cannot escape its handler",
              "rethrow within the token's original handler scope");
          break;
        }
        push_instr("FAULT_RETHROW", {saved->second}, s.line);
      } else if (raise.checked_fault_type && raise.fault_expr) {
        const auto type = cg_.materialize_struct_schema(
            substitute_nil_type_name(*raise.checked_fault_type, substitutions),
            mod_, s.line, 0, true);
        emit_expr(*raise.fault_expr, type);
        push_instr("FAULT_RAISE", {type}, s.line);
      } else {
        cg_.reject_codegen_construct(s.line,
            "fault lowering requires checked fault payload identity",
            "recheck the source before emitting NIL");
      }
      break;
    }
    default: {
      cg_.reject_codegen_construct(
          s.line,
          "statement AST kind " + std::to_string(static_cast<int>(s.kind)) +
              " has no NIL lowering",
          "implement this statement lowering before compiling it");
      break;
    }
    }
  }

  bool expression_statement_produces_value(const ast_expr &expression,
                                           std::size_t first_instruction) {
    if (expression.kind == ast_node_kind::assignment_expr_node ||
        expression.kind == ast_node_kind::assert_expr_node)
      return false;

    const nil_instruction *last = out_fn.body.size() > first_instruction
                                      ? &out_fn.body.back()
                                      : nullptr;
    // A named void function used as a value still pushes its function pointer.
    // The identifier's inferred type currently describes the return type,
    // unlike a lambda's full fn(...) type, so inspect this exact lowering.
    if (last && last->opname == "PUSH_FN_PTR")
      return true;

    if (expression.inferred_type && !expression.inferred_type->empty() &&
        *expression.inferred_type != "any" &&
        *expression.inferred_type != "<unknown>") {
      return substitute_nil_type_name(*expression.inferred_type,
                                      substitutions) != "void";
    }

    // Other supported non-call expressions, including expression matches,
    // lower to one value. Unsupported assertion/hotreload/propagation
    // expressions already fail.
    if (expression.kind != ast_node_kind::call_expr_node)
      return true;

    // --no-typecheck has no inferred expression types. Prefer the concrete
    // lowering for opcode intrinsics and constructors, then a declaration for
    // ordinary calls. An unresolved dynamic call must not guess a stack ABI.
    if (last) {
      if (last->opname == "INTEROP_CALL" && last->args.size() >= 4)
        return last->args[3] != "void";
      if (last->opname == "CONSOLE_WRITE" ||
          last->opname == "CONSOLE_WRITELN" || last->opname == "TIME_SLEEP")
        return false;
      if (last->opname.rfind("MATH_", 0) == 0 ||
          last->opname == "TIME_NOW" || last->opname == "NEW_VARIANT" ||
          last->opname == "NEW_STRUCT" || last->opname == "SET_FIELD" ||
          last->opname == "IS_VARIANT" || last->opname == "UNWRAP_VARIANT" ||
          last->opname == "GET_FIELD" || last->opname == "LOAD_LOCAL" ||
          last->opname == "LOAD_GLOBAL" || last->opname == "PUSH_CONST" ||
          last->opname == "PUSH_CONST_STR")
        return true;
    }

    const auto &call = static_cast<const ast_call_expr &>(expression);
    std::function<std::string(const ast_expr &)> declaration_name =
        [&](const ast_expr &callee) -> std::string {
      if (callee.kind == ast_node_kind::identifier_expr_node) {
        const auto &identifier =
            static_cast<const ast_identifier_expr &>(callee);
        // A local function value can shadow a same-named declaration.
        return local_bindings.count(identifier.name) ? std::string{}
                                                     : identifier.name;
      }
      if (callee.kind == ast_node_kind::member_access_expr_node) {
        const auto &member = static_cast<const ast_member_access_expr &>(callee);
        if (!member.object)
          return {};
        std::string receiver = member.object->inferred_type.value_or("");
        if (receiver.empty() &&
            member.object->kind == ast_node_kind::identifier_expr_node) {
          const auto &identifier =
              static_cast<const ast_identifier_expr &>(*member.object);
          const auto local = local_var_types.find(identifier.name);
          if (local != local_var_types.end())
            receiver = local->second;
        }
        if (receiver.empty() || receiver == "any" || receiver == "<unknown>")
          receiver = declaration_name(*member.object);
        if (!receiver.empty())
          return receiver + "::" + member.member_name;
      }
      return {};
    };
    const std::string name = call.callee ? declaration_name(*call.callee) : "";
    if (!name.empty()) {
      const auto *declaration = cg_.lookup_function_decl(
          cg_.lookup_function_fqn(name, current_module_));
      if (!declaration)
        declaration = cg_.lookup_function_decl(name);
      const auto separator = name.rfind("::");
      if (!declaration && separator != std::string::npos) {
        const std::string receiver = name.substr(0, separator);
        const std::string method = name.substr(separator + 2);
        if (const auto *impl = cg_.lookup_generic_impl(receiver, method))
          declaration = cg_.lookup_impl_method(impl, method);
      }
      if (declaration)
        return substitute_nil_type_name(
                   declaration->return_type.value_or("void"), substitutions) !=
               "void";
    }

    if (last && last->opname == "CALL" && !last->args.empty()) {
      const std::string &callee = last->args[0];
      static const std::set<std::string> void_builtins = {
          "println", "panic", "ny__lang__core__panic", "free", "write_i32",
          "core__print", "console__write", "console__writeln",
          "console__sys_write", "array__push", "array__set"};
      if (void_builtins.count(callee))
        return false;
      static const std::set<std::string> value_builtins = {
          "alloc", "read_i32", "core__to_string", "core__to_string_i64",
          "core__to_string_f64", "f64_to_str", "String__len", "String__length",
          "String__to_uppercase", "String__to_upper", "String__to_lowercase",
          "String__to_lower", "String__trim", "String__contains",
          "String__starts_with", "String__ends_with", "String__find",
          "String__substring", "String__substr", "String__replace",
          "array__len", "array__length", "array__get"};
      if (value_builtins.count(callee))
        return true;
      for (const auto &function : mod_.functions) {
        if (function.name == callee)
          return !function.return_type.empty() && function.return_type != "void";
      }
    }

    cg_.reject_codegen_construct(
        expression.line,
        "cannot determine the return contract of this call statement",
        "enable type checking or provide a resolvable function declaration");
    return false;
  }

  // Helper function to recursively check if an expression contains string
  // operations This is needed for nested binary expressions like "hello" + " "
  // + "world"
  bool expr_involves_string(const ast_expr &e) {
    if (e.kind == ast_node_kind::string_literal_node) {
      return true;
    }
    if (e.kind == ast_node_kind::interpolated_string_expr_node) {
      return true;
    }
    if (e.kind == ast_node_kind::binary_expr_node) {
      const auto &bin = static_cast<const ast_binary_expr &>(e);
      // If this is a plus operation with string operands, it produces a string
      if (bin.op == token_kind::plus) {
        if (expr_involves_string(*bin.left) ||
            expr_involves_string(*bin.right)) {
          return true;
        }
      }
    }
    // Check inferred type as fallback
    std::string type = e.inferred_type.value_or("");
    return (type == "text" || type == "string" || type == "String");
  }

  void
  emit_expr(const ast_expr &e,
            const std::optional<std::string> &contextual_type = std::nullopt) {
    if (e.rejected_dynamic_operation) {
      cg_.reject_codegen_construct(e.line, *e.rejected_dynamic_operation,
          "enable type checking and extract a concrete type before using anything");
      return;
    }
    if (contextual_type && canonical_dynamic_type(*contextual_type) == "anything" &&
        e.inferred_type.value_or("") != "anything" && !e.checked_any_box_type) {
      cg_.reject_codegen_construct(e.line, "anything boxing requires a checked source type",
          "enable type checking");
      return;
    }
    if (e.rejected_tuple_operation) {
      cg_.reject_codegen_construct(e.line, *e.rejected_tuple_operation,
          "correct the tuple's shape and preserve immutable positions");
      return;
    }
    if (contains_ownership_type(e.inferred_type.value_or("")) &&
        (!ownership_checked || !e.checked_ownership)) {
      cg_.reject_codegen_construct(e.line, "owned value access requires a checked affine lifetime proof",
          "enable type checking and keep owners and borrows local");
      return;
    }
    emit_expr_impl(e, e.checked_any_box_type ? e.checked_any_box_type : contextual_type);
    if (e.checked_class_upcast)
      push_instr("CLASS_UPCAST", {*e.checked_class_upcast}, e.line);
    if (e.checked_any_box_type) {
      const auto source = canonical_dynamic_type(substitute_nil_type_name(*e.checked_any_box_type, substitutions));
      if (!dynamic_type_syntax_supported(source)) {
        cg_.reject_codegen_construct(e.line, "anything boxing has an unavailable concrete source type '" + source + "'",
            "use a non-borrowed concrete value");
        return;
      }
      cg_.materialize_struct_schema(source, mod_, e.line);
      emit_scalar_boundary(source, e.line);
      push_instr("BOX_ANY", {source}, e.line);
    }
  }

  // Queries lower to ordinary typed NIL arrays and loops. Row expressions are
  // evaluated here in the enclosing frame, so captures require no closures.
  void emit_query(const ast_query_expr &query) {
    if (!query.checked_query || !query.source || !query.projection) {
      cg_.reject_codegen_construct(query.line,
          "collect query requires checked source, clauses, and projection",
          "enable type checking and use supported scalar, text, or tuple arrays");
      return;
    }
    const int line = query.line;
    const auto element = substitute_nil_type_name(query.element_type, substitutions);
    const auto result_element = substitute_nil_type_name(query.result_element_type, substitutions);
    const auto key_type = substitute_nil_type_name(query.key_type, substitutions);
    auto instruction = [&](const std::string &op, std::vector<std::string> args = {}) {
      push_instr(op, args, line);
    };
    auto load = [&](const std::string &slot) { instruction("LOAD_LOCAL", {slot}); };
    auto store = [&](const std::string &slot) { instruction("STORE_LOCAL", {slot}); };
    auto constant = [&](const std::string &value) {
      instruction("PUSH_CONST", {value}); instruction("CAST", {"i64"});
    };
    auto initialize = [&](const std::string &slot, const std::string &value = "0") {
      constant(value); store(slot);
    };
    auto increment = [&](const std::string &slot, const std::string &amount = "1") {
      load(slot); constant(amount); instruction("ADD", {"i64"}); store(slot);
    };
    auto label = [&](const std::string &name) { instruction("LABEL", {name}); };
    auto jump = [&](const std::string &name) { instruction("JMP", {name}); };
    auto jump_if_not = [&](const std::string &name) { instruction("JMP_IF_NOT", {name}); };
    auto array = [&](const std::string &slot, const std::string &type) {
      if (is_array_tuple_type(type))
        cg_.materialize_struct_schema(type, mod_, line, 0, true);
      instruction("NEW_ARRAY_LITERAL", {"0", type}); store(slot);
    };
    auto at = [&](const std::string &values, const std::string &index) {
      load(values); load(index); instruction("GET_INDEX");
    };
    auto append = [&]() { instruction("CALL", {"array__push", "2"}); };
    auto length = [&](const std::string &values, const std::string &slot) {
      load(values); instruction("LEN"); instruction("CAST", {"i64"}); store(slot);
    };
    const auto source = new_temp("query.source"), skip = new_temp("query.skip"),
        take = new_temp("query.take"), count = new_temp("query.count"),
        snapshot = new_temp("query.snapshot"), index = new_temp("query.index"),
        result = new_temp("query.result"), produced = new_temp("query.produced");
    emit_expr(*query.source); store(source);
    // Bounds have outer lexical scope and are evaluated exactly once, even for
    // an empty input. Invalid counts fail before any predicate or projection.
    auto bound = [&](const ast_expr_ptr &expression, const std::string &slot,
                     const std::string &fallback, const std::string &name) {
      if (expression) { emit_expr(*expression); instruction("CAST", {"i64"}); store(slot); }
      else initialize(slot, fallback);
      const auto valid = new_label("L_query_bound");
      load(slot); constant("0"); instruction("CMP_LT"); jump_if_not(valid);
      instruction("PUSH_CONST_STR", {get_or_add_const_string_name(
          "Query " + name + " count must be nonnegative")});
      instruction("CALL", {"panic", "1"});
      label(valid);
    };
    bound(query.skip, skip, "0", "skip");
    bound(query.take, take, "9223372036854775807", "take");
    // Snapshot the source before row evaluation: mutation through another
    // alias cannot change which elements this eager query visits.
    length(source, count); array(snapshot, element); initialize(index);
    const auto copy = new_label("L_query_copy"), copied = new_label("L_query_copied");
    label(copy); load(index); load(count); instruction("CMP_LT"); jump_if_not(copied);
    load(snapshot); at(source, index); append(); increment(index); jump(copy); label(copied);
    lexical_scope scope(*this);
    declare_local(query.binding.name, element);
    array(result, result_element); initialize(produced);
    const auto finish = new_label("L_query_end");
    auto project = [&]() {
      load(result); emit_expr(*query.projection, result_element);
      emit_scalar_boundary(result_element, query.projection->line); append();
      increment(produced);
    };
    if (!query.order_key) {
      initialize(index);
      const auto visit = new_label("L_query_visit"), next = new_label("L_query_next"),
          select = new_label("L_query_select");
      label(visit);
      load(index); load(count); instruction("CMP_LT"); jump_if_not(finish);
      load(produced); load(take); instruction("CMP_LT"); jump_if_not(finish);
      at(snapshot, index); store(query.binding.name);
      if (query.predicate) { emit_expr(*query.predicate); jump_if_not(next); }
      load(skip); constant("0"); instruction("CMP_GT"); jump_if_not(select);
      load(skip); constant("1"); instruction("SUB", {"i64"}); store(skip); jump(next);
      label(select); project();
      label(next); increment(index); jump(visit);
    } else {
      const auto rows = new_temp("query.rows"), keys = new_temp("query.keys"),
          order = new_temp("query.order"), buffer = new_temp("query.buffer"),
          width = new_temp("query.width"), start = new_temp("query.start"),
          middle = new_temp("query.middle"), stop = new_temp("query.stop"),
          left = new_temp("query.left"), right = new_temp("query.right"),
          output = new_temp("query.output"), chosen = new_temp("query.chosen"),
          left_key = new_temp("query.left_key"), right_key = new_temp("query.right_key"),
          swapped = new_temp("query.swapped");
      array(rows, element); array(keys, key_type); array(order, "i64"); array(buffer, "i64");
      initialize(index); initialize(chosen);
      const auto visit = new_label("L_query_filter"), next = new_label("L_query_filter_next"),
          filtered = new_label("L_query_filtered");
      label(visit); load(index); load(count); instruction("CMP_LT"); jump_if_not(filtered);
      at(snapshot, index); store(query.binding.name);
      if (query.predicate) { emit_expr(*query.predicate); jump_if_not(next); }
      load(rows); load(query.binding.name); append();
      load(keys); emit_expr(*query.order_key, key_type);
      emit_scalar_boundary(key_type, query.order_key->line); append();
      load(order); load(chosen); append(); load(buffer); constant("0"); append();
      increment(chosen);
      label(next); increment(index); jump(visit); label(filtered);
      length(rows, count); initialize(width, "1");
      // Stable bottom-up merge sort uses O(n) extra indices and O(n log n)
      // comparisons. Keys are evaluated once per matching input row.
      const auto pass = new_label("L_query_sort_pass"), segment = new_label("L_query_sort_segment"),
          merge = new_label("L_query_sort_merge"), segment_done = new_label("L_query_sort_segment_done"),
          pass_done = new_label("L_query_sort_pass_done"), sorted = new_label("L_query_sorted"),
          use_left = new_label("L_query_sort_left"), use_right = new_label("L_query_sort_right"),
          write = new_label("L_query_sort_write"), has_left = new_label("L_query_sort_has_left"),
          has_right = new_label("L_query_sort_has_right");
      label(pass); load(width); load(count); instruction("CMP_LT"); jump_if_not(sorted);
      initialize(start);
      label(segment); load(start); load(count); instruction("CMP_LT"); jump_if_not(pass_done);
      auto clamped_sum = [&](const std::string &a, const std::string &b, const std::string &target) {
        load(a); load(b); instruction("ADD", {"i64"}); store(target);
        const auto within = new_label("L_query_sort_clamp");
        load(target); load(count); instruction("CMP_GT"); jump_if_not(within);
        load(count); store(target); label(within);
      };
      clamped_sum(start, width, middle); clamped_sum(middle, width, stop);
      load(start); store(left); load(middle); store(right); load(start); store(output);
      label(merge); load(output); load(stop); instruction("CMP_LT"); jump_if_not(segment_done);
      load(left); load(middle); instruction("CMP_GE"); jump_if_not(has_left); jump(use_right);
      label(has_left); load(right); load(stop); instruction("CMP_GE"); jump_if_not(has_right); jump(use_left);
      label(has_right);
      load(keys); at(order, left); instruction("GET_INDEX"); store(left_key);
      load(keys); at(order, right); instruction("GET_INDEX"); store(right_key);
      if (key_type == "f64") {
        // NaNs sort last in both directions; ties (including NaN pairs) keep
        // source order rather than inheriting platform floating comparisons.
        const auto left_number = new_label("L_query_sort_left_number");
        load(left_key); load(left_key); instruction("CMP_NE"); jump_if_not(left_number);
        load(right_key); load(right_key); instruction("CMP_NE"); jump_if_not(use_right); jump(use_left);
        label(left_number);
        load(right_key); load(right_key); instruction("CMP_NE"); jump_if_not(has_right + "_numbers"); jump(use_left);
        label(has_right + "_numbers");
      }
      load(left_key);
      if (key_type == "bool" || key_type == "u8") instruction("CAST", {"i64"});
      load(right_key);
      if (key_type == "bool" || key_type == "u8") instruction("CAST", {"i64"});
      instruction(query.descending ? "CMP_GE" : "CMP_LE");
      jump_if_not(use_right); jump(use_left);
      label(use_left); at(order, left); store(chosen); increment(left); jump(write);
      label(use_right); at(order, right); store(chosen); increment(right);
      label(write); load(buffer); load(output); load(chosen); instruction("SET_INDEX");
      increment(output); jump(merge);
      label(segment_done); load(stop); store(start); jump(segment);
      label(pass_done); load(order); store(swapped); load(buffer); store(order); load(swapped); store(buffer);
      load(width); constant("2"); instruction("MUL", {"i64"}); store(width); jump(pass);
      label(sorted); load(skip); store(index);
      const auto select = new_label("L_query_ordered_select");
      label(select); load(index); load(count); instruction("CMP_LT"); jump_if_not(finish);
      load(produced); load(take); instruction("CMP_LT"); jump_if_not(finish);
      load(rows); at(order, index); instruction("GET_INDEX"); store(query.binding.name);
      project(); increment(index); jump(select);
    }
    label(finish); load(result);
  }

  void emit_expr_impl(const ast_expr &e,
                      const std::optional<std::string> &contextual_type) {
    if (ny::is_debug_enabled()) {
      ny::diag_debug("[codegen] emit_expr kind=" + std::to_string((int)e.kind) +
                     " line=" + std::to_string(e.line));
    }
    switch (e.kind) {
    case ast_node_kind::number_literal_node: {
      const auto &num = static_cast<const ast_number_literal &>(e);
      if (!is_floating_literal_spelling(num.value)) {
        integer_literal_info integer;
        if (const auto *message = parse_integer_literal(num.value, integer)) {
          cg_.reject_codegen_construct(num.line, message,
              "use a signed i64 value or a supported floating-point literal");
          break;
        }
      }
      push_instr("PUSH_CONST", {num.value}, num.line);
      break;
    }
    case ast_node_kind::char_literal_node: {
      const auto &ch = static_cast<const ast_char_literal &>(e);
      // Convert to integer value
      int val = 0;
      if (!ch.value.empty()) {
        val = (unsigned char)ch.value[0];
        if (ch.value.size() > 1 && ch.value[0] == '\\') {
          // Basic escapes
          char esc = ch.value[1];
          if (esc == 'n')
            val = 10;
          else if (esc == 'r')
            val = 13;
          else if (esc == 't')
            val = 9;
          else if (esc == '0')
            val = 0;
          else if (esc == '\\')
            val = 92;
          else if (esc == '\'')
            val = 39;
          else
            val = (unsigned char)esc;
        }
      }
      push_instr("PUSH_CONST", {std::to_string(val)}, ch.line);
      break;
    }
    case ast_node_kind::boolean_literal_node: {
      const auto &b = static_cast<const ast_boolean_literal &>(e);
      push_instr("PUSH_CONST", {b.value ? "1" : "0"}, b.line);
      break;
    }
    case ast_node_kind::string_literal_node: {
      const auto &str = static_cast<const ast_string_literal &>(e);
      std::string name = get_or_add_const_string_name(str.value);
      push_instr("PUSH_CONST_STR", {name}, str.line);
      break;
    }
    case ast_node_kind::query_expr_node:
      emit_query(static_cast<const ast_query_expr &>(e));
      break;
    case ast_node_kind::array_literal_expr_node: {
      const auto &arr = static_cast<const ast_array_literal_expr &>(e);
      for (const auto &elem : arr.elements) {
        emit_expr(*elem);
      }
      std::string element_type = "any";
      if (arr.inferred_type) {
        if (const auto inferred = array_element_type_name(*arr.inferred_type)) {
          element_type = substitute_nil_type_name(*inferred, substitutions);
        }
      }
      if (contextual_type) {
        const auto context = substitute_nil_type_name(*contextual_type, substitutions);
        if (const auto element = array_element_type_name(context);
            element && *element != "any")
          element_type = *element;
      }
      // Direct AST clients may omit checker metadata. Recover every known
      // element's width so native lowering can enforce an exact array ABI
      // instead of accepting a wildcard handle or trusting the first value.
      if (element_type == "any")
        element_type = unambiguous_array_element_type(arr);
      // An empty array still exposes its tuple shape through its element
      // metadata, even when no tuple constructor otherwise emits the schema.
      if (is_array_tuple_type(element_type))
        cg_.materialize_struct_schema(element_type, mod_, arr.line, 0, true);
      push_instr("NEW_ARRAY_LITERAL",
                 {std::to_string(arr.elements.size()), element_type}, arr.line);
      break;
    }
    case ast_node_kind::identifier_expr_node: {
      const auto &id = static_cast<const ast_identifier_expr &>(e);
      // Check if it's an enum unit variant
      size_t last_colon = id.name.rfind("::");
      if (last_colon != std::string::npos) {
        std::string enum_name = id.name.substr(0, last_colon);
        if (cg_.lookup_enum_decl(enum_name)) {
          emit_new_variant(id.name, 0,
                           enum_type_for_expression(e, contextual_type),
                           id.line);
          break;
        }
      }
      // LOCAL VARIABLE CHECK: must come before function-decl lookup so that a
      // local variable whose name happens to match a function (e.g. `let len =
      // ...` when `pub function len` also exists) is loaded as a local rather
      // than pushed as a function-pointer reference.
      if (local_var_types.count(id.name)) {
        push_instr("LOAD_LOCAL", {id.name}, id.line);
        break;
      }
      if (const auto global = resolve_global_operand(id)) {
        push_instr("LOAD_GLOBAL", {*global}, id.line);
        break;
      }
      if (id.resolved_global_name) break; // A rejected global is not a function value.
      reject_escaped_local(id.name, id.line);
      // Check if this identifier refers to a named function used as a value
      // (fn-as-value, e.g. router.get("/ping", ping_handler))
      if (cg_.lookup_function_decl(id.name)) {
        std::string fqn = cg_.lookup_function_fqn(id.name, current_module_);
        push_instr("PUSH_FN_PTR", {mangle_fully(fqn)}, id.line);
        break;
      }
      cg_.reject_codegen_construct(id.line,
          "unresolved variable '" + id.name + "' has no exact global storage identity",
          "declare it locally or qualify a declared module global; enable type checking for imports");
      break;
    }
    case ast_node_kind::binary_expr_node: {
      const auto &bin = static_cast<const ast_binary_expr &>(e);
      if (bin.op == token_kind::amp_amp) {
        std::string label_false = new_label("L_and_false");
        std::string label_end = new_label("L_and_end");
        std::string result = new_temp("logical_and");
        local_var_types[result] = "bool";
        emit_expr(*bin.left);
        push_instr("JMP_IF_NOT", {label_false}, bin.line);
        emit_expr(*bin.right);
        push_instr("STORE_LOCAL", {result}, bin.line);
        push_instr("JMP", {label_end}, bin.line);
        push_instr("LABEL", {label_false}, bin.line);
        push_instr("PUSH_CONST", {"0"}, bin.line);
        push_instr("STORE_LOCAL", {result}, bin.line);
        push_instr("LABEL", {label_end}, bin.line);
        push_instr("LOAD_LOCAL", {result}, bin.line);
        break;
      }
      if (bin.op == token_kind::pipe_pipe) {
        std::string label_true = new_label("L_or_true");
        std::string label_end = new_label("L_or_end");
        std::string result = new_temp("logical_or");
        local_var_types[result] = "bool";
        emit_expr(*bin.left);
        push_instr("JMP_IF", {label_true}, bin.line);
        emit_expr(*bin.right);
        push_instr("STORE_LOCAL", {result}, bin.line);
        push_instr("JMP", {label_end}, bin.line);
        push_instr("LABEL", {label_true}, bin.line);
        push_instr("PUSH_CONST", {"1"}, bin.line);
        push_instr("STORE_LOCAL", {result}, bin.line);
        push_instr("LABEL", {label_end}, bin.line);
        push_instr("LOAD_LOCAL", {result}, bin.line);
        break;
      }

      emit_expr(*bin.left);
      emit_expr(*bin.right);
      const bool pointer_lhs = bin.left->inferred_type &&
                               bin.left->inferred_type->rfind("ptr<", 0) == 0;
      std::string opname;
      switch (bin.op) {
      case token_kind::plus: {
        // Use recursive helper to detect string operations in nested
        // expressions
        bool is_string_op =
            expr_involves_string(*bin.left) || expr_involves_string(*bin.right);
        opname =
            is_string_op ? "STR_CONCAT" : (pointer_lhs ? "PTR_ADD" : "ADD");
        break;
      }
      case token_kind::minus:
        opname = pointer_lhs ? "PTR_SUB" : "SUB";
        break;
      case token_kind::star:
        opname = "MUL";
        break;
      case token_kind::slash:
        opname = "DIV";
        break;
      case token_kind::percent:
      case token_kind::mod_kw:
        opname = "MOD";
        break;
      case token_kind::double_equal:
        opname = "CMP_EQ";
        break;
      case token_kind::not_equal:
        opname = "CMP_NE";
        break;
      case token_kind::less:
        opname = "CMP_LT";
        break;
      case token_kind::less_equal:
        opname = "CMP_LE";
        break;
      case token_kind::greater:
        opname = "CMP_GT";
        break;
      case token_kind::greater_equal:
        opname = "CMP_GE";
        break;
      case token_kind::amp:
        opname = "BIT_AND";
        break;
      case token_kind::pipe:
        opname = "BIT_OR";
        break;
      case token_kind::caret:
        opname = "BIT_XOR";
        break;
      case token_kind::less_less:
        opname = "SHL";
        break;
      case token_kind::greater_greater:
        opname = "SHR";
        break;
      default: {
        const bool is_range = bin.op == token_kind::double_dot;
        cg_.reject_codegen_construct(
            bin.line,
            is_range ? "standalone range expressions are unavailable: NIL only "
                       "lowers ranges used directly by for loops"
                     : "binary operator has no NIL lowering",
            is_range ? "use this range directly in a for loop"
                     : "use a binary operator supported by NIL");
        return;
      }
      }
      if (opname == "ADD" || opname == "SUB" || opname == "MUL" ||
          opname == "DIV" || opname == "MOD" || opname == "BIT_AND" ||
          opname == "BIT_OR" || opname == "BIT_XOR" || opname == "SHL" ||
          opname == "SHR") {
        // Array concatenation is a separate historical interpreter operation.
        if (opname == "ADD" && e.inferred_type &&
            (e.inferred_type->rfind("array<", 0) == 0 ||
             e.inferred_type->rfind("vec<", 0) == 0))
          push_instr(opname, {}, bin.line);
        else
          emit_arithmetic(opname, e);
      } else {
        push_instr(opname, {}, bin.line);
      }
      break;
    }
    case ast_node_kind::assignment_expr_node: {
      const auto &asn = static_cast<const ast_assignment_expr &>(e);

      // Check for dereference assignment: *ptr = val
      if (asn.target->kind == ast_node_kind::unary_expr_node) {
        const auto &ue = static_cast<const ast_unary_expr &>(*asn.target);
        if (ue.op == token_kind::star) {
          if (reject_reference_dereference(*ue.operand, asn.line))
            break;
          emit_expr(*ue.operand); // Pushes ptr
          emit_expr(*asn.value, asn.target->inferred_type);  // Pushes value
          push_instr("WRITE_PTR", {}, asn.line);
          break;
        }
      }

      // Check for index assignment: arr[i] = val
      if (asn.target->kind == ast_node_kind::index_expr_node) {
        const auto &ie = static_cast<const ast_index_expr &>(*asn.target);
        emit_expr(*ie.object); // Array
        emit_expr(*ie.index);  // Index
        const bool owned = is_ownership_type(ie.object->inferred_type.value_or(""));
        if (owned) {
          if (!ie.checked_ownership) {
            cg_.reject_codegen_construct(ie.line, "owned buffer mutation requires a checked exclusive borrow proof", "enable type checking");
            break;
          }
          push_instr("PTR_ADD", {}, asn.line);
        }
        emit_expr(*asn.value, asn.target->inferred_type); // Value
        if (owned) emit_scalar_boundary(ie.inferred_type, asn.line);
        push_instr(owned ? "WRITE_PTR" : "SET_INDEX", {}, asn.line);
        break;
      }

      // Check for field assignment: obj.field = val
      if (asn.target->kind == ast_node_kind::member_access_expr_node) {
        const auto &ma =
            static_cast<const ast_member_access_expr &>(*asn.target);
        // Detect (*ptr).field = val — emit SET_FIELD_PTR to write through
        // pointer
        if (ma.object && ma.object->kind == ast_node_kind::unary_expr_node) {
          const auto &ue = static_cast<const ast_unary_expr &>(*ma.object);
          if (ue.op == token_kind::star) {
            if (reject_reference_dereference(*ue.operand, asn.line))
              break;
            emit_expr(*ue.operand); // Push the POINTER address (not
                                    // dereferenced value)
            emit_expr(*asn.value, asn.target->inferred_type);  // Push value
            emit_scalar_boundary(ma.inferred_type, asn.line);
            push_instr("SET_FIELD_PTR", {ma.member_name}, asn.line);
            break;
          }
        }
        // Depth-1: local_var.field = val  →  LOAD, val, SET_FIELD, STORE_LOCAL
        if (ma.object &&
            ma.object->kind == ast_node_kind::identifier_expr_node) {
          const auto &obj_id =
              static_cast<const ast_identifier_expr &>(*ma.object);
          emit_member_receiver(*ma.object); // LOAD_LOCAL obj_id
          emit_expr(*asn.value, asn.target->inferred_type); // push value
          emit_scalar_boundary(ma.inferred_type, asn.line);
          push_instr("SET_FIELD", {ma.member_name}, asn.line);
          push_instr("STORE_LOCAL", {obj_id.name}, asn.line); // write back
          break;
        }
        // Depth-2: root.inner.leaf = val
        //   Stack: root_copy1, root_copy2 → GET_FIELD inner → inner_copy
        //         → val → SET_FIELD leaf → SET_FIELD inner → STORE_LOCAL root
        if (ma.object &&
            ma.object->kind == ast_node_kind::member_access_expr_node) {
          const auto &inner_ma =
              static_cast<const ast_member_access_expr &>(*ma.object);
          if (inner_ma.object &&
              inner_ma.object->kind == ast_node_kind::identifier_expr_node) {
            const auto &root_id =
                static_cast<const ast_identifier_expr &>(*inner_ma.object);
            // Push root twice: first for eventual SET_FIELD+STORE, second for
            // GET
            push_instr("LOAD_LOCAL", {root_id.name}, asn.line);
            push_instr("LOAD_LOCAL", {root_id.name}, asn.line);
            push_instr("GET_FIELD", {inner_ma.member_name}, asn.line);
            emit_expr(*asn.value, asn.target->inferred_type);
            emit_scalar_boundary(ma.inferred_type, asn.line);
            push_instr("SET_FIELD", {ma.member_name}, asn.line);
            push_instr("SET_FIELD", {inner_ma.member_name}, asn.line);
            push_instr("STORE_LOCAL", {root_id.name}, asn.line);
            break;
          }
        }
        // Fallback: emit as before, then discard leaked struct via temp store
        emit_member_receiver(*ma.object); // Push struct reference
        emit_expr(*asn.value, asn.target->inferred_type); // Push value
        emit_scalar_boundary(ma.inferred_type, asn.line);
        push_instr("SET_FIELD", {ma.member_name}, asn.line);
        push_instr("STORE_LOCAL", {new_temp("_sf_discard")}, asn.line);
        break;
      }

      emit_expr(*asn.value, asn.target->inferred_type);
      if (asn.target->kind == ast_node_kind::identifier_expr_node) {
        auto &id = static_cast<const ast_identifier_expr &>(*asn.target);
        emit_scalar_boundary(id.inferred_type, asn.line);
        // Match identifier load resolution: a local declaration shadows a
        // module global with the same name.
        const bool is_local = local_var_types.count(id.name) != 0;
        if (is_local)
          push_instr("STORE_LOCAL", {id.name}, asn.line);
        else if (const auto global = resolve_global_operand(id))
          push_instr("STORE_GLOBAL", {*global}, asn.line);
        else
          cg_.reject_codegen_construct(id.line,
              "unresolved variable '" + id.name + "' has no exact global storage identity",
              "declare it locally or qualify a declared module global; enable type checking for imports");
      } else {
        cg_.reject_codegen_construct(
            asn.line, "assignment target has no NIL store lowering",
            "assign to a variable, field, index, or pointer dereference");
      }
      break;
    }

    case ast_node_kind::call_expr_node: {
      const auto &ce = static_cast<const ast_call_expr &>(e);
      const auto direct_name = ce.callee && ce.callee->kind == ast_node_kind::identifier_expr_node
          ? static_cast<const ast_identifier_expr &>(*ce.callee).name : "";
      if (direct_name == "anything_is") {
        if (!ce.checked_any_type_test || ce.args.size() != 1) {
          cg_.reject_codegen_construct(ce.line, "anything_is requires a checked target type and anything argument",
              "enable type checking and pass one type and one value");
          break;
        }
        const auto target = canonical_dynamic_type(substitute_nil_type_name(*ce.checked_any_type_test, substitutions));
        if (!dynamic_type_syntax_supported(target)) {
          cg_.reject_codegen_construct(ce.line, "anything_is target '" + target + "' is unavailable",
              "use a non-borrowed concrete type");
          break;
        }
        cg_.materialize_struct_schema(target, mod_, ce.line);
        emit_expr(*ce.args.front());
        push_instr("IS_ANY_TYPE", {target}, ce.line);
        break;
      }
      const bool owned_call = direct_name == "owned" || direct_name == "owned_move" ||
          direct_name == "owned_borrow" || direct_name == "owned_borrow_mut";
      if (owned_call) {
        if (!ownership_checked || !ce.checked_ownership || ce.ownership_operation != direct_name ||
            ce.args.size() != 1 || !ce.inferred_type) {
          cg_.reject_codegen_construct(ce.line, "ownership operation requires a checked affine lifetime proof",
              "enable type checking and keep owners and borrows in local lexical scopes");
          break;
        }
        if (direct_name == "owned") {
          emit_expr(*ce.args[0]);
          push_instr("OWN_ALLOC", {ownership_element_type(*ce.inferred_type)}, ce.line);
        } else {
          if (ce.args[0]->kind != ast_node_kind::identifier_expr_node) {
            cg_.reject_codegen_construct(ce.line, "ownership operation requires a local source binding", "use a local owner");
            break;
          }
          const auto &name = static_cast<const ast_identifier_expr &>(*ce.args[0]).name;
          push_instr(direct_name == "owned_move" ? "OWN_MOVE" : "LOAD_LOCAL", {name}, ce.line);
        }
        break;
      }
      if (direct_name == "parallel_map") {
        if (!ce.parallel_worker || ce.args.size() != 2 || ce.parallel_input_type.empty() || ce.parallel_output_type.empty()) {
          cg_.reject_codegen_construct(ce.line, "parallel_map requires a checked static scalar worker signature",
              "enable type checking and pass a source function and matching scalar array");
          break;
        }
        emit_expr(*ce.args[1]);
        push_instr("PARALLEL_MAP", {cg_.sanitize_identifier(*ce.parallel_worker),
                    ce.parallel_input_type, ce.parallel_output_type}, ce.line);
        break;
      }

      if (ce.concrete_class_receiver && ce.class_instance_call && ce.callee &&
          ce.callee->kind == ast_node_kind::member_access_expr_node) {
        if (ce.class_argument_types.size() != ce.args.size() || !ce.class_method_owner) {
          cg_.reject_codegen_construct(ce.line,
              "class call has no exact checked argument signature",
              "enable type checking and provide every declared method argument");
          break;
        }
        const auto &member = static_cast<const ast_member_access_expr &>(*ce.callee);
        const auto receiver = substitute_nil_type_name(*ce.concrete_class_receiver, substitutions);
        emit_member_receiver(*member.object);
        for (std::size_t index = 0; index < ce.args.size(); ++index) {
          emit_expr(*ce.args[index], ce.class_argument_types[index]);
          emit_scalar_boundary(ce.class_argument_types[index], ce.line);
        }
        // The checked method registry preserves both virtual dispatch and
        // declaring-owner selection for nonvirtual calls, including safe
        // receiver-only adaptation of inherited ref<Self> methods.
        push_instr("CALL_METHOD", {receiver, member.member_name,
                                   std::to_string(ce.args.size() + 1)}, ce.line);
        break;
      }

      if (ce.static_receiver_type && ce.callee &&
          ce.callee->kind == ast_node_kind::member_access_expr_node) {
        const auto &member = static_cast<const ast_member_access_expr &>(*ce.callee);
        const std::string receiver = substitute_nil_type_name(*ce.static_receiver_type, substitutions);
        if (receiver == *ce.static_receiver_type) {
          cg_.reject_codegen_construct(ce.line,
              "associated trait call has an unresolved receiver type '" + receiver + "'",
              "instantiate the enclosing generic function or implementation");
          break;
        }
        const auto parsed = parse_generic_type_name(receiver);
        std::string base = parsed ? parsed->base_name : receiver;
        const std::string struct_fqn = cg_.lookup_struct_fqn(base);
        const std::string enum_fqn = cg_.lookup_enum_fqn(base);
        const std::string class_fqn = cg_.lookup_class_fqn(base);
        if (struct_fqn != base) base = struct_fqn;
        else if (enum_fqn != base) base = enum_fqn;
        else if (class_fqn != base) base = class_fqn;

        std::string callee_name = cg_.sanitize_identifier(base) + "__" + member.member_name;
        if (parsed) {
          const auto *impl = cg_.lookup_generic_impl(base, member.member_name);
          const auto *method = cg_.lookup_impl_method(impl, member.member_name);
          if (!impl || !method || impl->generic_params.size() != parsed->type_args.size()) {
            cg_.reject_codegen_construct(ce.line,
                "associated trait method has no concrete generic implementation for '" + receiver + "'",
                "provide a matching generic trait implementation");
            break;
          }
          cg_.queue_impl_method_specialization(base, member.member_name,
                                               parsed->type_args, impl, method);
          for (const auto &argument : parsed->type_args)
            callee_name += "__" + cg_.sanitize_identifier(argument);
        } else if (!cg_.lookup_function_decl(base + "::" + member.member_name)) {
          cg_.reject_codegen_construct(ce.line,
              "associated trait method has no concrete implementation for '" + receiver + "'",
              "provide a concrete trait implementation before calling the associated method");
          break;
        }
        for (const auto &argument : ce.args)
          emit_expr(*argument);
        push_instr("CALL", {callee_name, std::to_string(ce.args.size())}, ce.line);
        break;
      }

      // A lexical function value takes precedence over a named function,
      // constructor or intrinsic. Snapshot its resolved slot into a scratch
      // name so the interpreter's built-in CALL dispatch cannot intercept a
      // source binding named, for example, `range`.
      if (ce.callee &&
          ce.callee->kind == ast_node_kind::identifier_expr_node) {
        const auto &identifier =
            static_cast<const ast_identifier_expr &>(*ce.callee);
        if (local_bindings.count(identifier.name)) {
          const std::string callee = new_temp("callee");
          emit_expr(*ce.callee);
          push_instr("STORE_LOCAL", {callee}, ce.line);
          for (const auto &argument : ce.args)
            emit_expr(*argument);
          push_instr("CALL", {callee, std::to_string(ce.args.size())}, ce.line);
          break;
        }
        reject_escaped_local(identifier.name, ce.line);
      }

      // Check if this is an enum variant construction: Enum::Variant(args)
      // The callee would be a member_access_expr where object is an identifier
      // matching a known enum name
      if (ce.callee &&
          ce.callee->kind == ast_node_kind::member_access_expr_node) {
        const auto &ma =
            static_cast<const ast_member_access_expr &>(*ce.callee);
        if (ma.object &&
            ma.object->kind == ast_node_kind::identifier_expr_node) {
          const auto &obj_id =
              static_cast<const ast_identifier_expr &>(*ma.object);
          // Check if this is a known enum
          if (const auto *enum_decl = cg_.lookup_enum_decl(obj_id.name);
              enum_decl && std::any_of(enum_decl->variants.begin(),
                                       enum_decl->variants.end(),
                                       [&](const ast_enum_variant &variant) {
                                         return variant.name == ma.member_name;
                                       })) {
            std::string variant_fqn = obj_id.name + "::" + ma.member_name;
            const auto enum_type = enum_type_for_expression(e, contextual_type);
            emit_variant_arguments(ce.args, variant_fqn, enum_type, ce.line);
            emit_new_variant(variant_fqn, ce.args.size(),
                             enum_type, ce.line);
            break;
          }

          // Check if this is a generic impl static method call (e.g.,
          // vec::new())
          if (auto *impl =
                  cg_.lookup_generic_impl(obj_id.name, ma.member_name)) {
            if (auto *method = cg_.lookup_impl_method(impl, ma.member_name)) {
              // Determine type args from explicit type_args or context
              std::vector<std::string> type_args = ce.type_args;
              for (auto &type_arg : type_args) {
                type_arg = substitute_nil_type_name(type_arg, substitutions);
              }

              // If no explicit type args, try substitutions from enclosing
              // context
              if (type_args.empty() && !impl->generic_params.empty()) {
                for (const auto &gp : impl->generic_params) {
                  auto it = substitutions.find(gp.name);
                  if (it != substitutions.end()) {
                    type_args.push_back(it->second);
                  }
                }
              }

              // Debug: check if inferred_type is set
              if (ny::is_debug_enabled()) {
                ny::diag_debug("[codegen] Generic impl method " + obj_id.name +
                               "::" + ma.member_name + " inferred_type=" +
                               (e.inferred_type ? *e.inferred_type : "<none>"));
              }

              // If still no type args, try to extract from call expression's
              // inferred_type For vec::new() returning vec<i32>, the call
              // expr's inferred_type would be "vec<i32>"
              if (type_args.empty() && e.inferred_type) {
                // Preserve nested generic arguments as one type and resolve
                // any enclosing specialization parameters before this request
                // becomes a concrete NIL function.
                type_args = specialization_type_arguments(*e.inferred_type,
                                                          substitutions);
              }

              if (!type_args.empty()) {
                // Queue specialization
                ny::diag_debug("[codegen] Queueing impl specialization for " +
                               obj_id.name + "::" + ma.member_name);
                cg_.queue_impl_method_specialization(
                    obj_id.name, ma.member_name, type_args, impl, method);

                // Build mangled call name: type__typeargs___method
                std::string mangled_name = cg_.sanitize_identifier(obj_id.name);
                mangled_name += "__" + ma.member_name;
                for (const auto &arg : type_args) {
                  mangled_name += "__" + cg_.sanitize_identifier(arg);
                }
                // mangled_name += "___" + ma.member_name;

                // Emit args and call (static method, no self)
                for (const auto &arg : ce.args)
                  emit_expr(*arg);
                push_instr("CALL",
                           {mangled_name, std::to_string(ce.args.size())},
                           ce.line);
                break;
              } else {
                if (ny::is_debug_enabled()) {
                  ny::diag_debug("[codegen] Warning: Generic impl method " +
                                 obj_id.name + "::" + ma.member_name +
                                 " needs type args");
                }
              }
            }
          }
        }
        // Check if this is a STATIC method call: Type::method(args)
        // e.g. engine_math::mat4::look_at(...)
        // We detect this if the object is an identifier that matches a known
        // Type declaration. Also handle qualified types like
        // Module::Struct::method via recursive name building.
        if (ma.object) {
          std::function<std::string(const ast_expr &)> get_type_name_recurse =
              [&](const ast_expr &expr) -> std::string {
            if (expr.kind == ast_node_kind::identifier_expr_node) {
              return static_cast<const ast_identifier_expr &>(expr).name;
            } else if (expr.kind == ast_node_kind::member_access_expr_node) {
              const auto &ma_inner =
                  static_cast<const ast_member_access_expr &>(expr);
              if (ma_inner.object) {
                std::string lhs = get_type_name_recurse(*ma_inner.object);
                if (!lhs.empty())
                  return lhs + "::" + ma_inner.member_name;
              }
            }
            return std::string("");
          };

          std::string obj_type_name = get_type_name_recurse(*ma.object);
          if (!obj_type_name.empty()) {
            // If unqualified, try to resolve to current module's scope first
            if (obj_type_name.find("::") == std::string::npos &&
                !current_module_.empty()) {
              std::string candidate = current_module_ + "::" + obj_type_name;
              if (cg_.lookup_struct_decl(candidate) ||
                  cg_.lookup_class_decl(candidate) ||
                  cg_.lookup_enum_decl(candidate)) {
                obj_type_name = candidate;
              }
            }

            // Imported types retain their short source spelling in the AST.
            // Resolve that spelling to the registered fully-qualified type so
            // the call label matches the qualified impl function we emit.
            if (obj_type_name.find("::") == std::string::npos) {
              const std::string struct_fqn =
                  cg_.lookup_struct_fqn(obj_type_name);
              const std::string class_fqn = cg_.lookup_class_fqn(obj_type_name);
              const std::string enum_fqn = cg_.lookup_enum_fqn(obj_type_name);
              if (struct_fqn != obj_type_name)
                obj_type_name = struct_fqn;
              else if (class_fqn != obj_type_name)
                obj_type_name = class_fqn;
              else if (enum_fqn != obj_type_name)
                obj_type_name = enum_fqn;
            }

            if (cg_.lookup_struct_decl(obj_type_name) ||
                cg_.lookup_class_decl(obj_type_name) ||
                cg_.lookup_enum_decl(obj_type_name)) {

              ny::diag_debug("[codegen] Static call " + obj_type_name +
                             "::" + ma.member_name);

              // Static Call!
              std::string mangled_name =
                  cg_.sanitize_identifier(obj_type_name) + "__" +
                  ma.member_name;

              // Emit arguments (NO self)
              for (const auto &arg : ce.args)
                emit_expr(*arg);

              push_instr("CALL", {mangled_name, std::to_string(ce.args.size())},
                         ce.line);
              break;
            }
          }
        }

        // Check if this is a method call: obj.method(args)
        if (ma.object && ma.object->inferred_type) {
          std::string receiver_type = *ma.object->inferred_type;

          // Apply substitutions to resolve generic params in receiver type
          // (e.g. inside a specialization of vec<T>, "ref<core::vec<T>>" ->
          // "ref<core::vec<i32>>")
          for (const auto &kv : substitutions) {
            std::string needle = kv.first;
            size_t fpos = 0;
            while ((fpos = receiver_type.find(needle, fpos)) !=
                   std::string::npos) {
              bool before_ok =
                  (fpos == 0 ||
                   !std::isalnum((unsigned char)receiver_type[fpos - 1]));
              bool after_ok =
                  (fpos + needle.size() >= receiver_type.size() ||
                   !std::isalnum(
                       (unsigned char)receiver_type[fpos + needle.size()]));
              if (before_ok && after_ok) {
                receiver_type.replace(fpos, needle.size(), kv.second);
                fpos += kv.second.size();
              } else {
                fpos += needle.size();
              }
            }
          }

          // Strip ref<...> wrapper for method dispatch (VM passes handles, ref
          // is semantic)
          if (receiver_type.rfind("ref<", 0) == 0 && !receiver_type.empty() &&
              receiver_type.back() == '>') {
            receiver_type = receiver_type.substr(4, receiver_type.size() - 5);
          }

          // String is the checker's spelling of built-in text. Length is a
          // NIL operation, not an interpreter-only String__len function; this
          // also covers text returned by calls instead of only local values.
          if (canonical_nil_type_name(receiver_type) == "text" &&
              (ma.member_name == "len" || ma.member_name == "length")) {
            if (!ce.args.empty() || !ce.type_args.empty()) {
              cg_.reject_codegen_construct(
                  ce.line, "built-in text length expects no arguments or type arguments",
                  "use value.len() or value.length()");
              break;
            }
            emit_expr(*ma.object);
            push_instr("STRLEN", {}, ce.line);
            break;
          }

          // Special case: array.len() -> LEN opcode
          if (receiver_type.rfind("array", 0) == 0 && ma.member_name == "len") {
            emit_expr(*ma.object);
            push_instr("LEN", {}, ce.line);
            break;
          }

          // If receiver is a generic instantiation (e.g. "vec<i32>"), route
          // through the impl specialization mechanism so the correct
          // specialized function is queued and called with a matching mangled
          // name.
          bool dispatched_via_specialization = false;
          if (receiver_type.find('<') != std::string::npos) {
            // Parse "module::Base<Arg1,Arg2>" -> base="Base", args=[...]
            std::string rtype = receiver_type;
            auto bracket = rtype.find('<');
            std::string base_part = rtype.substr(0, bracket);
            std::string args_part = rtype.substr(bracket + 1);
            if (!args_part.empty() && args_part.back() == '>')
              args_part.pop_back();
            // Strip module prefix from base (keep last component)
            auto colon2 = base_part.rfind("::");
            if (colon2 != std::string::npos)
              base_part = base_part.substr(colon2 + 2);
            // Split type args by comma (handles nested generics via depth)
            std::vector<std::string> type_args;
            {
              std::string cur;
              int depth = 0;
              for (char c : args_part) {
                if (c == '<') {
                  ++depth;
                  cur += c;
                } else if (c == '>') {
                  --depth;
                  cur += c;
                } else if (c == ',' && depth == 0) {
                  while (!cur.empty() && cur.front() == ' ')
                    cur.erase(0, 1);
                  while (!cur.empty() && cur.back() == ' ')
                    cur.pop_back();
                  if (!cur.empty())
                    type_args.push_back(cur);
                  cur.clear();
                } else {
                  cur += c;
                }
              }
              while (!cur.empty() && cur.front() == ' ')
                cur.erase(0, 1);
              while (!cur.empty() && cur.back() == ' ')
                cur.pop_back();
              if (!cur.empty())
                type_args.push_back(cur);
            }
            if (!type_args.empty()) {
              if (auto *impl =
                      cg_.lookup_generic_impl(base_part, ma.member_name)) {
                if (auto *method =
                        cg_.lookup_impl_method(impl, ma.member_name)) {
                  cg_.queue_impl_method_specialization(
                      base_part, ma.member_name, type_args, impl, method);
                  // Build name using specialization format:
                  // base__method__typeargs
                  std::string spec_name = cg_.sanitize_identifier(base_part);
                  spec_name += "__" + ma.member_name;
                  for (const auto &ta : type_args)
                    spec_name += "__" + cg_.sanitize_identifier(ta);
                  emit_expr(*ma.object);
                  for (const auto &arg : ce.args)
                    emit_expr(*arg);
                  if (ny::is_debug_enabled()) {
                    std::fprintf(
                        stderr,
                        "[codegen] Emit generic method call '%s' -> '%s'\n",
                        ma.member_name.c_str(), spec_name.c_str());
                  }
                  push_instr("CALL",
                             {spec_name, std::to_string(ce.args.size() + 1)},
                             ce.line);
                  dispatched_via_specialization = true;
                }
              }
            }
          }

          if (!dispatched_via_specialization) {
            // Lower option<T>/result<T,E> method calls to VM opcodes.
            // These are semantic methods on built-in types; there are no
            // actual Ny functions for them — they map to IS_VARIANT /
            // UNWRAP_VARIANT instructions.
            {
              const std::string base_recv =
                  normalized_receiver_base_name(receiver_type);

              const std::string &mname = ma.member_name;
              bool is_opt_result_method =
                  (base_recv == "option" || base_recv == "result") &&
                  (mname == "is_some" || mname == "is_none" ||
                   mname == "is_ok" || mname == "is_err" ||
                   mname == "is_error" || mname == "unwrap" ||
                   mname == "unwrap_err" || mname == "unwrap_error");
              if (is_opt_result_method) {
                emit_expr(*ma.object);
                const std::optional<std::string> enum_type = receiver_type;
                if (mname == "is_some")
                  emit_is_variant("option::some", enum_type, ce.line);
                else if (mname == "is_none")
                  emit_is_variant("option::none", enum_type, ce.line);
                else if (mname == "is_ok")
                  emit_is_variant("result::ok", enum_type, ce.line);
                else if (mname == "is_err" || mname == "is_error")
                  emit_is_variant("result::error", enum_type, ce.line);
                else {
                  const std::string variant_name =
                      base_recv == "option"
                          ? "option::some"
                          : (mname == "unwrap_err" || mname == "unwrap_error"
                                 ? "result::error"
                                 : "result::ok");
                  emit_unwrap_variant(1, variant_name, enum_type, ce.line);
                }
                break;
              }
            }

            // Strip generic args for consistent built-in method dispatch.
            // array<text> -> array, so we get array__push not
            // array_text___push.
            std::string base_receiver = receiver_type;
            {
              size_t lt = base_receiver.find('<');
              if (lt != std::string::npos)
                base_receiver = base_receiver.substr(0, lt);
            }
            std::string sanitized_type = mangle_fully(base_receiver);
            std::string mangled_name = sanitized_type + "__" + ma.member_name;

            // Emit receiver (self) as first argument
            emit_expr(*ma.object);
            // Emit remaining arguments
            for (const auto &arg : ce.args)
              emit_expr(*arg);
            if (ny::is_debug_enabled()) {
              std::fprintf(stderr, "[codegen] Emit method call '%s' -> '%s'\n",
                           ma.member_name.c_str(), mangled_name.c_str());
            }
            push_instr("CALL",
                       {mangled_name, std::to_string(ce.args.size() + 1)},
                       ce.line);
          }
          break;
        }
      } else if (ce.callee &&
                 ce.callee->kind == ast_node_kind::identifier_expr_node) {
        const auto &id = static_cast<const ast_identifier_expr &>(*ce.callee);
        size_t last_colon = id.name.rfind("::");
        if (last_colon != std::string::npos) {
          std::string type_name = id.name.substr(0, last_colon);
          std::string method_name = id.name.substr(last_colon + 2);

          // Check if this is an enum variant construction
          if (const auto *enum_decl = cg_.lookup_enum_decl(type_name);
              enum_decl && std::any_of(enum_decl->variants.begin(),
                                       enum_decl->variants.end(),
                                       [&](const ast_enum_variant &variant) {
                                         return variant.name == method_name;
                                       })) {
            const auto enum_type = enum_type_for_expression(e, contextual_type);
            emit_variant_arguments(ce.args, id.name, enum_type, ce.line);
            emit_new_variant(id.name, ce.args.size(),
                             enum_type, ce.line);
            break;
          }

          // Check if this is a call to a generic impl method (e.g., vec::new())
          if (auto *impl = cg_.lookup_generic_impl(type_name, method_name)) {
            if (auto *method = cg_.lookup_impl_method(impl, method_name)) {
              // Determine type args - for now use type_args if provided,
              // otherwise try to infer from context (call expression's inferred
              // type)
              std::vector<std::string> type_args = ce.type_args;
              for (auto &type_arg : type_args) {
                type_arg = substitute_nil_type_name(type_arg, substitutions);
              }

              // If no explicit type args, try to get from the inferred return
              // type For vec::new() returning vec<i32>, extract ["i32"] This is
              // a simplification - real inference would come from type checker
              if (type_args.empty() && !impl->generic_params.empty()) {
                // Try using substitutions if available (we're already in a
                // generic context)
                for (const auto &gp : impl->generic_params) {
                  auto it = substitutions.find(gp.name);
                  if (it != substitutions.end()) {
                    type_args.push_back(it->second);
                  }
                }
              }

              // If still no type args, try to extract from call expression's
              // inferred_type
              if (type_args.empty() && e.inferred_type) {
                type_args = specialization_type_arguments(*e.inferred_type,
                                                          substitutions);
              }

              // Debug: output type_args status
              if (type_args.empty()) {
                if (ny::is_debug_enabled()) {
                  ny::diag_debug(
                      "[codegen] Warning: Generic impl method call " + id.name +
                      " without type args - using context inference");
                }
              }
              if (!type_args.empty()) {
                // Queue specialization
                if (ny::is_debug_enabled()) {
                  ny::diag_debug("[codegen] Queueing specialization for " +
                                 type_name + "::" + method_name +
                                 " args=" + type_args[0]);
                }
                cg_.queue_impl_method_specialization(type_name, method_name,
                                                     type_args, impl, method);

                // Build mangled call name
                // Build mangled call name: Type__Method__Args
                std::string mangled_name = cg_.sanitize_identifier(type_name);
                mangled_name += "__" + method_name;
                for (const auto &arg : type_args) {
                  mangled_name += "__" + cg_.sanitize_identifier(arg);
                }

                // Emit args and call
                for (const auto &arg : ce.args)
                  emit_expr(*arg);
                push_instr("CALL",
                           {mangled_name, std::to_string(ce.args.size())},
                           ce.line);
                break;
              }
            }
          }
        }
      }

      // Early check for struct constructor: Struct(arg1, arg2, ...)
      // Must be checked BEFORE emitting args so we can emit them as SET_FIELD
      if (ce.callee && ce.callee->kind == ast_node_kind::identifier_expr_node) {
        const auto &id = static_cast<const ast_identifier_expr &>(*ce.callee);
        std::string callee_name = id.name;
        // Check if this is a struct constructor
        if (const auto *struct_decl = cg_.lookup_struct_decl(callee_name)) {
          // Struct constructor: emit NEW_STRUCT, then SET_FIELD for each arg
          const auto concrete_type = struct_construction_type(
              e.inferred_type.value_or(callee_name), contextual_type);
          std::string struct_name = cg_.materialize_struct_schema(
              concrete_type, mod_, ce.line);
          push_instr("NEW_STRUCT", {struct_name}, ce.line);
          // Now emit each arg and set the corresponding field
          if (struct_decl->fields.size() == ce.args.size()) {
            for (size_t i = 0; i < struct_decl->fields.size(); ++i) {
              const auto field_type = struct_field_type(
                  concrete_type, struct_decl->fields[i].name);
              // Unit generic variants (Option.None) and nested enum payloads
              // need their field context before emission. Other arguments
              // retain their own storage type, including rejected array
              // element types when compiling with --no-typecheck.
              emit_expr(*ce.args[i], field_type && is_declared_enum_type(cg_, *field_type)
                  ? field_type : std::nullopt);
              emit_scalar_boundary(field_type, ce.line);
              push_instr("SET_FIELD", {struct_decl->fields[i].name}, ce.line);
            }
          } else {
            // Field count mismatch - still emit args for error reporting
            for (const auto &arg : ce.args)
              emit_expr(*arg);
            push_instr("STRUCT_CTOR_MISMATCH", {callee_name}, ce.line);
          }
          break; // Done with this call expression
        }
      }

      // Handle the Result/Option constructor aliases before their arguments
      // are emitted, so nested constructors inherit the same payload context
      // as qualified variants.
      if (ce.callee && ce.callee->kind == ast_node_kind::identifier_expr_node) {
        const auto &id = static_cast<const ast_identifier_expr &>(*ce.callee);
        std::string bare = cg_.lookup_function_fqn(id.name, current_module_);
        const auto separator = bare.rfind("::");
        if (separator != std::string::npos)
          bare = bare.substr(separator + 2);
        if (bare == "ok" || bare == "error" || bare == "some" || bare == "none") {
          const std::string variant = bare == "ok" ? "result::ok"
              : bare == "error" ? "result::error"
              : bare == "some" ? "option::some" : "option::none";
          const auto enum_type = enum_type_for_expression(e, contextual_type);
          emit_variant_arguments(ce.args, variant, enum_type, ce.line);
          emit_new_variant(variant, ce.args.size(), enum_type, ce.line);
          break;
        }
      }

      // Direct native dispatch bypasses the declared wrapper's parameter
      // boundary. Preserve scalar representation explicitly: PUSH_CONST uses a word
      // representation even for an i32 literal, and an accepted scalar
      // argument can have a different width from the declared parameter.
      // This is source lowering only; handwritten NIL must still satisfy the
      // native provider's exact operand and declaration contracts.
      const ast_function_decl *call_declaration = nullptr;
      const ast_function_decl *native_call_declaration = nullptr;
      auto call_substitutions = substitutions;
      if (ce.callee && ce.callee->kind == ast_node_kind::identifier_expr_node) {
        const auto &id = static_cast<const ast_identifier_expr &>(*ce.callee);
        const auto name = cg_.lookup_function_fqn(id.name, current_module_);
        const auto *declaration = cg_.lookup_function_decl(name);
        if (declaration) {
          call_declaration = declaration;
          if (native_dispatch_annotation_count(*declaration) == 1)
            native_call_declaration = declaration;
          for (std::size_t index = 0;
               index < declaration->generic_params.size() && index < ce.type_args.size();
               ++index)
            call_substitutions[declaration->generic_params[index].name] =
                substitute_nil_type_name(ce.type_args[index], substitutions);
        }
      }
      const auto emit_call_argument = [&](std::size_t index) {
        std::optional<std::string> argument_context;
        if (call_declaration && native_dispatch_annotation_count(*call_declaration) == 0 &&
            index < call_declaration->params.size()) {
          const auto &parameter = call_declaration->params[index];
          if (parameter.type_name && !parameter.is_variadic) {
            const auto expected = substitute_nil_type_name(
                *parameter.type_name, call_substitutions);
            // Only enum construction needs this additional call context.
            // In particular, a rejected array<i32> argument must retain its
            // actual element ABI under --no-typecheck, not become array<i64>
            // merely because that is the callee's declared parameter type.
            if (is_declared_enum_type(cg_, expected))
              argument_context = expected;
          }
        }
        std::optional<std::string> scalar_type;
        if (native_call_declaration && index < native_call_declaration->params.size() &&
            ce.args[index]->inferred_type) {
          const auto &parameter = native_call_declaration->params[index];
          if (parameter.type_name && !parameter.is_variadic) {
            auto expected = substitute_nil_type_name(*parameter.type_name, call_substitutions);
            if (expected == "int") expected = "i32";
            static const std::set<std::string> scalar_types = {
                "i32", "i64", "u8", "bool", "f64", "f32"};
            if (scalar_types.count(expected)) {
              auto actual = substitute_nil_type_name(*ce.args[index]->inferred_type, substitutions);
              if (actual == "int") actual = "i32";
              const bool floating_argument = actual == "f64" || actual == "f32";
              const bool floating_parameter = expected == "f64" || expected == "f32";
              const bool boolean_argument = actual == "bool";
              const bool boolean_parameter = expected == "bool";
              // Providers own their value/category checks. Do not turn a
              // fractional duration into an integer or parse text/handles as
              // numeric values, including under --no-typecheck (which can
              // still leave inferred_type metadata). Float-to-integer
              // conversion and changes of boolean category therefore require
              // an explicit source cast.
              if (scalar_types.count(actual) &&
                  (!floating_argument || floating_parameter) &&
                  boolean_argument == boolean_parameter)
                scalar_type = std::move(expected);
            }
          }
        }
        // Ordinary functions provide enum construction context without adding
        // a conversion: their parameter prologue owns scalar boundaries.
        // Native providers retain the narrower conversion contract above.
        emit_expr(*ce.args[index], scalar_type ? scalar_type : argument_context);
        emit_scalar_boundary(scalar_type, ce.args[index]->line);
      };

      // Effective arg count (adjusted when variadic args are packed into array)
      size_t effective_arg_count = ce.args.size();
      // Check for variadic last param — pack trailing args into
      // NEW_ARRAY_LITERAL
      bool did_vararg_pack = false;
      if (ce.callee && ce.callee->kind == ast_node_kind::identifier_expr_node) {
        const auto &id_vc =
            static_cast<const ast_identifier_expr &>(*ce.callee);
        if (const auto *vdecl = cg_.lookup_function_decl(id_vc.name)) {
          if (!vdecl->params.empty() && vdecl->params.back().is_variadic) {
            size_t fixed = vdecl->params.size() - 1;
            for (size_t i = 0; i < fixed && i < ce.args.size(); ++i)
              emit_call_argument(i);
            size_t nvar = ce.args.size() > fixed ? ce.args.size() - fixed : 0;
            for (size_t i = fixed; i < ce.args.size(); ++i)
              emit_expr(*ce.args[i]);
            std::string variadic_element_type = "any";
            if (vdecl->params.back().type_name) {
              if (const auto inferred = array_element_type_name(
                      *vdecl->params.back().type_name)) {
                variadic_element_type = *inferred;
              }
            }
            push_instr("NEW_ARRAY_LITERAL",
                       {std::to_string(nvar), variadic_element_type}, ce.line);
            effective_arg_count = fixed + 1;
            did_vararg_pack = true;
          }
        }
      }
      if (!did_vararg_pack) {
        for (std::size_t index = 0; index < ce.args.size(); ++index)
          emit_call_argument(index);
      }
      std::string fname = "<call>";
      if (ce.callee && ce.callee->kind == ast_node_kind::identifier_expr_node) {
        const auto &id = static_cast<const ast_identifier_expr &>(*ce.callee);
        fname = id.name;
        // Resolve to the fully-qualified name (handles unqualified names,
        // partially-qualified names like "server_config::default" inside a
        // module, and cross-module references via the suffix search).
        fname = cg_.lookup_function_fqn(fname, current_module_);

        // Check if this is a call to a generic function
        // We need to look up the symbol to know if it's generic
        // For now, we rely on the fact that if type_args are present, it's
        // generic
        if (!ce.type_args.empty()) {
          // Apply substitutions to type args
          std::vector<std::string> concrete_type_args;
          for (const auto &arg : ce.type_args)
            concrete_type_args.push_back(substitute_nil_type_name(arg, substitutions));

          // Mangle name: foo<i32> -> foo__i32
          // Save FQN before type-arg suffix (used for queue_specialization key)
          std::string base_fqn_for_spec = fname;
          std::string mangled = fname;
          for (const auto &arg : concrete_type_args) {
            mangled += "__" + cg_.sanitize_identifier(arg);
          }
          fname = mangled;

          // Queue specialization
          // We need the AST node for the function declaration to generate it.
          // Since we don't have easy access to the symbol table here in the
          // emitter, we might need to rely on the fact that the caller
          // (codegen) can look it up? OR, we can try to look it up if we had
          // access to symbols. For this MVP, we'll assume the codegen driver
          // has populated a map or we can't easily do it without symbol  //
          // Check for intrinsic annotation
          // NY-001: hoist decl pointer so it's in scope for the return-type
          // fallback below (used even when e.inferred_type is null).
          const ast_function_decl *fn_decl_for_intrinsic_check =
              cg_.lookup_function_decl(base_fqn_for_spec);
          std::string intrinsic_name_from_decl;
          if (fn_decl_for_intrinsic_check &&
              native_dispatch_annotation_count(
                  *fn_decl_for_intrinsic_check) == 1) {
            for (const auto &ann : fn_decl_for_intrinsic_check->annotations) {
              if (ann.name == "intrinsic") {
                if (ann.args.has_value()) {
                  std::string val = *ann.args;
                  if (val.size() >= 2 && val.front() == '"' &&
                      val.back() == '"') {
                    intrinsic_name_from_decl = val.substr(1, val.size() - 2);
                  } else {
                    intrinsic_name_from_decl = val;
                  }
                }
                break;
              }
            }
          }

          if (!intrinsic_name_from_decl.empty()) {
            if (emit_array_intrinsic(*fn_decl_for_intrinsic_check,
                                     intrinsic_name_from_decl, concrete_type_args,
                                     ce.args.size(), ce.line))
              break;
            // It's an intrinsic! Emit INTEROP_CALL "intrinsic" "name" arg_count
            // Arguments are already emitted by the loop above: `for (const auto
            // &arg : ce.args) emit_expr(*arg);`
            std::string ret_type_str = "void"; // Default to void
            // NY-001: Prefer inferred_type (set by typechecker), but fall back
            // to the declared return type from the AST when --no-typecheck is
            // used. Without this, every @intrinsic call defaults to "void" and
            // pushes nothing to the stack, causing stack underflow in callers.
            std::string raw_ret;
            if (e.inferred_type) {
              raw_ret = *e.inferred_type;
            } else if (fn_decl_for_intrinsic_check &&
                       fn_decl_for_intrinsic_check->return_type) {
              raw_ret = *fn_decl_for_intrinsic_check->return_type;
            }
            if (!raw_ret.empty() && raw_ret != "void") {
              if (intrinsic_name_from_decl.rfind("regex.", 0) == 0 ||
                  intrinsic_name_from_decl.rfind("nildata.", 0) == 0) {
                ret_type_str = canonical_nil_type_name(raw_ret);
              } else if (is_declared_enum_type(cg_, raw_ret)) {
                // Intrinsics return typed managed enum values. Their register
                // width is not a substitute for the concrete generic identity.
                ret_type_str = canonical_nil_type_name(raw_ret);
              } else if (raw_ret == "text" || raw_ret == "String") {
                ret_type_str = "text";
              } else {
                size_t sz = get_native_size(cg_, raw_ret);
                if (sz > 0 && sz <= 8)
                  ret_type_str = "i64";
                else if (sz > 8)
                  ret_type_str = "struct:" + std::to_string(sz) + ":" + raw_ret;
                else
                  ret_type_str = "i64"; // unknown size — assume register-width
              }
            }

            push_instr("INTEROP_CALL",
                       {"intrinsic", intrinsic_name_from_decl,
                        std::to_string(ce.args.size()), ret_type_str},
                       ce.line);
            break; // Done with this call expression
          }

          // access. WAIT: The emitter doesn't have access to the symbol table.
          // However, `assemble_module` iterates over declarations. We can store
          // a map of function decls in `codegen`. Let's assume `codegen` has a
          // `lookup_function_decl` method or map. We need to add that to
          // `codegen`.
          if (auto *decl = cg_.lookup_function_decl(base_fqn_for_spec)) {
            // Pass the FQN (e.g. "f3::print_it") so generate_specialization
            // builds "f3__print_it__text", matching what mangle_fully emits.
            cg_.queue_specialization(base_fqn_for_spec, concrete_type_args,
                                     decl);
          }
        }
        // Inferred-type specialization: generic function called without
        // explicit type args (e.g. print_it(d) where T inferred as Dog from
        // argument).
        if (ce.type_args.empty()) {
          // Use the resolved FQN. Imported identifiers may arrive here through
          // an alias spelling that is not itself a declaration-map key; using
          // the source token silently emitted an unspecialized runtime call.
          if (auto *decl = cg_.lookup_function_decl(fname)) {
            if (!decl->generic_params.empty()) {
              std::vector<std::string> inferred_args;
              bool ok = true;
              for (const auto &gp : decl->generic_params) {
                std::string found;
                for (size_t pi = 0;
                     pi < decl->params.size() && pi < ce.args.size(); ++pi) {
                  if (decl->params[pi].type_name == gp.name &&
                      ce.args[pi]->inferred_type.has_value()) {
                    found = *ce.args[pi]->inferred_type;
                    break;
                  }
                }
                if (found.empty()) {
                  ok = false;
                  break;
                }
                inferred_args.push_back(found);
              }
              if (ok && !inferred_args.empty()) {
                std::string base_fqn = fname; // FQN before type-arg suffix
                for (const auto &a : inferred_args)
                  fname += "__" + cg_.sanitize_identifier(a);
                cg_.queue_specialization(base_fqn, inferred_args, decl);
              }
            }
          }
        }
      } else if (ce.callee &&
                 ce.callee->kind == ast_node_kind::member_access_expr_node) {
        // Handle obj.method() calls
        // We need to resolve the method name based on the object's type
        const auto &ma =
            static_cast<const ast_member_access_expr &>(*ce.callee);

        // Use inferred type of the object if available
        std::string obj_type_for_dispatch;
        if (ma.object->inferred_type.has_value()) {
          obj_type_for_dispatch = *ma.object->inferred_type;
        } else if (ma.object->kind == ast_node_kind::identifier_expr_node) {
          // NY-002: When typechecker didn't run (--no-typecheck),
          // inferred_type is null. Fall back to the declared type annotation
          // recorded in local_var_types (populated from `let x: Type = ...`
          // declarations).
          const auto &obj_id =
              static_cast<const ast_identifier_expr &>(*ma.object);
          auto it = local_var_types.find(obj_id.name);
          if (it != local_var_types.end()) {
            obj_type_for_dispatch = it->second;
          }
        }
        if (!obj_type_for_dispatch.empty()) {
          std::string type_name = obj_type_for_dispatch;
          // Strip generic args for method name resolution (vec<i32> → vec)
          size_t lt = type_name.find('<');
          if (lt != std::string::npos) {
            type_name = type_name.substr(0, lt);
          }
          fname = type_name + "::" + ma.member_name;
        } else {
          // Last resort: use member name directly
          fname = ma.member_name;
        }
        // Lower option<T>/result<T,E> method calls directly to IS_VARIANT /
        // UNWRAP_VARIANT opcodes are interpreter operations, not real
        // functions.
        {
          const std::string base_recv =
              normalized_receiver_base_name(obj_type_for_dispatch);
          const std::string &mname = ma.member_name;
          bool is_opt_result_method =
              (base_recv == "option" || base_recv == "result") &&
              (mname == "is_some" || mname == "is_none" || mname == "is_ok" ||
               mname == "is_err" || mname == "is_error" || mname == "unwrap" ||
               mname == "unwrap_err" || mname == "unwrap_error");
          if (is_opt_result_method) {
            emit_expr(
                *ma.object); // push receiver (args already emitted — none)
            const std::optional<std::string> enum_type = obj_type_for_dispatch;
            if (mname == "is_some") {
              emit_is_variant("option::some", enum_type, ce.line);
            } else if (mname == "is_none") {
              emit_is_variant("option::none", enum_type, ce.line);
            } else if (mname == "is_ok") {
              emit_is_variant("result::ok", enum_type, ce.line);
            } else if (mname == "is_err" || mname == "is_error") {
              emit_is_variant("result::error", enum_type, ce.line);
            } else { // unwrap / unwrap_err / unwrap_error
              const std::string variant_name =
                  base_recv == "option"
                      ? "option::some"
                      : (mname == "unwrap_err" || mname == "unwrap_error"
                             ? "result::error"
                             : "result::ok");
              emit_unwrap_variant(1, variant_name, enum_type, ce.line);
            }
            break;
          }
        }
      } else {
        fname = "anon_call";
        if (ny::is_debug_enabled()) {
          if (ce.callee) {
            std::fprintf(stderr, "[codegen] Anonymous callee kind: %d\n",
                         (int)ce.callee->kind);
          } else {
            std::fprintf(stderr, "[codegen] Anonymous callee is null\n");
          }
        }
      }

      // Mangle function name for VM call (replace :: with __)
      // Intrinsics are handled below and usually don't need mangling (or have
      // their own logic). We only mangle if NOT an intrinsic. But we don't know
      // if it's intrinsic yet. Intrinsic detection uses `fname`. Most
      // intrinsics use dots (io.console.write). If we mangle,
      // `io.console.write` -> `io.console.write` (no ::). But `math::sin` ->
      // `math__sin`. Let's defer mangling until we confirm it's NOT an
      // intrinsic.

      // Check if the function has @intrinsic annotation
      std::string intrinsic_name;
      auto *fn_decl_for_intrinsic = cg_.lookup_function_decl(fname);
      const bool ambiguous_native_dispatch =
          fn_decl_for_intrinsic != nullptr &&
          native_dispatch_annotation_count(*fn_decl_for_intrinsic) > 1;
      if (ny::is_debug_enabled()) {
        std::fprintf(stderr, "[codegen] Call %s, decl=%s, annotations=%zu\n",
                     fname.c_str(), fn_decl_for_intrinsic ? "found" : "null",
                     fn_decl_for_intrinsic
                         ? fn_decl_for_intrinsic->annotations.size()
                         : 0);
      }
      if (fn_decl_for_intrinsic && !ambiguous_native_dispatch) {
        for (const auto &ann : fn_decl_for_intrinsic->annotations) {
          if (ny::is_debug_enabled())
            std::fprintf(stderr, "[codegen]   @%s\n", ann.name.c_str());
          if (ann.name == "intrinsic") {
            if (ann.args.has_value()) {
              std::string val = *ann.args;
              if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
                intrinsic_name = val.substr(1, val.size() - 2);
              } else {
                intrinsic_name = val;
              }
            }
            break;
          }
        }
      }

      if (!intrinsic_name.empty()) {
        if (emit_array_intrinsic(*fn_decl_for_intrinsic, intrinsic_name, {},
                                 ce.args.size(), ce.line))
          break;
        if (ny::is_debug_enabled()) {
          std::fprintf(stderr, "[codegen] Intrinsic %s for function %s\n",
                       intrinsic_name.c_str(), fname.c_str());
        }
        std::string opcode;
        if (intrinsic_name == "math.sin")
          opcode = "MATH_SIN";
        else if (intrinsic_name == "math.cos")
          opcode = "MATH_COS";
        else if (intrinsic_name == "math.tan")
          opcode = "MATH_TAN";
        else if (intrinsic_name == "math.asin")
          opcode = "MATH_ASIN";
        else if (intrinsic_name == "math.acos")
          opcode = "MATH_ACOS";
        else if (intrinsic_name == "math.atan")
          opcode = "MATH_ATAN";
        else if (intrinsic_name == "math.atan2")
          opcode = "MATH_ATAN2";
        else if (intrinsic_name == "math.sqrt")
          opcode = "MATH_SQRT";
        else if (intrinsic_name == "math.pow")
          opcode = "MATH_POW";
        else if (intrinsic_name == "math.exp")
          opcode = "MATH_EXP";
        else if (intrinsic_name == "math.log")
          opcode = "MATH_LOG";
        else if (intrinsic_name == "math.log10")
          opcode = "MATH_LOG10";
        else if (intrinsic_name == "math.fabs")
          opcode = "MATH_ABS";
        else if (intrinsic_name == "math.floor")
          opcode = "MATH_FLOOR";
        else if (intrinsic_name == "math.ceil")
          opcode = "MATH_CEIL";
        else if (intrinsic_name == "math.round")
          opcode = "MATH_ROUND";
        else if (intrinsic_name == "math.trunc")
          opcode = "MATH_TRUNC";
        else if (intrinsic_name == "math.cbrt")
          opcode = "MATH_CBRT";
        else if (intrinsic_name == "math.fmin")
          opcode = "MATH_MIN";
        else if (intrinsic_name == "math.fmax")
          opcode = "MATH_MAX";
        // Console intrinsics
        else if (intrinsic_name == "io.console.write")
          opcode = "CONSOLE_WRITE";
        // Time intrinsics
        else if (intrinsic_name == "time.now")
          opcode = "TIME_NOW";
        else if (intrinsic_name == "time.sleep")
          opcode = "TIME_SLEEP";

        if (!opcode.empty()) {
          // Call arguments were emitted once above, before dispatch selection.
          // Re-emitting them here leaves duplicate operands on the VM stack
          // and corrupts enclosing expressions (for example cosh(0.0)).
          push_instr(opcode, {}, ce.line);
          break;
        }
      }

      // Check if the function has @interop annotation for FFI dispatch
      std::string interop_lib;
      if (!ambiguous_native_dispatch) {
        if (auto *fn_decl = cg_.lookup_function_decl(fname)) {
          for (const auto &ann : fn_decl->annotations) {
            if (ann.name == "interop") {
              if (ann.args.has_value()) {
                // Extract library name, removing quotes if present
                std::string lib = *ann.args;
                if (lib.size() >= 2 && lib.front() == '"' &&
                    lib.back() == '"') {
                  lib = lib.substr(1, lib.size() - 2);
                }
                interop_lib = lib;
              } else {
                interop_lib = "default";
              }
              break;
            }
          }
        }
      }

      if (!interop_lib.empty()) {
        // Emit INTEROP_CALL: lib_name, fn_name, arg_count
        // Use raw function name for interop since DLL exports use original
        // names Strip any module prefix (e.g., "mimic_app::ny_mimic_init" ->
        // "ny_mimic_init")
        std::string interop_fn = fname;
        size_t last_colon = interop_fn.rfind("::");
        if (last_colon != std::string::npos) {
          interop_fn = interop_fn.substr(last_colon + 2);
        }
        std::string ret_type_str =
            "void"; // Default to void when no return type specified
        if (auto *fn_decl = cg_.lookup_function_decl(fname)) {
          if (fn_decl->return_type && *fn_decl->return_type == "void") {
            ret_type_str = "void";
          } else if (fn_decl->return_type) {
            const std::string &rt = *fn_decl->return_type;
            // Native text/String results use const char* at the FFI boundary.
            // Tag them as "text" so the VM knows to dereference the pointer.
            if (rt == "text" || rt == "String") {
              ret_type_str = "text";
            } else {
              size_t sz = get_native_size(cg_, rt);
              if (sz == 0) {
                ret_type_str = "void";
              } else if (sz <= 8) {
                ret_type_str = "i64";
              } else {
                // Use fully qualified name for struct types in FFI
                std::string qualified_rt = rt;
                std::string fqn = cg_.lookup_struct_fqn(rt);
                if (!fqn.empty()) {
                  qualified_rt = fqn;
                } else {
                  // Fallback: search for module prefix if not already qualified
                  if (rt.find("::") == std::string::npos) {
                    size_t last_c = fname.rfind("::");
                    if (last_c != std::string::npos) {
                      std::string mod = fname.substr(0, last_c);
                      qualified_rt = mod + "::" + rt;
                    }
                  }
                }
                ret_type_str =
                    "struct:" + std::to_string(sz) + ":" + qualified_rt;
              }
            }
          }
        }
        push_instr("INTEROP_CALL",
                   {interop_lib, interop_fn, std::to_string(ce.args.size()),
                    ret_type_str},
                   ce.line);
      } else {
        std::string mangled = mangle_fully(fname);
        if (ny::is_debug_enabled()) {
          std::fprintf(stderr, "[codegen] Emit call '%s' -> '%s'\n",
                       fname.c_str(), mangled.c_str());
        }
        push_instr("CALL", {mangled, std::to_string(effective_arg_count)},
                   ce.line);
      }
      break;
    }
    case ast_node_kind::cast_expr_node: {
      const auto &ce = static_cast<const ast_cast_expr &>(e);
      const std::string target = substitute_nil_type_name(
          ce.inferred_type.value_or(ce.target_type), substitutions);
      const std::string source = ce.operand
          ? substitute_nil_type_name(ce.operand->inferred_type.value_or(""),
                                     substitutions)
          : "";
      if (canonical_dynamic_type(target) == "anything" || canonical_dynamic_type(source) == "anything") {
        if (!ce.checked_any_cast || !ce.operand) {
          cg_.reject_codegen_construct(ce.line, "anything conversion requires a checked source and target",
              "enable type checking");
          break;
        }
        const auto concrete = canonical_dynamic_type(target == "anything" ? source : target);
        if (!dynamic_type_syntax_supported(concrete, true)) {
          cg_.reject_codegen_construct(ce.line, "anything conversion has an unavailable concrete type '" + concrete + "'",
              "use a non-borrowed concrete value");
          break;
        }
        cg_.materialize_struct_schema(concrete, mod_, ce.line);
        emit_expr(*ce.operand);
        if (target == "anything" && source != "anything") {
          emit_scalar_boundary(source, ce.line);
          push_instr("BOX_ANY", {canonical_dynamic_type(source)}, ce.line);
        } else if (source == "anything" && target != "anything")
          push_instr("UNBOX_ANY", {canonical_dynamic_type(target)}, ce.line);
        break;
      }
      if (ce.operand)
        emit_expr(*ce.operand);
      // A checked nominal/handle identity cast needs no runtime conversion.
      // In particular, interpreter pointer and array values do not retain
      // enough runtime type metadata to prove such identity in a CAST opcode.
      // Keep scalar casts: they also canonicalize physical storage widths.
      if (ce.checked_identity && source == target && target != "bool" && target != "i32" &&
          target != "i64" && target != "u8" && target != "f64" &&
          ce.inferred_type.has_value())
        break;
      push_instr("CAST", {target}, ce.line);
      break;
    }
    case ast_node_kind::unary_expr_node: {
      const auto &u = static_cast<const ast_unary_expr &>(e);
      if (u.op == token_kind::minus) {
        emit_expr(*u.operand);
        emit_arithmetic("NEG", e);
      } else if (u.op == token_kind::tilde) {
        emit_expr(*u.operand);
        push_instr("PUSH_CONST", {"-1"}, u.line);
        emit_arithmetic("BIT_XOR", e);
      } else if (u.op == token_kind::not_kw) {
        emit_expr(*u.operand);
        push_instr("NOT", {}, u.line);
      } else if (u.op == token_kind::star) {
        if (reject_reference_dereference(*u.operand, u.line))
          break;
        emit_expr(*u.operand);
        push_instr("READ_PTR", {}, u.line);
      } else if (u.op == token_kind::ref_kw) {
        emit_checked_reference(u, u.operand);
      } else {
        cg_.reject_codegen_construct(
            u.line, "unary operator has no NIL lowering",
            "use '-', 'not', pointer dereference, or ref on a local");
      }
      break;
    }
    case ast_node_kind::switch_expr_node: {
      const auto &expression = static_cast<const ast_switch_expr &>(e);
      if (!expression.inferred_type || expression.inferred_type == "<unknown>") {
        cg_.reject_codegen_construct(e.line, "switch expression has no checked result type",
                                     "provide a compatible break value on every result path");
        break;
      }
      emit_switch(expression.target, expression.arms, expression.checked_switch,
                  expression.line, expression.inferred_type);
      break;
    }
    case ast_node_kind::match_expr_node: {
      const auto &match_e = static_cast<const ast_match_expr &>(e);
      // Expression matches must initialize one result on every path. Keep
      // this contract explicit until full expression-pattern exhaustiveness
      // and block-tail lowering are available; statement matches are separate.
      const auto is_wildcard_pattern = [](const ast_expr_ptr &pattern) {
        return pattern &&
               pattern->kind == ast_node_kind::identifier_expr_node &&
               static_cast<const ast_identifier_expr &>(*pattern).name == "_";
      };
      if (match_e.cases.empty() ||
          !is_wildcard_pattern(match_e.cases.back().pattern) ||
          match_e.cases.back().guard) {
        cg_.reject_codegen_construct(
            match_e.line,
            "match expression requires a final unguarded wildcard arm",
            "add '_ -> value' so every path produces a result");
        break;
      }
      bool valid_arm_bodies = true;
      for (const auto &arm : match_e.cases) {
        if (!arm.pattern || !arm.body ||
            arm.body->kind != ast_node_kind::expr_stmt_node ||
            !static_cast<const ast_expr_stmt &>(*arm.body).expression) {
          valid_arm_bodies = false;
          break;
        }
      }
      if (!valid_arm_bodies) {
        cg_.reject_codegen_construct(
            match_e.line,
            "match expression arms require a single value expression",
            "use expression arms, or use statement-form match for blocks");
        break;
      }
      std::string tmp_target = new_temp("match_target");
      std::string tmp_result = new_temp("match_result");
      const auto result_type = match_e.inferred_type
          ? match_e.inferred_type : contextual_type;
      emit_expr(*match_e.target);
      push_instr("STORE_LOCAL", {tmp_target}, match_e.line);
      const std::optional<std::string> match_enum_type =
          enum_type_for_expression(*match_e.target);
      std::string label_end = new_label("L_match_end");

      for (size_t i = 0; i < match_e.cases.size(); ++i) {
        const auto &case_item = match_e.cases[i];
        lexical_scope scope(*this);
        std::string label_next = (i + 1 < match_e.cases.size())
                                     ? new_label("L_match_case")
                                     : label_end;
        const auto emit_arm_value = [&]() {
          if (case_item.guard) {
            emit_expr(*case_item.guard);
            push_instr("JMP_IF_NOT", {label_next}, match_e.line);
          }
          const auto &body =
              static_cast<const ast_expr_stmt &>(*case_item.body);
          const std::size_t first_instruction = out_fn.body.size();
          // The parser wraps each arm in an expression statement, but an
          // expression match owns that value. Ordinary statement cleanup
          // would discard it before the enclosing expression could use it.
          emit_expr(*body.expression, result_type);
          if (!expression_statement_produces_value(*body.expression,
                                                    first_instruction)) {
            cg_.reject_codegen_construct(
                body.line, "match expression arm does not produce a value",
                "return one value from every expression arm");
          }
          // Normalize every scalar arm to the common source type before a
          // scratch local's first store can determine its native LLVM width.
          emit_scalar_boundary(result_type, body.line);
          push_instr("STORE_LOCAL", {tmp_result}, match_e.line);
          push_instr("JMP", {label_end}, match_e.line);
          if (i + 1 < match_e.cases.size())
            push_instr("LABEL", {label_next}, match_e.line);
        };

        if (case_item.pattern->kind == ast_node_kind::struct_literal_expr_node &&
            static_cast<const ast_struct_literal_expr &>(*case_item.pattern).tuple_literal) {
          const auto pattern = tuple_expression_pattern(*case_item.pattern);
          emit_tuple_pattern(pattern, tmp_target,
              match_e.target->inferred_type.value_or(""), label_next, match_e.line);
          emit_arm_value();
          continue;
        }

        // Handle enum variant patterns (e.g., Option::Some(v))
        if (ny::is_debug_enabled()) {
          ny::diag_debug(
              "[codegen] match case " + std::to_string(i) +
              " pattern kind: " + std::to_string((int)case_item.pattern->kind));
        }
        if (case_item.pattern->kind == ast_node_kind::call_expr_node) {
          const auto &call =
              static_cast<const ast_call_expr &>(*case_item.pattern);

          std::string variant_fqn;
          if (call.callee->kind == ast_node_kind::member_access_expr_node) {
            const auto &ma =
                static_cast<const ast_member_access_expr &>(*call.callee);
            if (ma.object->kind == ast_node_kind::identifier_expr_node) {
              const auto &obj_id =
                  static_cast<const ast_identifier_expr &>(*ma.object);
              if (cg_.lookup_enum_decl(obj_id.name)) {
                variant_fqn = obj_id.name + "::" + ma.member_name;
              }
            }
          } else if (call.callee->kind == ast_node_kind::identifier_expr_node) {
            const auto &id =
                static_cast<const ast_identifier_expr &>(*call.callee);
            size_t last_colon = id.name.rfind("::");
            if (last_colon != std::string::npos) {
              std::string enum_name = id.name.substr(0, last_colon);
              if (cg_.lookup_enum_decl(enum_name)) {
                variant_fqn = id.name;
              } else {
                if (ny::is_debug_enabled()) {
                  ny::diag_debug(
                      "[codegen] call: lookup_enum_decl failed for " +
                      enum_name);
                }
              }
            }
          }

          if (!variant_fqn.empty()) {
            bool supported_payload = call.args.size() <= 1;
            if (supported_payload && !call.args.empty()) {
              supported_payload =
                  call.args[0] &&
                  call.args[0]->kind == ast_node_kind::identifier_expr_node;
            }
            if (!supported_payload) {
              cg_.reject_codegen_construct(
                  case_item.pattern->line,
                  "match pattern is unavailable: NIL only lowers a variant "
                  "with zero payloads or one variable/wildcard payload",
                  "split or simplify this variant pattern until structured "
                  "payload matching is implemented");
              continue;
            }

            std::string label_match = new_label("L_match_body");
            push_instr("LOAD_LOCAL", {tmp_target}, match_e.line);
            emit_is_variant(variant_fqn, match_enum_type, match_e.line);
            push_instr("JMP_IF_NOT", {label_next}, match_e.line);

            // Bind variables if present
            if (!call.args.empty()) {
              push_instr("LOAD_LOCAL", {tmp_target}, match_e.line);
              emit_unwrap_variant(call.args.size(), variant_fqn,
                                  match_enum_type, match_e.line);
              for (const auto &arg : call.args) {
                if (arg->kind == ast_node_kind::identifier_expr_node) {
                  const auto &arg_id =
                      static_cast<const ast_identifier_expr &>(*arg);
                  declare_local(arg_id.name);
                  push_instr("STORE_LOCAL", {arg_id.name}, match_e.line);
                } else {
                  // Wildcard or literal pattern inside variant?
                  // For now just pop to discard if not identifier
                  // But UNWRAP pushes distinct values.
                  // If we don't bind, we should probably POP?
                  // Or STORE_LOCAL to _ (dummy)?
                  // Since NIL machine has stack, we must consume the value.
                  // For wildcards `_`, we can emit `POP`?
                  // But `ast_identifier_expr` with `_` is handled as wildcard.
                  // If `const`, we need to check equality?
                  // Nested patterns not fully supported here yet.
                  // For MVP, if not identifier, emit POP?
                  // Actually, let's just assume identifier for now as per
                  // previous code. But we should emit STORE for valid
                  // identifiers. If wildcard, we should POP. Code
                  // simplification: just loop assuming identifiers for this
                  // fix.
                }
              }
            }

            emit_arm_value();
            continue;
          }
        }
        // Handle enum variant constant patterns (e.g., Option::None)
        else if (case_item.pattern->kind ==
                     ast_node_kind::member_access_expr_node ||
                 case_item.pattern->kind ==
                     ast_node_kind::identifier_expr_node) {
          std::string variant_fqn;
          if (case_item.pattern->kind ==
              ast_node_kind::member_access_expr_node) {
            const auto &ma =
                static_cast<const ast_member_access_expr &>(*case_item.pattern);
            if (ma.object->kind == ast_node_kind::identifier_expr_node) {
              const auto &obj_id =
                  static_cast<const ast_identifier_expr &>(*ma.object);
              if (cg_.lookup_enum_decl(obj_id.name)) {
                variant_fqn = obj_id.name + "::" + ma.member_name;
              }
            }
          } else {
            const auto &id =
                static_cast<const ast_identifier_expr &>(*case_item.pattern);
            // Check if it's a variant (contains ::)
            size_t last_colon = id.name.rfind("::");
            if (last_colon != std::string::npos) {
              std::string enum_name = id.name.substr(0, last_colon);
              if (cg_.lookup_enum_decl(enum_name)) {
                variant_fqn = id.name;
              } else {
                if (ny::is_debug_enabled()) {
                  ny::diag_debug("[codegen] id: lookup_enum_decl failed for " +
                                 enum_name);
                }
              }
            }
          }

          if (!variant_fqn.empty()) {
            // Enum::Variant check without payload
            std::string label_match = new_label("L_match_body");
            push_instr("LOAD_LOCAL", {tmp_target}, match_e.line);
            emit_is_variant(variant_fqn, match_enum_type, match_e.line);
            push_instr("JMP_IF_NOT", {label_next}, match_e.line);

            emit_arm_value();
            continue;
          }
        }

        bool is_wildcard = false;
        if (case_item.pattern->kind == ast_node_kind::identifier_expr_node) {
          const auto &id =
              static_cast<const ast_identifier_expr &>(*case_item.pattern);
          is_wildcard = (id.name == "_");
        }

        if (!is_wildcard) {
          push_instr("LOAD_LOCAL", {tmp_target}, match_e.line);
          emit_expr(*case_item.pattern);
          push_instr("CMP_EQ", {}, match_e.line);
          push_instr("JMP_IF_NOT", {label_next}, match_e.line);
        }

        emit_arm_value();
      }
      push_instr("LABEL", {label_end}, match_e.line);
      push_instr("LOAD_LOCAL", {tmp_result}, match_e.line);
      break;
    }
    case ast_node_kind::struct_literal_expr_node: {
      const auto &sl = static_cast<const ast_struct_literal_expr &>(e);
      if (sl.tuple_literal && (!sl.inferred_type || sl.inferred_type->rfind("tuple<", 0) != 0)) {
        cg_.reject_codegen_construct(sl.line, "tuple construction requires checked element types", "enable type checking");
        break;
      }
      std::string struct_name = sl.type_name;
      // Handle generic struct instantiation - build name in Type<Args> format
      // to match AST
      if (!sl.type_args.empty()) {
        struct_name += "<";
        for (size_t i = 0; i < sl.type_args.size(); ++i) {
          std::string arg = sl.type_args[i];
          auto it = substitutions.find(arg);
          if (it != substitutions.end()) {
            arg = it->second;
          }
          struct_name += arg;
          if (i < sl.type_args.size() - 1)
            struct_name += ",";
        }
        struct_name += ">";
      }
      const std::string concrete_type = struct_construction_type(
          e.inferred_type.value_or(struct_name), contextual_type);
      struct_name = cg_.materialize_struct_schema(concrete_type, mod_, sl.line);
      const auto require_field = [&](const std::string &name) {
        const auto expected = struct_field_type(concrete_type, name);
        if (!expected || !dynamic_default_requires_initializer(*expected)) return false;
        const bool supplied = std::any_of(sl.fields.begin(), sl.fields.end(),
            [&](const auto &field) { return field.first == name && field.second; });
        if (!supplied)
          cg_.reject_codegen_construct(sl.line, "field '" + name + "' requires an initialized value",
              "provide a value for every field containing anything");
        return !supplied;
      };
      bool missing_dynamic = false;
      const auto nominal = parse_generic_type_name(concrete_type);
      const auto base = nominal ? nominal->base_name : concrete_type;
      if (const auto *declaration = cg_.lookup_struct_decl(base))
        for (const auto &field : declaration->fields)
          missing_dynamic = require_field(field.name) || missing_dynamic;
      if (const auto *declaration = cg_.lookup_class_decl(base))
        for (const auto &field : declaration->checked_fields)
          missing_dynamic = require_field(field.name) || missing_dynamic;
      if (missing_dynamic) break;
      // Every nominal schema retains its exact punctuation. Sanitizing a
      // module name aliases left::Problem with unrelated left__Problem just
      // as sanitizing a generic aliases Box<i64> with Box_i64_.

      // Finish field evaluation before allocating the aggregate. A value
      // switch may return/raise while evaluating a later field; keeping a
      // partially initialized object on the operand stack would then require
      // abandoning an incomplete native object. Scratch locals preserve the
      // source's left-to-right evaluation without weakening escape checks.
      std::vector<std::string> field_values;
      field_values.reserve(sl.fields.size());
      for (const auto &field : sl.fields) {
        const auto field_type = struct_field_type(concrete_type, field.first);
        const auto first_instruction = out_fn.body.size();
        if (field.second) emit_expr(*field.second, field_type);
        if (!field.second || !expression_statement_produces_value(*field.second, first_instruction)) {
          cg_.reject_codegen_construct(sl.line, "aggregate field initializer does not produce a value",
                                       "provide one value for every field");
          break;
        }
        emit_scalar_boundary(field_type, sl.line);
        const auto value = new_temp("field_value");
        if (field_type) local_var_types[value] = *field_type;
        push_instr("STORE_LOCAL", {value}, sl.line);
        field_values.push_back(value);
      }
      if (field_values.size() != sl.fields.size()) break;
      push_instr("NEW_STRUCT", {struct_name}, sl.line);
      for (std::size_t index = 0; index < sl.fields.size(); ++index) {
        push_instr("LOAD_LOCAL", {field_values[index]}, sl.line);
        push_instr("SET_FIELD", {sl.fields[index].first}, sl.line);
      }
      break;
    }
    case ast_node_kind::member_access_expr_node: {
      const auto &ma = static_cast<const ast_member_access_expr &>(e);

      // Check if this is an enum unit variant access: Enum::Variant (no args)
      if (ma.object && ma.object->kind == ast_node_kind::identifier_expr_node) {
        const auto &obj_id =
            static_cast<const ast_identifier_expr &>(*ma.object);
        // Check if this is a known enum
        if (const auto *enum_decl = cg_.lookup_enum_decl(obj_id.name);
            enum_decl &&
            std::any_of(enum_decl->variants.begin(), enum_decl->variants.end(),
                        [&](const ast_enum_variant &variant) {
                          return variant.name == ma.member_name;
                        })) {
          // This is a unit variant construction (no payload)
          std::string variant_fqn = obj_id.name + "::" + ma.member_name;
          emit_new_variant(variant_fqn, 0,
                           enum_type_for_expression(e, contextual_type),
                           ma.line);
          break;
        }
      }

      emit_member_receiver(*ma.object);

      std::string obj_type = ma.object->inferred_type.value_or("");
      // Check for string.length
      if ((ma.member_name == "length") &&
          (obj_type == "string" || obj_type == "text" ||
           obj_type == "String")) {
        push_instr("STRLEN", {}, ma.line);
      } else if (ma.member_name == "len") {
        push_instr("LEN", {}, ma.line);
      } else {
        push_instr("GET_FIELD", {ma.member_name}, ma.line);
      }
      break;
    }

    case ast_node_kind::index_expr_node: {
      const auto &ie = static_cast<const ast_index_expr &>(e);
      emit_expr(*ie.object); // Pushes array_ptr
      emit_expr(*ie.index);  // Pushes index
      if (is_ownership_type(ie.object->inferred_type.value_or(""))) {
        if (!ie.checked_ownership) {
          cg_.reject_codegen_construct(ie.line, "owned buffer access requires a checked borrow proof", "enable type checking");
          break;
        }
        push_instr("PTR_ADD", {}, ie.line);
        push_instr("READ_PTR", {}, ie.line);
      } else push_instr("GET_INDEX", {}, ie.line);
      break;
    }
    case ast_node_kind::ref_expr_node: {
      const auto &re = static_cast<const ast_ref_expr &>(e);
      emit_checked_reference(re, re.operand);
      break;
    }
    case ast_node_kind::lambda_expr_node: {
      auto &le = const_cast<ast_lambda_expr &>(
          static_cast<const ast_lambda_expr &>(e));
      // Generate a unique name for this lambda
      static int lambda_id = 0;
      le.generated_name = "__lambda_" + std::to_string(lambda_id++);

      // Create the lambda as a separate function in the module
      nil_function fn;
      fn.name = le.generated_name;
      fn.return_type = le.return_type.value_or(std::string("void"));
      fn.source_line = le.line;
      fn.source_file = out_fn.source_file;

      for (const auto &p : le.params) {
        if (!append_serialized_nil_parameter(fn, p)) {
          cg_.reject_codegen_construct(
              p.line,
              "lambda parameter '" + p.name +
                  "' has no exact type for serialized NIL",
              "add an explicit type annotation to the parameter");
        }
      }

      // Use a new emitter to generate the lambda body
      emitter em(fn, mod_, cg_);
      // Closures do not yet have an environment representation. A lexical
      // capture must fail explicitly instead of falling back to an unrelated
      // global, named function, or caller-frame local with the same spelling.
      em.unavailable_captures = unavailable_captures;
      for (const auto &binding : local_bindings)
        em.unavailable_captures.insert(binding.first);
      em.register_function_params(le.params);
      for (auto parameter = le.params.rbegin();
           parameter != le.params.rend(); ++parameter)
        em.push_instr("STORE_LOCAL", {parameter->name}, le.line);
      if (le.body) {
        em.emit_stmt(*le.body);
      }
      append_implicit_function_return(fn, le.line);
      mod_.functions.push_back(std::move(fn));

      // Push function pointer
      push_instr("PUSH_FN_PTR", {le.generated_name}, le.line);
      break;
    }
    case ast_node_kind::interpolated_string_expr_node: {
      const auto &ie = static_cast<const ast_interpolated_string_expr &>(e);
      // Emit each expression exactly once, in source order.
      bool first = true;
      for (const auto &part : ie.parts) {
        if (part.is_literal) {
          std::string const_name =
              get_or_add_const_string_name(part.literal_value);
          push_instr("PUSH_CONST_STR", {const_name}, ie.line);
        } else if (part.expression) {
          emit_expr(*part.expression);
          const auto expr_type = scalar_text_type(substitute_nil_type_name(
              part.expression->inferred_type.value_or(""), substitutions));
          if (!supports_scalar_text(expr_type))
            cg_.reject_codegen_construct(part.expression->line,
                "interpolation requires a checked scalar or text expression",
                "enable type checking and call an explicit formatter for other types");
          if (expr_type != "text")
            push_instr("TO_STRING", {expr_type}, part.expression->line);
        }
        if (!first) {
          push_instr("STR_CONCAT", {}, ie.line);
        }
        first = false;
      }
      if (first)
        push_instr("PUSH_CONST_STR", {get_or_add_const_string_name("")}, ie.line);
      break;
    }
    case ast_node_kind::allocate_expr_node: {
      // allocate<T>(size) -> push size, emit ALLOC type_name
      const auto &alloc = static_cast<const ast_allocate_expr &>(e);
      if (dynamic_default_requires_initializer(alloc.type_name)) {
        cg_.reject_codegen_construct(alloc.line, "allocation requires initialized anything storage",
            "construct an array with explicit initial values");
        break;
      }
      if (alloc.size) {
        emit_expr(*alloc.size);
      } else {
        // Default to 1 element if no size specified
        push_instr("PUSH_CONST", {"1"}, alloc.line);
      }
      push_instr("ALLOC", {substitute_nil_type_name(alloc.type_name, substitutions)},
                 alloc.line);
      break;
    }
    case ast_node_kind::if_expr_node: {
      // Inline if expression: if cond then expr else expr end if
      // Emits: eval cond, JMP_IF_NOT L_else, eval then_expr, JMP L_end,
      //        L_else: eval else_expr, L_end:
      const auto &ie = static_cast<const ast_if_expr &>(e);
      const std::string result_type = substitute_nil_type_name(
          ie.inferred_type.value_or(contextual_type.value_or("")),
          substitutions);
      static const std::set<std::string> scalar_result_types = {
          "i32", "i64", "u8", "bool", "f32", "f64"};
      const auto emit_branch_value = [&](const ast_expr &branch) {
        emit_expr(branch, result_type.empty()
                              ? contextual_type
                              : std::optional<std::string>(result_type));
        // Both arms implement the same source expression type. Normalize
        // scalar widths before their SSA join (e.g. an i32 parameter uses
        // the word-sized call ABI, while an explicit i32 cast uses i32).
        // Never manufacture nominal/handle identity through a contextual cast.
        if (scalar_result_types.count(result_type))
          push_instr("CAST", {result_type}, ie.line);
      };
      std::string label_else = new_label("L_ifexpr_else");
      std::string label_end = new_label("L_ifexpr_end");
      emit_expr(*ie.condition);
      push_instr("JMP_IF_NOT", {label_else}, ie.line);
      emit_branch_value(*ie.then_expr);
      push_instr("JMP", {label_end}, ie.line);
      push_instr("LABEL", {label_else}, ie.line);
      if (ie.else_expr) {
        emit_branch_value(*ie.else_expr);
      } else {
        push_instr("PUSH_CONST", {"0"}, ie.line);
      }
      push_instr("LABEL", {label_end}, ie.line);
      break;
    }
    case ast_node_kind::try_expr_node: {
      cg_.reject_codegen_construct(
          e.line,
          "error propagation is unavailable: NIL has no lowering for the '?' "
          "operator",
          "match the Option or Result explicitly until propagation is "
          "implemented");
      break;
    }
    case ast_node_kind::assert_expr_node: {
      const auto &assertion = static_cast<const ast_assert_expr &>(e);
      if (!assertion.checked_assertion) {
        cg_.reject_codegen_construct(e.line, "assertions require checked argument types and arity",
            "fix the assertion diagnostics before generating NIL");
        break;
      }
      lexical_scope scope(*this);
      const bool boolean = assertion.method == "true" || assertion.method == "false";
      const std::size_t arity = boolean ? 1 : 2;
      std::vector<std::string> slots, types;
      for (const auto &argument : assertion.args) {
        const auto type = substitute_nil_type_name(argument->inferred_type.value_or(""), substitutions);
        const auto slot = new_label("$niltest.argument");
        emit_expr(*argument);
        emit_scalar_boundary(type, argument->line);
        declare_local(slot, type);
        push_instr("STORE_LOCAL", {slot}, argument->line);
        slots.push_back(slot); types.push_back(type);
      }
      const auto pass = new_label("L_assert_pass");
      if (boolean) {
        push_instr("LOAD_LOCAL", {slots[0]}, e.line);
        push_instr(assertion.method == "true" ? "JMP_IF" : "JMP_IF_NOT", {pass}, e.line);
      } else {
        const bool numeric = types[0] != "String" && types[0] != "text" && types[0] != "bool";
        const auto common = (types[0] == "f32" || types[0] == "f64" ||
                             types[1] == "f32" || types[1] == "f64") ? "f64" : "i64";
        for (std::size_t index = 0; index < 2; ++index) {
          push_instr("LOAD_LOCAL", {slots[index]}, e.line);
          if (numeric) push_instr("CAST", {common}, e.line);
        }
        const std::map<std::string, std::string> operations{
          {"equal", "CMP_EQ"}, {"not_equal", "CMP_NE"},
          {"greater_than", "CMP_GT"}, {"greater_or_equal", "CMP_GE"},
          {"less_than", "CMP_LT"}, {"less_or_equal", "CMP_LE"}};
        const auto operation = operations.find(assertion.method);
        if (operation == operations.end()) {
          cg_.reject_codegen_construct(e.line, "unsupported assertion method", "use a supported scalar assertion");
          break;
        }
        push_instr(operation->second, {}, e.line);
        push_instr("JMP_IF", {pass}, e.line);
      }
      const auto text = [&](const std::string &value) {
        push_instr("PUSH_CONST_STR", {get_or_add_const_string_name(value)}, e.line);
      };
      const auto append_text = [&](const std::string &value) {
        text(value); push_instr("STR_CONCAT", {}, e.line);
      };
      const auto append_value = [&](std::size_t index) {
        push_instr("LOAD_LOCAL", {slots[index]}, e.line);
        if (types[index] != "String" && types[index] != "text") {
          if (types[index] == "f32") push_instr("CAST", {"f64"}, e.line);
          push_instr("TO_STRING", {types[index] == "f32" ? "f64" : types[index]}, e.line);
        }
        push_instr("STR_CONCAT", {}, e.line);
      };
      const auto file = assertion.source_file.empty() ? out_fn.source_file : assertion.source_file;
      text(file + ":" + std::to_string(e.line) + ": assertion failed: assert." + assertion.method + "; actual=");
      append_value(0);
      append_text("; expected=");
      if (boolean) append_text(assertion.method);
      else append_value(1);
      if (slots.size() > arity) { append_text("; "); append_value(arity); }
      push_instr("CALL", {"panic", "1"}, e.line);
      push_instr("LABEL", {pass}, e.line);
      break;
    }
    case ast_node_kind::hotreload_expr_node: {
      const auto &hotreload = static_cast<const ast_hotreload_expr &>(e);
      cg_.reject_codegen_construct(
          hotreload.line,
          "hot reload expressions are unavailable: NIL has no lowering for "
          "hotreload." +
              hotreload.method,
          "remove this hotreload expression until runtime lowering is "
          "implemented");
      break;
    }
    default: {
      cg_.reject_codegen_construct(
          e.line,
          "expression AST kind " + std::to_string(static_cast<int>(e.kind)) +
              " has no NIL lowering",
          "implement this expression lowering before compiling it");
      break;
    }
    }
  }

private:
  nil_function &out_fn;
  nil_module &mod_;
  codegen &cg_;
  std::map<std::string, std::string> substitutions;
  int label_id;
  int tmp_id;
  std::string current_module_;
  // Known declared variable types observed during emission for a function
  // (name
  // -> textual type). This map is populated via `register_function_params`
  // and `decl_stmt` handling, and it's used by the emitter for
  // container/vec/array optimizations.
  std::unordered_map<std::string, std::string> local_var_types;
  std::unordered_map<std::string, std::string> local_bindings;
  std::unordered_map<std::string, int> local_depths;
  int lexical_depth = 0;
  std::set<std::string> used_local_names;
  std::set<std::string> unavailable_captures;
  std::size_t local_identity = 0;

  std::optional<std::string> resolve_global_operand(const ast_identifier_expr &id) const {
    if (!local_bindings.count(id.name) && unavailable_captures.count(id.name)) {
      reject_escaped_local(id.name, id.line);
      return std::nullopt;
    }
    if (id.resolved_global_name) {
      if (id.resolved_global_name->empty()) {
        cg_.reject_codegen_construct(id.line,
            "unresolved variable '" + id.name + "' has no exact global storage identity",
            "qualify an ambiguous imported global with its declaring module");
        return std::nullopt;
      }
      if (cg_.global_var_decls_.count(*id.resolved_global_name))
        return id.resolved_global_name;
      cg_.reject_codegen_construct(id.line,
          "checked global '" + *id.resolved_global_name + "' has no declaration",
          "recheck the source before emitting NIL");
      return std::nullopt;
    }
    return cg_.lookup_global_name(id.name, current_module_);
  }

  ast_pattern tuple_expression_pattern(const ast_expr &expression) {
    ast_pattern pattern;
    if (expression.kind == ast_node_kind::struct_literal_expr_node &&
        static_cast<const ast_struct_literal_expr &>(expression).tuple_literal) {
      pattern.kind = ast_pattern_kind::constructor_pattern;
      for (const auto &field : static_cast<const ast_struct_literal_expr &>(expression).fields)
        pattern.subpatterns.push_back(tuple_expression_pattern(*field.second));
    } else if (expression.kind == ast_node_kind::identifier_expr_node) {
      pattern.name = static_cast<const ast_identifier_expr &>(expression).name;
      pattern.kind = pattern.name == "_" ? ast_pattern_kind::wildcard_pattern
                                          : ast_pattern_kind::variable_pattern;
    } else if (expression.kind == ast_node_kind::number_literal_node) {
      pattern.kind = ast_pattern_kind::literal_pattern;
      pattern.literal_value = static_cast<const ast_number_literal &>(expression).value;
    } else if (expression.kind == ast_node_kind::string_literal_node) {
      pattern.kind = ast_pattern_kind::literal_pattern;
      pattern.literal_kind = expression.kind;
      pattern.literal_value = static_cast<const ast_string_literal &>(expression).value;
    } else if (expression.kind == ast_node_kind::boolean_literal_node) {
      pattern.kind = ast_pattern_kind::literal_pattern;
      pattern.literal_kind = expression.kind;
      pattern.literal_value = static_cast<const ast_boolean_literal &>(expression).value ? "true" : "false";
    } else if (expression.kind == ast_node_kind::char_literal_node) {
      pattern.kind = ast_pattern_kind::literal_pattern;
      pattern.literal_kind = expression.kind;
      pattern.literal_value = static_cast<const ast_char_literal &>(expression).value;
    } else if (expression.kind == ast_node_kind::unary_expr_node &&
               static_cast<const ast_unary_expr &>(expression).op == token_kind::minus &&
               static_cast<const ast_unary_expr &>(expression).operand->kind == ast_node_kind::number_literal_node) {
      pattern.kind = ast_pattern_kind::literal_pattern;
      pattern.literal_value = "-" + static_cast<const ast_number_literal &>(
          *static_cast<const ast_unary_expr &>(expression).operand).value;
    } else {
      cg_.reject_codegen_construct(expression.line, "unsupported tuple pattern element",
          "use a binding, wildcard, scalar literal, or nested tuple pattern");
    }
    return pattern;
  }

  void emit_tuple_pattern(const ast_pattern &pattern, const std::string &temporary,
                          const std::string &raw_type, const std::string &next, int line) {
    const auto type = substitute_nil_type_name(raw_type, substitutions);
    if (pattern.kind == ast_pattern_kind::constructor_pattern) {
      const auto tuple = parse_generic_type_name(type);
      if (!pattern.name.empty() || !tuple || tuple->base_name != "tuple" ||
          tuple->type_args.size() != pattern.subpatterns.size()) {
        cg_.reject_codegen_construct(line, "tuple pattern does not match a checked tuple shape",
            "match each position with a binding, wildcard, scalar literal, or nested tuple");
        return;
      }
      cg_.materialize_struct_schema(type, mod_, line);
      for (size_t index = 0; index < pattern.subpatterns.size(); ++index) {
        const auto field = new_temp("tuple_pattern");
        declare_local(field, tuple->type_args[index]);
        push_instr("LOAD_LOCAL", {temporary}, line);
        push_instr("GET_FIELD", {std::to_string(index)}, line);
        push_instr("STORE_LOCAL", {field}, line);
        emit_tuple_pattern(pattern.subpatterns[index], field, tuple->type_args[index], next, line);
      }
    } else if (pattern.kind == ast_pattern_kind::variable_pattern) {
      if (pattern.name == "_") return;
      push_instr("LOAD_LOCAL", {temporary}, line);
      declare_local(pattern.name, type);
      push_instr("STORE_LOCAL", {pattern.name}, line);
    } else if (pattern.kind == ast_pattern_kind::literal_pattern) {
      push_instr("LOAD_LOCAL", {temporary}, line);
      if (pattern.literal_kind == ast_node_kind::string_literal_node)
        push_instr("PUSH_CONST_STR", {get_or_add_const_string_name(pattern.literal_value)}, line);
      else if (pattern.literal_kind == ast_node_kind::char_literal_node) {
        ast_char_literal character(pattern.literal_value, line);
        emit_expr(character);
      } else if (pattern.literal_kind == ast_node_kind::boolean_literal_node)
        push_instr("PUSH_CONST", {pattern.literal_value == "true" ? "1" : "0"}, line);
      else {
        ast_number_literal number(pattern.literal_value, line);
        emit_expr(number);
      }
      emit_scalar_boundary(type, line);
      push_instr("CMP_EQ", {}, line);
      push_instr("JMP_IF_NOT", {next}, line);
    }
  }

  void declare_local(const std::string &name,
                     const std::string &type = "__local") {
    const bool first_binding = used_local_names.insert(name).second;
    local_bindings[name] = first_binding
        ? name
        : "$ny.local." + std::to_string(local_identity++) + "." + name;
    local_var_types[name] = type;
    local_depths[name] = lexical_depth;
  }

  void resolve_local_operand(nil_instruction &instruction) const {
    if ((instruction.opname == "LOAD_GLOBAL" ||
         instruction.opname == "STORE_GLOBAL") && !instruction.args.empty())
      reject_escaped_local(instruction.args.front(), instruction.debug_line);
    if ((instruction.opname == "LOAD_LOCAL" ||
         instruction.opname == "STORE_LOCAL" ||
         instruction.opname == "OWN_BIND" ||
         instruction.opname == "OWN_MOVE" ||
         instruction.opname == "BINDING_REF_LOCAL" ||
         instruction.opname == "REF_LOCAL") && !instruction.args.empty()) {
      const auto binding = local_bindings.find(instruction.args.front());
      if (binding != local_bindings.end())
        instruction.args.front() = binding->second;
      else
        reject_escaped_local(instruction.args.front(), instruction.debug_line);
    }
  }

  void reject_escaped_local(const std::string &name, int line) const {
    if (!local_bindings.count(name) && unavailable_captures.count(name)) {
      cg_.reject_codegen_construct(
          line, "lambda capture of local '" + name + "' is not supported",
          "pass the value as an explicit lambda parameter");
      return;
    }
    if (!local_bindings.count(name) && used_local_names.count(name) &&
        !cg_.lookup_global_name(name, current_module_) &&
        !cg_.lookup_function_decl(name)) {
      cg_.reject_codegen_construct(
          line, "local binding '" + name + "' is outside its lexical scope",
          "declare the value in an enclosing scope before using it");
    }
  }

  std::optional<std::string> enum_type_for_expression(
      const ast_expr &expression,
      const std::optional<std::string> &contextual_type = std::nullopt) const {
    std::vector<std::string> candidates;
    if (contextual_type)
      candidates.push_back(*contextual_type);
    if (expression.inferred_type)
      candidates.push_back(*expression.inferred_type);
    if (expression.kind == ast_node_kind::identifier_expr_node) {
      const auto &identifier =
          static_cast<const ast_identifier_expr &>(expression);
      const auto local = local_var_types.find(identifier.name);
      if (local != local_var_types.end())
        candidates.push_back(local->second);
    }

    for (const auto &candidate : candidates) {
      const std::string substituted =
          substitute_nil_type_name(candidate, substitutions);
      if (parse_generic_type_name(substituted) ||
          cg_.lookup_enum_decl(cg_.lookup_enum_fqn(substituted)))
        return substituted;
    }
    return std::nullopt;
  }

  void append_variant_metadata(std::vector<std::string> &arguments,
                               const std::string &variant_name,
                               const std::optional<std::string> &enum_type) {
    if (!enum_type)
      return;
    const auto metadata =
        resolve_concrete_enum_variant(cg_, mod_, *enum_type, variant_name);
    if (!metadata)
      return;
    arguments.push_back(metadata->enum_type);
    arguments.push_back(metadata->payload_type);
  }

  std::string resolved_variant_name(
      const std::string &variant_name,
      const std::optional<std::string> &enum_type, int debug_line,
      bool matching = false) {
    const std::string normalized =
        canonical_std_namespace(canonicalize_variant_name(variant_name));
    const auto separator = normalized.rfind("::");
    if (separator == std::string::npos)
      return normalized;
    const std::string owner = normalized.substr(0, separator);
    const ast_enum_decl *declaration = nullptr;
    if (owner.find("::") == std::string::npos) {
      if (enum_type) {
        const auto parsed = parse_generic_type_name(*enum_type);
        const auto base = parsed ? parsed->base_name : *enum_type;
        const auto *target = base.find("::") != std::string::npos
            ? cg_.lookup_enum_decl(canonical_builtin_enum_base(base)) : nullptr;
        if (target && canonical_builtin_enum_base(target->name) == owner)
          declaration = target;
      }
      if (!declaration && !current_module_.empty())
        declaration = cg_.lookup_enum_decl(current_module_ + "::" + owner);
    }
    if (!declaration)
      declaration = cg_.lookup_enum_decl(owner);
    // Lowercase declarations predate the built-in Option/Result spelling.
    if (!declaration) {
      std::string original = canonical_std_namespace(variant_name);
      for (auto dot = original.find('.'); dot != std::string::npos;
           dot = original.find('.', dot + 2))
        original.replace(dot, 1, "::");
      declaration = cg_.lookup_enum_decl(original.substr(0, original.rfind("::")));
    }
    if (!declaration) {
      if (matching)
        cg_.reject_codegen_construct(debug_line,
            "unknown enum owner '" + owner + "' in match pattern",
            "qualify the pattern with a declared enum type");
      return normalized;
    }
    const std::string leaf = normalized.substr(separator + 2);
    const auto variant = std::find_if(declaration->variants.begin(),
        declaration->variants.end(), [&](const ast_enum_variant &candidate) {
          return variant_leaf_name(declaration->name + "::" + candidate.name) == leaf;
        });
    if (variant == declaration->variants.end()) {
      if (matching)
        cg_.reject_codegen_construct(debug_line,
            "unknown variant '" + leaf + "' in match pattern for enum '" + owner + "'",
            "use a variant declared by the matched enum");
      return normalized;
    }
    if (matching && enum_type) {
      const auto parsed = parse_generic_type_name(*enum_type);
      const std::string base = parsed ? parsed->base_name : *enum_type;
      auto *target = cg_.lookup_enum_decl(base);
      if (!target)
        target = cg_.lookup_enum_decl(canonical_builtin_enum_base(base));
      if (target && target != declaration)
        cg_.reject_codegen_construct(debug_line,
            "match pattern enum '" + owner + "' does not match target type '" + *enum_type + "'",
            "use a variant from the target enum");
    }
    // A source spelling is an alias, not a runtime tag. Recover the owning
    // declaration so bare, qualified, and legacy namespace spellings agree,
    // without collapsing distinct enums that share a leaf name.
    const auto module = cg_.declaration_modules_.find(declaration);
    const std::string qualified_owner =
        module != cg_.declaration_modules_.end() && !module->second.empty()
            ? module->second + "::" + declaration->name : declaration->name;
    // Runtime intrinsics also construct these two standard enums. Preserve
    // their established tags after validating the actual declaration above.
    if (qualified_owner == "nil::std::core::Option" ||
        qualified_owner == "nil::std::core::Result")
      return canonicalize_variant_name(declaration->name + "::" + variant->name);
    return canonicalize_variant_name(qualified_owner + "::" + variant->name);
  }

  void emit_variant_arguments(const std::vector<ast_expr_ptr> &payloads,
                              const std::string &variant_name,
                              const std::optional<std::string> &enum_type,
                              int debug_line) {
    // Infer a fresh nested constructor from its destination payload before
    // emitting it. Post-emission scalar casts cannot recover the schema of
    // a nullary Option.None nested inside Result<Option<T>, E>::Ok(...).
    std::vector<std::string> payload_types;
    if (enum_type) {
      const auto resolved = resolved_variant_name(variant_name, enum_type, debug_line);
      if (const auto metadata = resolve_concrete_enum_variant(cg_, mod_, *enum_type, resolved)) {
        const auto type = trim_type_name(metadata->payload_type);
        if (type.size() >= 2 && type.front() == '(' && type.back() == ')') {
          if (const auto tuple = parse_generic_type_name(
                  "tuple<" + type.substr(1, type.size() - 2) + ">"))
            payload_types = tuple->type_args;
        } else if (type != "void") {
          payload_types.push_back(type);
        }
      }
    }
    for (std::size_t index = 0; index < payloads.size(); ++index)
      emit_expr(*payloads[index], payload_types.size() == payloads.size()
          ? std::optional<std::string>(payload_types[index]) : std::nullopt);
  }

  void emit_new_variant(const std::string &variant_name,
                        std::size_t payload_count,
                        const std::optional<std::string> &enum_type,
                        int debug_line) {
    const auto resolved = resolved_variant_name(variant_name, enum_type, debug_line);
    std::vector<std::string> arguments = {resolved,
                                          std::to_string(payload_count)};
    append_variant_metadata(arguments, resolved, enum_type);
    if (arguments.size() == 4 && payload_count != 0) {
      std::vector<std::string> payload_types;
      const auto payload_type = trim_type_name(arguments[3]);
      if (payload_type.size() >= 2 && payload_type.front() == '(' &&
          payload_type.back() == ')') {
        // Reuse the depth-aware type-list parser for a multi-field variant.
        const auto tuple = parse_generic_type_name(
            "tuple<" + payload_type.substr(1, payload_type.size() - 2) + ">");
        if (tuple)
          payload_types = tuple->type_args;
      } else {
        payload_types.push_back(payload_type);
      }
      if (payload_types.size() == payload_count) {
        if (payload_count == 1) {
          emit_scalar_boundary(payload_types.front(), debug_line);
        } else {
          // Arguments have already been evaluated left-to-right. Canonicalize
          // each destination representation without reevaluating or reordering
          // side effects, then restore the constructor's original stack order.
          std::vector<std::string> payload_slots(payload_count);
          for (std::size_t reverse = 0; reverse < payload_count; ++reverse) {
            const auto index = payload_count - reverse - 1;
            emit_scalar_boundary(payload_types[index], debug_line);
            payload_slots[index] = new_temp("enum.payload");
            push_instr("STORE_LOCAL", {payload_slots[index]}, debug_line);
          }
          for (const auto &slot : payload_slots)
            push_instr("LOAD_LOCAL", {slot}, debug_line);
        }
      }
    }
    push_instr("NEW_VARIANT", arguments, debug_line);
  }

  void emit_is_variant(const std::string &variant_name,
                       const std::optional<std::string> &enum_type,
                       int debug_line) {
    const auto resolved = resolved_variant_name(variant_name, enum_type, debug_line, true);
    std::vector<std::string> arguments = {resolved};
    append_variant_metadata(arguments, resolved, enum_type);
    push_instr("IS_VARIANT", arguments, debug_line);
  }

  void emit_unwrap_variant(std::size_t payload_count,
                           const std::string &variant_name,
                           const std::optional<std::string> &enum_type,
                           int debug_line) {
    std::vector<std::string> arguments = {std::to_string(payload_count)};
    append_variant_metadata(arguments, variant_name, enum_type);
    push_instr("UNWRAP_VARIANT", arguments, debug_line);
  }

  std::string new_label(const std::string &prefix) {
    std::ostringstream oss;
    oss << out_fn.name << "__" << prefix << "_" << (label_id++);
    return oss.str();
  }
  std::string new_temp(const std::string &prefix) {
    std::ostringstream oss;
    // '$' is not a Ny identifier character, so generated scratch storage
    // cannot alias even an adversarially named source local or parameter.
    oss << "$ny.temp." << prefix << "." << (tmp_id++);
    return oss.str();
  }

public:
  void attach_source_debug(nil_instruction &instruction) const {
    if (!cg_.options_.include_source_debug || out_fn.source_file.empty()) return;
    instruction.source_file = out_fn.source_file;
    for (const auto &binding : local_bindings) {
      const auto type = local_var_types.find(binding.first);
      const auto depth = local_depths.find(binding.first);
      instruction.debug_locals.push_back({binding.first, binding.second,
          type == local_var_types.end() ? "unknown" : substitute_nil_type_name(type->second, substitutions),
          depth == local_depths.end() ? 0 : depth->second});
    }
    std::sort(instruction.debug_locals.begin(), instruction.debug_locals.end(),
        [](const auto &a, const auto &b) { return a.name < b.name; });
    validate_source_debug(instruction.source_file, instruction.debug_locals);
  }
  void push_instr(const std::string &op,
                  const std::vector<std::string> &args = {},
                  int debug_line = 0) {
    nil_instruction mi;
    mi.opname = op;
    mi.args = args;
    mi.debug_line = debug_line;
    resolve_local_operand(mi);
    attach_source_debug(mi);
    out_fn.body.push_back(mi);
  }
  void push_instr(const nil_instruction &mi) {
    nil_instruction resolved = mi;
    resolve_local_operand(resolved);
    attach_source_debug(resolved);
    out_fn.body.push_back(std::move(resolved));
  }

  // Ensure module contains a string constant
  void ensure_const_in_module(const std::string &name,
                              const std::string &value) {
    // Check if already present
    for (auto &kv : mod_.string_constants) {
      if (kv.first == name)
        return;
    }
    mod_.string_constants.emplace_back(name, value);
  }

  // Get existing string constant name for a value or add it and return the
  // name
  std::string get_or_add_const_string_name(const std::string &value) {
    // Search by value, so we avoid emitting duplicate values
    for (auto &kv : mod_.string_constants) {
      if (kv.second == value)
        return kv.first;
    }
    std::string name = "S" + std::to_string(mod_.string_constants.size() + 1);
    mod_.string_constants.emplace_back(name, value);
    return name;
  }
};

// =============================================================================
// Ny test code-generation helpers
// =============================================================================

// FNV-1a is specified here rather than implementation-defined std::hash so
// discovery identities remain stable between compiler processes and platforms.
static std::string test_identity(const std::string &file, int line,
                                 const std::string &owner, const std::string &name) {
  const std::string key = file + "\n" + std::to_string(line) + "\n" + owner + "\n" + name;
  std::uint64_t hash = 14695981039346656037ull;
  for (unsigned char byte : key) { hash ^= byte; hash *= 1099511628211ull; }
  std::ostringstream result;
  result << std::hex << hash;
  return result.str();
}

static std::string test_suite(const std::vector<ast_annotation> &annotations,
                              const std::string &fallback) {
  for (const auto &annotation : annotations) {
    if (!annotation.has_name("suite")) continue;
    const auto parsed = parse_annotation_arguments(annotation.args.value_or(""));
    const auto &arguments = annotation.typed_arguments ? *annotation.typed_arguments : parsed.arguments;
    if (arguments.size() == 1 && arguments.front().kind == "text" && !arguments.front().name)
      return arguments.front().value;
  }
  return fallback;
}

static void add_test_case(codegen &cg, nil_module &mod, nil_test_case value) {
  for (const auto &existing : mod.test_cases) {
    if (existing.id == value.id) {
      cg.reject_codegen_construct(value.line, "duplicate test identity for '" + value.name + "'",
          "give colocated tests distinct names or source lines");
      return;
    }
  }
  mod.tests.push_back(value.function);
  mod.test_cases.push_back(std::move(value));
}

static std::string emit_test_body_wrapper(codegen &cg, nil_module &mod,
                                          const ast_test_block &test,
                                          const std::string &parent_func_name,
                                          const std::string &current_module,
                                          const std::string &parent_file = "",
                                          const std::string &parent_suite = "") {
  if (test.type == test_type::property || !test.generators.empty() ||
      std::any_of(test.annotations.begin(), test.annotations.end(), [](const auto &annotation) {
        return annotation.has_name("generate") || annotation.has_name("property");
      })) {
    cg.reject_codegen_construct(test.line, "property generators are not supported by niltest",
        "write deterministic test cases");
    return "";
  }
  if (!test.checked_test) {
    cg.reject_codegen_construct(test.line, "test bodies require checked lexical, type, and ownership boundaries",
        "fix the test diagnostics before generating NIL");
    return "";
  }
  const auto file = test.source_file.empty() ? parent_file : test.source_file;
  const auto name = test.description.empty() ? "test at line " + std::to_string(test.line) : test.description;
  const auto id = test_identity(file, test.line, current_module + "::" + parent_func_name, name);
  nil_function fn;
  fn.name = "__niltest_case_" + id;
  fn.return_type = "void";
  fn.source_line = test.line;
  fn.source_file = file;
  fn.is_test = true;
  emitter em(fn, mod, cg, {}, current_module);
  em.ownership_checked = test.checked_test;
  if (test.body) em.emit_stmt(*test.body);
  append_implicit_function_return(fn, test.line);
  const bool integration = test.type == test_type::integration ||
      std::any_of(test.annotations.begin(), test.annotations.end(), [](const auto &annotation) {
        return annotation.has_name("integration");
      });
  const auto kind = integration ? "integration" : test.type == test_type::example ? "example" : "unit";
  add_test_case(cg, mod, {"niltest-" + id, name,
      test_suite(test.annotations, parent_suite.empty() ? current_module : parent_suite),
      kind, fn.name, file, test.line});
  const auto function = fn.name;
  mod.functions.push_back(std::move(fn));
  return function;
}

static std::string emit_ai_context_registration_func(
    codegen &cg, nil_module &mod, const std::string &context_json,
    const std::string &module_name, const std::string &parent_func_name,
    const std::string &src_file, int line,
    const std::string &test_module_name) {

  nil_function fn;
  std::string id =
      std::to_string(std::hash<std::string>{}(parent_func_name + context_json));
  fn.name = "__nilaix_register_ai_context_" + id;
  fn.return_type = "void";
  fn.source_line = line;
  fn.is_test = false; // Internal registration function

  emitter em(fn, mod, cg);

  // void register_ai_context(String module, String function, String json,
  // String file, i32 line)

  // 1. Module name
  size_t s_mod = mod.string_constants.size();
  std::string k_mod = "S_MOD_" + id;
  mod.string_constants.push_back({k_mod, test_module_name});
  nil_instruction load_mod;
  load_mod.opname = "PUSH_CONST_STR";
  load_mod.args.push_back(k_mod);
  fn.body.push_back(load_mod);

  // 2. Function name
  size_t s_func = mod.string_constants.size();
  std::string k_func = "S_FUNC_" + id;
  mod.string_constants.push_back({k_func, parent_func_name});
  nil_instruction load_func;
  load_func.opname = "PUSH_CONST_STR";
  load_func.args.push_back(k_func);
  fn.body.push_back(load_func);

  // 3. JSON Content
  size_t s_json = mod.string_constants.size();
  std::string k_json = "S_JSON_" + id;
  mod.string_constants.push_back({k_json, context_json});
  nil_instruction load_json;
  load_json.opname = "PUSH_CONST_STR";
  load_json.args.push_back(k_json);
  fn.body.push_back(load_json);

  // 4. File
  size_t s_file = mod.string_constants.size();
  std::string k_file = "S_FILE_" + id;
  mod.string_constants.push_back({k_file, src_file});
  nil_instruction load_file;
  load_file.opname = "PUSH_CONST_STR";
  load_file.args.push_back(k_file);
  fn.body.push_back(load_file);

  // 5. Line
  nil_instruction load_line;
  load_line.opname = "PUSH_CONST";
  load_line.args.push_back(std::to_string(line));
  fn.body.push_back(load_line);

  // Call the Ny AI integration runtime.
  nil_instruction call;
  call.opname = "INTEROP_CALL";
  call.args.push_back("nilaix");                        // Library
  call.args.push_back("register_ai_context_internal"); // Function
  call.args.push_back("5");                            // Arg count
  call.args.push_back("void");                         // Return type
  fn.body.push_back(call);

  nil_instruction ret;
  ret.opname = "RET";
  fn.body.push_back(ret);

  cg.mangle_function_name(fn.name);
  mod.functions.push_back(std::move(fn));
  return fn.name;
}

/*
 * core codegen methods
 */

std::unique_ptr<nil_module>
codegen::assemble_module(const ast_program &program,
                         std::vector<codegen_diagnostic> *diags) {
  ny::diag_debug("[codegen] assemble_module");
  rejected_codegen_constructs_.clear();
  struct_decls_.clear();
  struct_qualified_names_.clear();
  global_var_decls_.clear();
  global_var_modules_.clear();
  global_var_names_.clear();
  global_storage_names_.clear();
  declaration_modules_.clear();
  auto mod = std::make_unique<nil_module>();
  mod->module_name = "root";

  // Collection of generated registration functions to call at init
  std::vector<std::string> registration_funcs; // AI context registrations only

  // Track current module for qualified names
  std::string current_module;

  // Prepare functions
  // First pass: register all function declarations for lookup
  for (const auto &decl : program.declarations) {
    if (decl && decl->kind == ast_node_kind::module_decl_node) {
      auto &md = static_cast<const ast_module_decl &>(*decl);
      current_module = md.name;
    }
    if (decl) declaration_modules_[decl.get()] = current_module;
    if (decl && decl->kind == ast_node_kind::function_decl_node) {
      auto &fn_decl = static_cast<const ast_function_decl &>(*decl);
      // Use qualified name for lookup registration
      std::string qualified = current_module.empty()
                                  ? fn_decl.name
                                  : current_module + "::" + fn_decl.name;
      // We store by simple name? No, if we qualify, lookup should use full
      // name? Existing lookup_function_decl uses whatever key we put here. But
      // resolution usually uses qualified name if imported. Let's store BOTH
      // simple (if in current module) and qualified? Or just assume lookup uses
      // qualified names by default? For now, key by fn_decl.name (simple) but
      // we might overwrite. Better: key by qualified name! But caller of lookup
      // might pass simple name.
      function_decls_[fn_decl.name] = &fn_decl; // Legacy/Simple
      function_decls_[qualified] = &fn_decl;    // Qualified
    }
    // ... enums, classes ...
    if (decl && decl->kind == ast_node_kind::enum_decl_node) {
      auto &enum_decl = static_cast<const ast_enum_decl &>(*decl);
      enum_decls_[enum_decl.name] = &enum_decl;
      if (!current_module.empty()) {
        enum_decls_[current_module + "::" + enum_decl.name] = &enum_decl;
      }
    }
    // Implementations: register methods for lookup
    if (decl && decl->kind == ast_node_kind::impl_decl_node) {
      auto &impl_decl = static_cast<const ast_impl_decl &>(*decl);

      // Determine prefix (e.g., "math::vector::vec3")
      std::string base_type = impl_decl.target_type;

      // Strip generic parameters for lookup key (e.g. "vec<T>" -> "vec")
      // Lookup typically happens on the base name unless specialized
      size_t lt = base_type.find('<');
      if (lt != std::string::npos) {
        base_type = base_type.substr(0, lt);
      }

      if (!impl_decl.generic_params.empty()) {
        generic_impl_decls_[base_type].push_back(&impl_decl);
      }

      std::string prefix = base_type;
      if (const auto builtin = builtin_impl_target(base_type)) {
        prefix = *builtin; // builtins are never module-qualified
      } else {
        prefix = qualify_impl_target(prefix, current_module);
      }
      // Register each method
      for (const auto &method : impl_decl.methods) {
        declaration_modules_[method.get()] = current_module;
        // Construct fully qualified name used for lookup (e.g.
        // "math::vector::vec3::new")
        std::string full_name = prefix + "::" + method->name;

        // An extension method's symbol is keyed by the TYPE it extends, not
        // by the extending module, so two implementations adding the same
        // method to one type emit one label. Left undetected, the only
        // report is NIL validation's "Duplicate NIL label '<symbol>'", which
        // names neither module nor a remedy -- and this is the collision
        // imported packages produce in practice, each shipping its own
        // slightly different 'do_something'.
        const auto existing = function_decls_.find(full_name);
        if (existing != function_decls_.end() &&
            existing->second != method.get()) {
          const auto other = declaration_modules_.find(existing->second);
          const std::string unnamed = "<unnamed module>";
          const std::string other_module =
              (other != declaration_modules_.end() && !other->second.empty())
                  ? other->second
                  : unnamed;
          const std::string this_module =
              current_module.empty() ? unnamed : current_module;
          // Only a CROSS-MODULE collision is rejected. Two same-named methods
          // in one module are that author's own business and the behaviour is
          // pre-existing: ny.lang.core's generic Result impl declares both an
          // associated 'ok(value: T)' and an instance 'ok(self)', and the
          // registration keeps the last. Rejecting that would break the
          // standard library for a wart its own author can see, while the
          // hazard worth catching is two independently imported packages each
          // adding the same method to one type -- which no single author can
          // see and which otherwise surfaces only as a duplicate NIL label.
          if (other_module != this_module)
          reject_codegen_construct(
              method->line,
              "method '" + method->name + "' is added to type '" + base_type +
                  "' by two implementations, in '" + other_module +
                  "' and '" + this_module + "'",
              "an extension method's symbol is keyed by the type it extends, "
              "not by the extending module, so one program may contain only "
              "one implementation adding a given method to a type; rename one "
              "of them, or do not import both modules together");
        }
        function_decls_[full_name] = method.get();
        // std::fprintf(stderr, "[CODEGEN] Registered Impl Method: %s\n",
        // full_name.c_str());
      }
    }
    if (decl && decl->kind == ast_node_kind::type_alias_decl_node &&
        static_cast<const ast_type_alias_decl &>(*decl).name == "tuple")
      reject_codegen_construct(decl->line, "Type name 'tuple' is reserved for structural tuples", "choose a different alias name");
    if (decl && decl->kind == ast_node_kind::class_decl_node) {
      auto &class_decl = static_cast<const ast_class_decl &>(*decl);
      if (!class_decl.checked_name) {
        reject_codegen_construct(class_decl.line,
            "class lowering requires checked class identity and inheritance layout",
            "enable type checking for class declarations");
      } else {
        class_decls_[*class_decl.checked_name] = &class_decl;
        for (const auto &method : class_decl.methods)
          declaration_modules_[method.get()] = current_module;
      }
    }
    if (decl && decl->kind == ast_node_kind::struct_decl_node) {
      auto &struct_decl = static_cast<const ast_struct_decl &>(*decl);
      if (struct_decl.name == "tuple") {
        reject_codegen_construct(struct_decl.line, "Type name 'tuple' is reserved for structural tuples", "choose a nominal record name");
        continue;
      }
      if (struct_decl.concrete_generic_type)
        continue;
      struct_qualified_names_[&struct_decl] = current_module.empty()
          ? struct_decl.name : current_module + "::" + struct_decl.name;
      struct_decls_[struct_decl.name] = &struct_decl;
      if (!current_module.empty()) {
        struct_decls_[current_module + "::" + struct_decl.name] = &struct_decl;
      }
    }
    // Collect module-level variable declarations (global let)
    if (decl && decl->kind == ast_node_kind::variable_decl_node) {
      auto &var_decl = static_cast<const ast_variable_decl &>(*decl);
      if (!var_decl.tuple_bindings.empty()) {
        reject_codegen_construct(var_decl.line, "Tuple destructuring is only supported for local bindings", "move destructuring into a function");
        continue;
      }
      const auto storage = current_module.empty() ? var_decl.name
                                                  : current_module + "::" + var_decl.name;
      if (global_var_decls_.count(storage)) {
        reject_codegen_construct(
            var_decl.line, "duplicate global storage name '" + storage + "'",
            "declare a module-level variable only once in its module");
        continue;
      }
      global_var_decls_[storage] = &var_decl;
      global_var_modules_[storage] = current_module;
      global_storage_names_[&var_decl] = storage;
      global_var_names_.insert(storage);
    }
  }

  if (!rejected_codegen_constructs_.empty()) {
    if (diags)
      diags->insert(diags->end(), rejected_codegen_constructs_.begin(),
                    rejected_codegen_constructs_.end());
    return nullptr;
  }

  // Generate __globals_init__ function if we have global variables
  if (!global_var_decls_.empty()) {
    nil_function init_fn;
    init_fn.name = "__globals_init__";
    init_fn.return_type = "void";
    init_fn.source_line = 0;

    // Emit STORE_GLOBAL for each global variable
    // Initializers run in declaration order, not lexical name order. An
    // earlier global may be used by a later initializer.
    for (const auto &declaration : program.declarations) {
      if (!declaration ||
          declaration->kind != ast_node_kind::variable_decl_node)
        continue;
      const auto *vd = static_cast<const ast_variable_decl *>(declaration.get());
      if (!vd->initializer && (vd->rejected_dynamic_initializer ||
          (vd->type_name && canonical_dynamic_type(*vd->type_name) == "anything"))) {
        reject_codegen_construct(vd->line, "an anything global requires an initial value",
            "initialize it or use Option<anything> for absence");
      }
      if (vd->initializer) {
        // Use a temporary emitter to emit the initializer expression
        const auto &storage = global_storage_names_.at(vd);
        std::string var_module = global_var_modules_.at(storage);
        emitter temp_em(init_fn, *mod, *this, {}, var_module);
        temp_em.emit_expr(*vd->initializer, vd->type_name);
        temp_em.emit_scalar_boundary(vd->type_name ? vd->type_name :
                                    vd->initializer->inferred_type, vd->line);

        // Emit STORE_GLOBAL
        nil_instruction store_instr;
        store_instr.opname = "STORE_GLOBAL";
        store_instr.args.push_back(storage);
        store_instr.debug_line = vd->line;
        init_fn.body.push_back(store_instr);
      }
    }

    // Emit RET
    nil_instruction ret_instr;
    ret_instr.opname = "RET";
    init_fn.body.push_back(ret_instr);

    mod->functions.push_back(init_fn);
  }

  // Reset current module for generation pass
  current_module.clear();

  for (const auto &decl : program.declarations) {
    if (!decl)
      continue;

    if (decl->kind == ast_node_kind::module_decl_node) {
      auto &md = static_cast<const ast_module_decl &>(*decl);
      current_module = md.name;
    } else if (decl->kind == ast_node_kind::impl_decl_node) {
      auto &impl_decl = static_cast<const ast_impl_decl &>(*decl);
      if (!impl_decl.generic_params.empty())
        continue; // Skip generic impls

      std::string base_type = impl_decl.target_type;
      size_t lt = base_type.find('<');
      if (lt != std::string::npos)
        base_type = base_type.substr(0, lt);

      std::string prefix = base_type;
      const auto builtin = builtin_impl_target(base_type);
      if (builtin) {
        prefix = *builtin; // builtins are never module-qualified
      } else {
        prefix = qualify_impl_target(prefix, current_module);
      }
      std::string receiver_type = impl_decl.target_type;
      if (builtin) {
        // 'self' has the canonical builtin type, matching what the call site
        // infers for the receiver.
        receiver_type = *builtin;
      } else {
        receiver_type = qualify_impl_target(receiver_type, current_module);
      }
      const std::map<std::string, std::string> method_substitutions = {
          {"Self", receiver_type}};

      for (const auto &fn_decl : impl_decl.methods) {
        if (!fn_decl->generic_params.empty())
          continue; // Skip generic methods

        nil_function fn;
        std::string qualified = prefix + "::" + fn_decl->name;
        fn.name = mangle_fully(qualified);
        fn.return_type = substitute_nil_type_name(
            fn_decl->return_type.value_or(std::string("void")),
            method_substitutions);
        fn.source_line = fn_decl->line;
        fn.source_file = fn_decl->source_file;

        for (const auto &p : fn_decl->params) {
          if (!append_serialized_nil_parameter(
                  fn, p, method_substitutions, receiver_type)) {
            reject_codegen_construct(
                p.line,
                "method parameter '" + p.name +
                    "' has no exact type for serialized NIL",
                "annotate non-receiver parameters with an explicit type");
          }
        }

        // Annotations
        fn.is_inline = false;
        fn.is_test = false;
        fn.annotations.clear();
        for (const auto &ann : fn_decl->annotations) {
          std::string t = "@" + ann.name;
          if (ann.args.has_value())
            t += "(" + *ann.args + ")";
          fn.annotations.push_back(t);
          if (ann.name == "inline")
            fn.is_inline = true;
          if (ann.name == "test") {
            reject_codegen_construct(fn_decl->line, "@test requires a module-level function",
                "move the test outside the implementation");
          }
        }

        emitter em(fn, *mod, *this, method_substitutions, current_module);
        em.register_function_params(*fn_decl);
        // Emit STORE_LOCAL preamble in reverse param order (LIFO) so args
        // pushed by callers are bound into locals.
        for (auto it = fn_decl->params.rbegin(); it != fn_decl->params.rend();
             ++it) {
          nil_instruction store;
          store.opname = "STORE_LOCAL";
          store.args.push_back(it->name);
          store.debug_line = fn_decl->line;
          fn.body.push_back(store);
        }
        if (fn_decl->body)
          em.emit_stmt(*fn_decl->body);

        append_implicit_function_return(fn, fn_decl->line);
        mod->functions.push_back(fn);
      }
    } else if (decl->kind == ast_node_kind::function_decl_node) {
      auto &fn_decl = static_cast<const ast_function_decl &>(*decl);
      const bool declared_test = std::any_of(fn_decl.annotations.begin(), fn_decl.annotations.end(),
          [](const auto &annotation) { return annotation.has_name("test"); });
      if (options_.mode == build_mode::test_mode && declared_test && !fn_decl.checked_test_function) {
        reject_codegen_construct(fn_decl.line, "test functions require a checked zero-argument, non-generic void signature",
            "fix test function diagnostics before generating NIL");
        continue;
      }
      // Attached cases are independent of the function's type parameters and
      // are discovered once even when the function itself is monomorphized.
      if (options_.mode == build_mode::test_mode) {
        const auto suite = test_suite(fn_decl.annotations, current_module);
        for (const auto &test : fn_decl.test_blocks)
          if (test) emit_test_body_wrapper(*this, *mod, *test, fn_decl.name,
              current_module, fn_decl.source_file, suite);
        for (const auto &test : fn_decl.example_blocks)
          if (test) emit_test_body_wrapper(*this, *mod, *test, fn_decl.name,
              current_module, fn_decl.source_file, suite);
      }

      // Skip generic function definitions; they are emitted via specialization
      if (!fn_decl.generic_params.empty()) {
        continue;
      }
      nil_function fn;
      if (fn_decl.name == "main" || fn_decl.name.find("ny_mimic") == 0) {
        fn.name = fn_decl.name;
      } else {
        std::string qualified = current_module.empty()
                                    ? fn_decl.name
                                    : current_module + "::" + fn_decl.name;
        fn.name = mangle_fully(qualified);
      }
      fn.return_type = fn_decl.return_type.value_or(std::string("void"));
      fn.source_line = fn_decl.line;
      fn.source_file = fn_decl.source_file;

      // Populate params
      for (const auto &p : fn_decl.params) {
        if (!append_serialized_nil_parameter(fn, p)) {
          reject_codegen_construct(
              p.line,
              "function parameter '" + p.name +
                  "' has no exact type for serialized NIL",
              "add an explicit type annotation to the parameter");
        }
      }

      // Propagate AST annotations (if any) into the nil_function and honor
      // popular hints such as @inline and @test. The AST stores structured
      // annotations as `ast_annotation` records.
      fn.is_inline = false;
      fn.is_test = false;
      fn.annotations.clear();
      for (const auto &ann : fn_decl.annotations) {
        // Build textual representation for the function-level annotations:
        // @name or @name(args)
        std::string t = "@" + ann.name;
        if (ann.args.has_value()) {
          t += "(" + *ann.args + ")";
        }
        fn.annotations.push_back(t);
        // Honor @inline as a direct codegen hint
        if (ann.name == "inline") {
          fn.is_inline = true;
        }
        // Honor @test as a built-in test marker and register it in the
        // module's test list for downstream test harness generation.
        if (ann.name == "test") {
          fn.is_test = true;
          if (options_.mode == build_mode::test_mode) {
            const auto id = test_identity(fn.source_file, fn.source_line, current_module, fn_decl.name);
            add_test_case(*this, *mod, {"niltest-" + id, fn_decl.name,
                test_suite(fn_decl.annotations, current_module), "unit", fn.name, fn.source_file, fn.source_line});
          }
        }
      }

      // Check for @if_test_mode annotation
      bool is_test_only = false;
      for (const auto &ann : fn_decl.annotations) {
        if (ann.name == "if_test_mode" || ann.name == "test") {
          is_test_only = true;
          break;
        }
      }

      // If code is test-only and we are not in test mode, skip entire function
      if (is_test_only && options_.mode != build_mode::test_mode) {
        continue;
      }

      // Emitter to fill function body
      emitter em(fn, *mod, *this, {}, current_module);
      // Register function parameters types so emitter can leverage them
      // during emission (e.g., optimize loops over parameter 'vec' or
      // 'array').
      em.register_function_params(fn_decl);

      // Inject call to __globals_init__ in main if we have globals
      if (fn_decl.name == "main") {
        if (!global_var_decls_.empty()) {
          nil_instruction call_init;
          call_init.opname = "CALL";
          call_init.args.push_back("__globals_init__");
          call_init.args.push_back("0");
          call_init.debug_line = fn_decl.line;
          fn.body.push_back(call_init);
        }
      }


      // Register AI context when present.
      // Extract from annotations directly to be robust against parser
      // timing/duplication issues.
      std::string ai_context_json;
      for (const auto &ann : fn_decl.annotations) {
        if ((ann.name == "ai_context" || ann.name == "@ai_context") &&
            ann.args.has_value()) {
          std::string val = *ann.args;
          if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
            ai_context_json = val.substr(1, val.size() - 2);
          }
          break;
        }
      }

      if (!ai_context_json.empty() &&
          options_.mode != build_mode::release_mode) {
        if (options_.mode == build_mode::test_mode ||
            options_.mode == build_mode::debug_mode) {
          std::string reg_name = emit_ai_context_registration_func(
              *this, *mod, ai_context_json, current_module, fn_decl.name, "src",
              fn_decl.line, current_module);
          registration_funcs.push_back(reg_name);
        }
      }

      // Check if function has @interop annotation - if so, emit INTEROP_CALL
      std::string interop_lib;
      if (ny::is_debug_enabled()) {
        ny::diag_debug("[codegen] Function " + fn_decl.name + " has " +
                       std::to_string(fn_decl.annotations.size()) +
                       " annotations");
        for (const auto &ann : fn_decl.annotations) {
          std::string msg = "[codegen]   annotation: " + ann.name;
          if (ann.args.has_value())
            msg += "(" + *ann.args + ")";
          ny::diag_debug(msg);
        }
      }
      for (const auto &ann : fn_decl.annotations) {
        if (ann.name == "interop") {
          if (ann.args.has_value()) {
            std::string lib = *ann.args;
            if (lib.size() >= 2 && lib.front() == '"' && lib.back() == '"') {
              lib = lib.substr(1, lib.size() - 2);
            }
            interop_lib = lib;
          } else {
            interop_lib = "default";
          }
          break;
        }
        if (ann.name == "intrinsic") {
          // Treat intrinsic as a special interop library "intrinsic"
          // The logic below will handle it if we set interop_lib = "intrinsic"
          // and maybe smuggle the intrinsic name?
          // Actually, INTEROP_CALL takes (lib, func_name, argc).
          // VM expects (lib="intrinsic", func_name=intrinsic_id, ...).
          // So we can reuse the interop path if we set:
          // interop_lib = "intrinsic"
          // fn_decl.name = intrinsic_name (wait, we shouldn't change fn_decl)
          // But strict interop emission below uses `fn_decl.name`.

          // Let's rely on a separate variable or logic.
          if (ann.args.has_value()) {
            std::string val = *ann.args;
            if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
              // We found @intrinsic("dir.list").
              // We want to emit INTEROP_CALL "intrinsic" "dir.list" ...
              // The existing interop block below uses `fn_decl.name`.
              // We need to override that.
              // Let's implement a separate block for intrinsic to be clean.
            }
          }
        }
      }

      // Check for @intrinsic specifically
      std::string intrinsic_name;
      bool is_intrinsic_fn = false;
      for (const auto &ann : fn_decl.annotations) {
        if (ann.name == "intrinsic") {
          is_intrinsic_fn = true;
          if (ann.args.has_value()) {
            std::string val = *ann.args;
            if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
              intrinsic_name = val.substr(1, val.size() - 2);
            } else {
              intrinsic_name = val;
            }
          } else {
            // Default mapping for known intrinsics
            if (fn_decl.name == "current_time_ms")
              intrinsic_name = "platform.current_time_ms";
            else if (fn_decl.name == "sleep_ms")
              intrinsic_name = "platform.sleep_ms";
            else if (fn_decl.name == "get_current_test_name")
              intrinsic_name = "ny.testing.get_current_test_name";
            else if (fn_decl.name == "get_current_module_name")
              intrinsic_name = "compiler.get_current_module_name";
            else
              intrinsic_name = fn_decl.name;
          }
          break;
        }
      }

      if (is_intrinsic_fn) {
        // Intrinsic declarations are ordinary Ny wrapper functions at their
        // public boundary. Bind caller arguments into locals before loading
        // them again for INTEROP_CALL, matching normal and @interop functions.
        for (auto it = fn_decl.params.rbegin(); it != fn_decl.params.rend();
             ++it) {
          nil_instruction store;
          store.opname = "STORE_LOCAL";
          store.args.push_back(it->name);
          store.debug_line = fn_decl.line;
          fn.body.push_back(store);
        }

        // Optimization for get_current_module_name: compile-time constant
        if (intrinsic_name == "compiler.get_current_module_name") {
          std::string k_mod = "S_CUR_MOD_" + mangle_function_name(fn_decl.name);
          mod->string_constants.push_back(
              {k_mod, current_module.empty() ? "root" : current_module});

          nil_instruction load;
          load.opname = "PUSH_CONST_STR";
          load.args.push_back(k_mod);
          load.debug_line = fn_decl.line;
          fn.body.push_back(load);

          nil_instruction ret;
          ret.opname = "RET";
          fn.body.push_back(ret);
        } else {
          // Emit wrapper for intrinsic
          // 2. Now load params for INTEROP_CALL (in forward order for call)
          for (const auto &p : fn_decl.params) {
            nil_instruction load;
            load.opname = "LOAD_LOCAL";
            load.args.push_back(p.name);
            load.debug_line = fn_decl.line;
            fn.body.push_back(load);
          }

          // INTEROP_CALL "intrinsic" "name" argc ret_type
          nil_instruction call;
          call.opname = "INTEROP_CALL";
          call.args.push_back("intrinsic");
          call.args.push_back(intrinsic_name);
          call.args.push_back(std::to_string(fn_decl.params.size()));

          std::string ret_type_str = "void";
          if (intrinsic_name.rfind("regex.", 0) == 0 ||
              intrinsic_name.rfind("nildata.", 0) == 0 ||
              intrinsic_name.rfind("ui.v1.", 0) == 0) {
            ret_type_str = canonical_nil_type_name(fn.return_type);
          } else if (is_declared_enum_type(*this, fn.return_type)) {
            ret_type_str = canonical_nil_type_name(fn.return_type);
          } else if (fn_decl.return_type.has_value()) {
            ret_type_str = *fn_decl.return_type;
            size_t sz = get_native_size(
                *this, ret_type_str); // Need `get_native_size` visibility?
            // get_native_size might be a static helper not in class.
            // Previous code at line 1797 used it. It seems available.
            if (sz > 0 && sz <= 8)
              ret_type_str = "i64";
            else if (sz > 8)
              ret_type_str =
                  "struct:" + std::to_string(sz) + ":" + ret_type_str;
          }
          call.args.push_back(ret_type_str);

          call.debug_line = fn_decl.line;
          fn.body.push_back(call);

          // RET
          nil_instruction ret;
          ret.opname = "RET";
          ret.debug_line = fn_decl.line;
          fn.body.push_back(ret);
        }
      } else if (!interop_lib.empty()) {
        // For @interop functions:
        // 1. First store all function parameters into locals (reverse order
        // since stack is LIFO)
        // 2. Then load params for INTEROP_CALL to pop
        for (auto it = fn_decl.params.rbegin(); it != fn_decl.params.rend();
             ++it) {
          nil_instruction store;
          store.opname = "STORE_LOCAL";
          store.args.push_back(it->name);
          store.debug_line = fn_decl.line;
          fn.body.push_back(store);
        }

        // Now load params for INTEROP_CALL (in forward order for call)
        for (const auto &p : fn_decl.params) {
          nil_instruction load;
          load.opname = "LOAD_LOCAL";
          load.args.push_back(p.name);
          load.debug_line = fn_decl.line;
          fn.body.push_back(load);
        }

        // Emit INTEROP_CALL with type annotations
        // Format: INTEROP_CALL lib fn argc ret_type param_types
        nil_instruction call;
        call.opname = "INTEROP_CALL";
        call.args.push_back(interop_lib);
        call.args.push_back(fn_decl.name);
        call.args.push_back(std::to_string(fn_decl.params.size()));

        // Return type
        std::string ret_type_str = "void";
        if (fn_decl.return_type.has_value()) {
          const std::string &rt = *fn_decl.return_type;
          // text/String come back as const char* — tag as "text" for VM
          if (rt == "text" || rt == "String") {
            ret_type_str = "text";
          } else {
            ret_type_str = rt;
            size_t sz = get_native_size(*this, rt);
            if (sz > 0 && sz <= 8)
              ret_type_str = "i64";
            else if (sz > 8) {
              // Ensure we use the qualified type name if possible for VM
              // unpacking
              std::string full_rt = rt;
              if (rt.find("::") == std::string::npos &&
                  !current_module.empty()) {
                full_rt = current_module + "::" + rt;
              }
              ret_type_str = "struct:" + std::to_string(sz) + ":" + full_rt;
            }
          }
        }
        call.args.push_back(ret_type_str);

        // Parameter types (for FFI float/double handling)
        // Format: "f32,i32,f64,i64" etc
        std::string param_types;
        for (size_t i = 0; i < fn_decl.params.size(); ++i) {
          if (i > 0)
            param_types += ",";
          if (fn_decl.params[i].type_name.has_value()) {
            param_types += *fn_decl.params[i].type_name;
          } else {
            param_types += "any";
          }
        }
        if (!param_types.empty()) {
          call.args.push_back(param_types);
        }

        call.debug_line = fn_decl.line;
        fn.body.push_back(call);

        // RET (return value is on stack from INTEROP_CALL if any)
        nil_instruction ret;
        ret.opname = "RET";
        ret.debug_line = fn_decl.line;
        fn.body.push_back(ret);

      } else {
        // Normal function body. Emit STORE_LOCAL preamble in reverse param
        // order (LIFO) so args pushed by callers are bound into locals.
        // nil_loader does NOT add these — they live exclusively in the body.
        for (auto it = fn_decl.params.rbegin(); it != fn_decl.params.rend();
             ++it) {
          nil_instruction store;
          store.opname = "STORE_LOCAL";
          store.args.push_back(it->name);
          store.debug_line = fn_decl.line;
          fn.body.push_back(store);
        }
        if (fn_decl.body) {
          em.emit_stmt(*fn_decl.body);
        }
        append_implicit_function_return(fn, fn_decl.line);
      }

      mod->functions.push_back(std::move(fn));
    } else if (decl->kind == ast_node_kind::class_decl_node) {
      auto &class_decl = static_cast<const ast_class_decl &>(*decl);
      if (!class_decl.checked_name || !class_decl.generic_params.empty())
        continue;
      const std::string concrete_class = *class_decl.checked_name;
      nil_struct_def s;
      s.name = concrete_class;
      for (const auto &f : class_decl.checked_fields) {
        s.fields.push_back({f.name, f.type_name});
      }
      mod->structs.push_back(s);

      nil_class_def class_metadata;
      class_metadata.name = concrete_class;
      class_metadata.is_abstract = class_decl.is_abstract;
      class_metadata.bases = class_decl.base_classes;
      for (const auto &method : class_decl.methods) {
        if (!method->generic_params.empty()) continue;
        nil_class_method metadata;
        metadata.name = method->name;
        metadata.is_virtual = class_decl.checked_virtual_methods.count(method->name) != 0;
        metadata.is_abstract = method->is_abstract;
        metadata.implementation = method->is_abstract ? "" : concrete_class + "::" + method->name;
        const std::map<std::string, std::string> substitutions{{"Self", concrete_class}};
        metadata.receiver_type = method->params.empty() ? concrete_class :
            substitute_nil_type_name(method->params.front().type_name.value_or(concrete_class), substitutions);
        metadata.return_type = substitute_nil_type_name(method->return_type.value_or("void"), substitutions);
        for (std::size_t i = 1; i < method->params.size(); ++i)
          metadata.parameter_types.push_back(substitute_nil_type_name(
              method->params[i].type_name.value_or("any"), substitutions));
        class_metadata.methods.push_back(std::move(metadata));
      }
      // Rendered as a function's and a record's are, so one spelling reaches
      // the artifact whatever the declaration is.
      for (const auto &annotation : class_decl.annotations) {
        std::string text = "@" + annotation.name;
        if (annotation.args.has_value()) text += "(" + *annotation.args + ")";
        class_metadata.annotations.push_back(std::move(text));
      }
      mod->classes.push_back(std::move(class_metadata));

      // A method body retains its declaring-owner view and lexical module.
      // Dispatch is explicit in CALL_METHOD, so inherited self may
      // escape without cloning/relabeling explicit owner-valued expressions.
      for (const auto &declared_method : class_decl.methods) {
        const auto *method = declared_method.get();
        const auto &owner = concrete_class;
        if (method->is_abstract || !method->generic_params.empty()) continue;
        nil_function fn;
        fn.name = concrete_class + "::" + method->name;
        const std::map<std::string, std::string> method_substitutions = {
            {"Self", owner}};
        fn.return_type = substitute_nil_type_name(
            method->return_type.value_or("void"), method_substitutions);
        fn.source_line = method->line;
        fn.source_file = method->source_file;

        for (const auto &p : method->params) {
          auto parameter_substitutions = method_substitutions;
          if (p.name == "self") {
            parameter_substitutions["Self"] = concrete_class;
            parameter_substitutions[owner] = concrete_class;
          }
          if (!append_serialized_nil_parameter(
                  fn, p, parameter_substitutions, concrete_class)) {
            reject_codegen_construct(
                p.line,
                "class method parameter '" + p.name +
                    "' has no exact type for serialized NIL",
                "annotate non-receiver parameters with an explicit type");
          }
        }

        // Annotations
        for (const auto &ann : method->annotations) {
          std::string t = "@" + ann.name;
          if (ann.args.has_value())
            t += "(" + *ann.args + ")";
          fn.annotations.push_back(t);
        }

        emitter em(fn, *mod, *this, method_substitutions, declaration_modules_[method]);
        em.register_function_params(*method);
        // Also register 'self' type if implicit
        // register_function_params only checks p.type_name.
        // We might need to manually add self type to local_var_types in
        // emitter? But emitter uses local_var_types for loop optimization
        // mostly.

        for (auto parameter = method->params.rbegin();
             parameter != method->params.rend(); ++parameter)
          em.push_instr("STORE_LOCAL", {parameter->name}, method->line);
        if (method->body) {
          em.emit_stmt(*method->body);
        }

        append_implicit_function_return(fn, method->line);
        mod->functions.push_back(std::move(fn));
      }
    } else if (decl->kind == ast_node_kind::test_block_node) {
      if (options_.mode == build_mode::test_mode)
        emit_test_body_wrapper(*this, *mod,
            static_cast<const ast_test_block &>(*decl), "", current_module);
    } else if (decl->kind == ast_node_kind::struct_decl_node) {
      auto &struct_decl = static_cast<const ast_struct_decl &>(*decl);
      if (struct_decl.concrete_generic_type) {
        materialize_struct_schema(*struct_decl.concrete_generic_type, *mod,
                                  struct_decl.line, 0, false);
        continue;
      }
      // Skip generic struct definitions
      if (!struct_decl.generic_params.empty()) {
        continue;
      }
      nil_struct_def def;
      def.name = current_module.empty()
                     ? struct_decl.name
                     : current_module + "::" + struct_decl.name;
      for (const auto &field : struct_decl.fields) {
        def.fields.emplace_back(field.name, field.type_name);
      }
      // Rendered the same way a function's are, so one spelling reaches the
      // artifact whatever the declaration is.
      for (const auto &annotation : struct_decl.annotations) {
        std::string text = "@" + annotation.name;
        if (annotation.args.has_value()) text += "(" + *annotation.args + ")";
        def.annotations.push_back(std::move(text));
      }
      mod->structs.push_back(def);
    } else if (decl->kind == ast_node_kind::enum_decl_node) {
      auto &enum_decl = static_cast<const ast_enum_decl &>(*decl);
      // Skip generic enum definitions
      if (!enum_decl.generic_params.empty()) {
        continue;
      }
      nil_enum_def def;
      def.name = current_module.empty()
                     ? enum_decl.name
                     : current_module + "::" + enum_decl.name;
      for (const auto &variant : enum_decl.variants) {
        nil_enum_def::variant v;
        v.name = variant.name;
        if (variant.associated_type.has_value()) {
          v.associated_type = *variant.associated_type;
        }
        def.variants.push_back(v);
      }
      for (const auto &annotation : enum_decl.annotations) {
        std::string text = "@" + annotation.name;
        if (annotation.args.has_value()) text += "(" + *annotation.args + ")";
        def.annotations.push_back(std::move(text));
      }
      mod->enums.push_back(def);
    } else if (decl->kind == ast_node_kind::import_decl_node) {
      // For now, ignore import declarations at codegen time (handled in
      // linker/resolver)
    } else if (decl->kind == ast_node_kind::module_decl_node) {
      // Module decl - not needed in textual NIL module output; it's already a
      // file-level concept
    } else if (decl->kind == ast_node_kind::type_alias_decl_node) {
      // Type aliases don't produce code in NIL text output at this stage
    } else {
      // Unhandled decl-kind - ignore
    }
  }

  // Process pending specializations
  // We use an index loop because the vector might grow as we process items
  for (size_t i = 0; i < pending_specializations_.size(); ++i) {
    auto req = pending_specializations_[i]; // copy
    generate_specialization(req, *mod, diags);
  }

  // Process pending impl method specializations  }

  ny::diag_debug("[codegen] Starting impl specialization loop; queue size=" +
                 std::to_string(pending_impl_specializations_.size()));
  // Process pending impl method specializations
  for (size_t i = 0; i < pending_impl_specializations_.size(); ++i) {
    auto req = pending_impl_specializations_[i]; // copy
    ny::diag_debug("[codegen] Processing impl specialization " +
                   req.target_type + "::" + req.method_name);
    generate_impl_method_specialization(req, *mod, diags);
  }

  // Preserve the checked storage type on every access, including initializer
  // stores and accesses in specialized functions. Non-scalar globals retain
  // their legacy interpreted representation until they have a native ABI.
  for (auto &function : mod->functions) {
    for (auto &instruction : function.body) {
      if ((instruction.opname != "LOAD_GLOBAL" &&
           instruction.opname != "STORE_GLOBAL") ||
          instruction.args.size() != 1)
        continue;
      const auto declaration = global_var_decls_.find(instruction.args.front());
      if (declaration == global_var_decls_.end())
        continue;
      const auto *variable = declaration->second;
      auto type = variable->type_name;
      if (!type && variable->initializer)
        type = variable->initializer->inferred_type;
      if (!type)
        continue;
      const auto storage_type = canonical_nil_type_name(*type);
      if (is_typed_global_storage_type(storage_type))
        instruction.args.push_back(storage_type);
    }
  }

  // Concrete aggregate types can be reachable through ABI signatures without
  // a literal or variant operation in this module. Materialize those schemas
  // independently of the checker's optional synthetic AST declarations.
  std::set<std::string> visited_enum_signatures;
  std::function<void(const std::string &, unsigned)> materialize_enum_signature =
      [&](const std::string &raw_type, unsigned depth) {
    const std::string type = canonical_nil_type_name(raw_type);
    if (depth > 64 || !visited_enum_signatures.insert(type).second)
      return;
    const auto parsed = parse_generic_type_name(type);
    if (!parsed)
      return;
    const auto base = canonical_builtin_enum_base(parsed->base_name);
    const auto *declaration = lookup_enum_decl(lookup_enum_fqn(base));
    if (declaration && !declaration->variants.empty()) {
      const auto metadata = resolve_concrete_enum_variant(
          *this, *mod, type, base + "::" + declaration->variants.front().name);
      if (metadata) {
        // Copy payload type names before recursive materialization can grow
        // mod->enums and invalidate references into its storage.
        std::vector<std::string> payload_types;
        for (const auto &schema : mod->enums)
          if (schema.name == metadata->enum_type)
            for (const auto &variant : schema.variants)
              if (!variant.associated_type.empty())
                payload_types.push_back(variant.associated_type);
        for (const auto &payload : payload_types)
          materialize_enum_signature(payload, depth + 1);
      }
    }
    for (const auto &argument : parsed->type_args)
      materialize_enum_signature(argument, depth + 1);
  };
  for (auto &function : mod->functions) {
    // Parameters already use this normalization at serialization. Return
    // types must share the exact schema key: String/text and spacing cannot
    // produce distinct native ABI identities for the same qualified enum.
    function.return_type = canonical_nil_type_name(function.return_type);
    for (const auto &instruction : function.body)
      if ((instruction.opname == "BOX_ANY" || instruction.opname == "UNBOX_ANY" ||
           instruction.opname == "IS_ANY_TYPE") && instruction.args.size() == 1)
        materialize_enum_signature(instruction.args.front(), 0);
    materialize_struct_schema(function.return_type, *mod, function.source_line,
                              0, false);
    materialize_enum_signature(function.return_type, 0);
    for (const auto &parameter : function.params) {
      const auto colon = parameter.find(':');
      if (colon != std::string::npos) {
        materialize_struct_schema(parameter.substr(colon + 1), *mod,
                                  function.source_line, 0, false);
        materialize_enum_signature(parameter.substr(colon + 1), 0);
      }
    }
  }

  // Add string constants from local mapping
  for (const auto &kv : string_const_map_) {
    mod->string_constants.emplace_back(kv.second, kv.first);
  }

  // Preserve optional AI context initialization; niltest itself uses direct calls.
  if (!registration_funcs.empty() && options_.mode == build_mode::test_mode) {
    nil_function init_fn; // Removed explicit type-specifier to match container
                          // value_type if needed, but it's nil_function
    std::string suffix = current_module.empty() ? "root" : current_module;
    init_fn.name = "__nilaix_init_" + sanitize_identifier(suffix) + "_" +
                   std::to_string(registration_funcs.size());
    init_fn.return_type = "void";
    init_fn.is_test = true;
    init_fn.source_line = 0;

    // Emitter
    emitter em(init_fn, *mod, *this, {}, current_module);

    for (const auto &reg_func : registration_funcs) {
      em.push_instr("CALL", {reg_func, "0"}, 0);
    }

    // RET
    nil_instruction ret;
    ret.opname = "RET";
    ret.debug_line = 0;
    init_fn.body.push_back(ret);

    // Inject call in main
    for (auto &fn : mod->functions) {
      if (fn.name == "main") {
        nil_instruction call_nytest;
        call_nytest.opname = "CALL";
        call_nytest.args = {init_fn.name, "0"};
        call_nytest.debug_line = 0;

        // Insert after globals init (if present) or at start.
        auto it = fn.body.begin();
        if (it != fn.body.end() && it->opname == "CALL" && !it->args.empty() &&
            it->args[0] == "__globals_init__") {
          ++it;
        }
        fn.body.insert(it, call_nytest);
        break;
      }
    }

    mod->functions.push_back(std::move(init_fn));
  }

  // Keep the historical reverse STORE_LOCAL prefix recognizable to native
  // consumers, then canonicalize scalar parameters before the source body.
  // This covers ordinary functions, methods, lambdas and specializations.
  for (auto &function : mod->functions) {
    if (function.params.empty() || function.body.size() < function.params.size())
      continue;
    bool has_prologue = true;
    std::vector<nil_instruction> conversions;
    for (std::size_t index = 0; index < function.params.size(); ++index) {
      const auto &parameter = function.params[function.params.size() - 1 - index];
      const auto colon = parameter.find(':');
      if (colon == std::string::npos) { has_prologue = false; break; }
      const auto name = parameter.substr(0, colon);
      auto type = parameter.substr(colon + 1);
      type.erase(std::remove_if(type.begin(), type.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
      }), type.end());
      if (type == "int")
        type = "i32";
      const auto &instruction = function.body[index];
      if (instruction.opname != "STORE_LOCAL" || instruction.args.size() != 1 ||
          instruction.args.front() != name) { has_prologue = false; break; }
      if (type == "i32" || type == "i64" || type == "u8" || type == "bool" ||
          type == "f64") {
        conversions.push_back({"LOAD_LOCAL", {name}, function.source_line});
        conversions.push_back({"CAST", {type}, function.source_line});
        conversions.push_back({"STORE_LOCAL", {name}, function.source_line});
      }
    }
    if (has_prologue)
      function.body.insert(function.body.begin() + function.params.size(),
                           conversions.begin(), conversions.end());
  }

  if (!rejected_codegen_constructs_.empty()) {
    if (diags)
      diags->insert(diags->end(), rejected_codegen_constructs_.begin(),
                    rejected_codegen_constructs_.end());
    return nullptr;
  }

  try {
    lower_binding_references(*mod);
    finalize_nil_switch_returns(*mod);
    if (std::any_of(mod->structs.begin(), mod->structs.end(),
                    [](const auto &schema) { return binding_cell_value_type(schema.name).has_value(); }))
      validate_nil_class_contract(*mod);
    validate_nil_parallel_contract(*mod);
    validate_nil_owned_contract(*mod);
  } catch (const std::exception &error) {
    reject_codegen_construct(0, error.what(), "use checked ownership scopes and isolated scalar parallel workers");
    if (diags) diags->insert(diags->end(), rejected_codegen_constructs_.begin(), rejected_codegen_constructs_.end());
    return nullptr;
  }
  return mod;
}

void codegen::reject_codegen_construct(int line, std::string message,
                                       std::string hint) {
  for (const auto &diagnostic : rejected_codegen_constructs_) {
    if (diagnostic.line == line && diagnostic.message == message)
      return;
  }
  rejected_codegen_constructs_.push_back(
      {line, std::move(message), std::move(hint)});
}

void codegen::generate_specialization(const specialization_request &req,
                                      nil_module &mod,
                                      std::vector<codegen_diagnostic> *diags) {
  // Mangle name — starts with FQN (e.g. "f3::print_it"), appends type args,
  // then normalizes "::" → "__" so labels match what mangle_fully() emits.
  std::string mangled_name = req.generic_fn_name;
  for (const auto &arg : req.type_args) {
    mangled_name += "__" + sanitize_identifier(arg);
  }
  // Normalize module separators: "f3::print_it__text" → "f3__print_it__text"
  {
    size_t pos = 0;
    while ((pos = mangled_name.find("::", pos)) != std::string::npos) {
      mangled_name.replace(pos, 2, "__");
      pos += 2;
    }
  }

  // Check if already generated
  if (generated_specializations_.count(mangled_name)) {
    return;
  }
  generated_specializations_.insert(mangled_name);

  // Create function
  nil_function fn;
  fn.name = mangled_name;
  // Build substitutions map
  std::map<std::string, std::string> subs;
  for (size_t i = 0; i < req.decl_node->generic_params.size(); ++i) {
    if (i < req.type_args.size()) {
      subs[req.decl_node->generic_params[i].name] = req.type_args[i];
    }
  }

  // Generic parameters may occur at any depth (`array<T>`, `Result<T, E>`,
  // and so on). Keep the specialized NIL signature concrete all the way to
  // native ABI validation rather than only replacing a whole-type `T`.
  fn.return_type = substitute_nil_type_name(
      req.decl_node->return_type.value_or("void"), subs);
  fn.source_line = req.decl_node->line;
  fn.source_file = req.decl_node->source_file;

  // Populate params with type substitutions
  for (const auto &p : req.decl_node->params) {
    if (!append_serialized_nil_parameter(fn, p, subs)) {
      reject_codegen_construct(
          p.line,
          "specialized function parameter '" + p.name +
              "' has no exact type for serialized NIL",
          "add an explicit type annotation to the generic function parameter");
    }
  }

  // Emit body
  emitter em(fn, mod, *this, subs, declaration_modules_[req.decl_node]);
  em.register_function_params(*req.decl_node);

  // Emit STORE_LOCAL parameter prologue — pops call args off the stack
  // into named locals (reverse order = LIFO, matching caller push order).
  // Without this, any reference to a parameter inside the body fails with
  // "Local not found: '<param_name>'".
  {
    nil_instruction store;
    store.opname = "STORE_LOCAL";
    store.debug_line = req.decl_node->line;
    for (auto it = req.decl_node->params.rbegin();
         it != req.decl_node->params.rend(); ++it) {
      store.args = {it->name};
      fn.body.push_back(store);
    }
  }

  if (req.decl_node->body) {
    em.emit_stmt(*req.decl_node->body);
  }

  append_implicit_function_return(fn, req.decl_node->line);

  mod.functions.push_back(std::move(fn));
}

void codegen::generate_impl_method_specialization(
    const impl_method_specialization_request &req, nil_module &mod,
    std::vector<codegen_diagnostic> *diags) {

  if (!req.impl_decl || !req.method_decl) {
    ny::diag_debug("[codegen] Impl specialization is missing declarations");
    return;
  }

  // Build mangled name: target_type__method_name__type_args
  // e.g., vec__new__i32
  std::string mangled_name = sanitize_identifier(req.target_type);
  mangled_name += "__" + req.method_name;
  for (const auto &arg : req.type_args) {
    mangled_name += "__" + sanitize_identifier(arg);
  }

  // Check if already generated
  if (generated_specializations_.count(mangled_name)) {
    return;
  }
  generated_specializations_.insert(mangled_name);

  // Build substitutions map from impl's generic params
  std::map<std::string, std::string> subs;
  const std::size_t impl_argument_count =
      req.impl_decl->generic_params.size();
  for (size_t i = 0; i < impl_argument_count; ++i) {
    if (i < req.type_args.size()) {
      subs[req.impl_decl->generic_params[i].name] = req.type_args[i];
    }
  }
  for (size_t i = 0; i < req.method_decl->generic_params.size(); ++i) {
    const std::size_t argument_index = impl_argument_count + i;
    if (argument_index < req.type_args.size()) {
      subs[req.method_decl->generic_params[i].name] =
          req.type_args[argument_index];
    }
  }

  // Create function
  nil_function fn;
  fn.name = mangled_name;

  // Build concrete_type string for Self substitution (e.g. "vec<i32>")
  std::string concrete_type_for_self = req.target_type;
  if (impl_argument_count != 0U &&
      req.type_args.size() >= impl_argument_count) {
    concrete_type_for_self += "<";
    for (size_t i = 0; i < impl_argument_count; ++i) {
      if (i > 0)
        concrete_type_for_self += ",";
      concrete_type_for_self += req.type_args[i];
    }
    concrete_type_for_self += ">";
  }
  subs["Self"] = concrete_type_for_self;

  fn.return_type = substitute_nil_type_name(
      req.method_decl->return_type.value_or("void"), subs);
  fn.source_line = req.method_decl->line;
  fn.source_file = req.method_decl->source_file;

  // Populate params with substitutions
  for (const auto &p : req.method_decl->params) {
    if (!append_serialized_nil_parameter(fn, p, subs,
                                         concrete_type_for_self)) {
      reject_codegen_construct(
          p.line,
          "specialized method parameter '" + p.name +
              "' has no exact type for serialized NIL",
          "annotate non-receiver parameters with an explicit type");
    }
  }

  // Emit body with substitutions
  emitter em(fn, mod, *this, subs, declaration_modules_[req.method_decl]);
  em.register_function_params(*req.method_decl);

  // Materialize specialized method parameters exactly like ordinary and
  // generic free functions. Callers push arguments from left to right, so the
  // callee stores them into named locals in reverse order.
  {
    nil_instruction store;
    store.opname = "STORE_LOCAL";
    store.debug_line = req.method_decl->line;
    for (auto it = req.method_decl->params.rbegin();
         it != req.method_decl->params.rend(); ++it) {
      store.args = {it->name};
      fn.body.push_back(store);
    }
  }

  if (req.method_decl->body) {
    em.emit_stmt(*req.method_decl->body);
  }

  append_implicit_function_return(fn, req.method_decl->line);

  mod.functions.push_back(std::move(fn));
}

std::string codegen::qualify_impl_target(const std::string &name,
                                         const std::string &current_module) const {
  if (name.find("::") != std::string::npos)
    return name; // already qualified
  const std::string local =
      current_module.empty() ? name : current_module + "::" + name;
  // A type declared in this very book wins, so a book that declares and
  // extends the same short name stays unambiguous.
  if (struct_decls_.count(local) || class_decls_.count(local))
    return local;
  const std::string as_struct = lookup_struct_fqn(name);
  if (as_struct != name)
    return as_struct;
  const std::string as_class = lookup_class_fqn(name);
  if (as_class != name)
    return as_class;
  return local; // not found; behave as before
}

const ast_class_decl *codegen::lookup_class_decl(const std::string &name) {
  auto it = class_decls_.find(name);
  if (it != class_decls_.end()) {
    return it->second;
  }
  return nullptr;
}

std::unique_ptr<nil_module>
codegen::generate_module(const ast_program &program,
                         std::vector<codegen_diagnostic> *diags) {
  ny::diag_debug("[codegen] generate_module");
  // Reset constants map
  string_const_map_.clear();
  function_decls_.clear();
  pending_specializations_.clear();
  generated_specializations_.clear();
  return assemble_module(program, diags);
}

std::string codegen::generate_text(const ast_program &program,
                                   std::vector<codegen_diagnostic> *diags) {
  std::ostringstream ss;
  auto module = generate_module(program, diags);
  if (!module)
    return {};
  nil_writer w(ss, options_);
  w.emit_text_module(*module);
  return ss.str();
}

bool codegen::emit_nil_to_file(const ast_program &program,
                               const std::filesystem::path &out_path,
                               std::vector<codegen_diagnostic> *diags) {
  auto module = generate_module(program, diags);
  if (!module)
    return false;
  std::ofstream ofs(out_path);
  if (!ofs) {
    if (diags)
      diags->push_back({0, "Cannot open output file", "Verify path"});
    return false;
  }
  nil_writer w(ofs, options_);
  w.emit_text_module(*module);
  return true;
}

/*
 * nil_binary_writer Implementation
 */
void nil_binary_writer::write_u8(uint8_t v) {
  const char bytes[] = {static_cast<char>(v)};
  out_.write(bytes, sizeof(bytes));
}

void nil_binary_writer::write_u16(uint16_t v) {
  const char bytes[] = {static_cast<char>(v & 0xffU),
                        static_cast<char>((v >> 8U) & 0xffU)};
  out_.write(bytes, sizeof(bytes));
}

void nil_binary_writer::write_u32(uint32_t v) {
  char bytes[4];
  for (unsigned i = 0; i < 4; ++i)
    bytes[i] = static_cast<char>((v >> (8U * i)) & 0xffU);
  out_.write(bytes, sizeof(bytes));
}

void nil_binary_writer::write_u64(uint64_t v) {
  char bytes[8];
  for (unsigned i = 0; i < 8; ++i)
    bytes[i] = static_cast<char>((v >> (8U * i)) & 0xffU);
  out_.write(bytes, sizeof(bytes));
}

void nil_binary_writer::write_string(const std::string &s) {
  write_u32(static_cast<uint32_t>(s.size()));
  out_.write(s.data(), s.size());
}

void nil_binary_writer::emit_binary_module(const nil_module &mod) {
  const bool is_library = mod.is_library;
  bool has_source_debug = false;
  for (const auto &function : mod.functions)
    for (const auto &instruction : function.body) {
      validate_source_debug(instruction.source_file, instruction.debug_locals);
      has_source_debug |= !instruction.source_file.empty();
    }
  if (has_source_debug && is_library)
    throw std::runtime_error("Source debug metadata is not supported in NIL libraries");
  const bool has_classes = !mod.classes.empty() || has_source_debug;
  const uint32_t format_version = has_source_debug ? source_debug_binary_nil_version : has_classes ? class_binary_nil_version : is_library ? library_binary_nil_version
                                            : executable_binary_nil_version;
  const uint32_t section_count = (is_library ? 6U : 5U) + (has_classes ? 1U : 0U) + (has_source_debug ? 1U : 0U);
  const auto states_intent = [](const std::vector<std::string> &annotations) {
    return !annotations.empty();
  };
  bool annotates_types = false;
  if (!is_library || has_classes) {
    for (const auto &structure : mod.structs) annotates_types |= states_intent(structure.annotations);
    for (const auto &enumeration : mod.enums) annotates_types |= states_intent(enumeration.annotations);
  }
  if (has_classes)
    for (const auto &klass : mod.classes) annotates_types |= states_intent(klass.annotations);
  const uint32_t header_flags =
      (is_library ? binary_nil_library_flag : 0U) |
      (annotates_types ? binary_nil_type_annotation_flag : 0U);

  // 1. Header
  // Four-byte, byte-order-independent magic: "NIL\0".
  out_.write("NIL\0", 4);
  // Version
  write_u32(format_version);
  // Flags
  write_u32(header_flags);
  // Type, Function, Constant, Symbol, Module, and (for libraries) Source.
  write_u32(section_count);

  // 2. Section Table (Placeholder)
  auto section_table_pos = out_.tellp();
  for (uint32_t i = 0; i < section_count; ++i) {
    write_u32(0); // Type
    write_u64(0); // Offset
    write_u64(0); // Size
  }

  struct SectionInfo {
    uint32_t type;
    uint64_t offset;
    uint64_t size;
  };
  std::vector<SectionInfo> sections;

  auto start_section = [&](uint32_t type) {
    sections.push_back({type, (uint64_t)out_.tellp(), 0});
  };

  auto end_section = [&]() {
    auto &sec = sections.back();
    sec.size = (uint64_t)out_.tellp() - sec.offset;
  };

  // 3. Sections

  // Type Section (ID 1)
  start_section(1);
  // Executable v3 carries exactly the schemas used by textual/native NIL.
  // Libraries retain their v2 bootstrap layout and checked source interface.
  write_u32(is_library && !has_classes ? 0U : static_cast<uint32_t>(mod.structs.size()));
  if (!is_library || has_classes) {
    const auto write_annotations = [&](const std::vector<std::string> &annotations) {
      if (!annotates_types) return;
      write_u32(static_cast<uint32_t>(annotations.size()));
      for (const auto &annotation : annotations) write_string(annotation);
    };
    for (const auto &structure : mod.structs) {
      write_string(structure.name);
      write_u32(static_cast<uint32_t>(structure.fields.size()));
      for (const auto &field : structure.fields) {
        write_string(field.first);
        write_string(field.second);
      }
      write_annotations(structure.annotations);
    }
    write_u32(static_cast<uint32_t>(mod.enums.size()));
    for (const auto &enumeration : mod.enums) {
      write_string(enumeration.name);
      write_u32(static_cast<uint32_t>(enumeration.variants.size()));
      for (const auto &variant : enumeration.variants) {
        write_string(variant.name);
        write_string(variant.associated_type);
      }
      write_annotations(enumeration.annotations);
    }
  }
  end_section();

  if (has_classes) {
    start_section(7);
    write_u32(static_cast<uint32_t>(mod.classes.size()));
    for (const auto &klass : mod.classes) {
      write_string(klass.name);
      write_u32(klass.is_abstract ? 1U : 0U);
      if (annotates_types) {
        write_u32(static_cast<uint32_t>(klass.annotations.size()));
        for (const auto &annotation : klass.annotations) write_string(annotation);
      }
      write_u32(static_cast<uint32_t>(klass.bases.size()));
      for (const auto &base : klass.bases) write_string(base);
      write_u32(static_cast<uint32_t>(klass.methods.size()));
      for (const auto &method : klass.methods) {
        write_string(method.name);
        write_string(method.implementation);
        write_string(method.receiver_type);
        write_string(method.return_type);
        write_u32((method.is_virtual ? 1U : 0U) | (method.is_abstract ? 2U : 0U));
        write_u32(static_cast<uint32_t>(method.parameter_types.size()));
        for (const auto &type : method.parameter_types) write_string(type);
      }
    }
    end_section();
  }

  if (is_library) {
    // Source Interface Section (ID 6).  NIL libraries retain their checked,
    // compiled bodies above and carry the source interface needed for generic
    // instantiation and semantic analysis.  Consumers therefore need only
    // the binary library directory; nilstd remains the human-readable source
    // distribution and debugger reference.
    start_section(6);
    write_u32(static_cast<uint32_t>(mod.embedded_sources.size()));
    for (const auto &source : mod.embedded_sources) {
      write_string(source.path);
      write_string(source.contents);
    }
    write_u32(static_cast<uint32_t>(mod.dependencies.size()));
    for (const auto &dependency : mod.dependencies)
      write_string(dependency);
    end_section();
  }

  // Function Section (ID 2)
  start_section(2);
  write_u32(mod.functions.size());
  for (const auto &fn : mod.functions) {
    write_string(fn.name);
    write_u32(fn.params.size());
    for (const auto &p : fn.params)
      write_string(p);
    write_u32(fn.generic_params.size());
    for (const auto &gp : fn.generic_params)
      write_string(gp);
    write_string(fn.return_type);

    if (!is_library || has_classes) {
      write_u32((fn.is_inline ? 1U : 0U) | (fn.is_test ? 2U : 0U));
      write_u32(static_cast<uint32_t>(fn.source_line));
      write_u32(static_cast<uint32_t>(fn.annotations.size()));
      for (const auto &annotation : fn.annotations)
        write_string(annotation);
    }

    // Body
    write_u32(fn.body.size());
    for (const auto &instr : fn.body) {
      // Simple opcode mapping (hash of name for MVP)
      // Real implementation would map strings to opcodes
      // For now, write opname string + args
      write_string(instr.opname);
      write_u32(instr.args.size());
      for (const auto &arg : instr.args)
        write_string(arg);
      write_u32(instr.debug_line);
    }
  }
  end_section();

  // Constant Section (ID 3)
  start_section(3);
  write_u32(mod.string_constants.size());
  for (const auto &kv : mod.string_constants) {
    write_string(kv.first);  // Name
    write_string(kv.second); // Value
  }
  end_section();

  // Symbol Section (ID 4)
  start_section(4);
  // TODO: Emit symbols in a future binary NIL format version.
  write_u32(0);
  end_section();

  // Module Section (ID 5)
  start_section(5);
  write_string(mod.module_name);
  write_u32(mod.tests.size());
  for (const auto &t : mod.tests)
    write_string(t);
  end_section();

  // 4. Update Section Table
  if (has_source_debug) {
    start_section(8);
    write_u32(static_cast<std::uint32_t>(mod.functions.size()));
    for (const auto &function : mod.functions) {
      write_u32(static_cast<std::uint32_t>(function.body.size()));
      for (const auto &instruction : function.body) {
        write_string(instruction.source_file);
        write_u32(static_cast<std::uint32_t>(instruction.debug_locals.size()));
        for (const auto &local : instruction.debug_locals) {
          write_string(local.name);
          write_string(local.storage);
          write_string(local.type);
          write_u32(static_cast<std::uint32_t>(local.scope_depth));
        }
      }
    }
    end_section();
  }
  auto end_pos = out_.tellp();
  out_.seekp(section_table_pos);
  for (const auto &sec : sections) {
    write_u32(sec.type);
    write_u64(sec.offset);
    write_u64(sec.size);
  }
  out_.seekp(end_pos);
}

bool codegen::emit_nil_binary_to_file(const ast_program &program,
                                      const std::filesystem::path &out_path,
                                      std::vector<codegen_diagnostic> *diags) {
  auto mod = assemble_module(program, diags);
  if (!mod)
    return false;

  std::ofstream ofs(out_path, std::ios::binary);
  if (!ofs)
    return false;

  nil_binary_writer writer(ofs, options_);
  writer.emit_binary_module(*mod);
  ofs.flush();
  if (!ofs) {
    if (diags) {
      diags->push_back({0, "Failed to write complete binary NIL output",
                        "Check free space"});
    }
    return false;
  }
  return true;
}

#ifdef UNIT_TEST_CODEGEN_EMIT_TESTS
// Unit-test routine (when compiled under the test define) to validate tests
// emission in the textual NIL writer.
static int run_codegen_emit_test_emission() {
  // Build a sample module containing a test function and a test registry
  // entry and ensure the textual NIL emits the '@test' registry entry.
  nil_module m;
  m.module_name = "root";

  nil_function f;
  f.name = "test_fn";
  f.is_test = true;
  f.annotations.push_back("@test");

  m.functions.push_back(f);
  m.tests.push_back(f.name);

  std::ostringstream ss;
  nil_writer w(ss);
  w.emit_text_module(m);
  std::string out = ss.str();

  // Verify the test registry contains the expected listing. Look for "@test
  // test_fn".
  if (out.find("@test test_fn") == std::string::npos) {
    std::cerr
        << "[unit] FAIL: expected '@test test_fn' in emitted module output\n";
    std::cerr << out << "\n";
    return 1;
  }
  std::cerr << "[unit] PASS: found '@test test_fn' in emitted module output\n";
  return 0;
}
#endif // UNIT_TEST_CODEGEN_EMIT_TESTS

// ----------------------------------------------------------------------------
// UNIT_TEST helper: emit a minimal NIL sequence representing a zip-based
// for-loop and validate that the textual emission contains the expected
// ritual iterator calls. This helper is intentionally lightweight as it's
// designed to be used by the codegen test driver.

int run_codegen_emit_zip_for() {
  nil_module m;
  m.module_name = "test_zip";

  nil_function f;
  f.name = "zip_loop_emitted";

  // Simulate pseudo-NIL for: for (a, b) in zip(iter1, iter2)
  // CALL iter::zip, %1, %2 -> %zip_iter
  nil_instruction i1;
  i1.opname = "CALL";
  i1.args = {"iter::zip", "%iter1", "%iter2", "%zip_iter"};
  f.body.push_back(i1);

  // LABEL loop_start
  nil_instruction i2;
  i2.opname = "LABEL";
  i2.args = {"loop_start"};
  f.body.push_back(i2);

  // CALL iter::next, %zip_iter -> %opt_val
  nil_instruction i3;
  i3.opname = "CALL";
  i3.args = {"iter::next", "%zip_iter", "%opt_val"};
  f.body.push_back(i3);

  m.functions.push_back(f);

  std::ostringstream ss;
  nil_writer w(ss);
  w.emit_text_module(m);
  std::string out = ss.str();

  if (out.find("iter::zip") == std::string::npos) {
    std::cerr << "[unit] FAIL: expected 'iter::zip' in emission\n";
    return 1;
  }
  std::cerr << "[unit] PASS: run_codegen_emit_zip_for\n";
  return 0;
}

int run_codegen_emit_vec_for() {
  nil_module m;
  m.module_name = "test_vec";

  nil_function f;
  f.name = "vec_loop_emitted";

  // Simulate pseudo-NIL for: for x in vec
  // CALL vec::len, %v -> %len
  nil_instruction i1;
  i1.opname = "CALL";
  i1.args = {"vec::len", "%v", "%len"};
  f.body.push_back(i1);

  // LABEL loop_check
  nil_instruction i2;
  i2.opname = "LABEL";
  i2.args = {"loop_check"};
  f.body.push_back(i2);

  // GE %i, %len -> %done
  nil_instruction i3;
  i3.opname = "GE.i32"; // Assuming i32
  i3.args = {"%i", "%len", "%done"};
  f.body.push_back(i3);

  // CALL vec::get, %v, %i -> %val
  nil_instruction i4;
  i4.opname = "CALL";
  i4.args = {"vec::get", "%v", "%i", "%val"};
  f.body.push_back(i4);

  m.functions.push_back(f);

  std::ostringstream ss;
  nil_writer w(ss);
  w.emit_text_module(m);
  std::string out = ss.str();

  if (out.find("vec::len") == std::string::npos ||
      out.find("vec::get") == std::string::npos) {
    std::cerr << "[unit] FAIL: expected vec ops in emission\n";
    return 1;
  }
  std::cerr << "[unit] PASS: run_codegen_emit_vec_for\n";
  return 0;
}

} // namespace ny

// Global wrapper to expose the vector for-loop helper to the test driver.

// Global wrapper to expose the vector for-loop helper to the test driver.
