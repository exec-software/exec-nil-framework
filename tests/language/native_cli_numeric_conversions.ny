module ny.tests.native_cli_numeric_conversions

import ny.lang.core.*

fn parse_i32(value: text) -> Result<i32, text>
    return Convert.try_to_i32(value)
end fn

fn main() -> i32
    println(Convert.i32_to_text(-2147483648))
    println(Convert.i64_to_text(9223372036854775807 as i64))

    let byte_value: u8 = 255 as u8
    println(Convert.i64_to_text(byte_value as i64))

    let parsed: Result<i32, text> = parse_i32("+2048")
    match parsed
        Result.Ok(value) -> println(value)
        Result.Error(_) -> return 20
    end match

    let rejected: Result<i32, text> = parse_i32("2147483648")
    match rejected
        Result.Ok(_) -> return 21
        Result.Error(message) ->
            if message == "" then return 22 end if
            println("parse-error")
    end match
    return 0
end fn
