import ny.lang.core.*

fn byte_result(succeed: bool) -> Result<array<u8>, text>
    let bytes: array<u8> = []
    if succeed then
        return Result.Ok(bytes)
    end if
    return Result.Error("byte-error")
end fn

fn bool_result(succeed: bool) -> Result<bool, text>
    if succeed then
        return Result.Ok(true)
    end if
    return Result.Error("bool-error")
end fn

fn text_option(present: bool) -> Option<text>
    if present then
        return Option.Some("present")
    end if
    return Option.None
end fn

fn main() -> i32
    let byte_value: Result<array<u8>, text> = byte_result(true)
    match byte_value
        Result.Ok(bytes) -> println("byte-bound")
        Result.Error(message) -> println(message)
    end match

    let byte_wildcard: Result<array<u8>, text> = byte_result(true)
    match byte_wildcard
        Result.Ok(_) -> println("byte-wildcard")
        Result.Error(_) -> println("unexpected-byte-error")
    end match

    let byte_failure: Result<array<u8>, text> = byte_result(false)
    match byte_failure
        Result.Ok(_) -> println("unexpected-byte-success")
        Result.Error(message) -> println(message)
    end match

    let bool_value: Result<bool, text> = bool_result(true)
    println(bool_value.is_ok())
    match bool_value
        Result.Ok(bool_payload) -> println(bool_payload)
        Result.Error(message) -> println(message)
    end match

    let text_value: Option<text> = text_option(true)
    println(text_value.is_some())
    match text_value
        Option.Some(text_payload) -> println(text_payload)
        Option.None -> println("unexpected-none")
    end match

    let no_text: Option<text> = text_option(false)
    match no_text
        Option.Some(_) -> println("unexpected-some")
        Option.None -> println("none")
    end match
    return 0
end fn
