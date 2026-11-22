// Copyright (C) 2026 Execute Software. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <cstring>
#include <string>

// WebView v1 JSON text grammar. This validates without decoding or normalizing
// JSON.stringify output, including escaped lone UTF-16 surrogates. A configuration
// parser with duplicate-key/scalar restrictions is not interchangeable with it.
// The provider and framework carry identical copies of this versioned grammar.
namespace nilwebview::json_v1 {
namespace detail {
class Parser {
public:
    explicit Parser(const std::string &source) : source_(source) {}
    bool parse() noexcept { whitespace(); return value(0) && (whitespace(), offset_ == source_.size()); }
private:
    void whitespace() noexcept {
        while (offset_ < source_.size() && (source_[offset_] == ' ' || source_[offset_] == '\t' ||
               source_[offset_] == '\r' || source_[offset_] == '\n')) ++offset_;
    }
    bool take(char expected) noexcept {
        if (offset_ == source_.size() || source_[offset_] != expected) return false;
        ++offset_; return true;
    }
    bool literal(const char *text) noexcept {
        const auto size = std::strlen(text);
        if (source_.compare(offset_, size, text) != 0) return false;
        offset_ += size; return true;
    }
    bool string() noexcept {
        if (!take('"')) return false;
        while (offset_ < source_.size()) {
            const auto c = static_cast<unsigned char>(source_[offset_++]);
            if (c == '"') return true;
            if (c < 0x20) return false;
            if (c != '\\') continue;
            if (offset_ == source_.size()) return false;
            const auto escape = source_[offset_++];
            if (escape == 'u') {
                for (int index = 0; index < 4; ++index) {
                    if (offset_ == source_.size()) return false;
                    const auto digit = source_[offset_++];
                    if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f') ||
                          (digit >= 'A' && digit <= 'F'))) return false;
                }
            } else if (!std::strchr("\"\\/bfnrt", escape)) return false;
        }
        return false;
    }
    bool number() noexcept {
        take('-');
        if (!take('0')) {
            if (offset_ == source_.size() || source_[offset_] < '1' || source_[offset_] > '9') return false;
            do { ++offset_; } while (offset_ < source_.size() && source_[offset_] >= '0' && source_[offset_] <= '9');
        }
        if (take('.')) {
            const auto first = offset_;
            while (offset_ < source_.size() && source_[offset_] >= '0' && source_[offset_] <= '9') ++offset_;
            if (offset_ == first) return false;
        }
        if (take('e') || take('E')) {
            if (!take('+')) take('-');
            const auto first = offset_;
            while (offset_ < source_.size() && source_[offset_] >= '0' && source_[offset_] <= '9') ++offset_;
            if (offset_ == first) return false;
        }
        return true;
    }
    bool value(unsigned depth) noexcept {
        if (depth > 64 || offset_ == source_.size()) return false;
        switch (source_[offset_]) {
        case '"': return string();
        case 't': return literal("true");
        case 'f': return literal("false");
        case 'n': return literal("null");
        case '[': {
            ++offset_; whitespace();
            if (take(']')) return true;
            do { whitespace(); if (!value(depth + 1)) return false; whitespace(); } while (take(','));
            return take(']');
        }
        case '{': {
            ++offset_; whitespace();
            if (take('}')) return true;
            do {
                whitespace(); if (!string()) return false;
                whitespace(); if (!take(':')) return false;
                whitespace(); if (!value(depth + 1)) return false;
                whitespace();
            } while (take(','));
            return take('}');
        }
        default: return number();
        }
    }
    const std::string &source_;
    std::size_t offset_ = 0;
};
} // namespace detail

inline bool valid_utf8(const std::string &text) noexcept {
    for (std::size_t index = 0; index < text.size();) {
        const auto first = static_cast<unsigned char>(text[index++]);
        if (!first) return false;
        if (first < 0x80) continue;
        unsigned count = 0;
        std::uint32_t code = 0, minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) { count = 1; code = first & 0x1f; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { count = 2; code = first & 0x0f; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { count = 3; code = first & 7; minimum = 0x10000; }
        else return false;
        if (text.size() - index < count) return false;
        while (count--) {
            const auto byte = static_cast<unsigned char>(text[index++]);
            if ((byte & 0xc0) != 0x80) return false;
            code = (code << 6) | (byte & 0x3f);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
    }
    return true;
}

inline bool valid_json(const std::string &text) noexcept {
    return text.size() <= 1048576 && valid_utf8(text) && detail::Parser(text).parse();
}
} // namespace nilwebview::json_v1
