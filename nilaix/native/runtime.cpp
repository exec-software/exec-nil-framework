// runtime.cpp - Runtime library for Ny FFI functions
#include <cstdio>
#include <cstring>
#include <string>
#include <cstdint>

extern "C" {

// print(s: text) -> void
void print(const char* s) {
    printf("%s\n", s);
}

// to_string(i: i32) -> text
static char to_string_buffer[64];
const char* to_string(int32_t i) {
    snprintf(to_string_buffer, sizeof(to_string_buffer), "%d", i);
    return to_string_buffer;
}

// _substring(s: text, start: i32, length: i32) -> text
// Note: Returns static buffer, not thread-safe
static char substring_buffer[4096];
const char* _substring(const char* s, int32_t start, int32_t length) {
    size_t len = strlen(s);
    if (start < 0) start = 0;
    if (start >= (int32_t)len) {
        substring_buffer[0] = '\0';
        return substring_buffer;
    }
    if (length < 0) length = 0;
    if (start + length > (int32_t)len) {
        length = (int32_t)len - start;
    }
    memcpy(substring_buffer, s + start, length);
    substring_buffer[length] = '\0';
    return substring_buffer;
}

// _string_length(s: text) -> i32
int32_t _string_length(const char* s) {
    return (int32_t)strlen(s);
}

// _char_from_int(i: i32) -> text (single character string)
static char char_from_int_buffer[2];
const char* _char_from_int(int32_t i) {
    if (i >= 0 && i <= 127) {
        char_from_int_buffer[0] = (char)i;
        char_from_int_buffer[1] = '\0';
    } else {
        char_from_int_buffer[0] = '\0';
    }
    return char_from_int_buffer;
}

} // extern "C"
