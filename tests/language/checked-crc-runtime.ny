import ny.lang.core
import ny.lang.io.*

fn require(value: bool, label: text)
    if not value then panic(label) end if
end fn

fn checksum(result: Result<i64, text>) -> i64
    match result
        Result.Ok(value) -> return value
        Result.Error(error) -> panic(error)
    end match
    return 0
end fn

fn rejects(result: Result<i64, text>, expected: text) -> bool
    match result
        Result.Ok(_) -> return false
        Result.Error(error) -> return error == expected
    end match
end fn

fn split_points(data: array<u8>, expected: i64)
    let size: i64 = data.len() as i64
    let mut split: i64 = 0
    while split <= size do
        let first = checksum(crc32(data, 0, split))
        let second = checksum(crc32_update(first, data, split, size))
        require(second == expected, "CRC split point")
        require(checksum(crc32_update(second, data, size, size)) == expected, "CRC empty suffix")
        split = split + 1
    end while
end fn

fn main() -> i32
    # Independent Python zlib.crc32 oracle, not a second copy of this algorithm:
    # b"123456789" -> cbf43926; bytes(range(256)) -> 29058c73.
    let digits: array<u8> = [49 as u8, 50 as u8, 51 as u8, 52 as u8, 53 as u8,
                             54 as u8, 55 as u8, 56 as u8, 57 as u8]
    let digits_alias = digits
    let empty: array<u8> = []
    let octets: array<u8> = []
    let mut i: i32 = 0
    while i < 256 do
        octets.push(i as u8)
        i = i + 1
    end while
    let octets_alias = octets
    let digits_crc: i64 = 3421780262 as i64
    let octets_crc: i64 = 688229491
    require(checksum(crc32(empty, 0, 0)) == 0, "CRC empty input")
    require(checksum(crc32(digits, 0, 9)) == digits_crc, "CRC standard check value")
    require(checksum(crc32(octets, 0, 256)) == octets_crc, "CRC all octets")
    require(checksum(crc32(digits, 9, 9)) == 0, "CRC empty end window")
    require(checksum(crc32(digits, 4, 4)) == 0, "CRC empty interior window")
    println("crc32 known vectors ok")

    let mixed: array<u8> = [0 as u8, 128 as u8, 255 as u8, 0 as u8, 127 as u8, 1 as u8]
    require(checksum(crc32(mixed, 0, 1)) == (3523407757 as i64), "CRC zero byte")
    require(checksum(crc32(mixed, 1, 2)) == 1069182125, "CRC high-bit byte")
    require(checksum(crc32(mixed, 2, 3)) == (4278190080 as i64), "CRC ff byte")
    require(checksum(crc32(mixed, 0, 6)) == 25248367, "CRC mixed high-bit and zero")
    require(checksum(crc32(octets, 17, 240)) == 1930254136, "CRC bounded subwindow")
    let high: i64 = 2147483648 as i64
    let mask: i64 = 4294967295 as i64
    require(checksum(crc32_update(high, empty, 0, 0)) == high, "CRC empty high-bit state")
    require(checksum(crc32_update(mask, empty, 0, 0)) == mask, "CRC empty maximum state")
    require(checksum(crc32_update(high, mixed, 0, 1)) == (3531796365 as i64), "CRC high-bit seeded zero")
    require(checksum(crc32_update(high, mixed, 1, 2)) == 1060793517, "CRC high-bit seeded byte")
    require(checksum(crc32_update(mask, mixed, 1, 2)) == 306674911, "CRC maximum seeded byte")
    require(checksum(crc32_update(mask, digits, 0, 9)) == (3523400311 as i64), "CRC maximum seeded digits")
    println("crc32 subwindows and finalized seeds ok")

    split_points(empty, 0)
    split_points(digits, digits_crc)
    split_points(mixed, 25248367)
    split_points(octets, octets_crc)
    let mut chunk_size: i64 = 1
    while chunk_size <= 33 do
        let mut cursor: i64 = 0
        let mut previous: i64 = 0
        while cursor < 256 do
            let mut limit: i64 = cursor + chunk_size
            if limit > 256 then limit = 256 end if
            previous = checksum(crc32_update(previous, octets, cursor, limit))
            cursor = limit
        end while
        require(previous == octets_crc, "CRC repeated chunk updates")
        chunk_size = chunk_size + 1
    end while
    println("crc32 every split and chunk chain ok")

    let invalid = "binary: invalid byte window"
    let state_error = "crc32: previous value is outside the unsigned 32-bit range"
    let maximum: i64 = 9223372036854775807
    let minimum: i64 = -maximum - 1
    require(rejects(crc32(digits, -1, 9), invalid), "CRC negative offset")
    require(rejects(crc32(digits, 0, -1), invalid), "CRC negative limit")
    require(rejects(crc32(digits, 5, 4), invalid), "CRC reversed window")
    require(rejects(crc32(digits, 0, 10), invalid), "CRC limit after end")
    require(rejects(crc32(empty, 1, 1), invalid), "CRC empty array invalid window")
    require(rejects(crc32(digits, minimum, 9), invalid), "CRC minimum offset")
    require(rejects(crc32(digits, 0, minimum), invalid), "CRC minimum limit")
    require(rejects(crc32(digits, maximum, maximum), invalid), "CRC maximum window")
    require(rejects(crc32(digits, 0, maximum), invalid), "CRC maximum limit")
    require(rejects(crc32_update(0, digits, -1, 9), invalid), "CRC update negative offset")
    require(rejects(crc32_update(0, digits, 0, -1), invalid), "CRC update negative limit")
    require(rejects(crc32_update(0, digits, 5, 4), invalid), "CRC update reversed window")
    require(rejects(crc32_update(0, digits, 0, 10), invalid), "CRC update limit after end")
    require(rejects(crc32_update(0, digits, minimum, maximum), invalid), "CRC update extreme window")
    require(rejects(crc32_update(-1, empty, 0, 0), state_error), "CRC negative empty state")
    require(rejects(crc32_update(mask + 1, empty, 0, 0), state_error), "CRC overflowing empty state")
    require(rejects(crc32_update(minimum, digits, 0, 9), state_error), "CRC minimum state")
    require(rejects(crc32_update(maximum, digits, 0, 9), state_error), "CRC maximum state")
    require(rejects(crc32_update(-1, empty, -1, 0), invalid), "CRC window precedes negative state")
    require(rejects(crc32_update(maximum, digits, 0, maximum), invalid), "CRC window precedes oversized state")
    require(checksum(crc32(digits, 0, 9)) == digits_crc, "CRC no hidden state after failure")
    println("crc32 invalid windows and states rejected")

    require(empty.len() == 0 and digits.len() == 9 and octets.len() == 256, "CRC input sizes preserved")
    i = 0
    while i < 9 do
        require(digits_alias[i] == ((49 + i) as u8), "CRC digits unchanged")
        i = i + 1
    end while
    i = 0
    while i < 256 do
        require(octets_alias[i] == (i as u8), "CRC octets unchanged")
        i = i + 1
    end while
    require(mixed.len() == 6 and mixed[0] == (0 as u8) and mixed[1] == (128 as u8) and mixed[2] == (255 as u8), "CRC mixed prefix unchanged")
    require(mixed[3] == (0 as u8) and mixed[4] == (127 as u8) and mixed[5] == (1 as u8), "CRC mixed suffix unchanged")
    println("crc32 input bytes unchanged")
    return 0
end fn
