module nil.tests.durable_query_invalid_width
import nil.std.core

@intrinsic("file.file_lock_is_held")
fn wrong_query(token: i64) -> Result<i64, text>
end fn

fn main() -> i32
    if wrong_query(0).is_ok() then return 1 end if
    return 0
end fn
