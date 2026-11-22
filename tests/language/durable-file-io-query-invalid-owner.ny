module nil.tests.durable_query_invalid_owner

enum Result<T, E>
    Ok(T)
    Error(E)
end enum

@intrinsic("file.file_lock_is_held")
fn wrong_query(token: i64) -> Result<bool, text>
end fn

fn main() -> i32
    match wrong_query(0)
        Result.Ok(_) -> return 1
        Result.Error(_) -> return 0
    end match
    return 0
end fn
