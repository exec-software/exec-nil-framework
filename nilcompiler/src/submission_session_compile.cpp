#include "submission_session_compile.hpp"
#include "matrix_parser.hpp"
#include "submission_request.hpp"
#include "submission_profile.hpp"
#include "codegen.hpp"
#include "numeric_literal.hpp"
#include "type_checker.hpp"
#include "../../common/strict_json.hpp"

#include <charconv>
#include <cstdint>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace matrix {
namespace {
constexpr std::size_t max_snapshot_nodes = 4096;
constexpr std::size_t max_snapshot_bytes = 1024 * 1024;

void fail(const std::string &message) { throw std::runtime_error(message); }
std::string safe_text(const std::string &text) {
  try { nil::detail::valid_utf8(text); return text; }
  catch (const std::exception &) {
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    for (unsigned char byte : text) {
      if (byte < 128) result.push_back(static_cast<char>(byte));
      else { result += "\\x"; result.push_back(hex[byte >> 4]); result.push_back(hex[byte & 15]); }
    }
    return result;
  }
}
void issue(submission_report &report, const std::string &stage,
           const std::string &message, const std::string &location,
           int line = 0, const std::string &code = "MS5001") {
  if (report.diagnostics.size() >= 64) return;
  auto text = safe_text(message);
  if (text.size() > 4096) {
    auto end = std::size_t{4096};
    while (end && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) --end;
    text.resize(end); text += "...";
  }
  report.diagnostics.push_back({stage, code, std::move(text), safe_text(location), line, 0, 0});
}
void validate_name(const std::string &name) {
  if (name.empty() || name.size() > 256 || nil_symbol(name) != name)
    fail("session binding has an invalid canonical Matrix name");
  nil::detail::valid_utf8(name);
  lexer scanner(name);
  const auto tokens = scanner.scan();
  if (scanner.has_errors() || tokens.size() != 2 ||
      tokens[0].kind != token_kind::identifier || tokens[0].escaped_identifier ||
      tokens[0].start != 0 || tokens[0].length != name.size() ||
      tokens[0].lexeme != name || tokens[1].kind != token_kind::end_of_file)
    fail("session bindings require an unescaped Matrix identifier");
}

struct type_shape { std::string name, spelling; std::vector<type_shape> children; };
type_shape parse_shape(const std::string &type) {
  if (type.empty() || type.size() > 4096) fail("invalid session value type");
  std::size_t at = 0, nodes = 0;
  const auto parse = [&](const auto &self, unsigned depth) -> type_shape {
    if (depth > 32 || ++nodes > 256) fail("session value type exceeds shape limits");
    const auto start = at;
    while (at < type.size() && ((type[at] >= 'a' && type[at] <= 'z') ||
           (type[at] >= '0' && type[at] <= '9'))) ++at;
    type_shape result{type.substr(start, at - start), {}, {}};
    if (result.name == "array" || result.name == "tuple") {
      if (at == type.size() || type[at++] != '<') fail("invalid session collection type");
      do {
        result.children.push_back(self(self, depth + 1));
        if (at == type.size()) fail("unterminated session collection type");
        if (type[at] == '>') { ++at; break; }
        if (type[at++] != ',') fail("invalid session collection type separator");
      } while (true);
      if (result.name == "array" && result.children.size() != 1)
        fail("session arrays require exactly one element type");
    } else if (result.name != "i32" && result.name != "i64" && result.name != "u8" &&
               result.name != "f64" && result.name != "bool" && result.name != "text") {
      fail("unsupported session value type");
    }
    result.spelling = type.substr(start, at - start);
    return result;
  };
  auto result = parse(parse, 0);
  if (at != type.size()) fail("session value type is not canonical");
  return result;
}

class snapshot_literals {
  std::size_t nodes = 0, bytes = 0;
  template<class T, class... Args>
  std::shared_ptr<T> make(const std::string &path, Args&&... args) {
    auto node = std::make_shared<T>(std::forward<Args>(args)...);
    node->source_file = path;
    return node;
  }
public:
  ny::ast_expr_ptr expression(const ny::submission_value &value,
                             const std::string &path, unsigned depth = 0) {
    if (++nodes > max_snapshot_nodes || depth > 32)
      fail("session snapshot exceeds value nesting or node limits");
    if (value.type.size() > max_snapshot_bytes - bytes) fail("session snapshot exceeds byte limit");
    bytes += value.type.size();
    if (value.scalar.size() > max_snapshot_bytes - bytes) fail("session snapshot exceeds byte limit");
    bytes += value.scalar.size();
    const auto type = parse_shape(value.type);
    if (type.name == "array" || type.name == "tuple") {
      if (!value.scalar.empty() || value.elements.size() > 256)
        fail("invalid session collection payload");
      if (type.name == "tuple" && value.elements.size() != type.children.size())
        fail("session tuple payload does not match its type");
      std::vector<ny::ast_expr_ptr> elements;
      for (std::size_t i = 0; i < value.elements.size(); ++i) {
        const auto &expected = type.children[type.name == "array" ? 0 : i].spelling;
        if (value.elements[i].type != expected)
          fail("session collection element does not match its exact type");
        elements.push_back(expression(value.elements[i], path + "[" + std::to_string(i) + "]", depth + 1));
      }
      if (type.name == "array") {
        auto array = make<ny::ast_array_literal_expr>(path, std::move(elements));
        array->inferred_type = value.type; // Preserves concrete empty arrays.
        return array;
      }
      std::vector<std::pair<std::string, ny::ast_expr_ptr>> fields;
      for (std::size_t i = 0; i < elements.size(); ++i)
        fields.emplace_back(std::to_string(i), std::move(elements[i]));
      auto tuple = make<ny::ast_struct_literal_expr>(path, "tuple", std::move(fields));
      tuple->tuple_literal = true;
      return tuple;
    }
    if (!value.elements.empty()) fail("scalar session value contains collection elements");
    if (type.name == "text") {
      nil::detail::valid_utf8(value.scalar);
      return make<ny::ast_string_literal>(path, value.scalar);
    }
    if (type.name == "bool") {
      if (value.scalar != "true" && value.scalar != "false") fail("invalid session boolean payload");
      return make<ny::ast_boolean_literal>(path, value.scalar == "true");
    }
    if (value.scalar.size() > 256) fail("session numeric payload exceeds limit");
    if (type.name == "f64") {
      double parsed;
      if (!ny::is_floating_literal_spelling(value.scalar) ||
          ny::parse_floating_literal(value.scalar, parsed)) fail("invalid session floating-point payload");
    } else {
      std::int64_t parsed = 0;
      const auto result = std::from_chars(value.scalar.data(), value.scalar.data() + value.scalar.size(), parsed);
      if (result.ec != std::errc{} || result.ptr != value.scalar.data() + value.scalar.size() ||
          std::to_string(parsed) != value.scalar) fail("invalid canonical session integer payload");
      if ((type.name == "u8" && (parsed < 0 || parsed > 255)) ||
          (type.name == "i32" && (parsed < INT32_MIN || parsed > INT32_MAX)))
        fail("session integer payload is outside its declared width");
    }
    auto literal = make<ny::ast_number_literal>(path, value.scalar);
    // A small retained i64/u8 must not silently infer i32 when restored.
    return make<ny::ast_cast_expr>(path, literal, type.name);
  }
};

void compile(prepared_session_submission &out,
             const std::shared_ptr<ny::ast_block_stmt> &source_body,
             const ny::ast_expr_ptr &value, const std::string &filename,
             const std::vector<submission_binding> &bindings) {
  auto &report = out.report;
  auto body = std::make_shared<ny::ast_block_stmt>();
  body->source_file = filename;
  snapshot_literals snapshots;
  std::set<std::string> names;
  for (const auto &binding : bindings) {
    validate_name(binding.name);
    if (!names.insert(fold_key(binding.name)).second) fail("duplicate retained binding after Matrix name normalization");
    const auto path = "$session.bindings." + binding.name;
    auto declaration = std::make_shared<ny::ast_variable_decl>(
        binding.name, snapshots.expression(binding.value, path), binding.value.type);
    declaration->source_file = path;
    body->statements.push_back(std::make_shared<ny::ast_decl_stmt>(declaration));
  }
  for (const auto &statement : source_body->statements) {
    body->statements.push_back(statement);
    const auto declaration = std::dynamic_pointer_cast<ny::ast_decl_stmt>(statement);
    if (!declaration) continue;
    const auto variable = std::dynamic_pointer_cast<ny::ast_variable_decl>(declaration->declaration);
    if (!variable) continue;
    const auto collect = [&](const ny::ast_variable_decl &binding, bool component) {
      if (component && binding.name == "_") return;
      validate_name(binding.name);
      if (!names.insert(fold_key(binding.name)).second)
        throw submission_request_error(binding.source_file.empty() ? filename : binding.source_file,
                                       "binding already exists after Matrix name normalization: " + binding.name);
      out.binding_names.push_back(binding.name);
    };
    if (variable->tuple_bindings.empty()) collect(*variable, false);
    else for (const auto &component : variable->tuple_bindings) if (component) collect(*component, true);
  }
  if (bindings.size() + out.binding_names.size() > submission_session_max_bindings)
    fail("session cannot retain more than 64 bindings");
  for (const auto &diagnostic : ny::validate_submission_ast(*body, value))
    issue(report, "profile", diagnostic.message,
          diagnostic.source_file.empty() ? filename : diagnostic.source_file, diagnostic.line, "MS1001");
  if (!report.diagnostics.empty()) return;

  std::vector<std::pair<std::string, ny::ast_expr_ptr>> fields;
  if (value) fields.emplace_back("0", value);
  for (const auto &name : out.binding_names)
    fields.emplace_back(std::to_string(fields.size()), std::make_shared<ny::ast_identifier_expr>(name));
  std::shared_ptr<ny::ast_struct_literal_expr> result;
  if (!fields.empty()) {
    result = std::make_shared<ny::ast_struct_literal_expr>("tuple", std::move(fields));
    result->tuple_literal = true;
    result->source_file = filename;
    body->statements.push_back(std::make_shared<ny::ast_expr_stmt>(result));
  }
  auto function = std::make_shared<ny::ast_function_decl>(
      "__nil_submission", std::vector<ny::ast_param>{}, "void", body);
  ny::ast_program program;
  program.filename = filename;
  program.declarations.push_back(function);
  const auto check = [&]() {
    ny::type_checker checker;
    if (checker.check(program)) return true;
    for (const auto &diagnostic : checker.get_diagnostics())
      issue(report, "type", diagnostic.message,
            diagnostic.source_file.empty() ? filename : diagnostic.source_file, diagnostic.line, "MS2001");
    if (report.diagnostics.empty()) issue(report, "type", "submission failed type checking", filename, 0, "MS2001");
    return false;
  };
  if (!check()) return;
  if (result) {
    if (!result->inferred_type) fail("session result has no inferred type");
    function->return_type = *result->inferred_type;
    body->statements.back() = std::make_shared<ny::ast_return_stmt>(result);
    if (!check()) return;
  }
  ny::codegen_options options;
  options.include_debug_info = false;
  options.include_source_debug = false;
  options.mode = ny::build_mode::release_mode;
  ny::codegen generator(options);
  std::vector<ny::codegen_diagnostic> diagnostics;
  auto module = generator.generate_module(program, &diagnostics);
  for (const auto &diagnostic : diagnostics)
    issue(report, "lowering", diagnostic.message, filename, diagnostic.line, "MS3001");
  if (!report.diagnostics.empty()) return;
  if (!module) fail("submission lowering produced no module");
  try { ny::validate_nil_submission_profile(*module); }
  catch (const std::exception &error) { issue(report, "nil-profile", error.what(), filename, 0, "MS1002"); return; }
  std::ostringstream serialized;
  ny::nil_writer writer(serialized, options);
  writer.emit_text_module(*module);
  report.nil_text = serialized.str();
  if (report.nil_text.size() > 1024 * 1024) fail("submission NIL output exceeds 1048576 bytes");
  for (const auto &compiled : module->functions) report.instruction_count += compiled.body.size();
  report.result_type = "void";
  if (value) {
    const auto shape = parse_shape(module->functions.front().return_type);
    if (shape.name != "tuple" || shape.children.empty()) fail("invalid generated session result envelope");
    report.result_type = shape.children.front().spelling;
  }
  out.has_value = static_cast<bool>(value);
  out.module = std::move(module);
  report.status = "complete";
}
} // namespace

prepared_session_submission prepare_session_submission(
    const std::string &source, bool request, const std::string &filename,
    const std::vector<submission_binding> &bindings) {
  prepared_session_submission out;
  out.report.input_kind = request ? "nildata-request" : "matrix";
  try {
    if (source.size() > submission_max_input_bytes) fail("submission input exceeds 65536 bytes");
    nil::detail::valid_utf8(source);
    if (source.find('\0') != std::string::npos) fail("submission input must not contain NUL");
    if (bindings.size() > submission_session_max_bindings) fail("session cannot retain more than 64 bindings");
    std::vector<std::string> names;
    for (const auto &binding : bindings) { validate_name(binding.name); names.push_back(binding.name); }
    if (request) {
      const auto decoded = decode_submission_request(source, names);
      compile(out, decoded.body, decoded.value, filename, bindings);
    } else {
      parser_options options;
      options.filename = filename;
      options.submission = true;
      options.external_names = names;
      parser frontend(source, options);
      const auto parsed = frontend.parse_submission();
      if (parsed.state != submission_state::complete) {
        out.report.status = parsed.state == submission_state::incomplete ? "incomplete" : "invalid";
        for (const auto &diagnostic : parsed.diagnostics) {
          const auto before = out.report.diagnostics.size();
          issue(out.report, "parse", diagnostic.message, filename, diagnostic.primary.line, diagnostic.code);
          if (out.report.diagnostics.size() != before) {
            out.report.diagnostics.back().column = diagnostic.primary.column;
            out.report.diagnostics.back().length = diagnostic.primary.length;
          }
        }
        return out;
      }
      compile(out, parsed.body, parsed.value, filename, bindings);
    }
  } catch (const submission_request_error &error) {
    issue(out.report, request ? "request" : "session", error.what(), error.path, 0, request ? "MS4001" : "MS5001");
  } catch (const std::exception &error) {
    issue(out.report, "session", error.what(), filename);
  }
  if (out.report.status != "complete") {
    out.module.reset(); out.has_value = false; out.binding_names.clear(); out.report.nil_text.clear();
  }
  return out;
}
} // namespace matrix
