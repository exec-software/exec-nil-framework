#include <string>
#include <vector>
#include <cstring>
#include <random>
#include <openssl/rand.h>

// ML types mapping
typedef int32_t i32;
typedef int64_t i64;
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;

// =============================================================================
// Random Bytes
// =============================================================================

extern "C" {

std::vector<u8> ml_random_bytes(i32 length) {
    std::vector<u8> result(length);
    if (RAND_bytes(result.data(), length) != 1) {
        // Failed to generate random bytes, return zeros
        return std::vector<u8>(length, 0);
    }
    return result;
}

// =============================================================================
// Random Integers
// =============================================================================

std::vector<u8> ml_random_int(i32 min, i32 max) {
    // Use a simple PRNG for integers (not cryptographically secure)
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(min, max);

    i32 result = dis(gen);
    return std::vector<u8>(reinterpret_cast<u8*>(&result),
                           reinterpret_cast<u8*>(&result) + sizeof(i32));
}

// =============================================================================
// Random Text
// =============================================================================

std::string ml_random_text(i32 length, const char* charset) {
    std::string result;
    result.reserve(length);

    static std::random_device rd;
    static std::mt19937 gen(rd());
    size_t charset_len = std::strlen(charset);

    for (i32 i = 0; i < length; i++) {
        std::uniform_int_distribution<> dis(0, charset_len - 1);
        result += charset[dis(gen)];
    }

    return result;
}

} // extern "C"