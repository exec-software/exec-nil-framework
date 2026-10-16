import ny.lang.core
import ny.lang.io.*

fn require(value: bool, label: text)
    if not value then panic(label) end if
end fn

fn value32(result: Result<i32, text>) -> i32
    match result
        Result.Ok(value) -> return value
        Result.Error(error) -> panic(error)
    end match
    return 0
end fn

fn value64(result: Result<i64, text>) -> i64
    match result
        Result.Ok(value) -> return value
        Result.Error(error) -> panic(error)
    end match
    return 0
end fn

fn error32(result: Result<i32, text>, expected: text) -> bool
    match result
        Result.Ok(_) -> return false
        Result.Error(error) -> return error == expected
    end match
end fn

fn error64(result: Result<i64, text>, expected: text) -> bool
    match result
        Result.Ok(_) -> return false
        Result.Error(error) -> return error == expected
    end match
end fn

fn unchanged(data: array<u8>) -> bool
    let mut i: i32 = 0
    while i < data.len() do
        if data[i] != (37 as u8) then return false end if
        i = i + 1
    end while
    return true
end fn

fn main() -> i32
    let sample: array<u8> = [1 as u8, 35 as u8, 69 as u8, 103 as u8,
                             137 as u8, 171 as u8, 205 as u8, 239 as u8]
    let forward: i64 = 81985529216486895
    let backward: i64 = -(1167088121787636991 as i64)
    require(value32(read_u16_be(sample, 0, 8)) == 291, "u16 big")
    require(value32(read_u16_le(sample, 0, 8)) == 8961, "u16 little")
    require(value64(read_u32_be(sample, 4, 8)) == (2309737967 as i64), "u32 big")
    require(value64(read_u32_le(sample, 4, 8)) == (4023233417 as i64), "u32 little")
    require(value32(read_i32_be(sample, 4, 8)) == -1985229329, "i32 big")
    require(value32(read_i32_le(sample, 4, 8)) == -271733879, "i32 little")
    require(value64(read_i64_be(sample, 0, 8)) == forward, "i64 big")
    require(value64(read_i64_le(sample, 0, 8)) == backward, "i64 little")
    println("binary known reads ok")

    let data: array<u8> = []
    let mut i: i32 = 0
    while i < 12 do
        data.push(37 as u8)
        i = i + 1
    end while
    let alias = data
    require(value64(write_i64_be(data, 2, 10, forward)) == 10, "write cursor")
    i = 0
    while i < 8 do
        require(alias[i + 2] == sample[i], "big endian exact bytes")
        i = i + 1
    end while
    require(data[0] == (37 as u8) and data[1] == (37 as u8), "prefix preserved")
    require(data[10] == (37 as u8) and data[11] == (37 as u8), "suffix preserved")
    require(value64(write_i64_le(data, 2, 10, forward)) == 10, "little cursor")
    i = 0
    while i < 8 do
        require(data[i + 2] == sample[7 - i], "little endian exact bytes")
        i = i + 1
    end while
    require(value64(write_u16_be(data, 1, 3, 43981)) == 3, "u16 big cursor")
    require(data[1] == (171 as u8) and data[2] == (205 as u8), "u16 big bytes")
    require(value64(write_u16_le(data, 1, 3, 43981)) == 3, "u16 little cursor")
    require(data[1] == (205 as u8) and data[2] == (171 as u8), "u16 little bytes")
    require(value64(write_u32_be(data, 3, 7, 2309737967 as i64)) == 7, "u32 big cursor")
    require(data[3] == (137 as u8) and data[6] == (239 as u8), "u32 big bytes")
    require(value64(write_u32_le(data, 3, 7, 2309737967 as i64)) == 7, "u32 little cursor")
    require(data[3] == (239 as u8) and data[6] == (137 as u8), "u32 little bytes")
    require(value64(write_i32_be(data, 3, 7, -1985229329)) == 7, "i32 big cursor")
    require(value32(read_i32_be(data, 3, 7)) == -1985229329, "i32 big round trip")
    require(value64(write_i32_le(data, 3, 7, -1985229329)) == 7, "i32 little cursor")
    require(value32(read_i32_le(data, 3, 7)) == -1985229329, "i32 little round trip")
    let maximum32: i64 = 2147483647
    let minimum32: i64 = -maximum32 - 1
    require(value64(write_i32_be(data, 3, 7, minimum32)) == 7, "minimum32 big cursor")
    require(data[3] == (128 as u8) and data[4] == (0 as u8) and data[5] == (0 as u8) and data[6] == (0 as u8), "minimum32 big exact bytes")
    require(value32(read_i32_be(data, 3, 7)) == (minimum32 as i32), "minimum32 big read")
    require(value64(write_i32_le(data, 3, 7, minimum32)) == 7, "minimum32 little cursor")
    require(data[3] == (0 as u8) and data[4] == (0 as u8) and data[5] == (0 as u8) and data[6] == (128 as u8), "minimum32 little exact bytes")
    require(value32(read_i32_le(data, 3, 7)) == (minimum32 as i32), "minimum32 little read")
    require(value64(write_i32_be(data, 3, 7, maximum32)) == 7, "maximum32 big cursor")
    require(data[3] == (127 as u8) and data[4] == (255 as u8) and data[5] == (255 as u8) and data[6] == (255 as u8), "maximum32 big exact bytes")
    require(value32(read_i32_be(data, 3, 7)) == (maximum32 as i32), "maximum32 big read")
    require(value64(write_i32_le(data, 3, 7, maximum32)) == 7, "maximum32 little cursor")
    require(data[3] == (255 as u8) and data[4] == (255 as u8) and data[5] == (255 as u8) and data[6] == (127 as u8), "maximum32 little exact bytes")
    require(value32(read_i32_le(data, 3, 7)) == (maximum32 as i32), "maximum32 little read")
    let maximum: i64 = 9223372036854775807
    let minimum: i64 = -maximum - 1
    require(value64(write_i64_be(data, 2, 10, minimum)) == 10, "minimum64 cursor")
    require(data[2] == (128 as u8) and data[9] == (0 as u8), "minimum64 bytes")
    require(value64(read_i64_be(data, 2, 10)) == minimum, "minimum64 read")
    require(value64(write_i64_le(data, 2, 10, maximum)) == 10, "maximum64 cursor")
    require(data[2] == (255 as u8) and data[9] == (127 as u8), "maximum64 bytes")
    require(value64(read_i64_le(data, 2, 10)) == maximum, "maximum64 read")
    require(value64(write_u16_le(data, 0, 2, 65535)) == 2, "maximum16 cursor")
    require(value32(read_u16_le(data, 0, 2)) == 65535, "maximum16 read")
    require(value64(write_u32_be(data, 0, 4, 4294967295 as i64)) == 4, "maximum32 cursor")
    require(value64(read_u32_be(data, 0, 4)) == (4294967295 as i64), "maximum32 read")
    println("binary exact writes and limits ok")

    i = 0
    while i < data.len() do
        data[i] = 37 as u8
        i = i + 1
    end while
    let empty: array<u8> = []
    let invalid = "binary: invalid byte window"
    let short = "binary: truncated byte window"
    let range_error = "binary: value is outside the encoded range"
    require(error32(read_u16_le(empty, 0, 0), short), "empty read")
    require(error32(read_u16_be(data, -1, 12), invalid), "negative offset")
    require(error64(read_u32_le(data, 0, -1), invalid), "negative limit")
    require(error64(read_u32_be(data, 5, 4), invalid), "reversed window")
    require(error32(read_i32_le(data, 0, 13), invalid), "oversize limit")
    require(error32(read_i32_be(data, maximum, maximum), invalid), "overflow offset")
    require(error64(read_i64_le(data, 5, 12), short), "short read")
    require(error64(read_i64_be(data, 12, 12), short), "end read")
    require(error64(write_u16_le(data, -1, 12, 1), invalid), "invalid write")
    require(unchanged(alias) and data.len() == 12, "invalid write changed bytes")
    require(error64(write_u16_be(data, 0, maximum, 1), invalid), "oversize write limit")
    require(unchanged(alias) and data.len() == 12, "oversize write limit changed bytes")
    require(error64(write_u32_le(data, maximum, 12, 1), invalid), "overflow write offset")
    require(unchanged(alias) and data.len() == 12, "overflow write offset changed bytes")
    require(error64(write_u32_be(data, 10, 12, 1), short), "short write")
    require(unchanged(alias) and data.len() == 12, "short write changed bytes")
    require(error64(write_i64_le(empty, 0, 0, 0), short), "empty write")
    require(empty.len() == 0, "empty write grew the array")
    require(error64(write_i64_be(data, 5, 12, 1), short), "short signed write")
    require(unchanged(alias) and data.len() == 12, "short signed write changed bytes")
    require(error64(write_u16_le(data, 0, 2, -1), range_error), "negative u16")
    require(unchanged(alias) and data.len() == 12, "negative u16 changed bytes")
    require(error64(write_u16_be(data, 0, 2, 65536), range_error), "overflow u16")
    require(unchanged(alias) and data.len() == 12, "overflow u16 changed bytes")
    require(error64(write_u32_le(data, 0, 4, -1), range_error), "negative u32")
    require(unchanged(alias) and data.len() == 12, "negative u32 changed bytes")
    require(error64(write_u32_be(data, 0, 4, 4294967296 as i64), range_error), "overflow u32")
    require(unchanged(alias) and data.len() == 12, "overflow u32 changed bytes")
    require(error64(write_i32_le(data, 0, 4, 2147483648 as i64), range_error), "overflow i32")
    require(unchanged(alias) and data.len() == 12, "overflow i32 changed bytes")
    require(error64(write_i32_be(data, 0, 4, -(2147483648 as i64) - 1), range_error), "underflow i32")
    require(unchanged(alias) and data.len() == 12, "underflow i32 changed bytes")
    require(unchanged(alias) and data.len() == 12 and empty.len() == 0, "failed writes changed bytes")
    println("binary failures preserve bytes")

    let mut octet: i32 = 0
    while octet < 256 do
        let mut repeated: i64 = 0
        i = 0
        while i < 8 do
            repeated = repeated | ((octet as i64) << (i * 8))
            i = i + 1
        end while
        require(value64(write_i64_le(data, 2, 10, repeated)) == 10, "pattern little cursor")
        require(value64(read_i64_le(data, 2, 10)) == repeated, "pattern little read")
        i = 2
        while i < 10 do
            require(data[i] == (octet as u8), "pattern little bytes")
            i = i + 1
        end while
        require(value64(write_i64_be(data, 2, 10, repeated)) == 10, "pattern big cursor")
        require(value64(read_i64_be(data, 2, 10)) == repeated, "pattern big read")
        i = 2
        while i < 10 do
            require(data[i] == (octet as u8), "pattern big bytes")
            i = i + 1
        end while
        octet = octet + 1
    end while
    require(data[0] == (37 as u8) and data[11] == (37 as u8), "pattern bounds")
    println("binary all octets ok")
    return 0
end fn
