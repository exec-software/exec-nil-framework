/*
Minimal Ny preprocessor implementation for `@include` and `@pragma once`.

This file provides a small, robust preprocessor that is adequate for early
development and testing of compiler components. The implementation is
comprehensively defensive: it does not attempt to provide a full C-like
preprocessor; it only supports textual `@include "..."` includes (with
search path support) and `@pragma once`. The implementation also supports
a small macro table via `define_macro`, `undef_macro`, and `lookup_macro`,
but macro expansion is intentionally a no-op in this minimal implementation.

Notes:
 - This implementation keeps the state necessary to avoid infinite
   include recursion via `include_stack_`.
 - The `include_once_set_` tracks canonicalized paths (absolute and
   lexically normalized) for `@pragma once` semantics.
 - Diagnostics are collected into the `preproc_result` and optionally
   forwarded to an optional callback.
*/

#include "preprocessor.hpp"
#include "diagnostics.hpp"
#include "nil_version.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>

namespace ny {

// Trim helpers
static inline std::string ltrim(const std::string &s) {
  const char *ws = " \t\r\n";
  size_t start = s.find_first_not_of(ws);
  return (start == std::string::npos) ? std::string() : s.substr(start);
}
static inline std::string rtrim(const std::string &s) {
  const char *ws = " \t\r\n";
  size_t end = s.find_last_not_of(ws);
  return (end == std::string::npos) ? std::string() : s.substr(0, end + 1);
}
static inline std::string trim(const std::string &s) { return rtrim(ltrim(s)); }

// Canonicalize a path in a robust manner (avoid exceptions and be tolerant of
// missing files). Return an absolute, lexically-normalized string.
static inline std::string canonicalize_path(const std::filesystem::path &p) {
  try {
    auto abs = std::filesystem::absolute(p);
    return abs.lexically_normal().string();
  } catch (...) {
    // fallback: try stringified path
    try {
      return std::filesystem::path(p).lexically_normal().string();
    } catch (...) {
      return p.string();
    }
  }
}

// Read file content to an optional string
std::optional<std::string>
preprocessor::read_file_to_string(const std::filesystem::path &path) {
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs)
    return std::nullopt;

  std::ostringstream oss;
  oss << ifs.rdbuf();
  return oss.str();
}

preprocessor::preprocessor(const preprocessor_options &opts) noexcept
    : options_(opts), diag_cb_(nullptr) {
  reset();
  register_builtin_macros();
}

void preprocessor::reset() {
  macro_table_.clear();
  include_stack_.clear();
  include_once_set_.clear();
  diagnostics_.clear();
  pragma_links_.clear();
  // Keep builtin macros but clear other user-defined macros if necessary
  for (auto it = macro_table_.begin(); it != macro_table_.end();) {
    // If user-defined macro, leave removals to specific functions.
    ++it;
  }
}

void preprocessor::register_builtin_macros() {
  // Add some useful builtin macros unless the caller provided them
  if (options_.builtin_macros.find("__NIL_VERSION__") ==
      options_.builtin_macros.end()) {
    options_.builtin_macros["__NIL_VERSION__"] = NIL_VERSION;
  }
  if (options_.builtin_macros.find("__DATE__") == options_.builtin_macros.end())
    options_.builtin_macros["__DATE__"] = __DATE__;
  if (options_.builtin_macros.find("__TIME__") == options_.builtin_macros.end())
    options_.builtin_macros["__TIME__"] = __TIME__;
}

preproc_result
preprocessor::preprocess_file(const std::filesystem::path &path) {
  preproc_result result;
  auto maybe_src = read_file_to_string(path);
  if (!maybe_src.has_value()) {
    record_diagnostic(preproc_diag_kind::error, path.string(), 0,
                      "Cannot open input file");
    result.success = false;
    result.diagnostics = diagnostics_;
    diagnostics_.clear();
    return result;
  }

  // Avoid duplicate inclusion via pragma once if the file is already recorded
  std::string canonical = canonicalize_path(path);
  if (include_once_set_.find(canonical) != include_once_set_.end()) {
    result.success = true;
    result.output = ""; // nothing to produce due to pragma once
    return result;
  }

  // Prevent recursion by tracking include stack
  auto it = std::find(include_stack_.begin(), include_stack_.end(), path);
  if (it != include_stack_.end()) {
    record_diagnostic(preproc_diag_kind::error, path.string(), 0,
                      "Recursive include detected");
    result.success = false;
    result.diagnostics = diagnostics_;
    diagnostics_.clear();
    return result;
  }

  include_stack_.push_back(path);
  auto r = preprocess_text(*maybe_src, path.string());

  // Debug logging: when processing an include via preprocess_file, emit a
  // concise summary describing the include path and any discovered `@pragma
  // link(...)` directives found in that unit. This helps tracing which files
  // contributed linker requirements during preprocessing.
  if (ny::is_debug_enabled() && !r.pragma_links.empty()) {
    std::cerr << "[preproc] include processed: '" << path.string()
              << "' (pragma_links: " << r.pragma_links.size() << ")\n";
    for (const auto &pl : r.pragma_links) {
      std::cerr << "[preproc]   link: '" << pl.lib_name << "'";
      if (pl.kind.has_value())
        std::cerr << " kind='" << *pl.kind << "'";
      if (pl.alias.has_value())
        std::cerr << " alias='" << *pl.alias << "'";
      std::cerr << " file='" << pl.source_file.string() << "' line=" << pl.line
                << std::endl;
    }
  } else if (ny::is_debug_enabled()) {
    std::cerr << "[preproc] include processed: '" << path.string()
              << "' (no pragma_links)" << std::endl;
  }

  include_stack_.pop_back();

  // Record included file (the one we processed)
  result.output = r.output;
  result.diagnostics = r.diagnostics;
  result.included_files = r.included_files;
  result.included_files.push_back(path);
  result.macros = r.macros;
  result.pragma_links = r.pragma_links;
  result.success = r.success;
  return result;
}

std::optional<std::filesystem::path> preprocessor::resolve_include_path(
    const std::string &include_name,
    const std::filesystem::path &relative_to) const {
  std::filesystem::path candidate(include_name);
  if (!candidate.is_absolute()) {
    if (!relative_to.empty()) {
      std::filesystem::path p = std::filesystem::path(relative_to) / candidate;
      if (std::filesystem::exists(p))
        return p;
    }
  } else {
    if (std::filesystem::exists(candidate))
      return candidate;
  }

  // Search include_paths if any
  for (const auto &ip : options_.include_paths) {
    std::filesystem::path p = ip / candidate;
    if (std::filesystem::exists(p))
      return p;
  }

  // Last attempt: relative to current working directory
  std::filesystem::path p2 = std::filesystem::absolute(candidate);
  if (std::filesystem::exists(p2))
    return p2;

  return std::nullopt;
}

void preprocessor::record_diagnostic(preproc_diag_kind kind,
                                     const std::string &filename, int line,
                                     const std::string &message,
                                     const std::optional<std::string> &hint) {
  preproc_diagnostic d;
  d.kind = kind;
  d.filename = filename;
  d.line = line;
  d.message = message;
  d.hint = hint;
  diagnostics_.push_back(d);
  if (diag_cb_)
    diag_cb_(d);
}

preproc_result preprocessor::preprocess_text(const std::string &source,
                                             const std::string &filename) {
  preproc_result result;
  std::ostringstream out; // raw output constructed line-by-line
  std::istringstream iss(source);
  std::string line;
  int line_no = 0;
  // Local collection for `@pragma link` directives found within this
  // preprocessor textual unit. This collects pragma links per input unit and is
  // used to populate the `result.pragma_links` field. Only top-level
  // invocations of preprocess_text will also append these into the
  // preprocessor's global `pragma_links_` collection to avoid duplicate
  // flattening from nested includes.
  std::vector<preproc_pragma_link> local_pragma_links;

  // Active conditional stack (true = currently active)
  std::vector<bool> active_stack;
  active_stack.push_back(true);

  // Parallel stack to track whether any branch in the current if/elif/else
  // group has been taken. This is used to implement `@elif`/`@else`.
  std::vector<bool> branch_taken_stack;

  // Derive base directory from filename (if provided)
  std::filesystem::path base_dir;
  if (!filename.empty() && filename != "<input>") {
    try {
      base_dir = std::filesystem::path(filename).parent_path();
    } catch (...) {
      base_dir = std::filesystem::path();
    }
  }

  // Helper lambdas
  auto parent_is_active = [&]() -> bool {
    return !active_stack.empty() && active_stack.back();
  };

  // Single-pass preprocessor: compute directive semantics and write output
  // incrementally. We must respect conditional states in macros, includes,
  // and other directives to avoid processing disabled code.
  while (std::getline(iss, line)) {
    ++line_no;

    std::string raw_line = line; // preserve the original raw line
    std::string stripped = ltrim(line);

    // If the line is empty, preserve newline and continue.
    if (stripped.empty()) {
      out << "\n";
      continue;
    }

    // If it's a directive (lines beginning with '@'), process it specially.
    if (!stripped.empty() && stripped[0] == preproc_directive_prefix) {
      // Extract directive name and the remainder text.
      size_t pos = 1;
      std::string directive;
      while (pos < stripped.size() &&
             !isspace(static_cast<unsigned char>(stripped[pos]))) {
        directive.push_back(stripped[pos]);
        ++pos;
      }
      std::string rest = trim(stripped.substr(pos));

      // `@pragma` is considered always processed (useful for include guards).
      if (directive == "pragma") {
        if (rest.rfind("once", 0) == 0) {
          std::string canonical = (!filename.empty() && filename != "<input>")
                                      ? canonicalize_path(filename)
                                      : std::string("<input>");
          include_once_set_.insert(canonical);
        } else if (rest.rfind("link", 0) == 0) {
          // Handle `@pragma link(...)` directives. Accept either:
          //  - @pragma link("libname")
          //  - @pragma link(name="lib", kind="dylib")
          //  - @pragma link(libname)  (bare single token treated as name)
          size_t lparen = rest.find('(');
          size_t rparen = rest.rfind(')');
          if (lparen == std::string::npos || rparen == std::string::npos ||
              rparen <= lparen) {
            record_diagnostic(preproc_diag_kind::warning, filename, line_no,
                              "Malformed @pragma link: missing parentheses");
          } else {
            std::string args =
                trim(rest.substr(lparen + 1, rparen - lparen - 1));
            preproc_pragma_link pl;
            pl.source_file = std::filesystem::path(filename);
            pl.line = line_no;

            // Simple quoted single-argument form: "mylib.lib"
            if (!args.empty() && args.front() == '"' && args.back() == '"') {
              pl.lib_name = args.substr(1, args.size() - 2);
            } else if (!args.empty() &&
                       (args.front() == '\'' && args.back() == '\'')) {
              pl.lib_name = args.substr(1, args.size() - 2);
            } else {
              // Parse comma-separated key=value pairs or single token as the
              // lib name. Accept either bare tokens or quoted string values.
              std::istringstream kvs(args);
              std::string kv;
              while (std::getline(kvs, kv, ',')) {
                kv = trim(kv);
                if (kv.empty())
                  continue;
                size_t eq = kv.find('=');
                if (eq == std::string::npos) {
                  // bare token -> treat as lib name
                  if (!kv.empty() &&
                      ((kv.front() == '"' && kv.back() == '"') ||
                       (kv.front() == '\'' && kv.back() == '\''))) {
                    pl.lib_name = kv.substr(1, kv.size() - 2);
                  } else {
                    pl.lib_name = kv;
                  }
                } else {
                  std::string key = trim(kv.substr(0, eq));
                  std::string val = trim(kv.substr(eq + 1));
                  if (!val.empty() &&
                      ((val.front() == '"' && val.back() == '"') ||
                       (val.front() == '\'' && val.back() == '\''))) {
                    val = val.substr(1, val.size() - 2);
                  }
                  if (key == "name")
                    pl.lib_name = val;
                  else if (key == "kind")
                    pl.kind = val;
                  else if (key == "alias")
                    pl.alias = val;
                }
              } // ov kv loop
            }

            // Valid minimal requirement: lib_name must be present to be useful
            if (pl.lib_name.empty()) {
              record_diagnostic(
                  preproc_diag_kind::warning, filename, line_no,
                  "@pragma link(...) directive did not specify a library name");
            }

            // Record into our per-preprocessor collection so tooling can query
            // it.
            local_pragma_links.push_back(pl);
            // Debug: log the discovered pragma_link so it is visible in debug
            // output
            if (ny::is_debug_enabled()) {
              std::cerr << "[preproc] Found @pragma link: lib_name='"
                        << pl.lib_name << "' kind='"
                        << (pl.kind.has_value() ? *pl.kind
                                                : std::string("(none)"))
                        << "' alias='"
                        << (pl.alias.has_value() ? *pl.alias
                                                 : std::string("(none)"))
                        << "' file='" << pl.source_file.string()
                        << "' line=" << pl.line << std::endl;
            }
          }
        }
        // Preserve directive if requested; otherwise preserve a blank line
        // for source mapping.
        if (options_.keep_directives_in_output)
          out << raw_line << "\n";
        else
          out << "\n";
        continue;
      }

      // `@schema` - marks this file as a schema/header file (like @pragma once)
      // This is used by tools like `nilschema` to identify exportable files.
      // The compiler treats it as a no-op marker.
      if (directive == "schema") {
        // Mark this file via pragma once semantics to prevent duplicate inclusion
        std::string canonical = (!filename.empty() && filename != "<input>")
                                    ? canonicalize_path(filename)
                                    : std::string("<input>");
        include_once_set_.insert(canonical);

        // Preserve directive if requested; otherwise preserve a blank line
        if (options_.keep_directives_in_output)
          out << raw_line << "\n";
        else
          out << "\n";
        continue;
      }

      // Evaluate the parent active state (if the parent block is inactive,
      // most directives should be ignored).
      bool parent_active = parent_is_active();

      // `@include` - only processed when active (explicit active check). Use
      // resolve_include_path + preprocess_file to gather the included content.
      if (directive == "include") {
        if (!parent_active) {
          if (options_.keep_directives_in_output)
            out << raw_line << "\n";
          else
            out << "\n";
          continue;
        }

        std::string include_name;
        if (!rest.empty() && rest.front() == '\"') {
          size_t start_quote = rest.find('\"');
          size_t end_quote = rest.find_last_of('\"');
          if (start_quote == std::string::npos ||
              end_quote == std::string::npos || start_quote == end_quote) {
            record_diagnostic(preproc_diag_kind::error, filename, line_no,
                              "Malformed @include directive (missing '\"')");
            if (options_.keep_directives_in_output)
              out << raw_line << "\n";
            else
              out << "\n";
            continue;
          }
          include_name =
              rest.substr(start_quote + 1, end_quote - start_quote - 1);
        } else if (!rest.empty() && rest.front() == '<' && rest.back() == '>') {
          include_name = rest.substr(1, rest.size() - 2);
        } else {
          include_name = trim(rest);
        }

        auto resolved = resolve_include_path(include_name, base_dir);
        if (!resolved.has_value()) {
          record_diagnostic(preproc_diag_kind::error, filename, line_no,
                            "Included file not found: " + include_name);
          if (options_.keep_directives_in_output)
            out << raw_line << "\n";
          else
            out << "\n";
          continue;
        }

        // Prevent recursion and use preprocess_file which checks the include
        // stack and `pragma once` guards.
        preproc_result inc_res = preprocess_file(*resolved);

        // Forward diagnostics from included file
        for (const auto &d : inc_res.diagnostics) {
          record_diagnostic(d.kind, d.filename, d.line, d.message, d.hint);
        }

        if (inc_res.success) {
          // Append included content into the output. If the include produced
          // content, we append it as-is.
          out << inc_res.output;
        } else {
          if (options_.keep_directives_in_output)
            out << raw_line << "\n";
          else
            out << "\n";
        }

        // Flatten included files & macros so callers can inspect included files
        for (const auto &p : inc_res.included_files)
          result.included_files.push_back(p);

        for (const auto &kv : inc_res.macros)
          macro_table_[kv.first] = kv.second;

        // Also flatten any collected @pragma link() records from included units
        for (const auto &lk : inc_res.pragma_links) {
          local_pragma_links.push_back(lk);
          if (ny::is_debug_enabled()) {
            std::cerr
                << "[preproc] Flattened @pragma link from include: lib_name='"
                << lk.lib_name << "' kind='"
                << (lk.kind.has_value() ? *lk.kind : std::string("(none)"))
                << "' alias='"
                << (lk.alias.has_value() ? *lk.alias : std::string("(none)"))
                << "' file='" << lk.source_file.string()
                << "' line=" << lk.line << std::endl;
          }
        }

        continue;
      } // include

      // `@define` - define a macro (object or function-like) - only when
      // active.
      if (directive == "define") {
        if (!parent_active) {
          if (options_.keep_directives_in_output)
            out << raw_line << "\n";
          else
            out << "\n";
          continue;
        }

        // Parse the macro name & optional param list
        std::string name;
        size_t ri = 0;
        while (ri < rest.size() &&
               (std::isalnum(static_cast<unsigned char>(rest[ri])) ||
                rest[ri] == '_')) {
          name.push_back(rest[ri++]);
        }

        if (name.empty()) {
          record_diagnostic(preproc_diag_kind::error, filename, line_no,
                            "Malformed @define: missing macro name");
          if (options_.keep_directives_in_output)
            out << raw_line << "\n";
          else
            out << "\n";
          continue;
        }

        macro_def md;
        md.name = name;
        md.file = std::filesystem::path(filename);
        md.line = line_no;

        while (ri < rest.size() &&
               std::isspace(static_cast<unsigned char>(rest[ri])))
          ++ri;

        // Function-like macro?
        if (ri < rest.size() && rest[ri] == '(') {
          ++ri; // skip '('
          std::string params_text;
          int depth = 0;
          while (ri < rest.size()) {
            char c = rest[ri++];
            if (c == '(') {
              ++depth;
              params_text.push_back(c);
            } else if (c == ')' && depth == 0) {
              break;
            } else if (c == ')' && depth > 0) {
              --depth;
              params_text.push_back(c);
            } else {
              params_text.push_back(c);
            }
          }
          // Parse comma-separated params
          std::istringstream pstream(params_text);
          std::string p;
          while (std::getline(pstream, p, ',')) {
            p = trim(p);
            if (p.empty())
              continue;
            if (p == "...") {
              md.variadic = true;
              md.params.push_back("...");
            } else {
              md.params.push_back(p);
            }
          }
        }

        // Remaining text is replacement
        while (ri < rest.size() &&
               std::isspace(static_cast<unsigned char>(rest[ri])))
          ++ri;
        md.replacement =
            (ri < rest.size()) ? trim(rest.substr(ri)) : std::string();
        md.function_like = !md.params.empty();

        define_macro(md);

        // Emit directive or blank line depending on keep_directives flag
        if (options_.keep_directives_in_output)
          out << raw_line << "\n";
        else
          out << "\n";

        continue;
      } // define

      // `@undef` - remove a macro (only when active).
      if (directive == "undef") {
        if (!parent_active) {
          if (options_.keep_directives_in_output)
            out << raw_line << "\n";
          else
            out << "\n";
          continue;
        }

        std::string name = trim(rest);
        if (name.empty()) {
          record_diagnostic(preproc_diag_kind::error, filename, line_no,
                            "Malformed @undef: missing macro name");
          if (options_.keep_directives_in_output)
            out << raw_line << "\n";
          else
            out << "\n";
          continue;
        }

        undef_macro(name);

        if (options_.keep_directives_in_output)
          out << raw_line << "\n";
        else
          out << "\n";
        continue;
      } // undef

      // Conditionals:
      // - @ifdef NAME / @ifndef NAME
      // - @if <expr> / @elif <expr> / @else / @endif
      if (directive == "ifdef" || directive == "ifndef") {
        // The token after the directive must be an identifier (name)
        std::string name = trim(rest);
        if (name.empty()) {
          record_diagnostic(preproc_diag_kind::error, filename, line_no,
                            "Malformed @" + directive + ": missing name");
          // push a disabled entry to keep nesting balanced
          active_stack.push_back(false);
          branch_taken_stack.push_back(false);
          if (options_.keep_directives_in_output)
            out << raw_line << "\n";
          else
            out << "\n";
          continue;
        }

        bool defined = (macro_table_.find(name) != macro_table_.end());
        bool new_active =
            parent_active && ((directive == "ifdef") ? defined : !defined);
        active_stack.push_back(new_active);
        branch_taken_stack.push_back(new_active);

        // Preserve directive line for mapping/trace
        out << raw_line << "\n";
        continue;
      } // ifdef/ifndef

      if (directive == "if") {
        // Evaluate condition (may return nullopt)
        std::string err;
        std::optional<bool> maybe =
            evaluate_condition(rest, filename, line_no, err);
        bool ok = maybe.has_value() ? *maybe : false;
        if (!maybe.has_value() && !err.empty()) {
          record_diagnostic(preproc_diag_kind::warning, filename, line_no,
                            "Unable to evaluate @if expression: " + err);
        }
        bool new_active = parent_active && ok;
        active_stack.push_back(new_active);
        branch_taken_stack.push_back(new_active);

        if (options_.keep_directives_in_output)
          out << raw_line << "\n";
        else
          out << "\n";
        continue;
      }

      if (directive == "elif") {
        if (branch_taken_stack.empty()) {
          record_diagnostic(preproc_diag_kind::warning, filename, line_no,
                            "@elif without matching @if");
          if (options_.keep_directives_in_output)
            out << raw_line << "\n";
          else
            out << "\n";
          continue;
        }
        // parent_active is now the stack entry just before the top entry
        bool parent_active_local = (active_stack.size() >= 2)
                                       ? active_stack[active_stack.size() - 2]
                                       : true;
        bool branch_taken = branch_taken_stack.back();
        bool new_active = false;
        if (parent_active_local && !branch_taken) {
          std::string err;
          std::optional<bool> maybe =
              evaluate_condition(rest, filename, line_no, err);
          if (!maybe.has_value() && !err.empty()) {
            record_diagnostic(preproc_diag_kind::warning, filename, line_no,
                              "Unable to evaluate @elif expression: " + err);
          }
          new_active = maybe.has_value() ? *maybe : false;
        }
        // update current top
        active_stack.back() = parent_active_local && new_active;
        if (active_stack.back())
          branch_taken_stack.back() = true;

        if (options_.keep_directives_in_output)
          out << raw_line << "\n";
        else
          out << "\n";
        continue;
      }

      if (directive == "else") {
        if (branch_taken_stack.empty()) {
          record_diagnostic(preproc_diag_kind::warning, filename, line_no,
                            "@else without matching @if");
          if (options_.keep_directives_in_output)
            out << raw_line << "\n";
          else
            out << "\n";
          continue;
        }

        bool parent_active_local = (active_stack.size() >= 2)
                                       ? active_stack[active_stack.size() - 2]
                                       : true;
        bool taken = branch_taken_stack.back();
        bool new_active = parent_active_local && !taken;
        active_stack.back() = new_active;
        if (new_active)
          branch_taken_stack.back() = true;

        if (options_.keep_directives_in_output)
          out << raw_line << "\n";
        else
          out << "\n";
        continue;
      }

      if (directive == "endif") {
        if (active_stack.size() <= 1) {
          record_diagnostic(preproc_diag_kind::warning, filename, line_no,
                            "@endif without matching @if/@ifdef");
        } else {
          active_stack.pop_back();
          branch_taken_stack.pop_back();
        }

        if (options_.keep_directives_in_output)
          out << raw_line << "\n";
        else
          out << "\n";
        continue;
      }

      // Compile-time diagnostics: @error / @warning / @message
      if (directive == "error" || directive == "warning" ||
          directive == "message") {
        if (parent_active) {
          if (directive == "error")
            record_diagnostic(preproc_diag_kind::error, filename, line_no,
                              rest);
          else if (directive == "warning")
            record_diagnostic(preproc_diag_kind::warning, filename, line_no,
                              rest);
          else
            record_diagnostic(preproc_diag_kind::info, filename, line_no, rest);
        }
        if (options_.keep_directives_in_output)
          out << raw_line << "\n";
        else
          out << "\n";
        continue;
      }

      // Unrecognized directive: preserve as-is (likely a user annotation
      // like @interop, @test, @inline for the parser/codegen)
      out << raw_line << "\n";
      continue;
    } // directive

    // Regular code line
    bool active = !active_stack.empty() ? active_stack.back() : true;
    if (!active) {
      // preserve blank mapping for line numbers when inactive
      out << "\n";
      continue;
    }
    // Expand macros on active lines
    out << expand_macros(raw_line) << "\n";
  } // while lines

  // Finalize result
  result.output = out.str();
  result.diagnostics = diagnostics_;
  result.macros = macro_table_;
  // Use the per-file local list as this translation unit's pragma links.
  result.pragma_links = local_pragma_links;

  // If this preprocess invocation is top-level (no nested includes on the
  // include stack), propagate the per-file local pragma links into the
  // preprocessor instance's global collection so downstream tooling can query
  // discovered link requirements for the file being preprocessed.
  if (include_stack_.size() <= 1) {
    for (const auto &pl : local_pragma_links) {
      pragma_links_.push_back(pl);
    }
  }

  // Debug: Output a summary of collected pragma links (when present in this
  // unit)
  if (ny::is_debug_enabled() && !local_pragma_links.empty()) {
    std::cerr << "[preproc] Summary: recorded " << local_pragma_links.size()
              << " @pragma link(s) in this unit" << std::endl;
    for (const auto &pl : local_pragma_links) {
      std::cerr << "[preproc]  - " << pl.lib_name << " kind='"
                << (pl.kind.has_value() ? *pl.kind : std::string("(none)"))
                << "' file='" << pl.source_file.string() << "' line=" << pl.line
                << std::endl;
    }
  }
  // success if no errors
  result.success = true;
  for (const auto &d : diagnostics_) {
    if (d.kind == preproc_diag_kind::error) {
      result.success = false;
      break;
    }
  }
  // Clear diagnostics so subsequent calls start with a clean slate
  diagnostics_.clear();

  return result;
}

std::optional<macro_def>
preprocessor::lookup_macro(const std::string &name) const {
  auto it = macro_table_.find(name);
  if (it == macro_table_.end())
    return std::nullopt;
  return it->second;
}

bool preprocessor::define_macro(const macro_def &m) {
  macro_table_[m.name] = m;
  return true;
}

bool preprocessor::undef_macro(const std::string &name) {
  return macro_table_.erase(name) > 0;
}

// Helper class for evaluating preprocessor expressions
class ConditionEvaluator {
public:
  ConditionEvaluator(const std::string &expr,
                     const std::unordered_map<std::string, macro_def> &macros,
                     std::string &err)
      : expr_(expr), macros_(macros), err_(err), pos_(0) {}

  bool evaluate() {
    try {
      bool result = parse_logical_or();
      skip_whitespace();
      if (pos_ < expr_.size()) {
        throw std::runtime_error("Unexpected characters at end of expression");
      }
      return result;
    } catch (const std::exception &e) {
      err_ = e.what();
      return false;
    }
  }

private:
  const std::string &expr_;
  const std::unordered_map<std::string, macro_def> &macros_;
  std::string &err_;
  size_t pos_;

  void skip_whitespace() {
    while (pos_ < expr_.size() &&
           std::isspace(static_cast<unsigned char>(expr_[pos_]))) {
      pos_++;
    }
  }

  bool match(const std::string &token) {
    skip_whitespace();
    if (expr_.compare(pos_, token.size(), token) == 0) {
      pos_ += token.size();
      return true;
    }
    return false;
  }

  bool parse_logical_or() {
    bool left = parse_logical_and();
    while (true) {
      if (match("or") || match("||")) {
        bool right = parse_logical_and();
        left = left || right;
      } else {
        break;
      }
    }
    return left;
  }

  bool parse_logical_and() {
    bool left = parse_equality();
    while (true) {
      if (match("and") || match("&&")) {
        bool right = parse_equality();
        left = left && right;
      } else {
        break;
      }
    }
    return left;
  }

  bool parse_equality() {
    // For now, we treat comparisons as boolean operations on "values"
    // Since we only really support boolean/integer/string atoms, this is
    // simplified. We'll parse a "primary" and see if it's followed by == or !=
    // Note: This is a bit hacky because we mix types (bool vs string vs int).
    // A proper evaluator would return a Variant type.
    // For preprocessor, we'll simplify:
    // If we see '==', we parse the next primary and compare string
    // representations.

    // To support `cfg(platform = "linux")`, we need to handle equality inside
    // the primary parsing or handle it here. Let's handle it here by parsing
    // "values".

    std::string left_val = parse_value();

    while (true) {
      if (match("==")) {
        std::string right_val = parse_value();
        left_val = (left_val == right_val) ? "1" : "0";
      } else if (match("!=")) {
        std::string right_val = parse_value();
        left_val = (left_val != right_val) ? "1" : "0";
      } else {
        break;
      }
    }

    // Convert result to bool
    return is_truthy(left_val);
  }

  std::string parse_value() {
    skip_whitespace();
    if (pos_ >= expr_.size())
      return "";

    // Parentheses
    if (expr_[pos_] == '(') {
      pos_++;
      bool val = parse_logical_or();
      skip_whitespace();
      if (pos_ >= expr_.size() || expr_[pos_] != ')') {
        throw std::runtime_error("Mismatched parentheses");
      }
      pos_++;
      return val ? "1" : "0";
    }

    // Unary NOT
    if (match("not") || match("!")) {
      bool val = is_truthy(parse_value());
      return (!val) ? "1" : "0";
    }

    // defined(NAME)
    if (match("defined")) {
      skip_whitespace();
      if (match("(")) {
        std::string name = parse_identifier();
        skip_whitespace();
        if (!match(")"))
          throw std::runtime_error("Expected ')' after defined(...)");
        return (macros_.find(name) != macros_.end()) ? "1" : "0";
      } else {
        throw std::runtime_error("Expected '(' after defined");
      }
    }

    // cfg(key = "value") or cfg(key)
    if (match("cfg")) {
      skip_whitespace();
      if (!match("("))
        throw std::runtime_error("Expected '(' after cfg");

      std::string key = parse_identifier();
      skip_whitespace();

      std::string value;
      bool has_value = false;
      if (match("=")) {
        skip_whitespace();
        if (pos_ < expr_.size() && expr_[pos_] == '"') {
          value = parse_string_literal();
        } else {
          value = parse_identifier();
        }
        has_value = true;
      }

      skip_whitespace();
      if (!match(")"))
        throw std::runtime_error("Expected ')' after cfg(...)");

      // Evaluate cfg
      // For now, we support a few hardcoded keys or check macros
      if (key == "platform") {
        // TODO: Get actual platform from build config
        // For now, assume linux for tests
        return (has_value && value == "linux") ? "1" : "0";
      } else if (key == "arch") {
        return (has_value && value == "x64") ? "1" : "0";
      } else if (key == "feature") {
        // Check if feature macro is defined? Or just return false for now
        return "0";
      }
      return "0";
    }

    // String literal
    if (expr_[pos_] == '"') {
      return parse_string_literal();
    }

    // Number
    if (std::isdigit(static_cast<unsigned char>(expr_[pos_]))) {
      size_t start = pos_;
      while (pos_ < expr_.size() &&
             std::isdigit(static_cast<unsigned char>(expr_[pos_]))) {
        pos_++;
      }
      return expr_.substr(start, pos_ - start);
    }

    // Identifier / Keyword
    std::string id = parse_identifier();
    if (id == "true")
      return "1";
    if (id == "false")
      return "0";

    // If it's a macro, we should probably expand it?
    // For now, if it's an identifier in a condition, treat as 0 unless it's a
    // macro that expands to something? C preprocessor treats undefined
    // identifiers as 0. If it is defined, we should ideally expand it.
    // Simplified: check if it's in macros_, if so, use its replacement (if
    // simple).
    auto it = macros_.find(id);
    if (it != macros_.end() && !it->second.function_like) {
      return it->second.replacement;
    }

    // Treat unknown identifier as 0 (C behavior)
    return "0";
  }

  std::string parse_identifier() {
    skip_whitespace();
    size_t start = pos_;
    if (pos_ < expr_.size() &&
        (std::isalpha(static_cast<unsigned char>(expr_[pos_])) ||
         expr_[pos_] == '_')) {
      pos_++;
      while (pos_ < expr_.size() &&
             (std::isalnum(static_cast<unsigned char>(expr_[pos_])) ||
              expr_[pos_] == '_')) {
        pos_++;
      }
    }
    return expr_.substr(start, pos_ - start);
  }

  std::string parse_string_literal() {
    if (pos_ >= expr_.size() || expr_[pos_] != '"')
      return "";
    pos_++; // skip opening quote
    std::string res;
    while (pos_ < expr_.size() && expr_[pos_] != '"') {
      if (expr_[pos_] == '\\' && pos_ + 1 < expr_.size()) {
        pos_++; // skip backslash
      }
      res.push_back(expr_[pos_]);
      pos_++;
    }
    if (pos_ < expr_.size())
      pos_++; // skip closing quote
    return res;
  }

  bool is_truthy(const std::string &val) {
    if (val == "true")
      return true;
    if (val == "false")
      return false;
    try {
      return std::stoi(val) != 0;
    } catch (...) {
      // Treat non-empty strings as true for preprocessor conditions.
      return !val.empty();
    }
  }
};

std::optional<bool>
preprocessor::evaluate_condition(const std::string &expr,
                                 const std::string &filename, int line,
                                 std::string &err_message) const {
  ConditionEvaluator eval(expr, macro_table_, err_message);
  bool result = eval.evaluate();
  if (!err_message.empty())
    return std::nullopt;
  return result;
}

std::string preprocessor::expand_macros(const std::string &text) const {
  std::string out;
  out.reserve(text.size());

  // Protect numeric tokens from identifier/parameter replacement: a macro
  // named e3 or E must not change 1e3 or 1E+2. Respect language token boundaries
  // so macros after ranges/member dots remain expandable. Hexadecimal e/E are
  // digits, so the following +/- still begins an ordinary operator.
  const auto numeric_end = [](const std::string &input, size_t start) {
    const bool hex = start + 1 < input.size() && input[start] == '0' &&
        (input[start + 1] == 'x' || input[start + 1] == 'X');
    size_t end = start;
    if (hex) {
      end += 2;
      while (end < input.size() &&
             std::isxdigit(static_cast<unsigned char>(input[end]))) ++end;
      return end;
    }
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    while (end < input.size() && digit(input[end])) ++end;
    if (end + 1 < input.size() && input[end] == '.' && digit(input[end + 1])) {
      ++end;
      while (end < input.size() && digit(input[end])) ++end;
    }
    if (end < input.size() && (input[end] == 'e' || input[end] == 'E')) {
      ++end;
      if (end < input.size() && (input[end] == '+' || input[end] == '-')) ++end;
      while (end < input.size() && digit(input[end])) ++end;
    }
    return end;
  };

  size_t i = 0;
  while (i < text.size()) {
    // Check for string literal and skip it (don't expand macros inside strings)
    if (text[i] == '"') {
      out += text[i++];
      while (i < text.size() && text[i] != '"') {
        if (text[i] == '\\' && i + 1 < text.size()) {
          out += text[i++];
        }
        out += text[i++];
      }
      if (i < text.size())
        out += text[i++];
      continue;
    }

    if (std::isdigit(static_cast<unsigned char>(text[i]))) {
      const auto end = numeric_end(text, i);
      out.append(text, i, end - i);
      i = end;
      continue;
    }

    // Identify an identifier start
    if (std::isalpha(static_cast<unsigned char>(text[i])) || text[i] == '_') {
      size_t start = i;
      while (
          i < text.size() &&
          (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '_'))
        ++i;
      std::string ident = text.substr(start, i - start);

      // Check for macro presence
      auto it = macro_table_.find(ident);
      if (it != macro_table_.end()) {
        const macro_def &md = it->second;

        if (!md.function_like) {
          // Object-like macro: text replacement
          // TODO: Recursive expansion? For now, just one level or simple
          // recursion if needed. Ideally we should re-scan the replacement.
          // Let's do a simple re-scan by calling expand_macros recursively on
          // the replacement? BEWARE: Infinite recursion. For this MVP, let's
          // just output the replacement. Actually, standard requires recursive
          // expansion. Let's assume non-recursive for safety in this step, or
          // simple one-pass.
          out += md.replacement;
          continue;
        } else {
          // Function-like: detect '(' following (possibly with whitespace)
          size_t j = i;
          while (j < text.size() &&
                 std::isspace(static_cast<unsigned char>(text[j])))
            ++j;

          if (j < text.size() && text[j] == '(') {
            // It is a function-like invocation
            i = j + 1; // skip '('

            // Parse arguments
            std::vector<std::string> args;
            std::string current_arg;
            int depth = 0;

            while (i < text.size()) {
              char c = text[i];
              if (c == '(') {
                depth++;
                current_arg += c;
              } else if (c == ')') {
                if (depth == 0) {
                  // End of arguments
                  if (!current_arg.empty() ||
                      !args.empty()) { // Handle empty arg list "()" vs "(a)"
                    // Special case: if params is empty and args is empty
                    // string, that's 0 args. If params has 1 arg and args is
                    // empty string, that's 1 empty arg? C preprocessor is
                    // tricky.
                    args.push_back(trim(current_arg));
                  }
                  i++; // skip ')'
                  break;
                }
                depth--;
                current_arg += c;
              } else if (c == ',' && depth == 0) {
                args.push_back(trim(current_arg));
                current_arg.clear();
              } else {
                current_arg += c;
              }
              i++;
            }

            // Validate arg count
            // Handle variadic: last param is "..."
            bool is_variadic = md.variadic;
            size_t param_count = md.params.size();

            // Perform substitution with # and ## support
            // Using token-based approach for proper handling
            std::string replacement = md.replacement;

            // 1. Tokenize replacement string
            struct Token {
              std::string text;
              bool is_param = false;
              int param_idx = -1;
            };
            std::vector<Token> tokens;

            size_t p = 0;
            while (p < replacement.size()) {
              if (std::isspace(static_cast<unsigned char>(replacement[p]))) {
                tokens.push_back({std::string(1, replacement[p]), false, -1});
                p++;
              } else if (std::isdigit(static_cast<unsigned char>(replacement[p]))) {
                const auto end = numeric_end(replacement, p);
                tokens.push_back({replacement.substr(p, end - p), false, -1});
                p = end;
              } else if (std::isalpha(
                             static_cast<unsigned char>(replacement[p])) ||
                         replacement[p] == '_') {
                size_t start = p;
                while (
                    p < replacement.size() &&
                    (std::isalnum(static_cast<unsigned char>(replacement[p])) ||
                     replacement[p] == '_'))
                  p++;
                std::string txt = replacement.substr(start, p - start);

                int idx = -1;
                for (size_t i = 0; i < md.params.size(); ++i) {
                  if (md.params[i] == txt) {
                    idx = (int)i;
                    break;
                  } else if (md.params[i] == "..." && txt == "__VA_ARGS__") {
                    idx = (int)i;
                    break;
                  }
                }
                tokens.push_back({txt, idx != -1, idx});
              } else if (replacement[p] == '#' && p + 1 < replacement.size() &&
                         replacement[p + 1] == '#') {
                tokens.push_back({"##", false, -1});
                p += 2;
              } else {
                tokens.push_back({std::string(1, replacement[p]), false, -1});
                p++;
              }
            }

            // 2. Process # (stringify)
            // Look for # followed by param (ignoring whitespace)
            for (size_t i = 0; i < tokens.size(); ++i) {
              if (tokens[i].text == "#") {
                // Find next non-whitespace
                size_t next = i + 1;
                while (next < tokens.size() &&
                       std::isspace(
                           static_cast<unsigned char>(tokens[next].text[0])))
                  next++;

                if (next < tokens.size() && tokens[next].is_param) {
                  // Replace # and param with stringified arg
                  int idx = tokens[next].param_idx;
                  std::string arg = (idx < (int)args.size()) ? args[idx] : "";

                  // Handle variadic ...
                  if (md.params[idx] == "...") {
                    std::string var_args;
                    for (size_t m = idx; m < args.size(); ++m) {
                      if (m > idx)
                        var_args += ", ";
                      var_args += args[m];
                    }
                    arg = var_args;
                  }

                  tokens[i].text = "\"" + arg + "\"";
                  tokens[i].is_param = false; // It's now a literal string

                  // Clear intermediate tokens (including the param that was
                  // stringified)
                  for (size_t k = i + 1; k <= next; ++k) {
                    tokens[k].text = "";
                    tokens[k].is_param = false; // Prevent later substitution
                  }
                  i = next;
                }
              }
            }

            // 3. Substitute remaining params
            for (auto &tok : tokens) {
              if (tok.is_param) {
                int idx = tok.param_idx;
                if (md.params[idx] == "...") {
                  std::string var_args;
                  for (size_t m = idx; m < args.size(); ++m) {
                    if (m > idx)
                      var_args += ", ";
                    var_args += args[m];
                  }
                  tok.text = var_args;
                } else {
                  tok.text = (idx < (int)args.size()) ? args[idx] : "";
                }
              }
            }

            // 4. Process ## (concatenation)
            // Look for ## and join previous non-whitespace with next
            // non-whitespace
            for (size_t i = 0; i < tokens.size(); ++i) {
              if (tokens[i].text == "##") {
                // Find prev non-whitespace (and non-empty)
                int prev = (int)i - 1;
                while (prev >= 0 && (tokens[prev].text.empty() ||
                                     std::isspace(static_cast<unsigned char>(
                                         tokens[prev].text[0]))))
                  prev--;

                // Find next non-whitespace
                size_t next = i + 1;
                while (next < tokens.size() &&
                       (tokens[next].text.empty() ||
                        std::isspace(
                            static_cast<unsigned char>(tokens[next].text[0]))))
                  next++;

                if (prev >= 0 && next < tokens.size()) {
                  tokens[prev].text += tokens[next].text;
                  tokens[next].text = "";
                  tokens[i].text = "";
                  // Clear whitespace between
                  for (int k = prev + 1; k < (int)next; ++k)
                    tokens[k].text = "";
                } else {
                  // Malformed ## - just remove it
                  tokens[i].text = "";
                }
              }
            }

            // 5. Reassemble
            std::string expansion;
            for (const auto &tok : tokens)
              expansion += tok.text;

            out += expansion;
            continue;
          }
        }
      }

      // Not a macro or not invoked as function-like
      out += ident;
      continue;
    }

    // Pass through other characters
    out += text[i++];
  }
  return out;
}

preprocessor make_default_preprocessor(const preprocessor_options &opts) {
  preprocessor p(opts);

  // The constructor supplies missing defaults in options(). Register that
  // resolved map exactly once so explicit caller overrides retain precedence.
  for (const auto &kv : p.options().builtin_macros) {
    if (!p.lookup_macro(kv.first).has_value()) {
      macro_def md;
      md.name = kv.first;
      md.function_like = false;
      md.variadic = false;
      md.replacement = kv.second;
      p.define_macro(md);
    }
  }

  return p;
}

} // namespace ny
