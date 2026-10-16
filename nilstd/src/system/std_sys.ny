
# std_sys.ny - Standard System Library

module nil.std.system

import nil.std.core

@interop("std_sys")
pub fn exit(code: i32)
end fn

# Return the number of Ny program arguments. The executable/NIL path is not
# included; arguments begin after the interpreter's `--` separator.
@interop("std_sys")
pub fn arg_count() -> i64
end fn

# Direct indexed access rejects an invalid index. Use try_arg when absence is
# expected; both APIs preserve explicitly empty arguments.
@interop("std_sys")
pub fn arg(index: i64) -> text
end fn

# Checked argument lookup preserves empty arguments while distinguishing an
# invalid index.
pub fn try_arg(index: i64) -> Option<text>
    if index < 0 or index >= arg_count() then
        return Option.None
    end if
    return Option.Some(arg(index))
end fn
