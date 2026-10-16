@pragma once

module nil.std.security

import nil.std.core
import nil.std.collections

# Encode bytes as lowercase hexadecimal text.
pub fn hex_encode(data: vec<u8>) -> text
    let mut result: text = ""
    let alphabet = "0123456789abcdef"
    let mut i: i32 = 0
    while i < data.length() do
        match data.get(i)
            Option.None -> return ""
            Option.Some(byte) ->
                let value = byte as i32
                result = result + alphabet.substr(value / 16, 1)
                result = result + alphabet.substr(value % 16, 1)
        end match
        i = i + 1
    end while
    return result
end fn
