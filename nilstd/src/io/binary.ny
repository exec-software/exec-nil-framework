@pragma once

module nil.std.io

import nil.std.core

# Checked fixed-width operations on the half-open byte window [offset, limit).
# The caller owns the cursor. Reads do not mutate data; successful writes return
# the next offset. Every write validates the whole window and value before
# changing any byte. No operation grows the array or silently truncates a value.
# u16 values use i32 and u32 values use i64: Ny's unsigned scalar widths beyond
# u8 do not yet have a cross-route contract. Signed i64 covers all 64 raw bits.

fn binary_window_error(data: array<u8>, offset: i64, limit: i64, width: i32) -> text
    let size: i64 = data.len() as i64
    if offset < 0 or limit < 0 or limit > size or offset > limit then
        return "binary: invalid byte window"
    end if
    # Subtraction is safe after the checks above; offset + width could overflow
    # on invalid caller input and must never be used as the initial bounds test.
    if (width as i64) > limit - offset then
        return "binary: truncated byte window"
    end if
    return ""
end fn

fn binary_read_bits(data: array<u8>, offset: i64, limit: i64, width: i32, little: bool) -> Result<i64, text>
    let error = binary_window_error(data, offset, limit, width)
    if error != "" then return Result.Error(error) end if
    let mut value: i64 = 0
    let mut i: i32 = 0
    while i < width do
        let mut shift: i32 = i * 8
        if not little then shift = (width - 1 - i) * 8 end if
        let byte: i64 = data[(offset + (i as i64)) as i32] as i64
        value = value | (byte << shift)
        i = i + 1
    end while
    return Result.Ok(value)
end fn

fn binary_write_bits(data: array<u8>, offset: i64, limit: i64, value: i64, width: i32, little: bool, minimum: i64, maximum: i64) -> Result<i64, text>
    let error = binary_window_error(data, offset, limit, width)
    if error != "" then return Result.Error(error) end if
    if value < minimum or value > maximum then
        return Result.Error("binary: value is outside the encoded range")
    end if
    let mut i: i32 = 0
    while i < width do
        let mut shift: i32 = i * 8
        if not little then shift = (width - 1 - i) * 8 end if
        data[(offset + (i as i64)) as i32] = ((value >> shift) & (255 as i64)) as u8
        i = i + 1
    end while
    return Result.Ok(offset + (width as i64))
end fn

pub fn read_u16_le(data: array<u8>, offset: i64, limit: i64) -> Result<i32, text>
    match binary_read_bits(data, offset, limit, 2, true)
        Result.Ok(value) -> return Result.Ok(value as i32)
        Result.Error(error) -> return Result.Error(error)
    end match
end fn

pub fn read_u16_be(data: array<u8>, offset: i64, limit: i64) -> Result<i32, text>
    match binary_read_bits(data, offset, limit, 2, false)
        Result.Ok(value) -> return Result.Ok(value as i32)
        Result.Error(error) -> return Result.Error(error)
    end match
end fn

pub fn read_u32_le(data: array<u8>, offset: i64, limit: i64) -> Result<i64, text>
    return binary_read_bits(data, offset, limit, 4, true)
end fn

pub fn read_u32_be(data: array<u8>, offset: i64, limit: i64) -> Result<i64, text>
    return binary_read_bits(data, offset, limit, 4, false)
end fn

pub fn read_i32_le(data: array<u8>, offset: i64, limit: i64) -> Result<i32, text>
    match binary_read_bits(data, offset, limit, 4, true)
        Result.Ok(value) -> return Result.Ok(value as i32)
        Result.Error(error) -> return Result.Error(error)
    end match
end fn

pub fn read_i32_be(data: array<u8>, offset: i64, limit: i64) -> Result<i32, text>
    match binary_read_bits(data, offset, limit, 4, false)
        Result.Ok(value) -> return Result.Ok(value as i32)
        Result.Error(error) -> return Result.Error(error)
    end match
end fn

pub fn read_i64_le(data: array<u8>, offset: i64, limit: i64) -> Result<i64, text>
    return binary_read_bits(data, offset, limit, 8, true)
end fn

pub fn read_i64_be(data: array<u8>, offset: i64, limit: i64) -> Result<i64, text>
    return binary_read_bits(data, offset, limit, 8, false)
end fn

pub fn write_u16_le(data: array<u8>, offset: i64, limit: i64, value: i64) -> Result<i64, text>
    return binary_write_bits(data, offset, limit, value, 2, true, 0, 65535)
end fn

pub fn write_u16_be(data: array<u8>, offset: i64, limit: i64, value: i64) -> Result<i64, text>
    return binary_write_bits(data, offset, limit, value, 2, false, 0, 65535)
end fn

pub fn write_u32_le(data: array<u8>, offset: i64, limit: i64, value: i64) -> Result<i64, text>
    return binary_write_bits(data, offset, limit, value, 4, true, 0, 4294967295 as i64)
end fn

pub fn write_u32_be(data: array<u8>, offset: i64, limit: i64, value: i64) -> Result<i64, text>
    return binary_write_bits(data, offset, limit, value, 4, false, 0, 4294967295 as i64)
end fn

pub fn write_i32_le(data: array<u8>, offset: i64, limit: i64, value: i64) -> Result<i64, text>
    return binary_write_bits(data, offset, limit, value, 4, true, -(2147483647 as i64) - 1, 2147483647)
end fn

pub fn write_i32_be(data: array<u8>, offset: i64, limit: i64, value: i64) -> Result<i64, text>
    return binary_write_bits(data, offset, limit, value, 4, false, -(2147483647 as i64) - 1, 2147483647)
end fn

pub fn write_i64_le(data: array<u8>, offset: i64, limit: i64, value: i64) -> Result<i64, text>
    let maximum: i64 = 9223372036854775807
    return binary_write_bits(data, offset, limit, value, 8, true, -maximum - 1, maximum)
end fn

pub fn write_i64_be(data: array<u8>, offset: i64, limit: i64, value: i64) -> Result<i64, text>
    let maximum: i64 = 9223372036854775807
    return binary_write_bits(data, offset, limit, value, 8, false, -maximum - 1, maximum)
end fn
