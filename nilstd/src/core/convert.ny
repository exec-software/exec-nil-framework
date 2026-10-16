@schema
@pragma once

module nil.std.core

# Intrinsic declarations stay at module scope. Impl-member annotations are
# preserved as metadata by NIL today but do not form executable wrappers.
@intrinsic("convert.to_i32")
fn _convert_to_i32(value: text) -> i32
end fn

@intrinsic("convert.to_i64")
fn _convert_to_i64(value: text) -> i64
end fn

@intrinsic("convert.to_f64")
fn _convert_to_f64(value: text) -> f64
end fn

@intrinsic("convert.to_bool")
fn _convert_to_bool(value: text) -> bool
end fn

@intrinsic("convert.try_to_i32")
fn _convert_try_to_i32(value: text) -> Result<i32, text>
end fn

@intrinsic("convert.try_to_i64")
fn _convert_try_to_i64(value: text) -> Result<i64, text>
end fn

@intrinsic("convert.try_to_f64")
fn _convert_try_to_f64(value: text) -> Result<f64, text>
end fn

@intrinsic("convert.try_to_bool")
fn _convert_try_to_bool(value: text) -> Result<bool, text>
end fn

@intrinsic("convert.i32_to_text")
fn _convert_i32_to_text(value: i32) -> text
end fn

@intrinsic("convert.i64_to_text")
fn _convert_i64_to_text(value: i64) -> text
end fn

@intrinsic("convert.f64_to_text")
fn _convert_f64_to_text(value: f64) -> text
end fn

@intrinsic("convert.f64_to_text_precision")
fn _convert_f64_to_text_precision(value: f64, decimals: i32) -> text
end fn

@intrinsic("convert.to_hex")
fn _convert_to_hex(value: i32) -> text
end fn

@intrinsic("convert.to_hex_upper")
fn _convert_to_hex_upper(value: i32) -> text
end fn

@intrinsic("convert.to_hex_padded")
fn _convert_to_hex_padded(value: i32, width: i32) -> text
end fn

@intrinsic("convert.from_hex")
fn _convert_from_hex(value: text) -> i32
end fn

@intrinsic("convert.from_hex_i64")
fn _convert_from_hex_i64(value: text) -> i64
end fn

@intrinsic("convert.to_binary")
fn _convert_to_binary(value: i32) -> text
end fn

@intrinsic("convert.from_binary")
fn _convert_from_binary(value: text) -> i32
end fn

@intrinsic("convert.to_octal")
fn _convert_to_octal(value: i32) -> text
end fn

@intrinsic("convert.from_octal")
fn _convert_from_octal(value: text) -> i32
end fn

@intrinsic("convert.from_base")
fn _convert_from_base(value: text, base: i32) -> i64
end fn

@intrinsic("convert.char_to_i32")
fn _convert_char_to_i32(value: text) -> i32
end fn

@intrinsic("convert.i32_to_char")
fn _convert_i32_to_char(value: i32) -> text
end fn

# Hexadecimal byte strings are not signed numeric literals. Keep their digit
# validation in Ny, independent of the more permissive numeric parser.
fn _convert_hex_nibble(character: text) -> i32
    let lower = "0123456789abcdef"
    let upper = "0123456789ABCDEF"
    let mut i: i32 = 0
    while i < 16 do
        if character == lower.substr(i, 1) or character == upper.substr(i, 1) then
            return i
        end if
        i = i + 1
    end while
    return -1
end fn

pub struct Convert
end struct

impl Convert
    # =========================================================================
    # Text -> Numeric Conversions
    # =========================================================================

    # Direct conversions reject malformed input and overflow at runtime.
    # Use the try_* variants when failure is part of normal control flow.
    pub fn to_i32(value: text) -> i32
        return _convert_to_i32(value)
    end fn

    pub fn to_i64(value: text) -> i64
        return _convert_to_i64(value)
    end fn

    pub fn to_f64(value: text) -> f64
        return _convert_to_f64(value)
    end fn

    pub fn to_bool(value: text) -> bool
        return _convert_to_bool(value)
    end fn

    pub fn try_to_i32(value: text) -> Result<i32, text>
        return _convert_try_to_i32(value)
    end fn

    pub fn try_to_i64(value: text) -> Result<i64, text>
        return _convert_try_to_i64(value)
    end fn

    pub fn try_to_f64(value: text) -> Result<f64, text>
        return _convert_try_to_f64(value)
    end fn

    pub fn try_to_bool(value: text) -> Result<bool, text>
        return _convert_try_to_bool(value)
    end fn

    # =========================================================================
    # Numeric -> Text Conversions
    # =========================================================================

    pub fn i32_to_text(value: i32) -> text
        return _convert_i32_to_text(value)
    end fn

    pub fn i64_to_text(value: i64) -> text
        return _convert_i64_to_text(value)
    end fn

    pub fn f64_to_text(value: f64) -> text
        return _convert_f64_to_text(value)
    end fn

    pub fn f64_to_text_precision(value: f64, decimals: i32) -> text
        return _convert_f64_to_text_precision(value, decimals)
    end fn

    pub fn bool_to_text(value: bool) -> text
        if value then return "true" else return "false" end if
    end fn

    # =========================================================================
    # Hex Conversions
    # =========================================================================

    pub fn to_hex(value: i32) -> text
        return _convert_to_hex(value)
    end fn

    pub fn to_hex_upper(value: i32) -> text
        return _convert_to_hex_upper(value)
    end fn

    pub fn to_hex_padded(value: i32, width: i32) -> text
        return _convert_to_hex_padded(value, width)
    end fn

    pub fn from_hex(hex_str: text) -> i32
        return _convert_from_hex(hex_str)
    end fn

    pub fn from_hex_i64(hex_str: text) -> i64
        return _convert_from_hex_i64(hex_str)
    end fn

    # =========================================================================
    # Binary String Conversions
    # =========================================================================

    pub fn to_binary(value: i32) -> text
        return _convert_to_binary(value)
    end fn

    pub fn from_binary(bin_str: text) -> i32
        return _convert_from_binary(bin_str)
    end fn

    # =========================================================================
    # Octal Conversions
    # =========================================================================

    pub fn to_octal(value: i32) -> text
        return _convert_to_octal(value)
    end fn

    pub fn from_octal(oct_str: text) -> i32
        return _convert_from_octal(oct_str)
    end fn

    # =========================================================================
    # General Base Conversion
    # =========================================================================

    pub fn from_base(value: text, base: i32) -> i64
        return _convert_from_base(value, base)
    end fn

    # =========================================================================
    # Character Conversions
    # =========================================================================

    # Character conversions operate on one Unicode scalar, not one UTF-8 byte.
    pub fn char_to_i32(c: text) -> i32
        return _convert_char_to_i32(c)
    end fn

    pub fn i32_to_char(code: i32) -> text
        return _convert_i32_to_char(code)
    end fn

    # =========================================================================
    # Byte Array Conversions
    # =========================================================================

    pub fn bytes_to_hex(data: array<u8>) -> text
        let mut result = ""
        let hex_chars = "0123456789abcdef"
        for byte in data
            let hi = (byte as i32) / 16
            let lo = (byte as i32) % 16
            result = result + hex_chars.substr(hi, 1) + hex_chars.substr(lo, 1)
        end for
        return result
    end fn

    # Decode exactly two ASCII hexadecimal digits per byte, in either case.
    # Empty input is valid. Signs, prefixes, whitespace, separators, embedded
    # NULs and non-ASCII digits are errors; no partial array is returned.
    # Output length is exactly half the input's byte length on success.
    pub fn try_hex_to_bytes(hex_str: text) -> Result<array<u8>, text>
        let mut result: array<u8> = []
        let len = hex_str.len()
        if len mod 2 != 0 then
            return Result.Error("hex_to_bytes: input must contain an even number of digits")
        end if
        let mut i: i32 = 0
        while i < len do
            let hi = _convert_hex_nibble(hex_str.substr(i, 1))
            let lo = _convert_hex_nibble(hex_str.substr(i + 1, 1))
            if hi < 0 or lo < 0 then
                return Result.Error("hex_to_bytes: input contains a non-hexadecimal digit")
            end if
            result.push((hi * 16 + lo) as u8)
            i = i + 2
        end while
        return Result.Ok(result)
    end fn

    # Convenience form for callers that consider malformed input fatal.
    pub fn hex_to_bytes(hex_str: text) -> array<u8>
        match Convert.try_hex_to_bytes(hex_str)
            Result.Ok(bytes) -> return bytes
            Result.Error(message) -> panic(message)
        end match
    end fn
end impl

# Provide convenient method-style conversion for primitive integers
impl i32
    pub fn to_text(self: i32) -> text
        return Convert.i32_to_text(self)
    end fn
end impl

# =============================================================================
# Standalone Convenience Functions
# =============================================================================

pub fn parse_i32(value: text) -> i32
    return Convert.to_i32(value)
end fn

pub fn parse_i64(value: text) -> i64
    return Convert.to_i64(value)
end fn

pub fn parse_f64(value: text) -> f64
    return Convert.to_f64(value)
end fn

pub fn parse_bool(value: text) -> bool
    return Convert.to_bool(value)
end fn

pub fn to_hex(value: i32) -> text
    return Convert.to_hex(value)
end fn

pub fn from_hex(hex_str: text) -> i32
    return Convert.from_hex(hex_str)
end fn

# Module-level convenience wrappers for common conversions
# Accessible as core.convert.text_to_f64 etc.
pub fn text_to_f64(value: text) -> f64
    return Convert.to_f64(value)
end fn

pub fn f64_to_text(value: f64) -> text
    return Convert.f64_to_text(value)
end fn

pub fn text_to_i32(value: text) -> i32
    return Convert.to_i32(value)
end fn

pub fn text_to_i64(value: text) -> i64
    return Convert.to_i64(value)
end fn
