module nil.std.io

import nil.std.core

# Exact canonical type name, deliberately incompatible timestamp field. The
# native bridge must validate the schema rather than trust its spelling.
pub struct FileSnapshot
    content: array<u8>
    modified_ms: text
    sha256: text
end struct

@intrinsic("file.read_rooted_snapshot")
fn invalid_snapshot(root: text, relative: text, maximum_bytes: i64) -> Result<FileSnapshot, text>

fn main() -> i32
    match invalid_snapshot(".", "never-read.bin", 256)
        Result.Ok(snapshot) -> println(snapshot.sha256)
        Result.Error(message) -> println(message)
    end match
    return 0
end fn
