#ifndef NY_COMPILER_PREPROCESSOR_HPP
#define NY_COMPILER_PREPROCESSOR_HPP

// preprocessor.hpp
// Preprocessor public API for nilcompiler.
//
// The preprocessor performs textual transformations on source prior to the
// lexical and syntactic phases. It provides support for directives starting
// with the '@' prefix and a small macro system (object & function-like).
// This header contains the public types and class interface for the
// preprocessor subsystem.
//
// Style/conventions:
//  - K&R underscore naming (snake_case) for functions and variables.
//  - Types and functions live in the ny:: namespace.
//
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <regex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ny {

// Character/keyword used to introduce preprocessor directives (e.g., @include)
constexpr char preproc_directive_prefix = '@';

// Preprocessor diagnostics kinds
enum class preproc_diag_kind { info, warning, error };

// Preprocessor diagnostic (structured)
struct preproc_diagnostic {
  int line = 0;                          // 1-based source line
  preproc_diag_kind kind = preproc_diag_kind::error;
  std::string filename;                  // source file the diagnostic refers to
  std::string message;                   // human readable message
  std::optional<std::string> hint;       // optional suggested fix or hint
};

// Supported preprocessor directive kinds
enum class preproc_directive_kind {
  include,
  pragma,
  pragma_link,
  define,
  undef,
  if_,
  elif,
  else_,
  endif,
  ifdef,
  ifndef,
  error,
  warning,
  message,
  unknown
};

// Simple representation of a parsed preprocessor directive
struct preproc_directive {
  preproc_directive_kind kind = preproc_directive_kind::unknown;
  std::string name;                      // directive name or macro name
  std::string args;                      // raw args text after directive keyword
  std::string raw_text;                  // full directive line (original)
  int line = 0;                          // source line
  std::filesystem::path source_file;     // file where this directive was found
};

// Representation of a collected `@pragma link(...)` directive. The preprocessor
// will populate this structure when it encounters `@pragma link(...)` directives
// so callers and tools (linker/packager) can consume a canonical record of the
// link requirements discovered during preprocessing.
struct preproc_pragma_link {
  // The textual name/path of the library referenced in the pragma, e.g.
  // "mylib.lib", "system32.lib", "sqlite3", or "z".
  std::string lib_name;

  // Optional kind of link directive: "dylib", "lib", or "static".
  // This is an optional hint and may be empty depending on the pragma usage.
  std::optional<std::string> kind;

  // Optional alias or additional parameter captured by syntax like:
  // @pragma link(name="z", kind="dylib", alias="zlib")
  std::optional<std::string> alias;

  // Source file & line for diagnostics and tooling
  std::filesystem::path source_file;
  int line = 0;
};

// Macro definition record (object-like and function-like macros)
struct macro_def {
  std::string name;
  bool function_like = false;            // `macroname(...)`
  bool variadic = false;                 // variadic param `...` for function-like
  std::vector<std::string> params;       // parameter names for function-like macros
  std::string replacement;               // replacement text for expansion
  std::filesystem::path file;            // file where macro was defined
  int line = 0;                          // defining line number
};

// Preprocessor options to control behavior
struct preprocessor_options {
  // If set, macros will be expanded in textual output (default true).
  bool expand_macros = true;

  // If set, keep non-directive comments in the output stream (default false).
  bool preserve_comments = false;

  // Include search paths (e.g., for `@include <...>`)
  std::vector<std::filesystem::path> include_paths;

  // If set, keep preprocessor directives in the output as lines
  // optionally, useful for preserving `@pragma` tokens for later stages.
  bool keep_directives_in_output = false;

  // Built-in macro map for predefined macros (e.g., __FILE__, __LINE__, etc.)
  std::unordered_map<std::string, std::string> builtin_macros;

  // Analysis may provide unsaved source files without changing the filesystem.
  // An installed callback is authoritative: failure never falls back to disk.
  std::function<std::optional<std::filesystem::path>(const std::filesystem::path &)> resolve_source;
  std::function<std::optional<std::string>(const std::filesystem::path &)> read_source;
  // Zero preserves the ordinary compiler's existing behavior.
  std::size_t max_output_bytes = 0;
  std::size_t max_include_depth = 0;
  std::size_t max_include_visits = 0;
};

// Result of a preprocessing operation
struct preproc_result {
  bool success = true;
  std::string output;                                // Preprocessed source text
  std::vector<preproc_diagnostic> diagnostics;       // All diagnostics collected
  std::vector<std::filesystem::path> included_files; // Paths to files included
  std::unordered_map<std::string, macro_def> macros; // Macro table after preprocessing

  // Collected `@pragma link(...)` directives discovered during preprocessing.
  // This provides a canonical, tool-friendly list of link requirements
  // for packagers, linkers, and downstream build tooling.
  std::vector<preproc_pragma_link> pragma_links;
};

// Type alias for diagnostic callback invoked during processing
using preproc_diag_cb = std::function<void(const preproc_diagnostic &)>;

// Public preprocessor interface. A single preprocessor instance contains
// configuration and a working macro table state. The implementation should be
// reentrant as possible -- callers can create a fresh `preprocessor` instance
// per preproc run, or reuse it and call `reset()` between inputs.
class preprocessor {
public:
  explicit preprocessor(const preprocessor_options &opts = {}) noexcept;

  // Preprocess an input file. The method will read the file and return
  // preprocessor output along with diagnostics and included files.
  // On error (e.g., file not found), `result.success` will be false and
  // diagnostics provided.
  preproc_result preprocess_file(const std::filesystem::path &path);

  // Preprocess an in-memory string as an input unit. The filename is used
  // only for diagnostics and `__FILE__` macro expansion.
  preproc_result preprocess_text(const std::string &source,
                                 const std::string &filename = "<input>");

  // Lookup a macro definition by name. Returns optional<macro_def>.
  std::optional<macro_def> lookup_macro(const std::string &name) const;

  // Add or replace a macro definition into the preprocessor's macro table.
  // Returns true if the macro was inserted or replaced.
  bool define_macro(const macro_def &m);

  // Remove a macro by name from the table. Returns true if the macro existed.
  bool undef_macro(const std::string &name);

  // Reset preprocessor internal state (macros, include stack, diagnostics).
  void reset();

  // Accessors
  void set_options(const preprocessor_options &opts) { options_ = opts; }
  const preprocessor_options &options() const { return options_; }
  void set_diag_callback(preproc_diag_cb cb) { diag_cb_ = std::move(cb); }

  // Access collected `@pragma link(...)` directives recorded during the last
  // preprocessing run. This is useful for tools that need to extract link
  // requirements without re-running the preprocessor.
  const std::vector<preproc_pragma_link> &pragma_links() const { return pragma_links_; }

private:
  // Implementation helpers (not exposed). Implementations will:
  // - Tokenize/scan the source for lines starting with the preproc prefix
  // - Parse and evaluate directive expressions
  // - Expand macros and perform textual inclusion
  // - Maintain include guard tracking and include-once policy
  //
  // The specific functions are declared here for readability and to allow tests
  // to call internal behavior if needed.

  // Parse a single directive from a line of text. This returns a directive
  // structure which can then be acted upon.
  preproc_directive parse_directive_line(const std::string &line_text,
                                         int line_no,
                                         const std::string &filename) const;

  // Evaluate a conditional expression (e.g., `cfg(platform = "linux")`).
  // The implementation may support a simple expression language or call out
  // into the compiler's condition evaluator. This returns true iff the
  // condition evaluates to "truthy".
  //
  // On error, returns std::nullopt and populates 'err_message'.
  std::optional<bool> evaluate_condition(const std::string &expr,
                                         const std::string &filename,
                                         int line,
                                         std::string &err_message) const;

  // Expand macros in the provided textual range. This takes the current macro
  // table into account and returns the expanded text.
  std::string expand_macros(const std::string &text) const;

  // Resolve include path (searches include_paths then returns absolute path
  // if found). Returns std::optional<path> if found, otherwise nullopt.
  std::optional<std::filesystem::path>
  resolve_include_path(const std::string &include_name,
                       const std::filesystem::path &relative_to) const;

  // Read a text file into a string. Returns optional<string> - nullopt on fail.
  std::optional<std::string>
  read_file_to_string(const std::filesystem::path &path);

  // Notify of a diagnostic; calls callback if supplied and records into list.
  void record_diagnostic(preproc_diag_kind kind, const std::string &filename,
                         int line, const std::string &message,
                         const std::optional<std::string> &hint = std::nullopt);

  // Produce a textual representation of the builtin macros (helper)
  void register_builtin_macros();

private:
  preprocessor_options options_;
  preproc_diag_cb diag_cb_; // optional diagnostic callback

  // Internal macro and include state
  std::unordered_map<std::string, macro_def> macro_table_;
  std::vector<std::filesystem::path> include_stack_;
  std::unordered_set<std::string> include_once_set_; // canonical paths
  std::size_t include_visits_ = 0;

  // Diagnostics produced during the current operation
  std::vector<preproc_diagnostic> diagnostics_;

  // Collected `@pragma link(...)` directives discovered during the current
  // operation. Implementations populate this so callers can use link metadata
  // discovered while preprocessing.
  std::vector<preproc_pragma_link> pragma_links_;
};

// Convenience factory function that returns a preprocessor configured with
// common preprocessor options (include paths, common builtin macros).
preprocessor make_default_preprocessor(const preprocessor_options &opts = {});

} // namespace ny

#endif // NY_COMPILER_PREPROCESSOR_HPP
