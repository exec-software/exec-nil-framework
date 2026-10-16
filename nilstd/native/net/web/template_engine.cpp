// =============================================================================
// ml_nyweb — NyWeb C++ interop layer
//
// Implements all @interop("ml_nyweb") functions declared in:
//   nyweb/src/template.ny    — template parsing and rendering
//   nyweb/src/page.ny        — page model serialization
//   nyweb/src/middleware.ny  — timing and utility helpers
//
// ABI contract (matching the Ny @interop code-generator):
//   - text params arrive as  const char*
//   - text returns must be   const char*  pointing to thread-local stable memory
//   - i32  ↔  int32_t
//   - i64  ↔  int64_t
//   - bool ↔  bool
//
// All handle maps use int64_t keys issued by a global atomic counter.
// =============================================================================

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <fstream>
#include <memory>

// DLL export macro
#ifdef _WIN32
  #define NY_EXPORT __declspec(dllexport)
#else
  #define NY_EXPORT
#endif

// ---------------------------------------------------------------------------
// Thread-local string return buffer
// ---------------------------------------------------------------------------
// All functions that return const char* write into this buffer so callers get
// a stable pointer that lives until the next call on the same thread.

static thread_local std::string tl_return_buf;

static const char* tl_str(const std::string& s) {
    tl_return_buf = s;
    return tl_return_buf.c_str();
}

static const char* tl_str(std::string&& s) {
    tl_return_buf = std::move(s);
    return tl_return_buf.c_str();
}

// ---------------------------------------------------------------------------
// Handle counter
// ---------------------------------------------------------------------------

static std::atomic<int64_t> g_next_handle{1};
static int64_t next_handle() { return g_next_handle.fetch_add(1); }

// ---------------------------------------------------------------------------
// Template AST structures
// ---------------------------------------------------------------------------

enum class template_node_type {
    TEXT,
    EXPRESSION,
    RAW_EXPRESSION,
    STATEMENT,
    INCLUDE,
    LAYOUT,
    BODY,   // <%@ body %> — content injection point in layout files
    END
};

struct template_node {
    template_node_type type;
    std::string text;           // For TEXT nodes
    std::string expr;           // For EXPRESSION/RAW_EXPRESSION/STATEMENT nodes
    std::string include_path;   // For INCLUDE nodes
    std::string layout_path;    // For LAYOUT nodes
    std::string path;           // Originating template file path
    std::vector<template_node> children;  // Reserved for STATEMENT blocks
};

struct compiled_template {
    std::string path;
    std::string source;
    std::vector<template_node> nodes;
};

// ---------------------------------------------------------------------------
// Template cache
// ---------------------------------------------------------------------------

struct template_cache_entry {
    std::string path;
    compiled_template compiled;
    int64_t last_modified;
};

struct template_cache {
    std::vector<template_cache_entry> entries;
    std::string templates_dir;
    bool auto_reload;
};

static std::mutex g_cache_mu;
static template_cache g_template_cache;

// ---------------------------------------------------------------------------
// HTML escape
// ---------------------------------------------------------------------------

static std::string html_escape(const std::string& input) {
    std::ostringstream escaped;
    for (char c : input) {
        switch (c) {
            case '&':  escaped << "&amp;"; break;
            case '<':  escaped << "&lt;"; break;
            case '>':  escaped << "&gt;"; break;
            case '"':  escaped << "&quot;"; break;
            case '\'': escaped << "&#39;"; break;
            default:   escaped << c; break;
        }
    }
    return escaped.str();
}

// ---------------------------------------------------------------------------
// Read template file
// ---------------------------------------------------------------------------

static std::string read_template_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open template file: " + path);
    }
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

// ---------------------------------------------------------------------------
// Parse .nyhtml file
//
// Tag types:
//   <%= expr %>                  → EXPRESSION  (HTML-escaped output)
//   <%! expr %>                  → RAW_EXPRESSION (unescaped output)
//   <% stmt %>                   → STATEMENT   (code; no output)
//   <%@ include "path.nyhtml" %> → INCLUDE
//   <%@ layout  "path.nyhtml" %> → LAYOUT
//   <%@ body %>                  → BODY (injection point in layouts)
// ---------------------------------------------------------------------------

static std::vector<template_node> parse_template(const std::string& source, const std::string& path) {
    std::vector<template_node> nodes;
    std::string::size_type pos = 0;
    size_t line = 1;

    while (pos < source.length()) {
        std::string::size_type tag_start = source.find("<%", pos);

        if (tag_start == std::string::npos) {
            if (pos < source.length()) {
                nodes.push_back({template_node_type::TEXT, source.substr(pos), "", "", "", path, {}});
            }
            break;
        }

        // Text before the tag
        if (tag_start > pos) {
            nodes.push_back({template_node_type::TEXT, source.substr(pos, tag_start - pos), "", "", "", path, {}});
            // Count newlines for line tracking
            for (std::string::size_type i = pos; i < tag_start; ++i) {
                if (source[i] == '\n') ++line;
            }
        }

        std::string::size_type tag_end = source.find("%>", tag_start + 2);
        if (tag_end == std::string::npos) {
            throw std::runtime_error("Unclosed template tag at " + path + ":" + std::to_string(line));
        }

        // tag_content = everything between <% and %>
        std::string tag_content = source.substr(tag_start + 2, tag_end - tag_start - 2);
        pos = tag_end + 2;
        ++line;

        if (tag_content.empty()) continue;

        if (tag_content[0] == '=') {
            // <%= expr %> — HTML-escaped expression
            std::string expr = tag_content.substr(1);
            // ltrim
            std::string::size_type s = expr.find_first_not_of(" \t");
            if (s != std::string::npos) expr = expr.substr(s);
            nodes.push_back({template_node_type::EXPRESSION, "", expr, "", "", path, {}});

        } else if (tag_content[0] == '!') {
            // <%! expr %> — raw (unescaped) expression
            std::string expr = tag_content.substr(1);
            std::string::size_type s = expr.find_first_not_of(" \t");
            if (s != std::string::npos) expr = expr.substr(s);
            nodes.push_back({template_node_type::RAW_EXPRESSION, "", expr, "", "", path, {}});

        } else if (tag_content[0] == '@') {
            // <%@ directive %> — parse directive keyword after '@' + whitespace
            std::string directive = tag_content.substr(1);
            std::string::size_type ds = directive.find_first_not_of(" \t");
            if (ds != std::string::npos) directive = directive.substr(ds);

            if (directive.size() >= 7 && directive.substr(0, 7) == "include") {
                // <%@ include "path.nyhtml" %>
                std::string::size_type q1 = directive.find('"', 7);
                std::string::size_type q2 = (q1 != std::string::npos)
                    ? directive.find('"', q1 + 1) : std::string::npos;
                if (q1 == std::string::npos || q2 == std::string::npos) {
                    throw std::runtime_error("Invalid @include at " + path + ":" + std::to_string(line));
                }
                std::string inc_path = directive.substr(q1 + 1, q2 - q1 - 1);
                nodes.push_back({template_node_type::INCLUDE, "", "", inc_path, "", path, {}});

            } else if (directive.size() >= 6 && directive.substr(0, 6) == "layout") {
                // <%@ layout "path.nyhtml" %>
                std::string::size_type q1 = directive.find('"', 6);
                std::string::size_type q2 = (q1 != std::string::npos)
                    ? directive.find('"', q1 + 1) : std::string::npos;
                if (q1 == std::string::npos || q2 == std::string::npos) {
                    throw std::runtime_error("Invalid @layout at " + path + ":" + std::to_string(line));
                }
                std::string lay_path = directive.substr(q1 + 1, q2 - q1 - 1);
                nodes.push_back({template_node_type::LAYOUT, "", "", "", lay_path, path, {}});

            } else if (directive.size() >= 4 && directive.substr(0, 4) == "body") {
                // <%@ body %> — content injection point
                nodes.push_back({template_node_type::BODY, "", "", "", "", path, {}});

            } else {
                throw std::runtime_error("Unknown template directive '" + directive +
                    "' at " + path + ":" + std::to_string(line));
            }

        } else {
            // <% stmt %> — statement (control flow; no output yet)
            std::string stmt = tag_content;
            std::string::size_type s = stmt.find_first_not_of(" \t");
            if (s != std::string::npos) stmt = stmt.substr(s);
            nodes.push_back({template_node_type::STATEMENT, "", stmt, "", "", path, {}});
        }
    }

    return nodes;
}

// ---------------------------------------------------------------------------
// Path resolution
//
// Template paths in directives are resolved relative to templates_dir.
// Absolute paths (starting with '/') are used as-is.
// ---------------------------------------------------------------------------

static std::string resolve_template_path(const std::string& ref_path,
                                          const std::string& templates_dir) {
    if (ref_path.empty()) return ref_path;
    if (ref_path[0] == '/') return ref_path;
    if (templates_dir.empty()) return ref_path;
    return templates_dir + "/" + ref_path;
}

// ---------------------------------------------------------------------------
// Layout-aware render pipeline
//
// render_nodes():
//   Renders a flat node list into an HTML string.
//   - body_content: injected verbatim at every BODY node
//   - layout_out:   set to the layout path if a LAYOUT node is encountered
//   - INCLUDE nodes render their content inline (layout directives inside
//     includes are silently ignored to avoid double-wrapping)
//
// render_template_with_body():
//   Loads + parses a template file, calls render_nodes(), then wraps the
//   result in its layout (recursively) if a LAYOUT directive was found.
//   This drives the layout inheritance chain:
//
//     page.nyhtml       → page_content
//       <%@ layout "layouts/main.nyhtml" %>
//     layouts/main.nyhtml   renders with body=page_content → final_html
//       <%@ body %>
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Flat JSON value extractor
//
// Extracts a string or scalar value for a given key from a flat JSON object
// of the form {"key": "value", "count": 42, "flag": true}.
// Only top-level key lookup is needed for Phase 1.8 template variables.
// ---------------------------------------------------------------------------

static std::string json_get(const std::string& json, const std::string& key_raw) {
    // Trim leading and trailing whitespace from the key
    std::string key = key_raw;
    std::string::size_type ks = key.find_first_not_of(" \t\r\n");
    std::string::size_type ke = key.find_last_not_of(" \t\r\n");
    if (ks == std::string::npos) return "";  // blank key
    key = key.substr(ks, ke - ks + 1);

    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";

    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return "";
    pos++;

    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.size()) return "";

    if (json[pos] == '"') {
        pos++;
        std::string result;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                ++pos;
                switch (json[pos]) {
                    case '"':  result += '"';  break;
                    case '\\': result += '\\'; break;
                    case 'n':  result += '\n'; break;
                    case 't':  result += '\t'; break;
                    default:   result += json[pos]; break;
                }
            } else {
                result += json[pos];
            }
            pos++;
        }
        return result;
    } else {
        // Number, bool, or null — take until comma or closing brace
        auto end = json.find_first_of(",}\n", pos);
        std::string val = json.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        while (!val.empty() && (val.back() == ' ' || val.back() == '\t')) val.pop_back();
        return val;
    }
}

// Forward declaration (render_nodes and render_template_with_body are mutually
// used: render_nodes handles INCLUDE by calling render_template_with_body).
static std::string render_template_with_body(const std::string& path,
                                              const std::string& model_json,
                                              const std::string& body_content,
                                              const std::string& templates_dir);

static std::string render_nodes(const std::vector<template_node>& nodes,
                                 const std::string& model_json,
                                 const std::string& body_content,
                                 const std::string& templates_dir,
                                 std::string& layout_out) {
    std::ostringstream out;

    for (const auto& node : nodes) {
        switch (node.type) {

            case template_node_type::TEXT:
                out << node.text;
                break;

            case template_node_type::BODY:
                // Inject caller-supplied body content (used by layout files)
                out << body_content;
                break;

            case template_node_type::LAYOUT:
                // Record layout path; do not emit anything.
                // Only the first LAYOUT directive wins.
                if (layout_out.empty()) {
                    layout_out = node.layout_path;
                }
                break;

            case template_node_type::INCLUDE: {
                // Render included template inline.
                // Layout directives inside includes are ignored (layout_out discarded).
                std::string inc_path = resolve_template_path(node.include_path, templates_dir);
                try {
                    std::string inc_source = read_template_file(inc_path);
                    auto inc_nodes = parse_template(inc_source, inc_path);
                    std::string ignored_layout;
                    out << render_nodes(inc_nodes, model_json, body_content,
                                        templates_dir, ignored_layout);
                } catch (const std::exception& e) {
                    out << "<!-- ERROR including " << inc_path << ": " << e.what() << " -->";
                }
                break;
            }

            case template_node_type::EXPRESSION:
                out << html_escape(json_get(model_json, node.expr));
                break;

            case template_node_type::RAW_EXPRESSION:
                out << json_get(model_json, node.expr);
                break;

            case template_node_type::STATEMENT:
                // Control flow statements not yet evaluated (Phase 1.9).
                // Silently skip — do not emit placeholder comments in HTML.
                break;

            case template_node_type::END:
                break;
        }
    }

    return out.str();
}

static std::string render_template_with_body(const std::string& path,
                                              const std::string& model_json,
                                              const std::string& body_content,
                                              const std::string& templates_dir) {
    std::string source = read_template_file(path);
    auto nodes = parse_template(source, path);

    std::string layout_path;
    std::string rendered = render_nodes(nodes, model_json, body_content, templates_dir, layout_path);

    if (!layout_path.empty()) {
        // Wrap the rendered content in the layout.
        // The layout receives our rendered output as its body_content,
        // and may itself reference another layout (nested layout support).
        std::string full_layout_path = resolve_template_path(layout_path, templates_dir);
        return render_template_with_body(full_layout_path, model_json, rendered, templates_dir);
    }

    return rendered;
}

// ---------------------------------------------------------------------------
// Native bindings
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Thread-local error buffer (set on any failure; read via ml_nyweb_last_error)
// ---------------------------------------------------------------------------

static thread_local std::string tl_error_buf;

extern "C" {

// =============================================================================
// ABI contract (@interop rules):
//   text params    → const char*
//   text returns   → const char* into tl_return_buf (thread-local, stable)
//   i64 returns    → int64_t
//   void functions → no return value
//
// On error, functions return NULL/"" and set tl_error_buf.
// Call ml_nyweb_last_error() to retrieve the message.
// =============================================================================

// Returns the last error message from any ml_nyweb function (thread-local).
NY_EXPORT const char* ml_nyweb_last_error() {
    return tl_error_buf.c_str();
}

// Set the templates base directory.
// Include and layout paths in .nyhtml files are resolved relative to this dir.
NY_EXPORT void ml_template_set_dir(const char* templates_dir) {
    std::lock_guard<std::mutex> lock(g_cache_mu);
    g_template_cache.templates_dir = templates_dir ? templates_dir : "";
}

// Render a .nyhtml template with full layout/include/body inheritance.
// model_json: flat JSON object {"key": "value", ...} for variable substitution.
// Returns the rendered HTML string on success (thread-local buffer).
// Returns empty string on error — call ml_nyweb_last_error() for details.
NY_EXPORT const char* ml_template_render(const char* template_path, const char* model_json) {
    try {
        std::string templates_dir;
        {
            std::lock_guard<std::mutex> lock(g_cache_mu);
            templates_dir = g_template_cache.templates_dir;
        }
        std::string resolved_path = resolve_template_path(
            template_path ? template_path : "", templates_dir);
        std::string rendered = render_template_with_body(
            resolved_path,
            model_json    ? model_json    : "{}",
            "",
            templates_dir);
        tl_error_buf.clear();
        return tl_str(std::move(rendered));
    } catch (const std::exception& e) {
        tl_error_buf = e.what();
        tl_return_buf.clear();
        return "";
    }
}

// HTML-escape a string (& < > " ' → entities).
NY_EXPORT const char* ml_html_escape(const char* raw) {
    return tl_str(html_escape(raw ? raw : ""));
}

// Returns the current wall-clock time in milliseconds since the Unix epoch.
NY_EXPORT int64_t ml_current_time_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()
    ).count();
}

// Build a minimal JSON model string from page type and data.
// For Phase 1.9+: replace with proper struct-to-JSON serialization.
NY_EXPORT const char* ml_page_to_json(const char* page_type, const char* page_data) {
    tl_return_buf = std::string("{\"type\":\"") + (page_type ? page_type : "") +
                    "\",\"data\":\"" + (page_data ? page_data : "") + "\"}";
    return tl_return_buf.c_str();
}

}  // extern "C"
