@pragma once
# =============================================================================
# Base64 Module - Base64 Encoding/Decoding
# =============================================================================
# Provides Base64 encoding and decoding for data representation:
#   - Base64 encode binary data to text
#   - Base64 decode text to binary data
#
# Decoding is strict: malformed padding, non-alphabet characters, and
# non-canonical pad bits return Result.Error instead of being ignored.
# =============================================================================

module nil.std.security

import nil.std.core
import nil.std.collections

pub fn encode(data: vec<u8>) -> text
    let alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
    let mut result: text = ""
    let mut i: i32 = 0
    while i < data.length() do
        let remaining = data.length() - i
        let mut b0: i32 = 0
        let mut b1: i32 = 0
        let mut b2: i32 = 0
        match data.get(i)
            Option.None -> return ""
            Option.Some(value) -> b0 = value as i32
        end match
        if remaining > 1 then
            match data.get(i + 1)
                Option.None -> return ""
                Option.Some(value) -> b1 = value as i32
            end match
        end if
        if remaining > 2 then
            match data.get(i + 2)
                Option.None -> return ""
                Option.Some(value) -> b2 = value as i32
            end match
        end if

        let packed = (b0 << 16) | (b1 << 8) | b2
        result = result + alphabet.substr((packed >> 18) & 63, 1)
        result = result + alphabet.substr((packed >> 12) & 63, 1)
        if remaining > 1 then
            result = result + alphabet.substr((packed >> 6) & 63, 1)
        else
            result = result + "="
        end if
        if remaining > 2 then
            result = result + alphabet.substr(packed & 63, 1)
        else
            result = result + "="
        end if
        i = i + 3
    end while
    return result
end fn

fn base64_value(character: text) -> i32
    let alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
    let mut i: i32 = 0
    while i < 64 do
        if alphabet.substr(i, 1) == character then return i end if
        i = i + 1
    end while
    return -1
end fn

pub fn decode(data: text) -> Result<vec<u8>, text>
    let mut result: vec<u8> = vec<u8>.new()
    let length: i32 = data.len()
    if length == 0 then return Result.Ok(result) end if
    if length % 4 != 0 then
        return Result.Error("Base64 length must be a multiple of four")
    end if

    let mut i: i32 = 0
    while i < length do
        let c0 = data.substr(i, 1)
        let c1 = data.substr(i + 1, 1)
        let c2 = data.substr(i + 2, 1)
        let c3 = data.substr(i + 3, 1)
        let v0 = base64_value(c0)
        let v1 = base64_value(c1)
        if v0 < 0 or v1 < 0 then
            return Result.Error("Base64 contains an invalid character")
        end if

        if c2 == "=" then
            if c3 != "=" or i + 4 != length then
                return Result.Error("Base64 padding is misplaced")
            end if
            if (v1 & 15) != 0 then
                return Result.Error("Base64 has non-canonical pad bits")
            end if
            result.push(((v0 << 2) | (v1 >> 4)) as u8)
        else
            let v2 = base64_value(c2)
            if v2 < 0 then
                return Result.Error("Base64 contains an invalid character")
            end if
            result.push(((v0 << 2) | (v1 >> 4)) as u8)

            if c3 == "=" then
                if i + 4 != length then
                    return Result.Error("Base64 padding is misplaced")
                end if
                if (v2 & 3) != 0 then
                    return Result.Error("Base64 has non-canonical pad bits")
                end if
                result.push((((v1 & 15) << 4) | (v2 >> 2)) as u8)
            else
                let v3 = base64_value(c3)
                if v3 < 0 then
                    return Result.Error("Base64 contains an invalid character")
                end if
                result.push((((v1 & 15) << 4) | (v2 >> 2)) as u8)
                result.push((((v2 & 3) << 6) | v3) as u8)
            end if
        end if
        i = i + 4
    end while
    return Result.Ok(result)
end fn
