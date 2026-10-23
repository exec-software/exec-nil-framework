module nil.std.io
import nil.std.core

# Line-Oriented Input
# Reads one line, preserving its bytes except for the trailing LF. Empty lines
# and a final nonempty line without LF are valid; EOF or an input error faults.
# On POSIX, CR is preserved. Native text cannot contain NUL, so native input
# rejects that byte explicitly. Flushes pending console output before reading.
@intrinsic("console.read_line")
pub fn read_line() -> text
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
