#include "analysis.hpp"
#include "default_analyzer.hpp"
#include "matrix_diagnostics.hpp"
#include "secure_file_io.hpp"
#include "matrix_lexer.hpp"
#include "matrix_parser.hpp"
#include "lexer.hpp"
#include "../../common/strict_json.hpp"
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
#include <tuple>

namespace ny {
namespace {
constexpr std::size_t source_limit = 2 * 1024 * 1024;
constexpr std::size_t workspace_limit = 8 * 1024 * 1024;
constexpr std::size_t workspace_source_limit = 256;
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
  return analysis_source_identity(path);
}
bool supported_source_path(const std::string &path) {
  auto ext = std::filesystem::path(path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return ext == ".ny" || ext == ".matrix" || ext == ".mat";
}
std::string read_bounded(std::istream &input, std::size_t limit, const std::string &label) {
  std::string result;
  char buffer[8192];
  while (input.read(buffer, sizeof(buffer)) || input.gcount()) {
    const auto size = static_cast<std::size_t>(input.gcount());
    if (size > limit - result.size()) throw std::runtime_error(label + " exceeds " + std::to_string(limit / (1024 * 1024)) + " MiB");
    result.append(buffer, size);
  }
  if (input.bad()) throw std::runtime_error("cannot read " + label);
  return result;
}
void read_workspace_request(analysis_session &session, const std::string &request) {
  using nil::detail::json;
  const auto value = nil::detail::json_parser(request).parse();
  if (value.type != json::kind::object || value.object.size() != 2 ||
      !value.object.count("schema") || !value.object.count("sources"))
    throw std::runtime_error("workspace request requires only schema and sources fields");
  if (value.at("schema").type != json::kind::string ||
      value.at("schema").text != "nil.analysis.workspace.v1")
    throw std::runtime_error("unsupported analysis workspace schema");
  const auto &sources = value.at("sources");
  if (sources.type != json::kind::array || sources.array.empty() || sources.array.size() > workspace_source_limit)
    throw std::runtime_error("analysis workspace requires 1 to 256 sources");
  std::size_t bytes = 0;
  for (const auto &source : sources.array) {
    if (source.type != json::kind::object || source.object.size() != 2 ||
        !source.object.count("path") || !source.object.count("text") ||
        source.at("path").type != json::kind::string || source.at("text").type != json::kind::string)
      throw std::runtime_error("workspace source requires only string path and text fields");
    const auto &path = source.at("path").text;
    const auto &text = source.at("text").text;
    if (path.empty() || path.size() > 4096 || path.find('\0') != std::string::npos ||
        !std::filesystem::path(path).is_absolute() || !supported_source_path(path))
      throw std::runtime_error("workspace source path must be an absolute .ny, .matrix or .mat path of at most 4096 bytes");
    if (text.find('\0') != std::string::npos) throw std::runtime_error("workspace source contains a NUL byte");
    if (text.size() > source_limit) throw std::runtime_error("analysis source exceeds 2 MiB: " + path);
    if (text.size() > workspace_limit - bytes) throw std::runtime_error("analysis workspace sources exceed 8 MiB");
    bytes += text.size();
    const auto canonical = absolute_path(path);
    std::error_code error;
    const auto status = std::filesystem::status(canonical, error);
    if ((!error && std::filesystem::exists(status) && !std::filesystem::is_regular_file(status)) ||
        (error && error != std::errc::no_such_file_or_directory))
      throw std::runtime_error("workspace source is not a regular file: " + path);
    if (!session.workspace_sources.emplace(canonical, text).second)
      throw std::runtime_error("duplicate canonical workspace source path: " + path);
  }
  if (!session.workspace_sources.count(session.primary_path))
    throw std::runtime_error("analysis workspace must include the primary source");
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

std::string analysis_source_identity(const std::string &path) {
  if (path.find('\0') != std::string::npos) throw std::runtime_error("analysis source path contains a NUL byte");
  return std::filesystem::weakly_canonical(std::filesystem::absolute(path)).string();
}

bool analysis_session::workspace_source_exists(const std::string &path) const {
  try {
    const auto canonical = analysis_source_identity(path);
    if (workspace_sources.count(canonical)) return true;
    std::error_code error;
    return std::filesystem::is_regular_file(canonical, error);
  } catch (const std::filesystem::filesystem_error &) {
    // Import/include candidates are probes. A broken or inaccessible earlier
    // candidate must not hide a valid source in a subsequent search root.
    return false;
  }
}

std::optional<std::string> analysis_session::read_workspace_source(const std::string &path) {
  const auto canonical = analysis_source_identity(path);
  if (const auto cached = sources.find(canonical); cached != sources.end()) return cached->second;
  std::string source;
  if (const auto overlay = workspace_sources.find(canonical); overlay != workspace_sources.end()) {
    source = overlay->second;
  } else {
    std::error_code error;
    if (!std::filesystem::is_regular_file(canonical, error)) return std::nullopt;
    const auto read = detail::read_regular_file_bounded(canonical, source_limit);
    if (!read.ok) throw std::runtime_error("cannot read analysis source: " + canonical + ": " + read.error);
    source.assign(read.bytes.begin(), read.bytes.end());
  }
  record_workspace_source(canonical, source);
  return source;
}

void analysis_session::record_workspace_source(const std::string &path, const std::string &source) {
  if (sources.count(path)) return;
  validate_analysis_utf8(source);
  if (source.find('\0') != std::string::npos) throw std::runtime_error("workspace source contains a NUL byte: " + path);
  if (source.size() > source_limit) throw std::runtime_error("analysis source exceeds 2 MiB: " + path);
  if (sources.size() >= workspace_source_limit) throw std::runtime_error("analysis workspace exceeds 256 source files");
  if (source.size() > workspace_limit - workspace_read_bytes) throw std::runtime_error("analysis workspace source reads exceed 8 MiB");
  workspace_read_bytes += source.size();
  sources.emplace(path, source);
}

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

std::string analysis_session::completion_json() const {
  constexpr std::size_t limit = 4 * 1024 * 1024;
  if (!completion_cursor || !completion || !completion->available ||
      semantic_binary_dependencies || completion->path != primary_path ||
      sources.empty() || sources.size() > workspace_source_limit || !sources.count(primary_path))
    return "null";
  const auto &text = sources.at(primary_path);
  const auto &query = *completion;
  const auto cursor = *completion_cursor;
  if (!query.span.length || query.span.start > text.size() ||
      query.span.length > text.size() - query.span.start || cursor <= query.span.start ||
      cursor - query.span.start > query.span.length ||
      (cursor < text.size() && (static_cast<unsigned char>(text[cursor]) & 0xc0) == 0x80))
    return "null";
  const bool matrix_language = language == "matrix";
  const auto prefix = text.substr(query.span.start, cursor - query.span.start);
  source_span replacement = query.span;
  bool escaped = false, token_found = false;
  std::optional<matrix::parser> matrix_names;
  if (matrix_language) {
    matrix::lexer scanner(text);
    for (const auto &token : scanner.scan()) {
      const std::size_t trim = token.escaped_identifier ? 1 : 0;
      if ((token.kind == matrix::token_kind::identifier ||
           (query.member && token.kind == matrix::token_kind::keyword)) && token.length > 2 * trim &&
          token.start + trim == query.span.start && token.length - 2 * trim == query.span.length) {
        replacement = {token.start, token.length};
        escaped = token.escaped_identifier;
        token_found = true;
      }
    }
    if (scanner.has_errors()) return "null";
    if (query.member) {
      if (matrix::nil_symbol(text.substr(query.span.start, query.span.length)) != query.name)
        return "null";
    } else {
      matrix_names.emplace(text);
      if (matrix_names->resolve_completion_name(text.substr(query.span.start, query.span.length)) != query.name)
        return "null";
    }
  } else {
    lexer scanner(text);
    for (const auto &token : scanner.scan_tokens())
      if (token.kind == token_kind::identifier && token.offset == query.span.start &&
          token.length == query.span.length && token.lexeme == query.name) token_found = true;
  }
  if (!token_found) return "null";
  std::string snapshot_json;
  for (const auto &[path, source] : sources) {
    try {
      if (path.empty() || !std::filesystem::path(path).is_absolute() ||
          analysis_source_identity(path) != path) return "null";
    } catch (const std::exception &) { return "null"; }
    if (!snapshot_json.empty()) snapshot_json += ',';
    snapshot_json += "{\"path\":" + quoted(path) + ",\"text\":" + quoted(source) + '}';
    if (snapshot_json.size() > limit) return "null";
  }
  std::string item_json;
  std::set<std::string> names;
  const auto folded_prefix = matrix_language ? matrix::fold_key(prefix) : prefix;
  for (const auto &candidate : query.candidates) {
    const auto key = matrix_language ? matrix::fold_key(candidate.name) : candidate.name;
    if (key.compare(0, folded_prefix.size(), folded_prefix) != 0) continue;
    const auto source = sources.find(candidate.path);
    if (source == sources.end() || !candidate.span.length || candidate.span.start > source->second.size() ||
        candidate.span.length > source->second.size() - candidate.span.start) continue;
    auto extension = std::filesystem::path(candidate.path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    const bool matrix_declaration = extension == ".matrix" || extension == ".mat";
    if (!matrix_declaration && extension != ".ny") continue;
    const auto original = source->second.substr(candidate.span.start, candidate.span.length);
    if ((matrix_declaration ? matrix::nil_symbol(original) : original) != candidate.name) continue;
    const std::string display = matrix_language && matrix_declaration ? original : candidate.name;
    std::string insertion = display;
    if (matrix_language) {
      // A different declaration anywhere on this page can redirect a folded
      // name. Omit such a candidate rather than inserting the wrong binding.
      if (!query.member && matrix_names->resolve_completion_name(display) != candidate.name) continue;
      if (escaped || matrix::keyword_for(matrix::fold_key(display)) != matrix::kw::not_a_keyword)
        insertion = '`' + display + '`';
      matrix::lexer scanner(insertion);
      const auto tokens = scanner.scan();
      if (scanner.has_errors() || tokens.size() != 2 || tokens.front().kind != matrix::token_kind::identifier)
        continue;
    } else {
      lexer scanner(insertion);
      const auto tokens = scanner.scan_tokens();
      if (tokens.size() != 2 || tokens.front().kind != token_kind::identifier) continue;
    }
    if (!names.insert(insertion).second) continue;
    if (names.size() > 4096) return "null";
    if (!item_json.empty()) item_json += ',';
    item_json += "{\"name\":" + quoted(display) + ",\"kind\":" + quoted(candidate.kind) +
        ",\"type\":" + quoted(matrix_language ? matrix::to_surface_vocabulary(candidate.type) : candidate.type) +
        ",\"insert_text\":" + quoted(static_cast<const std::string &>(insertion)) + '}';
    if (snapshot_json.size() + item_json.size() > limit) return "null";
  }
  auto result = "{\"schema\":\"nil.completion.v1\",\"partial\":true,\"path\":" + quoted(primary_path) +
      ",\"cursor\":" + std::to_string(cursor) + ",\"start\":" + std::to_string(replacement.start) +
      ",\"length\":" + std::to_string(replacement.length) + ",\"sources\":[" + snapshot_json +
      "],\"items\":[" + item_json + "]}";
  return result.size() <= limit ? result : "null";
}

std::string analysis_session::signature_json() const {
  constexpr std::size_t limit = 4 * 1024 * 1024;
  if (!signature_cursor || !signature || !signature->available || semantic_binary_dependencies ||
      signature->path != primary_path || sources.empty() || sources.size() > workspace_source_limit ||
      !sources.count(primary_path) || signature->parameters.size() > 256) return "null";
  const auto &query = *signature;
  const auto &source = sources.at(primary_path);
  const auto cursor = *signature_cursor;
  const auto boundary = [](const std::string &text, std::size_t offset) {
    return offset <= text.size() && (offset == text.size() ||
        (static_cast<unsigned char>(text[offset]) & 0xc0) != 0x80);
  };
  if (query.span.start > source.size() || query.span.length > source.size() - query.span.start ||
      cursor < query.span.start || cursor - query.span.start > query.span.length ||
      !boundary(source, cursor) || !boundary(source, query.span.start) ||
      !boundary(source, query.span.start + query.span.length)) return "null";
  const auto declaration = sources.find(query.declaration_path);
  if (declaration == sources.end() || !query.declaration_span || !query.declaration_span->length ||
      query.declaration_span->start > declaration->second.size() ||
      query.declaration_span->length > declaration->second.size() - query.declaration_span->start) return "null";
  auto extension = std::filesystem::path(query.declaration_path).extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  const bool matrix_declaration = extension == ".matrix" || extension == ".mat";
  if (!matrix_declaration && extension != ".ny") return "null";
  const auto original = declaration->second.substr(query.declaration_span->start, query.declaration_span->length);
  if ((matrix_declaration ? matrix::nil_symbol(original) : original) != query.name) return "null";
  const bool matrix_language = language == "matrix";
  const auto surface = [&](const std::string &type) {
    return matrix_language ? matrix::to_surface_vocabulary(type) : type;
  };
  std::string label = matrix_language && matrix_declaration ? original : query.name;
  label += matrix_language ? (query.parameters.empty() ? " receives nothing" : " receives ") : "(";
  std::string parameters;
  for (std::size_t index = 0; index < query.parameters.size(); ++index) {
    const auto &parameter = query.parameters[index];
    auto name = parameter.name;
    if (matrix_language && matrix_declaration && parameter.span &&
        parameter.span->start <= declaration->second.size() &&
        parameter.span->length <= declaration->second.size() - parameter.span->start) {
      const auto written = declaration->second.substr(parameter.span->start, parameter.span->length);
      if (matrix::nil_symbol(written) == name) name = written;
    }
    const auto type = surface(parameter.type);
    auto declared_type = parameter.type;
    if (parameter.variadic && declared_type.rfind("array<", 0) == 0 &&
        declared_type.size() > 7 && declared_type.back() == '>')
      declared_type = declared_type.substr(6, declared_type.size() - 7);
    const std::string mode = parameter.binding_reference ? (matrix_language ? "reference " : "ref ") :
        parameter.mutable_binding ? (matrix_language ? "mutable " : "mut ") : "";
    const std::string variadic = parameter.variadic ? (matrix_language ? "more " : "..") : "";
    const std::string parameter_label = (matrix_language ? variadic + mode : mode + variadic) +
        name + (matrix_language ? " as " : ": ") + surface(declared_type);
    if (index) { label += ", "; parameters += ','; }
    label += parameter_label;
    parameters += "{\"name\":" + quoted(static_cast<const std::string &>(name)) + ",\"type\":" + quoted(type) +
        ",\"label\":" + quoted(parameter_label) + ",\"variadic\":" +
        (parameter.variadic ? "true" : "false") + ",\"mutable\":" +
        (parameter.mutable_binding ? "true" : "false") + ",\"reference\":" +
        (parameter.binding_reference ? "true" : "false") + '}';
  }
  const auto return_type = surface(query.return_type);
  label += (matrix_language ? " returns " : ") -> ") + return_type;
  std::string snapshots;
  for (const auto &[path, text] : sources) {
    try {
      if (path.empty() || !std::filesystem::path(path).is_absolute() || analysis_source_identity(path) != path)
        return "null";
    } catch (const std::exception &) { return "null"; }
    if (!snapshots.empty()) snapshots += ',';
    snapshots += "{\"path\":" + quoted(path) + ",\"text\":" + quoted(text) + '}';
    if (snapshots.size() > limit) return "null";
  }
  const auto result = "{\"schema\":\"nil.signature.v1\",\"partial\":true,\"path\":" + quoted(primary_path) +
      ",\"cursor\":" + std::to_string(cursor) + ",\"start\":" + std::to_string(query.span.start) +
      ",\"length\":" + std::to_string(query.span.length) + ",\"sources\":[" + snapshots +
      "],\"label\":" + quoted(static_cast<const std::string &>(label)) + ",\"parameters\":[" + parameters +
      "],\"active_parameter\":" + (query.active_parameter ? std::to_string(*query.active_parameter) : "null") +
      ",\"return_type\":" + quoted(return_type) + '}';
  return result.size() <= limit ? result : "null";
}

std::string analysis_session::semantic_json() const {
  constexpr std::size_t limit = 4 * 1024 * 1024;
  if (!semantic_checked || semantic_binary_dependencies || !semantics || !semantics->available ||
      semantics->symbols.size() > 32768 || semantics->references.size() > 131072)
    return "null";
  const auto extension = [](const std::string &path) {
    auto ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    return ext;
  };
  const auto native_source = [&](const std::string &path) {
    if (path.empty() || !std::filesystem::path(path).is_absolute() || !sources.count(path))
      return false;
    try { return analysis_source_identity(path) == path; }
    catch (const std::exception &) { return false; }
  };
  const auto valid_span = [&](const std::string &path, source_span span,
                              std::string name) {
    if (!native_source(path)) return false;
    const auto ext = extension(path);
    if (ext != ".ny" && ext != ".matrix" && ext != ".mat") return false;
    const auto &text = sources.at(path);
    if (!span.length || span.start > text.size() || span.length > text.size() - span.start)
      return false;
    const auto separator = name.rfind("::");
    if (separator != std::string::npos) name.erase(0, separator + 2);
    auto spelling = text.substr(span.start, span.length);
    if (ext == ".matrix" || ext == ".mat") {
      // Matrix's bare-identifier resolution folds ASCII case and removes
      // hyphens/underscores. The checker already selected the binding; this
      // only verifies that its name span still identifies the original token.
      const auto folded = [](const std::string &value) {
        std::string result;
        for (unsigned char c : value) {
          if (c == '-' || c == '_') continue;
          result += static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
        }
        return result;
      };
      return folded(spelling) == folded(name);
    }
    return spelling == name;
  };
  if (!native_source(primary_path)) return "null";
  std::set<std::string> paths{primary_path};
  // Even an import with no emitted binding can affect resolution. Preserve
  // every reached native snapshot so clients can reject a changed graph.
  for (const auto &[path, text] : sources) {
    // Changed canonical identity or an input without a native snapshot makes
    // the whole graph unverifiable, even when its own symbols were omitted.
    if (!native_source(path)) return "null";
    paths.insert(path);
  }
  std::set<std::size_t> ids;
  for (const auto &s : semantics->symbols) {
    if (!valid_span(s.path, s.span, s.name)) continue;
    ids.insert(s.id);
    paths.insert(s.path);
  }
  // A physical name occurrence must resolve to exactly one declaration. A
  // generic/repeated checker visit that disagrees cannot provide navigation.
  using occurrence = std::tuple<std::string, std::size_t, std::size_t>;
  std::map<occurrence, const semantic_reference *> references;
  std::set<occurrence> ambiguous;
  for (const auto &r : semantics->references) {
    if (!ids.count(r.symbol_id) || !valid_span(r.path, r.span, r.name)) continue;
    const auto key = std::make_tuple(r.path, r.span.start, r.span.length);
    const auto [it, inserted] = references.emplace(key, &r);
    if (!inserted && it->second->symbol_id != r.symbol_id) ambiguous.insert(key);
  }

  // A partial index cannot certify editing from the references it happens to
  // contain. For lexical locals, prove coverage against every original name
  // token as well. Unrepresented type/member/assembly/argument-label tokens
  // conservatively disqualify that spelling; they never become guessed uses.
  std::map<occurrence, std::size_t> represented;
  for (const auto &s : semantics->symbols) {
    if (!ids.count(s.id)) continue;
    const auto key = std::make_tuple(s.path, s.span.start, s.span.length);
    const auto [it, inserted] = represented.emplace(key, s.id);
    if (!inserted && it->second != s.id) ambiguous.insert(key);
  }
  for (const auto &[key, r] : references) {
    const auto [it, inserted] = represented.emplace(key, r->symbol_id);
    if (!inserted && it->second != r->symbol_id) ambiguous.insert(key);
  }
  struct editing_source {
    std::string language;
    std::set<std::string> identifier_keys, reserved_keys, uncovered_keys;
    std::set<std::pair<std::size_t, std::size_t>> name_tokens;
    bool available = true;
  };
  std::map<std::string, editing_source> editing_sources;
  for (const auto &s : semantics->symbols)
    if (ids.count(s.id) && s.lexical_local && s.all_uses_located)
      editing_sources.try_emplace(s.path);
  std::size_t identifier_count = 0;
  for (auto &[path, source] : editing_sources) {
    const auto &text = sources.at(path);
    const auto ext = extension(path);
    source.language = ext == ".ny" ? "ny" : "matrix";
    const auto record = [&](const std::string &key, std::size_t start, std::size_t length) {
      if (!source.available) return;
      if (key.empty()) { source.available = false; return; }
      if (source.identifier_keys.insert(key).second) ++identifier_count;
      if (source.identifier_keys.size() > 32768 || identifier_count > 131072 ||
          source.name_tokens.size() >= 131072) {
        source.available = false;
        return;
      }
      source.name_tokens.emplace(start, length);
      const auto position = std::make_tuple(path, start, length);
      if (!represented.count(position) || ambiguous.count(position))
        source.uncovered_keys.insert(key);
    };
    if (source.language == "matrix") {
      matrix::lexer scanner(text);
      for (const auto &token : scanner.scan()) {
        // Analysis does not enforce the eventual native assembler profile.
        // Keep arbitrary assembly text outside the closed-local proof.
        if (token.is(matrix::kw::assembly)) source.available = false;
        if (token.kind != matrix::token_kind::identifier) continue;
        const auto trim = token.escaped_identifier ? 1u : 0u;
        if (token.length <= 2 * trim) { source.available = false; continue; }
        record(token.key, token.start + trim, token.length - 2 * trim);
      }
      if (scanner.has_errors()) source.available = false;
      for (int k = 0; k < static_cast<int>(matrix::kw::not_a_keyword); ++k)
        source.reserved_keys.insert(matrix::fold_key(matrix::keyword_name(static_cast<matrix::kw>(k))));
    } else {
      lexer scanner(text);
      for (const auto &token : scanner.scan_tokens()) {
        if (token.kind == token_kind::identifier)
          record(token.lexeme, token.offset, token.length);
        // Legacy implicit interpolation has no editable original spans.
        // Explicit formatted text emits normal expression tokens, so those
        // bindings participate in the same coverage proof as surrounding code.
        // Withhold editing for the whole file, including potentially captured
        // names absent from the ordinary token stream. Literal braces are an
        // intentional conservative exclusion until interpolation is mapped.
        if (token.kind == token_kind::invalid ||
            (token.kind == token_kind::identifier && token.lexeme == "asm") ||
            (token.kind == token_kind::string_literal && token.lexeme.find('{') != std::string::npos))
          source.available = false;
      }
      std::istringstream words("function fun let const if else elseif then return println while where for in break continue loop do mut import package module namespace using use pub type as end true false match when struct enum trait impl allocate deallocate ref fn mod class abstract virtual override inherits self attempt fault recover raise assert hotreload not test example property generate asm and or _");
      for (std::string word; words >> word;) source.reserved_keys.insert(word);
    }
    for (const auto *reserved : {"owned", "owned_move", "owned_borrow", "owned_borrow_mut", "parallel_map", "assert", "hotreload"})
      source.reserved_keys.insert(source.language == "matrix" ? matrix::fold_key(reserved) : reserved);
  }
  std::set<std::size_t> unlocated;
  for (const auto &r : semantics->references) {
    if (r.symbol_id >= semantics->symbols.size()) continue;
    const auto &symbol = semantics->symbols[r.symbol_id];
    const auto source = editing_sources.find(symbol.path);
    if (source == editing_sources.end()) continue;
    const auto position = std::make_tuple(r.path, r.span.start, r.span.length);
    if (r.path != symbol.path || !references.count(position) || ambiguous.count(position) ||
        !source->second.name_tokens.count({r.span.start, r.span.length}))
      unlocated.insert(r.symbol_id);
  }
  std::set<std::size_t> complete;
  for (const auto &s : semantics->symbols) {
    if (!ids.count(s.id) || !s.lexical_local || !s.all_uses_located) continue;
    const auto &source = editing_sources.at(s.path);
    const auto name = source.language == "matrix" ? matrix::fold_key(s.name) : s.name;
    if (!source.available || source.uncovered_keys.count(name) ||
        !source.name_tokens.count({s.span.start, s.span.length}) ||
        ambiguous.count(std::make_tuple(s.path, s.span.start, s.span.length))) continue;
    if (!unlocated.count(s.id)) complete.insert(s.id);
  }
  std::string symbol_json;
  std::set<std::string> policy_paths;
  for (const auto &s : semantics->symbols) {
    if (!ids.count(s.id)) continue;
    const bool rename_safe = complete.count(s.id);
    const auto entry = "{\"id\":" + std::to_string(s.id) +
        ",\"kind\":" + quoted(s.kind) + ",\"name\":" + quoted(s.name) +
        ",\"type\":" + quoted(s.type) + ",\"path\":" + quoted(s.path) +
        ",\"start\":" + std::to_string(s.span.start) +
        ",\"length\":" + std::to_string(s.span.length) +
        (complete.count(s.id) ? ",\"references_complete\":true" : "") +
        (rename_safe ? ",\"rename_safe\":true" : "") + '}';
    if (entry.size() + symbol_json.size() + 1 > limit) return "null";
    if (!symbol_json.empty()) symbol_json += ',';
    symbol_json += entry;
    if (rename_safe) policy_paths.insert(s.path);
  }
  std::string reference_json;
  for (const auto &[key, r] : references) {
    if (ambiguous.count(key)) continue;
    const auto entry = "{\"symbol_id\":" + std::to_string(r->symbol_id) +
        ",\"path\":" + quoted(r->path) +
        ",\"start\":" + std::to_string(r->span.start) +
        ",\"length\":" + std::to_string(r->span.length) + '}';
    if (entry.size() + symbol_json.size() + reference_json.size() + 1 > limit) return "null";
    if (!reference_json.empty()) reference_json += ',';
    reference_json += entry;
    paths.insert(r->path);
  }
  const auto keys_json = [&](const std::set<std::string> &keys) {
    std::string result = "[";
    for (const auto &key : keys) {
      if (result.size() > 1) result += ',';
      result += quoted(key);
    }
    return result + ']';
  };
  std::string policy_json = "{\"schema\":\"nil.rename.v1\",\"sources\":[";
  bool first_policy = true;
  for (const auto &path : policy_paths) {
    const auto &source = editing_sources.at(path);
    const auto entry = "{\"path\":" + quoted(path) + ",\"language\":" + quoted(source.language) +
        ",\"identifier_keys\":" + keys_json(source.identifier_keys) +
        ",\"reserved_keys\":" + keys_json(source.reserved_keys) + '}';
    if (entry.size() + policy_json.size() + symbol_json.size() + reference_json.size() + 1 > limit)
      return "null";
    if (!first_policy) policy_json += ',';
    first_policy = false;
    policy_json += entry;
  }
  policy_json += "]}";
  if (paths.size() > 256) return "null";
  std::string source_json;
  for (const auto &path : paths) {
    const auto entry = "{\"path\":" + quoted(path) + ",\"text\":" + quoted(sources.at(path)) + '}';
    if (entry.size() + source_json.size() + symbol_json.size() + reference_json.size() + policy_json.size() + 1 > limit)
      return "null";
    if (!source_json.empty()) source_json += ',';
    source_json += entry;
  }
  auto result = "{\"schema\":\"nil.semantic.v1\",\"partial\":true,\"sources\":[" + source_json +
      "],\"symbols\":[" + symbol_json + "],\"references\":[" + reference_json +
      "],\"rename_policy\":" + policy_json + '}';
  return result.size() > limit ? "null" : result;
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
  out << "]}";
  if (semantic_requested) {
    auto semantic = success ? semantic_json() : "null";
    // Optional navigation must not crowd otherwise valid diagnostics out of
    // the established analysis-response bound.
    if (static_cast<std::size_t>(out.tellp()) + semantic.size() + 16 > output_limit)
      semantic = "null";
    out << ",\"semantic\":" << semantic;
  }
  if (completion_cursor) {
    auto result = completion_json();
    if (static_cast<std::size_t>(out.tellp()) + result.size() + 18 > output_limit) result = "null";
    out << ",\"completion\":" << result;
  }
  if (signature_cursor) {
    auto result = signature_json();
    if (static_cast<std::size_t>(out.tellp()) + result.size() + 18 > output_limit) result = "null";
    out << ",\"signature\":" << result;
  }
  out << "}\n";
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
      if (a == "--workspace-stdin") { session.workspace_stdin = true; continue; }
      if (a == "--semantic-index") { session.semantic_requested = true; continue; }
      if (a == "--signature-at") {
        if (++i >= argc) throw std::runtime_error("missing value after " + a);
        const std::string offset = argv[i];
        if (session.signature_cursor || offset.empty() || offset.size() > 7 ||
            offset.find_first_not_of("0123456789") != std::string::npos)
          throw std::runtime_error("--signature-at requires one UTF-8 byte offset");
        const auto parsed = std::stoul(offset);
        if (parsed > source_limit) throw std::runtime_error("signature offset exceeds source limit");
        session.signature_cursor = parsed;
        continue;
      }
      if (a == "--completion-at") {
        if (++i >= argc) throw std::runtime_error("missing value after " + a);
        const std::string offset = argv[i];
        if (session.completion_cursor || offset.empty() || offset.size() > 7 ||
            offset.find_first_not_of("0123456789") != std::string::npos)
          throw std::runtime_error("--completion-at requires one UTF-8 byte offset");
        const auto parsed = std::stoul(offset);
        if (parsed > source_limit) throw std::runtime_error("completion offset exceeds source limit");
        session.completion_cursor = parsed;
        continue;
      }
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
    if (session.source_stdin && session.workspace_stdin)
      throw std::runtime_error("--source-stdin and --workspace-stdin are mutually exclusive");
    session.logical_path = std::filesystem::absolute(inputs.front()).lexically_normal().string();
    session.primary_path = absolute_path(inputs.front());
    auto ext = std::filesystem::path(session.logical_path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (ext != ".ny" && ext != ".matrix" && ext != ".mat")
      throw std::runtime_error("analysis source must end in .ny, .matrix or .mat");
    session.language = ext == ".ny" ? "ny" : "matrix";
    if (session.source_stdin || session.workspace_stdin) {
#ifdef _WIN32
      if (_setmode(_fileno(stdin), _O_BINARY) == -1)
        throw std::runtime_error("cannot enable binary analysis stdin");
#endif
      auto input = read_bounded(std::cin, session.workspace_stdin ? workspace_limit : source_limit,
                                session.workspace_stdin ? "analysis workspace request" : "analysis source");
      if (session.workspace_stdin) read_workspace_request(session, input);
      else session.stdin_source = std::move(input);
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
      if (session.sources.count(export_path) || session.workspace_sources.count(export_path))
        throw std::runtime_error("reflection output would overwrite an analyzed source");
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
