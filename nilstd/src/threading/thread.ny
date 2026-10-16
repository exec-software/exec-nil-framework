@pragma once

module nil.std.threading

@intrinsic("time.sleep")
fn sleep_ms_intrinsic(ms: i64)
end fn

# Suspend the current thread for approximately `ms` milliseconds on all routes.
# Negative and zero durations return immediately.
pub fn sleep_ms(ms: i64)
    sleep_ms_intrinsic(ms)
end fn

# Suspend the current thread for approximately `seconds` seconds.
pub fn sleep_sec(seconds: i32)
    sleep_ms(seconds as i64 * 1000)
end fn
