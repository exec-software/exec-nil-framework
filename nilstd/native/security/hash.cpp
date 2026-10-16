#include <string>
#include <vector>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <openssl/md5.h>
#include <openssl/sha.h>

// ML types mapping
typedef int32_t i32;
typedef int64_t i64;
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;

// =============================================================================
// MD5 Hashing
// =============================================================================

extern "C" {

std::string ml_hash_md5(const char* data, size_t len) {
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5((unsigned char*)data, len, digest);

    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
        ss << std::setw(2) << static_cast<int>(digest[i]);
    }
    return ss.str();
}

std::vector<u8> ml_hash_md5_bytes(const char* data, size_t len) {
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5((unsigned char*)data, len, digest);

    std::vector<u8> result;
    result.reserve(MD5_DIGEST_LENGTH);
    for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
        result.push_back(digest[i]);
    }
    return result;
}

// =============================================================================
// SHA-1 Hashing
// =============================================================================

std::string ml_hash_sha1(const char* data, size_t len) {
    unsigned char digest[SHA_DIGEST_LENGTH];
    SHA1((unsigned char*)data, len, digest);

    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
        ss << std::setw(2) << static_cast<int>(digest[i]);
    }
    return ss.str();
}

std::vector<u8> ml_hash_sha1_bytes(const char* data, size_t len) {
    unsigned char digest[SHA_DIGEST_LENGTH];
    SHA1((unsigned char*)data, len, digest);

    std::vector<u8> result;
    result.reserve(SHA_DIGEST_LENGTH);
    for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
        result.push_back(digest[i]);
    }
    return result;
}

// =============================================================================
// SHA-256 Hashing
// =============================================================================

std::string ml_hash_sha256(const char* data, size_t len) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)data, len, digest);

    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::setw(2) << static_cast<int>(digest[i]);
    }
    return ss.str();
}

std::vector<u8> ml_hash_sha256_bytes(const char* data, size_t len) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)data, len, digest);

    std::vector<u8> result;
    result.reserve(SHA256_DIGEST_LENGTH);
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        result.push_back(digest[i]);
    }
    return result;
}

// =============================================================================
// SHA-512 Hashing
// =============================================================================

std::string ml_hash_sha512(const char* data, size_t len) {
    unsigned char digest[SHA512_DIGEST_LENGTH];
    SHA512((unsigned char*)data, len, digest);

    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < SHA512_DIGEST_LENGTH; i++) {
        ss << std::setw(2) << static_cast<int>(digest[i]);
    }
    return ss.str();
}

std::vector<u8> ml_hash_sha512_bytes(const char* data, size_t len) {
    unsigned char digest[SHA512_DIGEST_LENGTH];
    SHA512((unsigned char*)data, len, digest);

    std::vector<u8> result;
    result.reserve(SHA512_DIGEST_LENGTH);
    for (int i = 0; i < SHA512_DIGEST_LENGTH; i++) {
        result.push_back(digest[i]);
    }
    return result;
}

} // extern "C"