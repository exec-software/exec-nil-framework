#pragma once

// Bounded ETP envelope and NIL control-profile CBOR subset. No I/O or opcodes.
// Liz legacy/src/server/etp.h: [payload length:u32 LE][type:u8][CBOR payload].
// The five header bytes are NOT included in length. Exactly one nonempty CBOR
// item is required per frame. This is not the complete Liz application protocol.
//
// Supported RFC 8949 values: null, bool, unsigned integers 0..2^64-1, negative
// integers -1..-2^64 (stored as argument n meaning -1-n), finite IEEE floats,
// byte strings, strict UTF-8 text (including NUL, no normalization), arrays,
// and maps whose keys are text. Reject tags, undefined/other simple values,
// indefinite lengths, invalid/reserved encodings, duplicate keys, trailing
// data and non-finite floats. Decode accepts definite nonpreferred widths and
// arbitrary map order for interoperability, including float16/32/64. Encode
// uses preferred integer/length widths, float64 (preserving -0), and text keys
// ordered by encoded-key bytes (length, then unsigned UTF-8 byte order). Thus
// encoding is deterministic, but is NOT RFC 8949 shortest-float serialization.
//
// Limits are hard ceilings; callers can only tighten them. Root depth is zero;
// each map key counts as a node at child depth. A container's size counts array
// elements or map pairs. Encoded bytes bound total string storage; node and
// container limits additionally bound tree allocations. No runtime dependency.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace nil::etp {

using bytes = std::vector<std::uint8_t>;

struct limits {
    std::size_t max_payload = 64 * 1024;
    std::size_t max_depth = 32;
    std::size_t max_container = 1024;
    std::size_t max_nodes = 4096;
};

class codec_error : public std::runtime_error {
public:
    explicit codec_error(const char* message) : std::runtime_error(message) {}
};

struct value {
    enum class kind { null, boolean, unsigned_integer, negative_integer, floating, binary, text, array, map };
    kind type = kind::null;
    bool boolean = false;
    std::uint64_t integer = 0; // Negative integer argument n represents -1-n.
    double real = 0;
    bytes binary;
    std::string text;
    std::vector<value> array;
    std::vector<std::pair<std::string, value>> map;

    static value flag(bool b) { value v; v.type = kind::boolean; v.boolean = b; return v; }
    static value uint(std::uint64_t n) { value v; v.type = kind::unsigned_integer; v.integer = n; return v; }
    static value negative(std::uint64_t n) { value v; v.type = kind::negative_integer; v.integer = n; return v; }
    static value number(double n) { value v; v.type = kind::floating; v.real = n; return v; }
    static value blob(bytes b) { value v; v.type = kind::binary; v.binary = std::move(b); return v; }
    static value string(std::string s) { value v; v.type = kind::text; v.text = std::move(s); return v; }
    static value list(std::vector<value> a = {}) { value v; v.type = kind::array; v.array = std::move(a); return v; }
    static value object(std::vector<std::pair<std::string, value>> m = {}) { value v; v.type = kind::map; v.map = std::move(m); return v; }
};

namespace detail {
static_assert(sizeof(float) == 4 && sizeof(double) == 8 &&
              std::numeric_limits<float>::is_iec559 && std::numeric_limits<double>::is_iec559,
              "ETP floats require IEEE binary32/binary64");

inline void validate_limits(const limits& l) {
    if (l.max_payload == 0 || l.max_payload > 65536 || l.max_depth > 32 ||
        l.max_container > 1024 || l.max_nodes == 0 || l.max_nodes > 4096)
        throw codec_error("ETP limits exceed supported bounds");
}

inline void valid_utf8(const std::string& s) {
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i++]);
        if (c < 0x80) continue;
        unsigned count = 0, scalar = 0, minimum = 0;
        if (c >= 0xc2 && c <= 0xdf) { count = 1; scalar = c & 31; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { count = 2; scalar = c & 15; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { count = 3; scalar = c & 7; minimum = 0x10000; }
        else throw codec_error("ETP text requires valid UTF-8");
        while (count--) {
            if (i == s.size() || (static_cast<unsigned char>(s[i]) & 0xc0) != 0x80)
                throw codec_error("ETP text requires valid UTF-8");
            scalar = (scalar << 6) | (static_cast<unsigned char>(s[i++]) & 63);
        }
        if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
            throw codec_error("ETP text requires valid UTF-8");
    }
}

inline bool key_less(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return a.size() < b.size();
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
        [](char x, char y) { return static_cast<unsigned char>(x) < static_cast<unsigned char>(y); });
}

class budget {
protected:
    const limits bound;
    std::size_t nodes = 0;
    explicit budget(limits l) : bound(l) { validate_limits(l); }
    void node(std::size_t depth) {
        if (depth > bound.max_depth) throw codec_error("ETP nesting limit exceeded");
        if (nodes == bound.max_nodes) throw codec_error("ETP node limit exceeded");
        ++nodes;
    }
    void container(std::uint64_t count, bool map) const {
        if (count > bound.max_container) throw codec_error("ETP container limit exceeded");
        const auto remaining = bound.max_nodes - nodes;
        if (count > remaining / (map ? 2 : 1)) throw codec_error("ETP node limit exceeded");
    }
};

class encoder : private budget {
    bytes out;
    void room(std::size_t n) const {
        if (n > bound.max_payload - out.size()) throw codec_error("ETP payload limit exceeded");
    }
    void byte(std::uint8_t b) { room(1); out.push_back(b); }
    void big_endian(std::uint64_t n, unsigned width) {
        for (unsigned i = width; i != 0; --i) byte(static_cast<std::uint8_t>(n >> (8 * (i - 1))));
    }
    void argument(unsigned major, std::uint64_t n) {
        const auto prefix = static_cast<std::uint8_t>(major << 5);
        if (n < 24) byte(static_cast<std::uint8_t>(prefix | n));
        else if (n <= 0xff) { byte(prefix | 24); big_endian(n, 1); }
        else if (n <= 0xffff) { byte(prefix | 25); big_endian(n, 2); }
        else if (n <= 0xffffffffULL) { byte(prefix | 26); big_endian(n, 4); }
        else { byte(prefix | 27); big_endian(n, 8); }
    }
    void string(const std::string& s) {
        room(s.size()); // Bound work before UTF-8 validation.
        valid_utf8(s);
        argument(3, s.size());
        room(s.size());
        out.insert(out.end(), s.begin(), s.end());
    }
    void item(const value& v, std::size_t depth) {
        node(depth);
        switch (v.type) {
        case value::kind::null: byte(0xf6); break;
        case value::kind::boolean: byte(v.boolean ? 0xf5 : 0xf4); break;
        case value::kind::unsigned_integer: argument(0, v.integer); break;
        case value::kind::negative_integer: argument(1, v.integer); break;
        case value::kind::floating: {
            if (!std::isfinite(v.real)) throw codec_error("ETP floats must be finite");
            std::uint64_t bits = 0;
            std::memcpy(&bits, &v.real, sizeof(bits));
            byte(0xfb); big_endian(bits, 8); break;
        }
        case value::kind::binary:
            room(v.binary.size()); argument(2, v.binary.size()); room(v.binary.size());
            out.insert(out.end(), v.binary.begin(), v.binary.end()); break;
        case value::kind::text: string(v.text); break;
        case value::kind::array:
            container(v.array.size(), false); argument(4, v.array.size());
            for (const auto& child : v.array) item(child, depth + 1);
            break;
        case value::kind::map: {
            container(v.map.size(), true);
            std::vector<const std::pair<std::string, value>*> ordered;
            ordered.reserve(v.map.size());
            // Bound aggregate key work before sorting; values are checked below.
            std::size_t key_bytes = 0;
            for (const auto& entry : v.map) {
                if (entry.first.size() > bound.max_payload - key_bytes)
                    throw codec_error("ETP payload limit exceeded");
                key_bytes += entry.first.size();
                valid_utf8(entry.first); ordered.push_back(&entry);
            }
            std::sort(ordered.begin(), ordered.end(), [](const auto* a, const auto* b) {
                return key_less(a->first, b->first);
            });
            argument(5, ordered.size());
            const std::string* previous = nullptr;
            for (const auto* entry : ordered) {
                if (previous && *previous == entry->first) throw codec_error("ETP duplicate map key");
                previous = &entry->first;
                node(depth + 1); string(entry->first); item(entry->second, depth + 1);
            }
            break;
        }
        default: throw codec_error("ETP invalid value kind");
        }
    }
public:
    explicit encoder(limits l) : budget(l) {}
    bytes encode(const value& v) { item(v, 0); return std::move(out); }
};

class decoder : private budget {
    const bytes& input;
    std::size_t position = 0;
    std::size_t remaining() const { return input.size() - position; }
    std::uint8_t byte() {
        if (position == input.size()) throw codec_error("ETP truncated CBOR");
        return input[position++];
    }
    std::uint64_t big_endian(unsigned width) {
        if (width > remaining()) throw codec_error("ETP truncated CBOR argument");
        std::uint64_t n = 0;
        while (width--) n = (n << 8) | byte();
        return n;
    }
    std::uint64_t argument(unsigned ai) {
        if (ai < 24) return ai;
        if (ai == 24) return big_endian(1);
        if (ai == 25) return big_endian(2);
        if (ai == 26) return big_endian(4);
        if (ai == 27) return big_endian(8);
        throw codec_error("ETP indefinite or reserved CBOR argument");
    }
    value floating(unsigned ai) {
        double result = 0;
        if (ai == 25) {
            const auto bits = static_cast<std::uint16_t>(big_endian(2));
            const unsigned exponent = (bits >> 10) & 31;
            const unsigned fraction = bits & 1023;
            if (exponent == 31) throw codec_error("ETP floats must be finite");
            result = exponent == 0 ? std::ldexp(static_cast<double>(fraction), -24)
                : std::ldexp(static_cast<double>(1024 + fraction), static_cast<int>(exponent) - 25);
            if ((bits & 0x8000) != 0) result = -result;
        } else if (ai == 26) {
            const auto bits = static_cast<std::uint32_t>(big_endian(4));
            float single = 0; std::memcpy(&single, &bits, sizeof(bits)); result = single;
        } else {
            const auto bits = big_endian(8); std::memcpy(&result, &bits, sizeof(bits));
        }
        if (!std::isfinite(result)) throw codec_error("ETP floats must be finite");
        return value::number(result);
    }
    value item(std::size_t depth) {
        node(depth);
        const unsigned initial = byte(), major = initial >> 5, ai = initial & 31;
        if (major == 7) {
            if (ai == 20 || ai == 21) return value::flag(ai == 21);
            if (ai == 22) return {};
            if (ai >= 25 && ai <= 27) return floating(ai);
            // Simple values 20..22 MUST use their single-byte encoding (RFC 8949 3.3).
            throw codec_error("ETP unsupported CBOR simple value");
        }
        if (major == 6) throw codec_error("ETP CBOR tags are unsupported");
        const auto n = argument(ai);
        if (major == 0) return value::uint(n);
        if (major == 1) return value::negative(n);
        if (major == 2 || major == 3) {
            if (n > remaining()) throw codec_error("ETP truncated CBOR string");
            const auto size = static_cast<std::size_t>(n);
            if (major == 2) {
                bytes b(input.begin() + static_cast<std::ptrdiff_t>(position),
                        input.begin() + static_cast<std::ptrdiff_t>(position + size));
                position += size; return value::blob(std::move(b));
            }
            std::string s(input.begin() + static_cast<std::ptrdiff_t>(position),
                          input.begin() + static_cast<std::ptrdiff_t>(position + size));
            position += size; valid_utf8(s); return value::string(std::move(s));
        }
        const bool is_map = major == 5;
        container(n, is_map);
        if (n > remaining() / (is_map ? 2 : 1)) throw codec_error("ETP truncated CBOR container");
        value result = is_map ? value::object() : value::list();
        if (is_map) {
            result.map.reserve(static_cast<std::size_t>(n));
            std::set<std::string> seen;
            for (std::uint64_t i = 0; i < n; ++i) {
                auto key = item(depth + 1);
                if (key.type != value::kind::text) throw codec_error("ETP map keys must be text");
                if (!seen.insert(key.text).second) throw codec_error("ETP duplicate map key");
                auto child = item(depth + 1);
                result.map.emplace_back(std::move(key.text), std::move(child));
            }
        } else {
            result.array.reserve(static_cast<std::size_t>(n));
            for (std::uint64_t i = 0; i < n; ++i) result.array.push_back(item(depth + 1));
        }
        return result;
    }
public:
    decoder(const bytes& b, limits l) : budget(l), input(b) {
        if (b.empty() || b.size() > bound.max_payload) throw codec_error("ETP invalid CBOR payload size");
    }
    value decode() {
        auto result = item(0);
        if (position != input.size()) throw codec_error("ETP trailing CBOR data");
        return result;
    }
};
} // namespace detail

inline bytes encode_cbor(const value& v, limits l = {}) { return detail::encoder(l).encode(v); }
inline value decode_cbor(const bytes& b, limits l = {}) { return detail::decoder(b, l).decode(); }

struct frame { std::uint8_t type; value payload; };

inline bytes encode_frame(const frame& f, limits l = {}) {
    auto payload = encode_cbor(f.payload, l);
    const auto length = static_cast<std::uint32_t>(payload.size());
    bytes wire;
    wire.reserve(5 + payload.size());
    for (unsigned i = 0; i < 4; ++i) wire.push_back(static_cast<std::uint8_t>(length >> (8 * i)));
    wire.push_back(f.type); wire.insert(wire.end(), payload.begin(), payload.end());
    return wire;
}

// Incremental, single-owner decoder. It retains at most one bounded payload;
// frames are delivered synchronously to the caller, not queued internally.
// Callback must not reenter; exceptions poison the decoder, as do wire errors.
// Previously delivered frames remain delivered on a later error. finish() is
// mandatory at EOF and rejects partial headers/payloads. There is no resync or
// implicit reset after error/EOF. Transport deadlines/backpressure belong to
// the caller; this codec never performs I/O or accepts unbounded wire lengths.
class frame_decoder {
    limits bound;
    std::array<std::uint8_t, 5> header{};
    std::size_t header_size = 0;
    std::size_t expected = 0;
    bytes payload;
    bool failed = false, ended = false, active = false;
public:
    explicit frame_decoder(limits l = {}) : bound(l) { detail::validate_limits(l); }
    frame_decoder(const frame_decoder&) = delete;
    frame_decoder& operator=(const frame_decoder&) = delete;
    template<class Emit>
    void feed(const std::uint8_t* data, std::size_t size, Emit&& emit) {
        if (failed || ended || active) { failed = true; throw codec_error("ETP decoder is not available"); }
        active = true;
        struct leave { bool& flag; ~leave() { flag = false; } } scope{active};
        try {
            if (size && !data) throw codec_error("ETP null input");
            while (size) {
                if (header_size < 5) {
                    const auto take = std::min(size, 5 - header_size);
                    std::copy_n(data, take, header.begin() + static_cast<std::ptrdiff_t>(header_size));
                    data += take; size -= take; header_size += take;
                    if (header_size != 5) continue;
                    std::uint32_t length = 0;
                    for (unsigned i = 0; i < 4; ++i) length |= std::uint32_t(header[i]) << (8 * i);
                    if (length == 0 || length > bound.max_payload) throw codec_error("ETP invalid frame length");
                    expected = length; payload.reserve(expected);
                }
                const auto take = std::min(size, expected - payload.size());
                payload.insert(payload.end(), data, data + take); data += take; size -= take;
                if (payload.size() == expected) {
                    frame decoded{header[4], decode_cbor(payload, bound)};
                    header_size = 0; expected = 0; payload.clear();
                    emit(std::move(decoded));
                    if (failed) throw codec_error("ETP decoder callback reentered");
                }
            }
        } catch (...) { failed = true; payload.clear(); throw; }
    }
    template<class Emit> void feed(const bytes& b, Emit&& emit) { feed(b.data(), b.size(), std::forward<Emit>(emit)); }
    void finish() {
        if (failed || ended || active || header_size != 0) {
            failed = true; throw codec_error("ETP incomplete or unavailable decoder at EOF");
        }
        ended = true;
    }
};
} // namespace nil::etp
