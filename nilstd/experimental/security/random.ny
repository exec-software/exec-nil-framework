@pragma once
# =============================================================================
# Random Number Generation
# =============================================================================
# Design declarations for cryptographically secure random generation:
#   - Random bytes
#   - Random integers
#   - Random text strings
#
# The preserved native provider is not linked into the SDK and does not yet
# have an ABI-safe error-return contract. These declarations are compile-only.
# =============================================================================

module nil.std.security

import nil.std.core

# =============================================================================
# Random Bytes
# =============================================================================

@interop("ml_random_bytes")
pub fn random_bytes(length: i32) -> vec<u8>
end fn

# =============================================================================
# Random Integers
# =============================================================================

@interop("ml_random_int")
pub fn random_int(min: i32, max: i32) -> i32
end fn

# =============================================================================
# Random Text
# =============================================================================

@interop("ml_random_text")
pub fn random_text(length: i32, charset: text) -> text
end fn

# Convenience functions
pub fn random_password(length: i32) -> text
    let charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*"
    return random_text(length, charset)
end fn

pub fn random_alphanumeric(length: i32) -> text
    let charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    return random_text(length, charset)
end fn

pub fn random_hex(length: i32) -> text
    let charset = "0123456789abcdef"
    return random_text(length, charset)
end fn
