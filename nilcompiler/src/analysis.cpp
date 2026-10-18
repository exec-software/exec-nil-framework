#include "analysis.hpp"
#include "default_analyzer.hpp"
#include "matrix_diagnostics.hpp"
#include "secure_file_io.hpp"
#include "matrix_lexer.hpp"
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace ny {
namespace {
constexpr std::size_t source_limit = 2 * 1024 * 1024;
constexpr std::size_t output_limit = 8 * 1024 * 1024;
constexpr std::size_t declaration_limit = 8192;
std::string quoted(const std::string &text) {
  validate_analysis_utf8(text);
  std::string out = "\"";
  const char *hex = "0123456789abcdef";
  for (unsigned char c : text) {
    if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
    else if (c < 32) {
      out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15];
    } else out += char(c);
  }
  return out + '"';
}
std::string ny_literal(const std::string &text) {
  std::string out = "\"";
  for (unsigned char c : text) {
    switch (c) {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    case '\r': out += "\\r"; break;
    case '\t': out += "\\t"; break;
    case '\f': out += "\\f"; break;
    case '\v': out += "\\v"; break;
    default:
      // Ny's lexer has no universal hexadecimal string escape. Never emit
      // an ambiguous source representation for a control byte.
      if (c < 32) throw std::runtime_error("reflection source contains an unsupported control byte");
      out += char(c);
    }
  }
  return out + '"';
}
std::string absolute_path(const std::string &path) {
  return std::filesystem::weakly_canonical(std::filesystem::absolute(path)).string();
}
std::string join(const std::string &prefix, const std::string &name) {
  return prefix.empty() ? name : prefix + "::" + name;
}
// Legacy compiler components occasionally write directly to the standard
// streams. Keep the protocol pure, and bound captured fallback diagnostics.
class capture_buffer : public std::streambuf {
public:
  std::string text;
  int_type overflow(int_type c) override {
    if (!traits_type::eq_int_type(c, traits_type::eof()) && text.size() < 65536)
      text += traits_type::to_char_type(c);
    return traits_type::not_eof(c);
  }
};
struct capture_streams {
  capture_buffer buffer;
  std::streambuf *out, *err;
  capture_streams() : out(std::cout.rdbuf(&buffer)), err(std::cerr.rdbuf(&buffer)) {}
  ~capture_streams() { std::cout.rdbuf(out); std::cerr.rdbuf(err); }
};
} // namespace

void validate_analysis_utf8(const std::string &text) {
  for (std::size_t i = 0; i < text.size();) {
    const auto first = static_cast<unsigned char>(text[i++]);
    if (first < 0x80) continue;
    unsigned count = first >= 0xc2 && first <= 0xdf ? 1 :
                     first >= 0xe0 && first <= 0xef ? 2 :
                     first >= 0xf0 && first <= 0xf4 ? 3 : 0;
    if (!count || text.size() - i < count) throw std::runtime_error("analysis requires valid UTF-8");
    unsigned code = first & ((1u << (6 - count)) - 1);
    for (unsigned n = 0; n < count; ++n) {
      const auto next = static_cast<unsigned char>(text[i++]);
      if ((next & 0xc0) != 0x80) throw std::runtime_error("analysis requires valid UTF-8");
      code = (code << 6) | (next & 0x3f);
    }
    if ((count == 2 && code < 0x800) || (count == 3 && code < 0x10000) ||
        (code >= 0xd800 && code <= 0xdfff) || code > 0x10ffff)
      throw std::runtime_error("analysis requires valid UTF-8");
  }
}

void analysis_session::add(std::string code, std::string severity,
    std::string message, const std::string &path, int line, int column,
    int length, std::string hint) {
  if (disabled_rules.count(code)) return;
  const auto bytes = code.size() + severity.size() + message.size() + path.size() + hint.size();
  if (diagnostics.size() >= 512 || bytes > 256 * 1024 - diagnostic_bytes) {
    truncated = true;
    if (diagnostics.empty()) diagnostics.push_back({"NIL0004", "error", "Diagnostic output exceeds the analysis limit", {}, {}, {}, {}, {}});
    return;
  }
  diagnostic_bytes += bytes;
  analysis_diagnostic d{std::move(code), std::move(severity), std::move(message),
                        path, std::move(hint), {}, {}, {}};
  auto source = sources.find(path);
  if (source != sources.end() && line > 0 && source->second.find("@include") == std::string::npos) {
    const auto &text = source->second;
    std::size_t begin = 0;
    for (int n = 1; n < line && begin <= text.size(); ++n) {
      const auto end = text.find('\n', begin);
      begin = end == std::string::npos ? text.size() + 1 : end + 1;
    }
    if (begin <= text.size()) {
      auto end = text.find('\n', begin);
      if (end == std::string::npos) end = text.size();
      if (end > begin && text[end - 1] == '\r') --end;
      begin = std::min(end, begin + std::size_t(std::max(0, column - 1)));
      d.start = begin;
      d.length = length > 0 ? std::min(std::size_t(length), end - begin) : end - begin;
    }
  }
  diagnostics.push_back(std::move(d));
}

void analysis_session::reflect(const ast_program &program, const std::string &path) {
  constexpr std::size_t reflection_limit = 1024 * 1024;
  const auto source = sources.find(path);
  const bool expanded = source != sources.end() &&
                        source->second.find("@include") != std::string::npos;
  const auto source_line = [&](int line) { return expanded ? 0 : line; };
  const auto reserve_bytes = [&](std::size_t bytes) {
    if (bytes > reflection_limit - reflection_bytes) {
      truncated = true;
      return false;
    }
    reflection_bytes += bytes;
    return true;
  };
  auto append = [&](reflected_declaration value, const ast_node *node) {
    if (truncated || declarations.size() >= declaration_limit) { truncated = true; return -1; }
    const std::size_t bytes = sizeof(reflected_declaration) + value.kind.size() +
        value.name.size() + value.qualified_name.size() + value.type_name.size() +
        value.source_path.size();
    if (!reserve_bytes(bytes)) return -1;
    value.line = source_line(value.line);
    int id = static_cast<int>(declarations.size());
    declarations.push_back(std::move(value));
    if (node) for (const auto &a : node->annotations) {
      if (annotations.size() >= declaration_limit) { truncated = true; break; }
      const auto bytes = sizeof(reflected_annotation) + a.name.size() +
                         (a.args ? a.args->size() : 0);
      if (!reserve_bytes(bytes)) break;
      annotations.push_back({id, a.name, a.args.value_or(""), source_line(a.line)});
    }
    return id;
  };
  std::function<void(const ast_decl &, const std::string &, int)> visit;
  visit = [&](const ast_decl &d, const std::string &scope, int parent) {
    if (truncated) return;
    reflected_declaration r;
    r.source_path = path; r.line = d.line; r.parent_id = parent;
    r.is_public = d.is_public(); r.semantic_checked = semantic_checked;
    const std::vector<ast_struct_field> *fields = nullptr;
    const std::vector<std::shared_ptr<ast_function_decl>> *methods = nullptr;
    const ast_function_decl *function = nullptr;
    const ast_enum_decl *enumeration = nullptr;
    switch (d.kind) {
    case ast_node_kind::function_decl_node: {
      function = &static_cast<const ast_function_decl &>(d);
      r.kind = "function"; r.name = function->name;
      r.type_name = function->return_type.value_or("void"); break;
    }
    case ast_node_kind::struct_decl_node: {
      const auto &record = static_cast<const ast_struct_decl &>(d);
      if (record.concrete_generic_type) return;
      r.kind = "record"; r.name = record.name; fields = &record.fields; break;
    }
    case ast_node_kind::class_decl_node: {
      const auto &c = static_cast<const ast_class_decl &>(d);
      r.kind = "class"; r.name = c.name; fields = &c.fields; methods = &c.methods; break;
    }
    case ast_node_kind::enum_decl_node:
      enumeration = &static_cast<const ast_enum_decl &>(d);
      r.kind = "enum"; r.name = enumeration->name; break;
    case ast_node_kind::trait_decl_node: {
      const auto &t = static_cast<const ast_trait_decl &>(d);
      r.kind = "trait"; r.name = t.name; methods = &t.methods; break;
    }
    case ast_node_kind::impl_decl_node: {
      const auto &i = static_cast<const ast_impl_decl &>(d);
      r.kind = "implementation"; r.name = i.target_type;
      r.type_name = i.trait_name.value_or(""); methods = &i.methods; break;
    }
    case ast_node_kind::type_alias_decl_node: {
      const auto &t = static_cast<const ast_type_alias_decl &>(d);
      r.kind = "type_alias"; r.name = t.name; r.type_name = t.target_type; break;
    }
    case ast_node_kind::variable_decl_node: {
      const auto &v = static_cast<const ast_variable_decl &>(d);
      r.kind = "global"; r.name = v.name; r.type_name = v.type_name.value_or(""); break;
    }
    default: return;
    }
    r.qualified_name = join(scope, r.name);
    const int id = append(r, &d);
    if (id < 0) return;
    if (function) for (const auto &p : function->params)
      if (append({"parameter", p.name, join(r.qualified_name, p.name),
              p.type_name.value_or(""), path, p.line, id, false, semantic_checked}, nullptr) < 0) return;
    if (fields) for (const auto &f : *fields)
      if (append({"field", f.name, join(r.qualified_name, f.name), f.type_name,
              path, f.line, id, r.is_public, semantic_checked}, nullptr) < 0) return;
    if (enumeration) for (const auto &v : enumeration->variants)
      if (append({"variant", v.name, join(r.qualified_name, v.name), v.associated_type.value_or(""),
              path, v.line, id, r.is_public, semantic_checked}, nullptr) < 0) return;
    if (methods) for (const auto &m : *methods) if (m) visit(*m, r.qualified_name, id);
  };
  std::string scope;
  for (const auto &d : program.declarations) {
    if (!d) continue;
    if (d->kind == ast_node_kind::module_decl_node)
      scope = static_cast<const ast_module_decl &>(*d).name;
    else visit(*d, scope, -1);
  }
}

void analysis_session::lint() {
  const auto found = sources.find(primary_path);
  if (found == sources.end()) return;
  const auto &source = found->second;
  std::vector<std::size_t> line_starts{0};
  for (std::size_t i = 0; i < source.size(); ++i)
    if (source[i] == '\n') line_starts.push_back(i + 1);
  std::vector<bool> protected_lines(line_starts.size(), false);
  if (language == "matrix") {
    matrix::lexer lexer(source);
    const auto tokens = lexer.scan();
    if (lexer.has_errors()) return;
    for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
      if (tokens[i].kind != matrix::token_kind::string_literal) continue;
      // A quoted literal cannot cross a line; a raw text token starts at
      // 'begin text'. Protect through the next token's line conservatively.
      if (tokens[i].line < 1 || std::size_t(tokens[i].line) > line_starts.size()) continue;
      const auto offset = line_starts[std::size_t(tokens[i].line - 1)] +
                          std::size_t(std::max(0, tokens[i].column - 1));
      if (offset >= source.size() || source[offset] == '"') continue;
      for (int line = tokens[i].line; line <= tokens[i + 1].line && std::size_t(line) <= protected_lines.size(); ++line)
        protected_lines[std::size_t(line - 1)] = true;
    }
  }
  std::string comment_end;
  char quote = 0;
  int line_number = 0;
  for (std::size_t begin = 0; begin < source.size();) {
    ++line_number;
    auto end = source.find('\n', begin);
    if (end == std::string::npos) end = source.size();
    auto content_end = end;
    if (content_end > begin && source[content_end - 1] == '\r') --content_end;
    bool protected_line = quote || protected_lines[std::size_t(line_number - 1)];
    for (std::size_t i = begin; language == "ny" && i < content_end; ++i) {
      char c = source[i];
      if (!comment_end.empty()) {
        if (source.compare(i, comment_end.size(), comment_end) == 0) {
          i += comment_end.size() - 1; comment_end.clear();
        }
        continue;
      }
      if (quote) {
        if (language == "ny" && c == '\\') { ++i; continue; }
        if (c == quote) quote = 0;
        continue;
      }
      if (language == "ny" && source.compare(i, 3, "###") == 0) {
        comment_end = "###"; i += 2; continue;
      }
      if (language == "ny" && source.compare(i, 2, "/*") == 0) {
        comment_end = "*/"; ++i; continue;
      }
      if (c == '#' || (language == "matrix" && source.compare(i, 3, "!!!") == 0) ||
          (c == '/' && i + 1 < content_end && source[i + 1] == '/')) break;
      if (c == '"' || (language == "ny" && c == '\'')) { quote = c; continue; }
    }
    protected_line = protected_line || quote;
    auto trim = content_end;
    while (trim > begin && (source[trim - 1] == ' ' || source[trim - 1] == '\t')) --trim;
    if (!protected_line && trim < content_end && diagnostics.size() < 512 &&
        !disabled_rules.count("NIL1001")) {
      diagnostics.push_back({"NIL1001", "warning", "Trailing whitespace", primary_path, "",
        trim, content_end - trim, analysis_fix{trim, content_end - trim, "", "Remove trailing whitespace"}});
    }
    begin = end < source.size() ? end + 1 : source.size();
  }
  if (!source.empty() && source.back() != '\n' && !quote && diagnostics.size() < 512 &&
      !disabled_rules.count("NIL1002")) {
    const std::string newline = source.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    diagnostics.push_back({"NIL1002", "warning", "Missing final newline", primary_path, "",
      source.size(), 0, analysis_fix{source.size(), 0, newline, "Add final newline"}});
  }
}

std::string analysis_session::json(bool success) const {
  std::ostringstream out;
  out << "{\"schema\":\"nil.analysis.v1\",\"source\":" << quoted(logical_path)
      << ",\"language\":" << quoted(language) << ",\"success\":" << (success ? "true" : "false")
      << ",\"diagnostics\":[";
  bool first = true;
  for (const auto &d : diagnostics) {
    if (!first) out << ','; first = false;
    out << "{\"code\":" << quoted(d.code) << ",\"severity\":" << quoted(d.severity)
        << ",\"message\":" << quoted(d.message) << ",\"path\":" << (d.path.empty() ? "null" : quoted(d.path == primary_path ? logical_path : d.path))
        << ",\"start\":" << (d.start ? std::to_string(*d.start) : "null")
        << ",\"length\":" << (d.length ? std::to_string(*d.length) : "null")
        << ",\"hint\":" << quoted(d.hint) << ",\"fix\":";
    if (!d.fix) out << "null";
    else out << "{\"start\":" << d.fix->start << ",\"length\":" << d.fix->length
             << ",\"replacement\":" << quoted(d.fix->replacement)
             << ",\"description\":" << quoted(d.fix->description) << '}';
    out << '}';
  }
  out << "],\"sources\":[";
  // Byte offsets in dependency diagnostics are valid only for this exact
  // snapshot. Consumers must compare it with the editor/disk before mapping.
  // Bound the encoded entries, since escaped source can expand substantially.
  constexpr std::size_t snapshot_limit = 1024 * 1024;
  std::size_t snapshot_bytes = 2; // JSON array brackets
  std::set<std::string> emitted;
  first = true;
  for (const auto &d : diagnostics) {
    if (d.path.empty() || d.path == primary_path || !d.start || !d.length ||
        !emitted.insert(d.path).second) continue;
    const auto source = sources.find(d.path);
    if (source == sources.end()) continue;
    const auto entry = "{\"path\":" + quoted(d.path) + ",\"text\":" + quoted(source->second) + '}';
    const auto bytes = entry.size() + (first ? 0 : 1);
    if (bytes > snapshot_limit - snapshot_bytes) continue;
    if (!first) out << ',';
    first = false;
    out << entry;
    snapshot_bytes += bytes;
  }
  out << "],\"reflection\":{\"schema\":\"nil.reflection.v1\",\"semantic_checked\":"
      << (semantic_checked ? "true" : "false") << ",\"truncated\":" << (truncated ? "true" : "false")
      << ",\"declarations\":[";
  for (std::size_t i = 0; i < declarations.size(); ++i) {
    if (i) out << ',';
    const auto &d = declarations[i];
    out << "{\"id\":" << i << ",\"kind\":" << quoted(d.kind) << ",\"name\":" << quoted(d.name)
        << ",\"qualified_name\":" << quoted(d.qualified_name) << ",\"type_name\":" << quoted(d.type_name)
        << ",\"source_path\":" << quoted(d.source_path) << ",\"line\":" << d.line
        << ",\"parent_id\":" << d.parent_id << ",\"is_public\":" << (d.is_public ? "true" : "false")
        << ",\"semantic_checked\":" << (d.semantic_checked ? "true" : "false") << '}';
  }
  out << "],\"annotations\":[";
  for (std::size_t i = 0; i < annotations.size(); ++i) {
    if (i) out << ',';
    const auto &a = annotations[i];
    out << "{\"owner_id\":" << a.owner_id << ",\"name\":" << quoted(a.name)
        << ",\"arguments\":" << quoted(a.arguments) << ",\"line\":" << a.line << '}';
  }
  out << "]}}\n";
  return out.str();
}

std::string analysis_session::companion_source() const {
  if (truncated) throw std::runtime_error("reflection snapshot exceeds the export limit");
  std::ostringstream out;
  out << "# Generated by nilcompiler --analyze; snapshot-local IDs.\nmodule analysis_snapshot\n"
      << "import nil.std.reflection.*\n\npub fn count() -> i32\n return " << declarations.size() << "\nend fn\n"
      << "pub fn declaration(id: i32) -> declaration_info\n";
  auto record = [&](const reflected_declaration &d) {
    out << "declaration_info { kind: " << ny_literal(d.kind) << ", name: " << ny_literal(d.name)
        << ", qualified_name: " << ny_literal(d.qualified_name) << ", type_name: " << ny_literal(d.type_name)
        << ", source_path: " << ny_literal(d.source_path) << ", line: " << d.line
        << ", parent_id: " << d.parent_id << ", is_public: " << (d.is_public ? "true" : "false")
        << ", semantic_checked: " << (d.semantic_checked ? "true" : "false") << " }\n";
  };
  for (std::size_t i = 0; i < declarations.size(); ++i) {
    out << " if id == " << i << " then\n  return "; record(declarations[i]); out << " end if\n";
  }
  out << " panic(\"Invalid reflection declaration ID\")\n return "; record({});
  out << "end fn\npub fn annotation_count() -> i32\n return " << annotations.size() << "\nend fn\n"
      << "pub fn annotation(id: i32) -> annotation_info\n";
  auto annotation = [&](const reflected_annotation &a) {
    out << "annotation_info { owner_id: " << a.owner_id << ", name: " << ny_literal(a.name)
        << ", arguments: " << ny_literal(a.arguments) << ", line: " << a.line << " }\n";
  };
  for (std::size_t i = 0; i < annotations.size(); ++i) {
    out << " if id == " << i << " then\n  return "; annotation(annotations[i]); out << " end if\n";
  }
  out << " panic(\"Invalid reflection annotation ID\")\n return "; annotation({-1, "", "", 0});
  out << "end fn\n";
  return out.str();
}

int run_analysis_cli(int argc, char **argv, compilation_entry compile) {
  try {
    analysis_session session;
    std::string export_path;
    std::vector<std::string> args{argv[0]};
    std::vector<std::string> inputs;
    for (int i = 1; i < argc; ++i) {
      const std::string a = argv[i];
      if (a == "--analyze") continue;
      if (a == "--source-stdin") { session.source_stdin = true; continue; }
      if (a == "--warnings-as-errors") { session.warnings_as_errors = true; continue; }
      if (a == "--disable-rule") {
        if (++i >= argc) throw std::runtime_error("missing value after " + a);
        if (!is_default_analysis_rule(argv[i]))
          throw std::runtime_error("unknown analysis rule: " + std::string(argv[i]));
        session.disabled_rules.insert(argv[i]);
        continue;
      }
      if (a == "--emit-reflection" || a == "-I" || a == "--sdk" || a == "--sdk-path") {
        if (++i >= argc) throw std::runtime_error("missing value after " + a);
        if (a == "--emit-reflection") export_path = absolute_path(argv[i]);
        else { args.push_back("-I"); args.push_back(argv[i]); }
      } else if (a == "--no-stdlib") args.push_back(a);
      else if (!a.empty() && a.front() != '-') inputs.push_back(a);
      else throw std::runtime_error("unsupported analysis option: " + a);
    }
    if (inputs.size() != 1) throw std::runtime_error("--analyze requires one source path");
    session.logical_path = std::filesystem::absolute(inputs.front()).lexically_normal().string();
    session.primary_path = absolute_path(inputs.front());
    auto ext = std::filesystem::path(session.logical_path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (ext != ".ny" && ext != ".matrix" && ext != ".mat")
      throw std::runtime_error("analysis source must end in .ny, .matrix or .mat");
    session.language = ext == ".ny" ? "ny" : "matrix";
    if (session.source_stdin) {
#ifdef _WIN32
      if (_setmode(_fileno(stdin), _O_BINARY) == -1)
        throw std::runtime_error("cannot enable binary analysis stdin");
#endif
      char buffer[8192];
      while (std::cin.read(buffer, sizeof(buffer)) || std::cin.gcount()) {
        session.stdin_source.append(buffer, static_cast<std::size_t>(std::cin.gcount()));
        if (session.stdin_source.size() > source_limit) throw std::runtime_error("analysis source exceeds 2 MiB");
      }
      if (std::cin.bad()) throw std::runtime_error("cannot read analysis stdin");
    }
    args.insert(args.end(), {"-I", std::filesystem::path(session.primary_path).parent_path().string(),
                            session.primary_path, "-o", "analysis-unused.nil", "--no-bundle", "-q"});
    std::vector<char *> pointers;
    for (auto &arg : args) pointers.push_back(arg.data());
    int status;
    std::string fallback;
    {
      capture_streams capture;
      status = compile(static_cast<int>(pointers.size()), pointers.data(), &session);
      fallback = capture.buffer.text;
    }
    if (status != 0 && session.diagnostics.empty())
      session.add("NIL0000", "error", fallback.empty() ? "Analysis failed" : fallback);
    if (status == 0) session.lint();
    session.diagnostics.erase(std::remove_if(session.diagnostics.begin(), session.diagnostics.end(),
        [&](const analysis_diagnostic &d) { return session.disabled_rules.count(d.code) != 0; }),
        session.diagnostics.end());
    if (session.warnings_as_errors) for (auto &d : session.diagnostics) {
      if (d.severity == "warning") { d.severity = "error"; status = 1; }
    }
    const auto report = session.json(status == 0);
    if (report.size() > output_limit) throw std::runtime_error("analysis output exceeds 8 MiB");
    if (status == 0 && !export_path.empty()) {
      if (session.sources.count(export_path)) throw std::runtime_error("reflection output would overwrite an analyzed source");
      const auto contents = session.companion_source();
      const auto write = detail::write_file_atomic(export_path,
          std::vector<char>(contents.begin(), contents.end()), true);
      if (!write.ok) throw std::runtime_error("cannot write reflection source: " + write.error);
    }
    std::cout << report;
    return status == 0 ? 0 : 1;
  } catch (const std::exception &error) {
    std::cerr << "Analysis error: " << error.what() << '\n';
    return 2;
  }
}
} // namespace ny
