@pragma once
# =============================================================================
# Cryptography Module - Encryption Functions
# =============================================================================
# Design declarations for encryption and key derivation:
#   - AES encryption/decryption (AES-128, AES-192, AES-256)
#   - Key derivation (PBKDF2)
#
# The preserved OpenSSL provider is not built into the SDK, does not expose an
# ABI-safe Ny vector/result contract, and includes unauthenticated ECB helpers.
# These declarations are compile-only and must not be used for production.
# =============================================================================

module nil.std.security

import nil.std.core
import nil.std.security.random

# =============================================================================
# AES Encryption
# =============================================================================

@interop("ml_crypto_aes_encrypt")
pub fn aes_encrypt(key: vec<u8>, plaintext: vec<u8>) -> vec<u8>
end fn

@interop("ml_crypto_aes_decrypt")
pub fn aes_decrypt(key: vec<u8>, ciphertext: vec<u8>) -> vec<u8>
end fn

# AES with ECB mode (simplified, not recommended for production)
pub fn aes_encrypt_ecb(key: vec<u8>, plaintext: vec<u8>) -> vec<u8>
    return aes_encrypt(key, plaintext)
end fn

pub fn aes_decrypt_ecb(key: vec<u8>, ciphertext: vec<u8>) -> vec<u8>
    return aes_decrypt(key, ciphertext)
end fn

# =============================================================================
# AES-256-GCM Authenticated Encryption (AEAD)
# =============================================================================
# Authenticated encryption — ciphertext includes a 16-byte authentication tag.
# aes_gcm_encrypt: returns ciphertext || tag (ciphertext_len + 16 bytes)
# aes_gcm_decrypt: returns plaintext or error if tag verification fails
#
# key   must be exactly 32 bytes (AES-256)
# nonce must be exactly 12 bytes (GCM standard nonce size)
#
# Required by: ny_files tool (.ny profile encryption)

@interop("ml_crypto_aes_gcm_encrypt")
pub fn aes_gcm_encrypt(key: vec<u8>, nonce: vec<u8>, plaintext: vec<u8>) -> vec<u8>
end fn

@interop("ml_crypto_aes_gcm_decrypt")
pub fn aes_gcm_decrypt(key: vec<u8>, nonce: vec<u8>, ciphertext_with_tag: vec<u8>) -> vec<u8>
end fn

# Returns false if tag verification fails (ciphertext was tampered with)
@interop("ml_crypto_aes_gcm_verify")
pub fn aes_gcm_verify(key: vec<u8>, nonce: vec<u8>, ciphertext_with_tag: vec<u8>) -> bool
end fn

# Convenience: generate a cryptographically random nonce (12 bytes)
pub fn gcm_nonce() -> vec<u8>
    return random_bytes(12)
end fn

# Convenience: generate a cryptographically random AES-256 key (32 bytes)
pub fn aes256_key() -> vec<u8>
    return random_bytes(32)
end fn

# =============================================================================
# Key Derivation
# =============================================================================

@interop("ml_crypto_pbkdf2")
pub fn pbkdf2(password: text, salt: vec<u8>, iterations: i32, key_length: i32) -> vec<u8>
end fn

# =============================================================================
# Base64 Encoding/Decoding
# =============================================================================

@interop("ml_crypto_base64_encode")
pub fn base64_encode(data: vec<u8>) -> text
end fn

@interop("ml_crypto_base64_decode")
pub fn base64_decode(data: text) -> vec<u8>
end fn

# Convenience functions
pub fn encrypt_text(key: text, plaintext: text) -> text
    let key_bytes = key as vec<u8>
    let plaintext_bytes = plaintext as vec<u8>
    let encrypted = aes_encrypt(key_bytes, plaintext_bytes)
    return base64_encode(encrypted)
end fn

pub fn decrypt_text(key: text, ciphertext: text) -> text
    let key_bytes = key as vec<u8>
    let ciphertext_bytes = base64_decode(ciphertext)
    let decrypted = aes_decrypt(key_bytes, ciphertext_bytes)
    return decrypted as text
end fn
