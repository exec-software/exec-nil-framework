@pragma once
# =============================================================================
# Security Module Tests
# =============================================================================
# Tests for cryptographic functionality in Ny.Language.Security
# =============================================================================

module nil.std.security

import nil.std.security
import nil.std.security
import nil.std.security

# =============================================================================
# Hashing Tests
# =============================================================================

pub fn test_hash_md5() -> bool
    let test_string = "Hello, World!"
    let hash = hash_md5(test_string)
    # Known MD5 hash: "Hello, World!" = "65a8e27d8879283831b664bd8b7f0ad4"
    if hash == "65a8e27d8879283831b664bd8b7f0ad4" then
        println("✓ MD5 hash test passed")
        return true
    else
        println("✗ MD5 hash test failed: " + hash)
        return false
    end if
end fn

pub fn test_hash_sha256() -> bool
    let test_string = "Hello, World!"
    let hash = hash_sha256(test_string)
    # Known SHA-256 hash: "Hello, World!" = "315f5bdb76d078c43b8ac0064e4a0164612b1fce77c869345bfc94c75894edd3"
    if hash == "315f5bdb76d078c43b8ac0064e4a0164612b1fce77c869345bfc94c75894edd3" then
        println("✓ SHA-256 hash test passed")
        return true
    else
        println("✗ SHA-256 hash test failed: " + hash)
        return false
    end if
end fn

pub fn test_hash_sha512() -> bool
    let test_string = "Hello, World!"
    let hash = hash_sha512(test_string)
    # Known SHA-512 hash: "Hello, World!" = "309ecc489c12d6eb4cc40f50c902f2b4d0ed77ee511a7c7a9bcd3ca86d4cd86f989dd35bc5ff499670da34255b45b0cfd830e81f605dcf7dc5542e93ae9cd76f"
    let expected = "309ecc489c12d6eb4cc40f50c902f2b4d0ed77ee511a7c7a9bcd3ca86d4cd86f989dd35bc5ff499670da34255b45b0cfd830e81f605dcf7dc5542e93ae9cd76f"
    if hash == expected then
        println("✓ SHA-512 hash test passed")
        return true
    else
        println("✗ SHA-512 hash test failed")
        return false
    end if
end fn

# =============================================================================
# Encryption Tests
# =============================================================================

pub fn test_aes_encrypt_decrypt() -> bool
    let key = "ThisIsA32ByteKey!"
    let plaintext = "Secret Message"
    let encrypted = encrypt_text(key, plaintext)
    let decrypted = decrypt_text(key, encrypted)

    if decrypted == plaintext then
        println("✓ AES encryption/decryption test passed")
        return true
    else
        println("✗ AES encryption/decryption test failed")
        println("  Original: " + plaintext)
        println("  Decrypted: " + decrypted)
        return false
    end if
end fn

pub fn test_base64_encode_decode() -> bool
    let original = "Hello, Base64!"
    let encoded = base64_encode(original as vec<u8>)
    let decoded = base64_decode(encoded) as text

    if decoded == original then
        println("✓ Base64 encode/decode test passed")
        return true
    else
        println("✗ Base64 encode/decode test failed")
        println("  Original: " + original)
        println("  Decoded: " + decoded)
        return false
    end if
end fn

# =============================================================================
# Random Tests
# =============================================================================

pub fn test_random_bytes() -> bool
    let bytes = random_bytes(16)
    if bytes.len() == 16 then
        println("✓ Random bytes test passed")
        return true
    else
        println("✗ Random bytes test failed")
        return false
    end if
end fn

pub fn test_random_int() -> bool
    let min = 0
    let max = 100
    let number = random_int(min, max)

    if number >= min and number <= max then
        println("✓ Random int test passed: " + number as text)
        return true
    else
        println("✗ Random int test failed: " + number as text)
        return false
    end if
end fn

pub fn test_random_password() -> bool
    let password = random_password(16)
    if password.len() == 16 then
        println("✓ Random password test passed")
        return true
    else
        println("✗ Random password test failed")
        return false
    end if
end fn

# =============================================================================
# Run All Tests
# =============================================================================

pub fn main() -> i32
    println("Running Security Module Tests...")
    println("")

    let test1 = test_hash_md5()
    let test2 = test_hash_sha256()
    let test3 = test_hash_sha512()
    let test4 = test_aes_encrypt_decrypt()
    let test5 = test_base64_encode_decode()
    let test6 = test_random_bytes()
    let test7 = test_random_int()
    let test8 = test_random_password()

    println("")
    println("Test Results:")
    println("  Passed: " + (test1 + test2 + test3 + test4 + test5 + test6 + test7 + test8) as text)
    println("  Failed: " + (8 - (test1 + test2 + test3 + test4 + test5 + test6 + test7 + test8)) as text)

    if test1 + test2 + test3 + test4 + test5 + test6 + test7 + test8 == 8 then
        println("✓ All tests passed!")
        return 0
    else
        println("✗ Some tests failed")
        return 1
    end if
end fn