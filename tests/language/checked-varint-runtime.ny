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

fn guarded_bytes() -> array<u8>
    let data: array<u8> = []
    let mut i: i32 = 0
    while i < 16 do
        data.push(37 as u8)
        i = i + 1
    end while
    return data
end fn

fn unchanged(data: array<u8>) -> bool
    if data.len() != 16 then return false end if
    let mut i: i32 = 0
    while i < data.len() do
        if data[i] != (37 as u8) then return false end if
        i = i + 1
    end while
    return true
end fn

fn exact_window(data: array<u8>, expected: array<u8>) -> bool
    if data.len() != 16 then return false end if
    let mut i: i32 = 0
    while i < 16 do
        if i >= 2 and i < 2 + expected.len() then
            if data[i] != expected[i - 2] then return false end if
        else
            if data[i] != (37 as u8) then return false end if
        end if
        i = i + 1
    end while
    return true
end fn

fn check_u32(value: i64, expected: array<u8>)
    let width: i64 = expected.len() as i64
    require(value32(var_u32_size(value)) == expected.len(), "u32 exact size")
    require(value64(read_var_u32(expected, 0, width)) == value, "u32 independent bytes read")
    let data = guarded_bytes()
    let alias = data
    require(value64(write_var_u32(data, 2, width + 2, value)) == width + 2, "u32 next cursor")
    require(exact_window(alias, expected), "u32 exact bytes and guards")
    require(value64(read_var_u32(data, 2, width + 2)) == value, "u32 offset read")
    let rejected = guarded_bytes()
    let mut cut: i64 = 0
    while cut < width do
        require(error64(read_var_u32(expected, 0, cut), "varint: truncated input"), "u32 each truncated prefix")
        require(error64(write_var_u32(rejected, 2, cut + 2, value), "varint: truncated input"), "u32 each insufficient write window")
        require(unchanged(rejected), "u32 rejected write changed bytes")
        cut = cut + 1
    end while
end fn

fn check_i64(value: i64, expected: array<u8>)
    let width: i64 = expected.len() as i64
    require(var_i64_size(value) == expected.len(), "i64 exact size")
    require(value64(read_var_i64(expected, 0, width)) == value, "i64 independent bytes read")
    let data = guarded_bytes()
    let alias = data
    require(value64(write_var_i64(data, 2, width + 2, value)) == width + 2, "i64 next cursor")
    require(exact_window(alias, expected), "i64 exact bytes and guards")
    require(value64(read_var_i64(data, 2, width + 2)) == value, "i64 offset read")
    let rejected = guarded_bytes()
    let mut cut: i64 = 0
    while cut < width do
        require(error64(read_var_i64(expected, 0, cut), "varint: truncated input"), "i64 each truncated prefix")
        require(error64(write_var_i64(rejected, 2, cut + 2, value), "varint: truncated input"), "i64 each insufficient write window")
        require(unchanged(rejected), "i64 rejected write changed bytes")
        cut = cut + 1
    end while
end fn

fn main() -> i32
    # Exact vectors were derived independently with unbounded integer arithmetic,
    # then stored as literal octets. They are not produced by the API under test.
    check_u32(0 as i64, [0 as u8])
    check_u32(1 as i64, [1 as u8])
    check_u32(2 as i64, [2 as u8])
    check_u32(127 as i64, [127 as u8])
    check_u32(128 as i64, [128 as u8, 1 as u8])
    check_u32(129 as i64, [129 as u8, 1 as u8])
    check_u32(16383 as i64, [255 as u8, 127 as u8])
    check_u32(16384 as i64, [128 as u8, 128 as u8, 1 as u8])
    check_u32(16385 as i64, [129 as u8, 128 as u8, 1 as u8])
    check_u32(2097151 as i64, [255 as u8, 255 as u8, 127 as u8])
    check_u32(2097152 as i64, [128 as u8, 128 as u8, 128 as u8, 1 as u8])
    check_u32(2097153 as i64, [129 as u8, 128 as u8, 128 as u8, 1 as u8])
    check_u32(268435455 as i64, [255 as u8, 255 as u8, 255 as u8, 127 as u8])
    check_u32(268435456 as i64, [128 as u8, 128 as u8, 128 as u8, 128 as u8, 1 as u8])
    check_u32(268435457 as i64, [129 as u8, 128 as u8, 128 as u8, 128 as u8, 1 as u8])
    check_u32(4294967295 as i64, [255 as u8, 255 as u8, 255 as u8, 255 as u8, 15 as u8])
    check_i64(0 as i64, [0 as u8])
    check_i64(-(1 as i64), [1 as u8])
    check_i64(1 as i64, [2 as u8])
    check_i64(-(64 as i64), [127 as u8])
    check_i64(64 as i64, [128 as u8, 1 as u8])
    check_i64(-(65 as i64), [129 as u8, 1 as u8])
    check_i64(-(8192 as i64), [255 as u8, 127 as u8])
    check_i64(8192 as i64, [128 as u8, 128 as u8, 1 as u8])
    check_i64(-(8193 as i64), [129 as u8, 128 as u8, 1 as u8])
    check_i64(-(1048576 as i64), [255 as u8, 255 as u8, 127 as u8])
    check_i64(1048576 as i64, [128 as u8, 128 as u8, 128 as u8, 1 as u8])
    check_i64(-(1048577 as i64), [129 as u8, 128 as u8, 128 as u8, 1 as u8])
    check_i64(-(134217728 as i64), [255 as u8, 255 as u8, 255 as u8, 127 as u8])
    check_i64(134217728 as i64, [128 as u8, 128 as u8, 128 as u8, 128 as u8, 1 as u8])
    check_i64(-(134217729 as i64), [129 as u8, 128 as u8, 128 as u8, 128 as u8, 1 as u8])
    check_i64(-(17179869184 as i64), [255 as u8, 255 as u8, 255 as u8, 255 as u8, 127 as u8])
    check_i64(17179869184 as i64, [128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 1 as u8])
    check_i64(-(17179869185 as i64), [129 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 1 as u8])
    check_i64(-(2199023255552 as i64), [255 as u8, 255 as u8, 255 as u8, 255 as u8, 255 as u8, 127 as u8])
    check_i64(2199023255552 as i64, [128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 1 as u8])
    check_i64(-(2199023255553 as i64), [129 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 1 as u8])
    check_i64(-(281474976710656 as i64), [255 as u8, 255 as u8, 255 as u8, 255 as u8, 255 as u8, 255 as u8, 127 as u8])
    check_i64(281474976710656 as i64, [128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 1 as u8])
    check_i64(-(281474976710657 as i64), [129 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 1 as u8])
    check_i64(-(36028797018963968 as i64), [255 as u8, 255 as u8, 255 as u8, 255 as u8, 255 as u8, 255 as u8, 255 as u8, 127 as u8])
    check_i64(36028797018963968 as i64, [128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 1 as u8])
    check_i64(-(36028797018963969 as i64), [129 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 1 as u8])
    check_i64(-(4611686018427387904 as i64), [255 as u8, 255 as u8, 255 as u8, 255 as u8, 255 as u8, 255 as u8, 255 as u8, 255 as u8, 127 as u8])
    check_i64(4611686018427387904 as i64, [128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 1 as u8])
    check_i64(-(4611686018427387905 as i64), [129 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 1 as u8])
    check_i64(9223372036854775807 as i64, [254 as u8, 255 as u8, 255 as u8, 255 as u8, 255 as u8, 255 as u8, 255 as u8, 255 as u8, 255 as u8, 1 as u8])
    check_i64(-(9223372036854775807 as i64) - 1, [255 as u8, 255 as u8, 255 as u8, 255 as u8, 255 as u8, 255 as u8, 255 as u8, 255 as u8, 255 as u8, 1 as u8])
    println("varint exact vectors and every truncated prefix ok")

    let mut octet: i32 = 0
    while octet < 128 do
        check_u32(octet as i64, [octet as u8])
        let mut signed: i64 = (octet / 2) as i64
        if octet mod 2 != 0 then signed = -signed - 1 end if
        check_i64(signed, [octet as u8])
        octet = octet + 1
    end while
    println("varint all one-byte values ok")

    let invalid = "binary: invalid byte window"
    let short = "varint: truncated input"
    let overflow = "varint: encoded value overflows its width"
    let canonical = "varint: non-canonical encoding"
    let range_error = "varint: value is outside the encoded range"
    let maximum: i64 = 9223372036854775807
    let minimum: i64 = -maximum - 1
    let data = guarded_bytes()
    let empty: array<u8> = []

    require(error64(read_var_u32(data, -1, 16), invalid), "negative read offset")
    require(error64(read_var_i64(data, 0, -1), invalid), "negative read limit")
    require(error64(read_var_u32(data, 3, 2), invalid), "reversed read window")
    require(error64(read_var_i64(data, 0, 17), invalid), "oversize read limit")
    require(error64(read_var_u32(data, maximum, maximum), invalid), "maximum read offset")
    require(error64(read_var_i64(data, minimum, 16), invalid), "minimum read offset")
    require(error64(read_var_i64(data, 0, minimum), invalid), "minimum read limit")
    require(error64(read_var_u32(empty, 0, 0), short), "empty u32 read")
    require(error64(read_var_i64(empty, 0, 0), short), "empty i64 read")
    require(error64(read_var_u32(data, 16, 16), short), "end u32 read")
    require(error64(read_var_i64(data, 16, 16), short), "end i64 read")

    require(error32(var_u32_size(-1), range_error), "negative u32 size")
    require(error32(var_u32_size(4294967296 as i64), range_error), "oversize u32 size")
    require(error32(var_u32_size(minimum), range_error), "minimum i64 u32 size")
    require(error32(var_u32_size(maximum), range_error), "maximum i64 u32 size")
    require(error64(write_var_u32(data, 0, 16, -1), range_error), "negative u32 write")
    require(unchanged(data), "negative u32 write changed bytes")
    require(error64(write_var_u32(data, 0, 16, 4294967296 as i64), range_error), "oversize u32 write")
    require(unchanged(data), "oversize u32 write changed bytes")
    require(error64(write_var_u32(data, -1, 16, -1), invalid), "window precedes value validation")
    require(unchanged(data), "invalid u32 write changed bytes")
    require(error64(write_var_u32(data, 0, 0, -1), range_error), "value precedes capacity validation")
    require(unchanged(data), "u32 range and capacity failure changed bytes")
    require(error64(write_var_i64(data, 0, -1, 0), invalid), "negative write limit")
    require(unchanged(data), "negative write limit changed bytes")
    require(error64(write_var_i64(data, 0, 17, 0), invalid), "oversize write limit")
    require(unchanged(data), "oversize write limit changed bytes")
    require(error64(write_var_i64(data, 3, 2, 0), invalid), "reversed write window")
    require(unchanged(data), "reversed write window changed bytes")
    require(error64(write_var_u32(data, maximum, maximum, 0), invalid), "maximum write offset")
    require(unchanged(data), "maximum write offset changed bytes")
    require(error64(write_var_i64(data, minimum, 16, 0), invalid), "minimum write offset")
    require(unchanged(data), "minimum write offset changed bytes")
    require(error64(write_var_i64(data, 0, maximum, 0), invalid), "maximum write limit")
    require(unchanged(data), "maximum write limit changed bytes")
    require(error64(write_var_u32(empty, 0, 0, 0), short), "empty u32 write")
    require(empty.len() == 0, "empty u32 write grew array")
    require(error64(write_var_i64(empty, 0, 0, 0), short), "empty i64 write")
    require(empty.len() == 0, "empty i64 write grew array")
    require(error64(write_var_u32(data, 16, 16, 0), short), "end u32 write")
    require(unchanged(data), "end u32 write changed bytes")
    require(error64(write_var_i64(data, 16, 16, 0), short), "end i64 write")
    require(unchanged(data), "end i64 write changed bytes")
    println("varint invalid windows and values preserve bytes")

    # Continuation at the last allowed byte is overflow, even at end of input.
    let over32: array<u8> = [128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 0 as u8]
    require(error64(read_var_u32(over32, 0, 4), short), "four continuations are truncated u32")
    require(error64(read_var_u32(over32, 0, 5), overflow), "five continuations overflow u32")
    require(error64(read_var_u32(over32, 0, 6), overflow), "six-byte u32 forbidden")
    let over64: array<u8> = [128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 128 as u8, 0 as u8]
    require(error64(read_var_i64(over64, 0, 9), short), "nine continuations are truncated i64")
    require(error64(read_var_i64(over64, 0, 10), overflow), "ten continuations overflow i64")
    require(error64(read_var_i64(over64, 0, 11), overflow), "eleven-byte i64 forbidden")

    # Exhaust every final octet: u32 permits 1..15 at byte five, while the
    # tenth i64 byte must be 1. Zero is redundant, not a valid shorter value.
    let last32: array<u8> = [255 as u8, 255 as u8, 255 as u8, 255 as u8, 0 as u8]
    let last64: array<u8> = [255 as u8, 255 as u8, 255 as u8, 255 as u8, 255 as u8, 255 as u8, 255 as u8, 255 as u8, 255 as u8, 0 as u8]
    octet = 0
    while octet < 256 do
        last32[4] = octet as u8
        if octet == 0 then
            require(error64(read_var_u32(last32, 0, 5), canonical), "redundant fifth u32 octet")
        else if octet < 16 then
            let expected: i64 = (octet as i64) * (268435456 as i64) + (268435455 as i64)
            require(value64(read_var_u32(last32, 0, 5)) == expected, "allowed final u32 payload")
        else
            require(error64(read_var_u32(last32, 0, 5), overflow), "invalid final u32 octet")
        end if
        last64[9] = octet as u8
        if octet == 0 then
            require(error64(read_var_i64(last64, 0, 10), canonical), "redundant tenth i64 octet")
        else if octet == 1 then
            require(value64(read_var_i64(last64, 0, 10)) == minimum, "allowed final i64 payload")
        else
            require(error64(read_var_i64(last64, 0, 10), overflow), "invalid final i64 octet")
        end if
        octet = octet + 1
    end while

    # Redundant final zero must fail at every multi-byte length, for both a
    # zero payload and a nonzero value which could be expressed more briefly.
    let redundant: array<u8> = [128 as u8, 0 as u8]
    let mut length: i32 = 2
    while length <= 10 do
        require(error64(read_var_i64(redundant, 0, length as i64), canonical), "redundant zero i64")
        if length <= 5 then
            require(error64(read_var_u32(redundant, 0, length as i64), canonical), "redundant zero u32")
        end if
        redundant[0] = 129 as u8
        require(error64(read_var_i64(redundant, 0, length as i64), canonical), "redundant nonzero i64")
        if length <= 5 then
            require(error64(read_var_u32(redundant, 0, length as i64), canonical), "redundant nonzero u32")
        end if
        redundant[0] = 128 as u8
        redundant[length - 1] = 128 as u8
        redundant.push(0 as u8)
        length = length + 1
    end while
    println("varint overflow and canonicality errors ok")

    # Trailing bytes belong to the next field, not to this read. The caller
    # advances by the canonical size, with no implicit end-of-window check.
    let joined = guarded_bytes()
    let mut cursor: i64 = 2
    cursor = value64(write_var_u32(joined, cursor, 16, 300))
    cursor = value64(write_var_i64(joined, cursor, 16, -65))
    cursor = value64(write_var_u32(joined, cursor, 16, 0))
    require(cursor == 7, "joined write cursor")
    joined[7] = 128 as u8
    let mut read_cursor: i64 = 2
    let first = value64(read_var_u32(joined, read_cursor, 8))
    require(first == 300, "joined first value")
    read_cursor = read_cursor + (value32(var_u32_size(first)) as i64)
    let second = value64(read_var_i64(joined, read_cursor, 8))
    require(second == -65, "joined second value")
    read_cursor = read_cursor + (var_i64_size(second) as i64)
    let third = value64(read_var_u32(joined, read_cursor, 8))
    require(third == 0, "trailing continuation does not affect zero")
    read_cursor = read_cursor + (value32(var_u32_size(third)) as i64)
    require(read_cursor == cursor, "joined read cursor")
    require(error64(read_var_u32(joined, read_cursor, 8), short), "next field is separately truncated")
    require(value64(read_var_i64([0 as u8, 128 as u8], 0, 2)) == 0, "signed zero permits trailing continuation")
    println("varint caller cursor and trailing bytes ok")

    # A bounded record: u32 identifier 300, minimum signed-i64 delta, then a
    # little-endian CRC-32 over those two varint fields only. Python's unbounded
    # base-128 oracle and zlib.crc32 independently give the literal record below
    # and checksum 0xe82e8206. Prefix/suffix bytes are outside both windows.
    let record = guarded_bytes()
    while record.len() < 20 do
        record.push(37 as u8)
    end while
    let record_alias = record
    let mut record_cursor: i64 = 2
    record_cursor = value64(write_var_u32(record, record_cursor, 14, 300))
    record_cursor = value64(write_var_i64(record, record_cursor, 14, minimum))
    require(record_cursor == 14, "record payload cursor")
    let computed_crc = value64(crc32(record, 2, record_cursor))
    require(computed_crc == (3895362054 as i64), "record independent CRC oracle")
    require(value64(write_u32_le(record, record_cursor, 18, computed_crc)) == 18, "record checksum cursor")
    let expected_record: array<u8> = [172 as u8, 2 as u8, 255 as u8, 255 as u8,
        255 as u8, 255 as u8, 255 as u8, 255 as u8, 255 as u8, 255 as u8,
        255 as u8, 1 as u8, 6 as u8, 130 as u8, 46 as u8, 232 as u8]
    let mut record_index: i32 = 0
    while record_index < 20 do
        if record_index >= 2 and record_index < 18 then
            require(record_alias[record_index] == expected_record[record_index - 2], "record independent exact bytes")
        else
            require(record_alias[record_index] == (37 as u8), "record untouched guards")
        end if
        record_index = record_index + 1
    end while

    let mut decoded_cursor: i64 = 2
    let decoded_identifier = value64(read_var_u32(record, decoded_cursor, 14))
    require(decoded_identifier == 300, "record decoded identifier")
    decoded_cursor = decoded_cursor + (value32(var_u32_size(decoded_identifier)) as i64)
    let decoded_delta = value64(read_var_i64(record, decoded_cursor, 14))
    require(decoded_delta == minimum, "record decoded full-range delta")
    decoded_cursor = decoded_cursor + (var_i64_size(decoded_delta) as i64)
    require(decoded_cursor == record_cursor, "record decoded payload cursor")
    let stored_crc = value64(read_u32_le(record, decoded_cursor, 18))
    require(value64(crc32(record, 2, decoded_cursor)) == stored_crc, "record checksum verification")

    let original_first = record[2]
    record[2] = original_first ^ (1 as u8)
    require(value64(crc32(record, 2, decoded_cursor)) != stored_crc, "record detects changed payload")
    require(value64(read_u32_le(record, decoded_cursor, 18)) == stored_crc, "payload change leaves stored checksum")
    record[2] = original_first
    require(value64(crc32(record, 2, decoded_cursor)) == stored_crc, "record restored checksum")
    require(record.len() == 20, "record fixed-size array")
    record_index = 0
    while record_index < 20 do
        if record_index >= 2 and record_index < 18 then
            require(record_alias[record_index] == expected_record[record_index - 2], "record restored exact bytes")
        else
            require(record_alias[record_index] == (37 as u8), "record final guards preserved")
        end if
        record_index = record_index + 1
    end while
    println("varint CRC record composition and tamper detection ok")
    return 0
end fn
