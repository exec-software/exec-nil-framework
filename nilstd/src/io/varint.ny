@pragma once

module nil.std.io

import nil.std.core

# Shortest-form base-128 encoding: unsigned 32-bit wire values use an i64
# carrier; signed i64 values use ZigZag (not signed LEB128). Reads consume the
# first canonical value in [offset, limit). Advance a caller-owned cursor by
# its canonical size. Writes return the next offset and validate before mutation.

fn varint_window_error(data: array<u8>, offset: i64, limit: i64) -> text
    let size: i64 = data.len() as i64
    if offset < 0 or limit < 0 or offset > limit or limit > size then
        return "binary: invalid byte window"
    end if
    return ""
end fn

# Ny's right shift is arithmetic. Mask it explicitly when treating an i64 as
# an unsigned bit pattern. Both masks are nonnegative i64 constants.
fn varint_shift_seven(bits: i64) -> i64
    let mask: i64 = 144115188075855871
    return (bits >> 7) & mask
end fn

fn varint_zigzag(value: i64) -> i64
    return (value << 1) ^ (value >> 63)
end fn

fn varint_bits_size(bits: i64) -> i32
    let mut remaining: i64 = bits
    let mut size: i32 = 1
    while remaining < 0 or remaining > 127 do
        remaining = varint_shift_seven(remaining)
        size = size + 1
    end while
    return size
end fn

fn varint_read_bits(data: array<u8>, offset: i64, limit: i64, maximum_bytes: i32, final_maximum: i64) -> Result<i64, text>
    let error = varint_window_error(data, offset, limit)
    if error != "" then return Result.Error(error) end if
    let available: i64 = limit - offset
    let mut value: i64 = 0
    let mut i: i32 = 0
    while i < maximum_bytes do
        if (i as i64) >= available then
            return Result.Error("varint: truncated input")
        end if
        let byte: i64 = data[(offset + (i as i64)) as i32] as i64
        let payload: i64 = byte & (127 as i64)
        let continuation: bool = (byte & (128 as i64)) != 0
        # Check before shifting; the final payload has only 4 bits (u32) or
        # 1 bit (ZigZag i64). Continuation at this width is always overflow.
        if i == maximum_bytes - 1 then
            if continuation or payload > final_maximum then
                return Result.Error("varint: encoded value overflows its width")
            end if
        end if
        value = value | (payload << (i * 7))
        if not continuation then
            if i > 0 and payload == 0 then
                return Result.Error("varint: non-canonical encoding")
            end if
            return Result.Ok(value)
        end if
        i = i + 1
    end while
    return Result.Error("varint: encoded value overflows its width")
end fn

fn varint_write_bits(data: array<u8>, offset: i64, limit: i64, bits: i64) -> Result<i64, text>
    # Public callers validate the window and value first; recheck the window
    # here to keep the helper's mutation boundary self-contained.
    let error = varint_window_error(data, offset, limit)
    if error != "" then return Result.Error(error) end if
    let size: i32 = varint_bits_size(bits)
    if (size as i64) > limit - offset then
        return Result.Error("varint: truncated input")
    end if
    let mut remaining: i64 = bits
    let mut i: i32 = 0
    while i < size do
        let mut byte: i64 = remaining & (127 as i64)
        remaining = varint_shift_seven(remaining)
        if i + 1 < size then byte = byte | (128 as i64) end if
        data[(offset + (i as i64)) as i32] = byte as u8
        i = i + 1
    end while
    return Result.Ok(offset + (size as i64))
end fn

pub fn var_u32_size(value: i64) -> Result<i32, text>
    let maximum: i64 = 4294967295
    if value < 0 or value > maximum then
        return Result.Error("varint: value is outside the encoded range")
    end if
    return Result.Ok(varint_bits_size(value))
end fn

pub fn var_i64_size(value: i64) -> i32
    return varint_bits_size(varint_zigzag(value))
end fn

pub fn read_var_u32(data: array<u8>, offset: i64, limit: i64) -> Result<i64, text>
    return varint_read_bits(data, offset, limit, 5, 15)
end fn

pub fn read_var_i64(data: array<u8>, offset: i64, limit: i64) -> Result<i64, text>
    match varint_read_bits(data, offset, limit, 10, 1)
        Result.Error(error) -> return Result.Error(error)
        Result.Ok(bits) ->
            let mask: i64 = 9223372036854775807
            let value: i64 = ((bits >> 1) & mask) ^ -(bits & (1 as i64))
            return Result.Ok(value)
    end match
end fn

pub fn write_var_u32(data: array<u8>, offset: i64, limit: i64, value: i64) -> Result<i64, text>
    let error = varint_window_error(data, offset, limit)
    if error != "" then return Result.Error(error) end if
    match var_u32_size(value)
        Result.Error(message) -> return Result.Error(message)
        Result.Ok(_) -> return varint_write_bits(data, offset, limit, value)
    end match
end fn

pub fn write_var_i64(data: array<u8>, offset: i64, limit: i64, value: i64) -> Result<i64, text>
    return varint_write_bits(data, offset, limit, varint_zigzag(value))
end fn
