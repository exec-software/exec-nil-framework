#ifndef NY_COMPILER_MATRIX_DIAGNOSTICS_HPP
#define NY_COMPILER_MATRIX_DIAGNOSTICS_HPP

// matrix_diagnostics.hpp
// The diagnostic record for the Matrix front end, and its renderers.
//
// This record is deliberately the ANALYZER surface as well as the compiler's
// own. An analyzer's entire output is diagnostics, so a record designed only
// for compiler messages caps what any analyzer can ever say. See
// matrix/docs/diagnostics-and-analyzers.md.
//
// Four properties the old `line: message` form could not express:
//
//   1. SURFACE VOCABULARY. A Matrix author wrote 'number' and 'text'; Ny's
//      checker reports 'i64' and 'String'. Those are internal spellings that
//      do not exist in Matrix, and they appear exactly where the author is
//      already confused. Every message is mapped back before display.
//   2. SPANS, not lines. The lexer records a column on every token, so a
//      caret can sit under the offending text and secondary labels can point
//      at the other operand or the original declaration.
//   3. A STABLE CODE, so a diagnostic can be suppressed, documented and
//      grouped. Without one every tool resorts to matching message
//      substrings, which is what this repository's own tests do today.
//   4. An optional machine-applicable FIX, which is what makes an analyzer
//      worth writing and an agent's edit loop cheap.

#include <optional>
#include <string>
#include <vector>

namespace matrix {

enum class severity { error, warning, note, help };

// A half-open span of one source line. `length` of zero means "to end of
// line"; a caret is still drawn at `column`.
struct span {
  int line = 0;
  int column = 0;
  int length = 0;
};

// A span with an explanatory label, rendered beneath the primary caret.
struct labelled_span {
  span at;
  std::string label;
};

// An edit a tool may apply without asking: replace `at` with `replacement`.
struct suggested_fix {
  span at;
  std::string replacement;
  std::string description;
};

struct diagnostic {
  std::string code;       // e.g. "MX0412"; empty for an unclassified message
  severity level = severity::error;
  std::string message;    // one sentence, in the surface's vocabulary
  span primary;
  std::vector<labelled_span> secondary;
  std::string hint;       // what to do, concretely
  std::optional<suggested_fix> fix;
};

// Rewrite NIL/Ny internal type spellings into the Matrix surface names a
// reader actually wrote. 'i64' -> 'number', 'String' -> 'text', and the
// machine types back to their 'override ...' form. Quoted and bare
// occurrences are both handled, and a word is only replaced when it stands
// alone, so 'i64_total' is left intact.
std::string to_surface_vocabulary(const std::string &message);

// Render one diagnostic as a block: code, message, file:line:column, the
// source line, a caret under the primary span, labelled secondary spans and
// the hint. `source` is the whole page; `path` is shown in the location line.
std::string render_diagnostic(const diagnostic &d, const std::string &source,
                              const std::string &path, bool colour);

// Render one diagnostic as a single JSON object, for editors, CI annotators,
// agents and analyzers. Newline-delimited JSON is the form `nildebugger --json`
// already uses for automated sessions.
std::string render_diagnostic_json(const diagnostic &d,
                                   const std::string &path);

} // namespace matrix

#endif // NY_COMPILER_MATRIX_DIAGNOSTICS_HPP
