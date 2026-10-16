
# std_time.ny - Standard Time Library

module nil.std.datetime

@interop("std_time")
pub fn now() -> i64
end fn

@interop("std_time")
pub fn sleep(ms: i32)
end fn
