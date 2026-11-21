module nil.std.io
import nil.std.core

# Line-Oriented Input
# Reads one line, preserving its bytes except for the trailing LF. Empty lines
# and a final nonempty line without LF are valid; EOF or an input error faults.
# On POSIX, CR is preserved. Native text cannot contain NUL, so native input
# rejects that byte explicitly. Flushes pending console output before reading.
@intrinsic("console.read_line")
pub fn read_line() -> text

# Reads one line like read_line, but answers None at end of input instead of
# faulting, so a reader can stop cleanly when its input closes. A genuine
# input error still faults: a broken stream must never be mistaken for a
# clean end of input by something that has to fail closed.
@intrinsic("console.read_line_option")
pub fn read_line_option() -> Option<text>
end fn

# Basic Output
@intrinsic("console.write")
pub fn write(s: text)
end fn

@intrinsic("console.writeln")
pub fn writeln(s: text)
end fn

@intrinsic("console.error")
pub fn write_error(s: text)
end fn

# Publishes pending standard output before continuing.
@intrinsic("console.flush")
pub fn flush()
end fn
