module ny.tests.checked_hex_runtime

import ny.lang.core.*

fn rejects(value: text, expected: text) -> bool
    match Convert.try_hex_to_bytes(value)
        Result.Ok(_) -> return false
        Result.Error(message) -> return message == expected
    end match
end fn

fn main() -> i32
    match Convert.try_hex_to_bytes("")
        Result.Error(_) -> return 10
        Result.Ok(bytes) ->
            if bytes.length() != 0 then return 11 end if
            if Convert.bytes_to_hex(bytes) != "" then return 12 end if
    end match
    if Convert.hex_to_bytes("").length() != 0 then return 13 end if
    println("empty")

    match Convert.try_hex_to_bytes("00FfAabB0123456789ABCDEF")
        Result.Error(_) -> return 14
        Result.Ok(bytes) ->
            if bytes.length() != 12 then return 15 end if
            if Convert.bytes_to_hex(bytes) != "00ffaabb0123456789abcdef" then return 16 end if
            if bytes[0] != (0 as u8) then return 17 end if
            if bytes[1] != (255 as u8) then return 18 end if
            if bytes[11] != (239 as u8) then return 19 end if
    end match
    println("mixed-case")

    let mut original: array<u8> = []
    let mut i: i32 = 0
    while i < 256 do
        original.push(i as u8)
        i = i + 1
    end while
    let encoded = Convert.bytes_to_hex(original)
    if encoded.len() != 512 then return 20 end if
    match Convert.try_hex_to_bytes(encoded)
        Result.Error(_) -> return 21
        Result.Ok(decoded) ->
            if decoded.length() != 256 then return 22 end if
            i = 0
            while i < 256 do
                if decoded[i] != original[i] then return 23 end if
                i = i + 1
            end while
            if Convert.bytes_to_hex(decoded) != encoded then return 24 end if
    end match
    let legacy = Convert.hex_to_bytes(encoded)
    if legacy.length() != 256 then return 25 end if
    i = 0
    while i < 256 do
        if legacy[i] != original[i] then return 26 end if
        i = i + 1
    end while
    println("all-256-octets")

    let odd_error = "hex_to_bytes: input must contain an even number of digits"
    if not rejects("0", odd_error) then return 30 end if
    if not rejects("abc", odd_error) then return 31 end if
    if not rejects("000ff", odd_error) then return 32 end if
    println("odd-length")

    let digit_error = "hex_to_bytes: input contains a non-hexadecimal digit"
    let invalid: array<text> = ["-1", "+f", "0x", "0X", "0g", "g0", "f/", "/f", ":0", "0:", " 0", "0 ", "0\n", "\t0", "0_", "_0", "é", "λ", "１２", "00ffGG", "00ff-1", "00ff+1", "00ff0x", "00ff 0", "00ff0g", "0000g0"]
    for malformed in invalid
        if not rejects(malformed, digit_error) then return 33 end if
    end for
    println("strict-alphabet")

    # A valid prefix must not turn an invalid suffix into a partial success.
    if not rejects(encoded + "gg", digit_error) then return 34 end if
    if not rejects(encoded + "f", odd_error) then return 35 end if
    # Errors do not alter the caller's input array or a subsequent decode.
    if original.length() != 256 then return 36 end if
    if original[255] != (255 as u8) then return 37 end if
    match Convert.try_hex_to_bytes("ff00")
        Result.Error(_) -> return 38
        Result.Ok(bytes) ->
            if bytes.length() != 2 then return 39 end if
            if bytes[0] != (255 as u8) or bytes[1] != (0 as u8) then return 40 end if
    end match
    println("no-partial-success")
    return 0
end fn
