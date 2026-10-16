#include <string>
#include <vector>
#include <cstring>
#include <openssl/aes.h>
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/err.h>
#include <openssl/buffer.h>
#include <openssl/bio.h>

// ML types mapping
typedef int32_t i32;
typedef int64_t i64;
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;

// =============================================================================
// AES Encryption
// =============================================================================

extern "C" {

std::vector<u8> ml_crypto_aes_encrypt(const u8* key, size_t key_len,
                                       const u8* plaintext, size_t plaintext_len) {
    // Check key length
    if (key_len != 16 && key_len != 24 && key_len != 32) {
        // Invalid key length, return empty
        return std::vector<u8>();
    }

    // Create AES key
    AES_KEY aes_key;
    if (AES_set_encrypt_key(key, key_len * 8, &aes_key) < 0) {
        return std::vector<u8>();
    }

    // Pad plaintext to multiple of AES block size (16 bytes)
    size_t padded_len = plaintext_len + (16 - (plaintext_len % 16));
    std::vector<u8> padded_plaintext(padded_len);
    std::memcpy(padded_plaintext.data(), plaintext, plaintext_len);

    // Pad with zeros
    for (size_t i = plaintext_len; i < padded_len; i++) {
        padded_plaintext[i] = 0;
    }

    // Encrypt
    std::vector<u8> ciphertext(padded_len);
    for (size_t i = 0; i < padded_len; i += 16) {
        AES_encrypt(padded_plaintext.data() + i, ciphertext.data() + i, &aes_key);
    }

    return ciphertext;
}

std::vector<u8> ml_crypto_aes_decrypt(const u8* key, size_t key_len,
                                       const u8* ciphertext, size_t ciphertext_len) {
    // Check key length
    if (key_len != 16 && key_len != 24 && key_len != 32) {
        return std::vector<u8>();
    }

    // Create AES key
    AES_KEY aes_key;
    if (AES_set_decrypt_key(key, key_len * 8, &aes_key) < 0) {
        return std::vector<u8>();
    }

    // Decrypt
    std::vector<u8> plaintext(ciphertext_len);
    u8* ciphertext_ptr = const_cast<u8*>(ciphertext);
    for (size_t i = 0; i < ciphertext_len; i += 16) {
        AES_decrypt(ciphertext_ptr + i, plaintext.data() + i, &aes_key);
    }

    return plaintext;
}

// =============================================================================
// PBKDF2 Key Derivation
// =============================================================================

std::vector<u8> ml_crypto_pbkdf2(const char* password, size_t password_len,
                                 const u8* salt, size_t salt_len,
                                 i32 iterations, i32 key_length) {
    // Derive key using PBKDF2-HMAC-SHA256
    std::vector<u8> key(key_length);

    PKCS5_PBKDF2_HMAC(password, password_len,
                      salt, salt_len,
                      iterations,
                      EVP_sha256(),
                      key_length,
                      key.data());

    return key;
}

// =============================================================================
// Base64 Encoding
// =============================================================================

std::string ml_crypto_base64_encode(const u8* data, size_t len) {
    BIO* bio = BIO_new(BIO_s_mem());
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

    bio = BIO_push(b64, bio);

    BIO_write(bio, data, len);
    BIO_flush(bio);

    BUF_MEM* buffer;
    BIO_get_mem_ptr(bio, &buffer);

    std::string result(buffer->data, buffer->length);
    BIO_free_all(bio);

    return result;
}

std::vector<u8> ml_crypto_base64_decode(const char* data, size_t len) {
    BIO* bio = BIO_new_mem_buf(data, len);
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

    bio = BIO_push(b64, bio);

    std::vector<u8> result(len);
    int decoded_len = BIO_read(bio, result.data(), len);
    BIO_free_all(bio);

    result.resize(decoded_len);
    return result;
}

// =============================================================================
// AES-256-GCM Authenticated Encryption
// =============================================================================
// Output layout for encrypt: ciphertext || 16-byte GCM tag
// Decrypt returns plaintext on success; empty vector on tag failure.

std::vector<u8> ml_crypto_aes_gcm_encrypt(const u8* key, size_t key_len,
                                           const u8* nonce, size_t nonce_len,
                                           const u8* plaintext, size_t plaintext_len) {
    if (key_len != 32 || nonce_len != 12) return std::vector<u8>();

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return std::vector<u8>();

    std::vector<u8> ciphertext(plaintext_len + 16); // +16 for GCM tag
    int len = 0;

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return std::vector<u8>();
    }
    EVP_EncryptInit_ex(ctx, nullptr, nullptr, key, nonce);
    EVP_EncryptUpdate(ctx, ciphertext.data(), &len, plaintext, (int)plaintext_len);
    int ciphertext_len = len;
    EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len);
    ciphertext_len += len;

    // Append GCM authentication tag (16 bytes)
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, ciphertext.data() + ciphertext_len);
    EVP_CIPHER_CTX_free(ctx);

    ciphertext.resize(ciphertext_len + 16);
    return ciphertext;
}

std::vector<u8> ml_crypto_aes_gcm_decrypt(const u8* key, size_t key_len,
                                           const u8* nonce, size_t nonce_len,
                                           const u8* ciphertext_with_tag, size_t ct_len) {
    if (key_len != 32 || nonce_len != 12 || ct_len < 16) return std::vector<u8>();

    size_t ciphertext_len = ct_len - 16;
    const u8* tag = ciphertext_with_tag + ciphertext_len;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return std::vector<u8>();

    std::vector<u8> plaintext(ciphertext_len);
    int len = 0;

    EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    EVP_DecryptInit_ex(ctx, nullptr, nullptr, key, nonce);
    EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext_with_tag, (int)ciphertext_len);
    int plaintext_len = len;

    // Set the expected tag before calling Final
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, (void*)tag);
    int ret = EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len);
    EVP_CIPHER_CTX_free(ctx);

    if (ret <= 0) return std::vector<u8>(); // Tag verification failed
    plaintext_len += len;
    plaintext.resize(plaintext_len);
    return plaintext;
}

bool ml_crypto_aes_gcm_verify(const u8* key, size_t key_len,
                               const u8* nonce, size_t nonce_len,
                               const u8* ciphertext_with_tag, size_t ct_len) {
    // Verify by attempting decrypt — returns false on tag failure
    auto result = ml_crypto_aes_gcm_decrypt(key, key_len, nonce, nonce_len,
                                             ciphertext_with_tag, ct_len);
    return !result.empty();
}

} // extern "C"