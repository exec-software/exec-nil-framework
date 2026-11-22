module nil.tests.text_from_bytes

import nil.std.text
import nil.std.collections.array

fn main() -> i32
    let mut bytes: array<u8> = []
    let mut index: i64 = 0
    while index < 256
        bytes.push(index as u8)
        index = index + 1
    end while
    let text = nil::std::text::from_bytes(bytes)
    if nil::std::text::length(text) != 256 then return 61 end if
    bytes[0] = 17
    index = 0
    while index < 256
        if nil::std::text::byte_at(text, index) != index then return 62 end if
        index = index + 1
    end while
    let empty: array<u8> = []
    if nil::std::text::from_bytes(empty) != "" then return 63 end if
    let mut large = nil::std::collections::new_u8(1048576, 255 as u8)
    large[524288] = 0
    large[1048575] = 128
    let copied = nil::std::text::from_bytes(large)
    if nil::std::text::length(copied) != 1048576 then return 64 end if
    if nil::std::text::byte_at(copied, 0) != 255 then return 65 end if
    if nil::std::text::byte_at(copied, 524288) != 0 then return 66 end if
    if nil::std::text::byte_at(copied, 1048575) != 128 then return 67 end if
    println("from-bytes-ok")
    return 0
end fn
