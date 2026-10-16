@pragma once
# =============================================================================
# Hashing Module - Cryptographic Hash Functions
# =============================================================================
# Design declarations for hash functions:
#   - MD5 (legacy, not cryptographically secure)
#   - SHA-1 (legacy, not cryptographically secure)
#   - SHA-256 (recommended for most use cases)
#   - SHA-512 (for high-security applications)
#
# The native provider is not built into the SDK and its C++ string/vector
# return ABI is not supported by Ny FFI. Only hash_bytes_to_hex below is a
# portable Ny implementation today; the digest declarations are compile-only.
# =============================================================================

module nil.std.security

import nil.std.core

# =============================================================================
# MD5 Hashing
# =============================================================================

@interop("ml_hash_md5")
pub fn md5(data: text) -> text
end fn

@interop("ml_hash_md5_bytes")
pub fn md5_bytes(data: vec<u8>) -> vec<u8>
end fn

# =============================================================================
# SHA-1 Hashing
# =============================================================================

@interop("ml_hash_sha1")
pub fn sha1(data: text) -> text
end fn

@interop("ml_hash_sha1_bytes")
pub fn sha1_bytes(data: vec<u8>) -> vec<u8>
end fn

# =============================================================================
# SHA-256 Hashing
# =============================================================================

@interop("ml_hash_sha256")
pub fn sha256(data: text) -> text
end fn

@interop("ml_hash_sha256_bytes")
pub fn sha256_bytes(data: vec<u8>) -> vec<u8>
end fn

# =============================================================================
# SHA-512 Hashing
# =============================================================================

@interop("ml_hash_sha512")
pub fn sha512(data: text) -> text
end fn

@interop("ml_hash_sha512_bytes")
pub fn sha512_bytes(data: vec<u8>) -> vec<u8>
end fn

# =============================================================================
# Convenience Functions
# =============================================================================

pub fn hash_md5(data: text) -> text
    return md5(data)
end fn

pub fn hash_sha1(data: text) -> text
    return sha1(data)
end fn

pub fn hash_sha256(data: text) -> text
    return sha256(data)
end fn

pub fn hash_sha512(data: text) -> text
    return sha512(data)
end fn

# Hash bytes to hex string
pub fn hash_bytes_to_hex(data: vec<u8>) -> text
    let mut result: text = ""
    let hex_chars = "0123456789abcdef"
    let mut i: i32 = 0
    while i < data.length() do
        match data.get(i)
            Option.None -> return ""
            Option.Some(byte) ->
                let value = byte as i32
                result = result + hex_chars.substr(value / 16, 1)
                result = result + hex_chars.substr(value % 16, 1)
        end match
        i = i + 1
    end while
    return result
end fn

# Hash text and return hex string
pub fn hash_text(data: text, algorithm: text) -> text
    if algorithm == "md5" then
        return hash_md5(data)
    else if algorithm == "sha1" then
        return hash_sha1(data)
    else if algorithm == "sha256" then
        return hash_sha256(data)
    else if algorithm == "sha512" then
        return hash_sha512(data)
    else
        panic("Unknown hash algorithm: " + algorithm)
    end if
end fn
