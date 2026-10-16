module native_exhaustive_result_return

import ny.lang.core.*

struct loaded_value
    value: i32
    ok: bool
end struct

fn from_result(result: Result<i32, text>) -> loaded_value
    match result
        Result.Ok(value) ->
            return loaded_value { value: value, ok: true }
        Result.Error(_) ->
            return loaded_value { value: 0, ok: false }
    end match
end fn

fn main() -> i32
    let parsed_success: Result<i32, text> = Convert.try_to_i32("42")
    let parsed_failure: Result<i32, text> = Convert.try_to_i32("not-a-number")
    let success = from_result(parsed_success)
    let failure = from_result(parsed_failure)
    if not success.ok || failure.ok then
        return 10
    end if
    println(success.value)
    println(failure.value)
    return 0
end fn
