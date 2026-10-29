#include "codegen.hpp"
#include "nil_contract.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace ny {
namespace {
[[noreturn]] void invalid(const std::string &message) {
  throw std::runtime_error("NIL class contract: " + message);
}
bool valid_name(const std::string &name) {
  return !name.empty() && std::none_of(name.begin(), name.end(),
      [](unsigned char c) { return c <= 32 || c == 127; });
}
bool valid_type(const std::string &type) {
  return !type.empty() && std::none_of(type.begin(), type.end(),
      [](unsigned char c) { return c < 32 || c == 127; });
}
bool reserved_class_name(std::string name) {
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  static const std::unordered_set<std::string> reserved = {
      "void", "any", "bool", "char", "text", "string", "str", "int",
      "float", "double", "i8", "i16", "i32", "i64", "u8", "u16",
      "u32", "u64", "f32", "f64", "isize", "usize", "pointer", "array", "slice"};
  return reserved.count(name) != 0;
}
std::string parameter_type(const std::string &parameter) {
  const auto colon = parameter.find(':');
  if (colon == std::string::npos) return {};
  const auto first = parameter.find_first_not_of(" \t", colon + 1);
  return first == std::string::npos ? "" : parameter.substr(first);
}
const nil_struct_def *schema(const nil_module &module, const std::string &name) {
  for (const auto &value : module.structs)
    if (value.name == name) return &value;
  return nullptr;
}
} // namespace

bool is_reserved_nil_class_name(const std::string &name) {
  return reserved_class_name(name);
}

const nil_class_def *find_nil_class(const nil_module &module,
                                  const std::string &name) {
  for (const auto &value : module.classes)
    if (value.name == name) return &value;
  return nullptr;
}

bool nil_class_assignable(const nil_module &module, const std::string &expected,
                          const std::string &actual) {
  if (!find_nil_class(module, expected) || !find_nil_class(module, actual))
    return false;
  std::unordered_set<std::string> seen;
  std::function<bool(const std::string &, std::size_t)> visit = [&](const std::string &name, std::size_t depth) {
    if (depth > 256) invalid("class ancestry exceeds the 256-level safety limit");
    if (name == expected) return true;
    if (!seen.insert(name).second) return false;
    const auto *value = find_nil_class(module, name);
    if (!value) return false;
    for (const auto &base : value->bases)
      if (visit(base, depth + 1)) return true;
    return false;
  };
  return visit(actual, 1);
}

nil_class_method_resolution resolve_nil_class_method(
    const nil_module &module, const std::string &static_class,
    const std::string &method_name) {
  std::unordered_set<std::string> active;
  std::unordered_map<std::string, std::vector<nil_class_method_resolution>> cache;
  std::function<std::vector<nil_class_method_resolution>(const std::string &)> visit =
      [&](const std::string &name) {
    if (const auto cached = cache.find(name); cached != cache.end()) return cached->second;
    if (!active.insert(name).second) invalid("cyclic class ancestry at '" + name + "'");
    if (active.size() > 256) invalid("class ancestry exceeds the 256-level safety limit");
    const auto *value = find_nil_class(module, name);
    if (!value) invalid("unknown class '" + name + "'");
    for (const auto &method : value->methods) {
      if (method.name == method_name) {
        active.erase(name);
        return cache[name] = std::vector<nil_class_method_resolution>{{name, &method}};
      }
    }
    std::vector<nil_class_method_resolution> candidates;
    for (const auto &base : value->bases) {
      auto inherited = visit(base);
      for (const auto &candidate : inherited)
        if (std::none_of(candidates.begin(), candidates.end(), [&](const auto &old) {
              return old.owner == candidate.owner;
            })) candidates.push_back(candidate);
    }
    active.erase(name);
    return cache[name] = candidates;
  };
  auto candidates = visit(static_class);
  const auto all = candidates;
  candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](const auto &candidate) {
    return std::any_of(all.begin(), all.end(), [&](const auto &other) {
      return candidate.owner != other.owner &&
          nil_class_assignable(module, candidate.owner, other.owner);
    });
  }), candidates.end());
  if (candidates.size() > 1)
    invalid("ambiguous inherited method '" + static_class + "::" + method_name + "'");
  return candidates.empty() ? nil_class_method_resolution{} : candidates.front();
}

void validate_nil_class_contract(const nil_module &module) {
  if (module.classes.empty()) {
    for (const auto &function : module.functions)
      for (const auto &instruction : function.body)
        if (instruction.opname == "CLASS_UPCAST" || instruction.opname == "CALL_METHOD")
          invalid("class instructions require explicit class metadata");
    // Binding-reference cells use the same CFG provenance engine even when
    // the program declares no classes.
    validate_nil_class_provenance(module);
    return;
  }
  std::unordered_map<std::string, const nil_function *> functions;
  for (const auto &function : module.functions)
    if (!functions.emplace(function.name, &function).second)
      invalid("duplicate function '" + function.name + "'");
  std::unordered_set<std::string> schemas;
  for (const auto &value : module.structs) {
    if (!valid_type(value.name) || !schemas.insert(value.name).second)
      invalid("invalid or duplicate struct schema '" + value.name + "'");
    std::unordered_set<std::string> fields;
    for (const auto &field : value.fields)
      if (!valid_name(field.first) || !valid_type(field.second) || !fields.insert(field.first).second)
        invalid("invalid or duplicate field in '" + value.name + "'");
  }
  std::unordered_set<std::string> names;
  for (const auto &value : module.classes) {
    if (reserved_class_name(value.name))
      invalid("class name '" + value.name + "' conflicts with a reserved scalar or handle type");
    if (!valid_name(value.name) || value.name.find_first_of("<>&*[]") != std::string::npos ||
        !names.insert(value.name).second || !schemas.count(value.name))
      invalid("class '" + value.name + "' requires one exact plain nominal schema");
    if (std::any_of(module.enums.begin(), module.enums.end(), [&](const auto &e) { return e.name == value.name; }))
      invalid("class identity conflicts with an enum");
  }
  std::unordered_map<std::string, unsigned> colors;
  std::unordered_map<std::string, std::size_t> heights;
  std::unordered_map<std::string, std::unordered_map<std::string, std::set<std::string>>> field_owners;
  std::size_t depth = 0;
  std::function<void(const nil_class_def &)> ancestry = [&](const nil_class_def &value) {
    if (colors[value.name] == 1) invalid("cyclic class ancestry at '" + value.name + "'");
    if (colors[value.name] == 2) return;
    if (++depth > 256) invalid("class ancestry exceeds the 256-level safety limit");
    colors[value.name] = 1;
    std::unordered_set<std::string> bases;
    heights[value.name] = 1;
    for (const auto &name : value.bases) {
      const auto *base = find_nil_class(module, name);
      if (!base || !bases.insert(name).second) invalid("unknown or duplicate base of '" + value.name + "'");
      ancestry(*base);
      heights[value.name] = std::max(heights[value.name], heights[name] + 1);
      if (heights[value.name] > 256) invalid("class ancestry exceeds the 256-level safety limit");
      for (const auto &field : schema(module, name)->fields) {
        const auto &fields = schema(module, value.name)->fields;
        const auto found = std::find_if(fields.begin(), fields.end(), [&](const auto &f) { return f.first == field.first; });
        if (found == fields.end() || found->second != field.second)
          invalid("flattened fields of '" + value.name + "' do not preserve base '" + name + "'");
      }
    }
    for (const auto &field : schema(module, value.name)->fields) {
      auto &owners = field_owners[value.name][field.first];
      for (const auto &base : value.bases) {
        const auto found = field_owners[base].find(field.first);
        if (found != field_owners[base].end()) owners.insert(found->second.begin(), found->second.end());
      }
      if (owners.empty()) owners.insert(value.name);
      if (owners.size() > 1) invalid("ambiguous inherited field '" + value.name + "." + field.first + "'");
    }
    colors[value.name] = 2;
    --depth;
  };
  for (const auto &value : module.classes) ancestry(value);

  std::set<std::string> all_methods;
  for (const auto &value : module.classes) {
    std::unordered_set<std::string> methods;
    for (const auto &method : value.methods) {
      if (!valid_name(method.name) || !valid_type(method.return_type) ||
          !methods.insert(method.name).second)
        invalid("invalid or duplicate method in '" + value.name + "'");
      all_methods.insert(method.name);
      if (method.receiver_type != value.name && method.receiver_type != "ref<" + value.name + ">")
        invalid("method receiver must be its exact declaring class or reference");
      if (std::any_of(method.parameter_types.begin(), method.parameter_types.end(),
                      [](const auto &type) { return !valid_type(type) || type == "void"; }))
        invalid("method parameter lacks an exact nonvoid type");
      if (method.is_abstract) {
        if (!method.is_virtual || !method.implementation.empty())
          invalid("abstract method must be virtual and have no implementation");
      } else {
        const auto found = functions.find(method.implementation);
        if (found == functions.end()) invalid("missing implementation for '" + value.name + "::" + method.name + "'");
        const auto &function = *found->second;
        if (function.params.size() != method.parameter_types.size() + 1 ||
            parameter_type(function.params.front()) != method.receiver_type ||
            function.return_type != method.return_type)
          invalid("method target signature does not match its declaration");
        for (std::size_t i = 0; i < method.parameter_types.size(); ++i)
          if (parameter_type(function.params[i + 1]) != method.parameter_types[i])
            invalid("method target parameter signature does not match its declaration");
      }
      for (const auto &base : value.bases) {
        const auto inherited = resolve_nil_class_method(module, base, method.name);
        if (!inherited.method) continue;
        const auto &old = *inherited.method;
        if (old.parameter_types != method.parameter_types || old.return_type != method.return_type ||
            ((old.receiver_type.rfind("ref<", 0) == 0) != (method.receiver_type.rfind("ref<", 0) == 0)) ||
            (old.is_virtual && !method.is_virtual))
          invalid("incompatible override of '" + method.name + "'");
      }
    }
  }
  for (const auto &value : module.classes) {
    for (const auto &name : all_methods) {
      const auto resolved = resolve_nil_class_method(module, value.name, name);
      if (resolved.method && resolved.method->is_abstract && !value.is_abstract)
        invalid("concrete class '" + value.name + "' has an unresolved abstract method");
    }
  }
  validate_nil_class_provenance(module);
}
} // namespace ny
