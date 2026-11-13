import nil.std.io.file

@intrinsic("file.read_rooted_snapshot")
fn invalid_snapshot(root: text, relative: text, maximum_bytes: i32) -> Result<nil::std::io::FileSnapshot, text>

fn main() -> i32
    match invalid_snapshot(".", "never-read.bin", 256)
        Result.Ok(snapshot) -> println(snapshot.sha256)
        Result.Error(message) -> println(message)
    end match
    return 0
end fn
