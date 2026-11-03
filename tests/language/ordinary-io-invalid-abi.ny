module nil.tests.ordinary_io_invalid_abi
import nil.std.core

@intrinsic("file.write_text")
fn wrong_file_write(path: i64, contents: text) -> Result<bool, text>
end fn

fn main() -> i32
    if wrong_file_write(1, "must not be written").is_ok() then return 1 end if
    return 0
end fn
