@pragma once

module nil.std.io

import nil.std.core

# CRC-32/ISO-HDLC (IEEE): reflected polynomial 0xedb88320, with initial
# and final XOR 0xffffffff. The caller passes the previous finalized checksum,
# starting at zero, so successive windows can be chained without hidden state.
# Parameters are independently specified in RFC 1952, section 8:
# https://www.rfc-editor.org/rfc/rfc1952.html#section-8
# This portable bit-at-a-time implementation has no table, FFI, or buffer copy.
# CRC detects accidental corruption; it is not cryptographic authentication.
pub fn crc32_update(previous: i64, data: array<u8>, offset: i64, limit: i64) -> Result<i64, text>
    let size: i64 = data.len() as i64
    if offset < 0 or limit < 0 or limit > size or offset > limit then
        return Result.Error("binary: invalid byte window")
    end if
    let mask: i64 = 4294967295 as i64
    if previous < 0 or previous > mask then
        return Result.Error("crc32: previous value is outside the unsigned 32-bit range")
    end if
    let polynomial: i64 = 3988292384 as i64
    let mut state: i64 = previous ^ mask
    let mut cursor: i64 = offset
    while cursor < limit do
        state = state ^ (data[cursor as i32] as i64)
        let mut bit: i32 = 0
        while bit < 8 do
            if (state & (1 as i64)) != 0 then
                state = (state >> 1) ^ polynomial
            else
                state = state >> 1
            end if
            bit = bit + 1
        end while
        cursor = cursor + 1
    end while
    # State stays in 0..0xffffffff, so signed i64 right shifts are zero-filling.
    # Empty valid windows preserve previous, including its high bit.
    return Result.Ok(state ^ mask)
end fn

# Checksum exactly the half-open byte window [offset, limit), without mutation.
pub fn crc32(data: array<u8>, offset: i64, limit: i64) -> Result<i64, text>
    return crc32_update(0, data, offset, limit)
end fn
