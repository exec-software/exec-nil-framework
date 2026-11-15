module nil.tests.durable_invalid_width
import nil.std.core

@intrinsic("file.acquire_file_lock")
fn wrong_lock(path: text, create: bool) -> Result<i32, text>
end fn

fn main() -> i32
    if wrong_lock("must-not-exist.lock", true).is_ok() then return 1 end if
    return 0
end fn
