module nil.tests.durable_invalid_owner

enum Result<T, E>
    Ok(T)
    Error(E)
end enum

@intrinsic("file.sync_directory")
fn wrong_sync(path: text) -> Result<bool, text>
end fn

fn main() -> i32
    match wrong_sync(".")
        Result.Ok(_) -> return 1
        Result.Error(_) -> return 0
    end match
    return 0
end fn
