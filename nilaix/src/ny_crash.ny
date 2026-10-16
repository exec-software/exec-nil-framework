module ny_crash
# =============================================================================
# ny_crash.ny — Structured crash reporter for Ny binaries
#
# Writes .nycrash files (NyData format) that can be ingested by the Enigma MCP
# tool `parse_nycrash` for immediate agent-level analysis.
#
# Usage:
#   import ny_crash
#
#   # On a caught error path:
#   ny_crash::write(crash_path, "FileNotFound", "project.nyrd missing",
#                   "", "nyrd.files.ny", 47, "project_open", "nyrd_files", "")
#
#   # From a build host (capturing nilinterpreter stderr):
#   ny_crash::write_interpreter(crash_path, stderr_text, exit_code)
# =============================================================================

import winplat
import string_utils

# ---------------------------------------------------------------------------
# ny_crash::write  — explicit crash report from handled error paths
# ---------------------------------------------------------------------------

pub function write(
    crash_path:  text,
    error_type:  text,
    message:     text,
    expression:  text,
    src_file:    text,
    src_line:    i32,
    fn_name:     text,
    module_name: text,
    hint:        text
) -> void
    let ts  = ""
    let mut out = "# Ny Crash Report — auto-generated\n"
    out = out + "# format: NyData v1\n\n"

    out = out + "[meta]\n"
    out = out + "format = nycrash/1\n"

    out = out + "\n[error]\n"
    out = out + "type = " + error_type + "\n"
    out = out + "message = " + message + "\n"
    if string_utils::length(expression) > 0 then
        out = out + "expression = " + expression + "\n"
    end if

    out = out + "\n[location]\n"
    out = out + "file = " + src_file + "\n"
    out = out + "line = " + string_utils::int_to_text(src_line) + "\n"
    out = out + "function = " + fn_name + "\n"
    out = out + "module = " + module_name + "\n"

    out = out + "\n[stack]\n"
    out = out + "0 = " + module_name + "::" + fn_name + "  " + src_file + ":" + string_utils::int_to_text(src_line) + "\n"

    if string_utils::length(hint) > 0 then
        out = out + "\n[hint]\n"
        out = out + "text = " + hint + "\n"
    end if

    winplat::write_file(crash_path, out)
end function

# ---------------------------------------------------------------------------
# ny_crash::write_interpreter — crash report captured from interpreter stderr
#
# Call this from a host process (e.g. Nyrd IDE) when a Ny child exits
# with a non-zero code and its stderr contains "NIL interpreter runtime error".
# ---------------------------------------------------------------------------

pub function write_interpreter(
    crash_path:   text,
    stderr_text:  text,
    exit_code:    i32
) -> void
    let mut out = "# Ny Interpreter Crash Report — captured from process stderr\n"
    out = out + "# format: NyData v1\n\n"

    out = out + "[meta]\n"
    out = out + "format = nycrash/1\n"
    out = out + "source = interpreter_stderr\n"

    out = out + "\n[error]\n"
    out = out + "type = NIL interpreter runtime error\n"
    out = out + "message = " + stderr_text + "\n"
    out = out + "exit_code = " + string_utils::int_to_text(exit_code) + "\n"

    out = out + "\n[hint]\n"
    out = out + "text = No source map available. Run the NIL program under nildebugger for a trace. Common causes include array access through a struct field and for..in over a returned array<T>.\n"

    winplat::write_file(crash_path, out)
end function

# ---------------------------------------------------------------------------
# ny_crash::write_interpreter_with_context — interpreter crash + known context
# ---------------------------------------------------------------------------

pub function write_interpreter_with_context(
    crash_path:    text,
    stderr_text:   text,
    exit_code:     i32,
    src_file:      text,
    fn_name:       text,
    module_name:   text,
    locals_block:  text
) -> void
    let mut out = "# Ny Interpreter Crash Report — with context\n"
    out = out + "# format: NyData v1\n\n"

    out = out + "[meta]\n"
    out = out + "format = nycrash/1\n"
    out = out + "source = interpreter_stderr_with_context\n"

    out = out + "\n[error]\n"
    out = out + "type = NIL interpreter runtime error\n"
    out = out + "message = " + stderr_text + "\n"
    out = out + "exit_code = " + string_utils::int_to_text(exit_code) + "\n"

    out = out + "\n[location]\n"
    out = out + "file = " + src_file + "\n"
    out = out + "function = " + fn_name + "\n"
    out = out + "module = " + module_name + "\n"

    if string_utils::length(locals_block) > 0 then
        out = out + "\n[locals]\n"
        out = out + locals_block + "\n"
    end if

    out = out + "\n[hint]\n"
    out = out + "text = No source map available. Run the NIL program under nildebugger for a trace.\n"

    winplat::write_file(crash_path, out)
end function
