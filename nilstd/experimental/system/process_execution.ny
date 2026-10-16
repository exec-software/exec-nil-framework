# EXPERIMENTAL DESIGN SOURCE — excluded from nilstd and nillib.
#
# The former active process API depended on an unlinked legacy ABI, invoked a
# shell through an ambiguous `execute` name, merged stderr into stdout, leaked
# returned strings and had no output or time budget. Preserve the useful result
# model here; promotion requires an explicit argv-based API plus bounded output,
# timeout/cancellation, separate streams and executable interpreter tests.

module nil.std.system.experimental

pub struct process_result
    pub stdout: text
    pub stderr: text
    pub exit_code: i32
end struct
