module nil.std.io
import nil.std.core

# Line-Oriented Input
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

@intrinsic("console.flush")
pub fn flush()
end fn
