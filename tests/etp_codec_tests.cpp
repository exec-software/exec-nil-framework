// Standalone codec checks. --golden and --roundtrip-hex are bounded, test-only
// interfaces consumed by etp_golden_frames.py; no server or live IPC is used.
#include "../common/etp_codec.hpp"
#include <iostream>
#include <functional>

using namespace nil::etp;

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> static void rejects(F&& f, const char* message) {
    try { f(); } catch (const codec_error&) { return; }
    throw std::runtime_error(message);
}
static bytes unhex(const std::string& text) {
    if (text.size() > 2 * (65536 + 5) || text.size() % 2) throw codec_error("invalid test hex size");
    auto digit = [](char c) -> unsigned {
        if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
        throw codec_error("invalid test hex digit");
    };
    bytes result;
    for (std::size_t i = 0; i < text.size(); i += 2)
        result.push_back(static_cast<std::uint8_t>(16 * digit(text[i]) + digit(text[i + 1])));
    return result;
}
static std::string hex(const bytes& b) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (auto x : b) { out += digits[x >> 4]; out += digits[x & 15]; }
    return out;
}
static frame decode_one(const bytes& wire) {
    frame result{};
    unsigned count = 0;
    frame_decoder decoder;
    decoder.feed(wire, [&](frame f) { ++count; result = std::move(f); });
    decoder.finish();
    if (count != 1) throw codec_error("expected one test frame");
    return result;
}
static std::vector<std::pair<std::string, value>> goldens() {
    bytes all;
    for (unsigned i = 0; i < 256; ++i) all.push_back(static_cast<std::uint8_t>(i));
    return {
        {"null", {}}, {"false", value::flag(false)}, {"true", value::flag(true)},
        {"integers", value::list({value::uint(0), value::uint(23), value::uint(24), value::uint(255),
            value::uint(256), value::uint(65535), value::uint(65536), value::uint(0xffffffffULL),
            value::uint(0x100000000ULL), value::uint(UINT64_MAX), value::negative(0),
            value::negative(23), value::negative(24), value::negative(UINT64_MAX)})},
        {"binary", value::blob(std::move(all))}, {"empty_binary", value::blob({})},
        {"text", value::string(u8"caf\u00e9 \u4e2d\u6587 \U0001f642 e\u0301")},
        {"nul_text", value::string(std::string("a\0b", 3))}, {"empty_text", value::string("")},
        {"map", value::object({{"zz", value::uint(2)}, {"b", value::blob({0, 255})},
            {"a", value::list({value::flag(true), {}})}, {u8"\u00e9", value::string("utf8")}})},
        {"empty_map", value::object()}, {"empty_array", value::list()},
        {"floats", value::list({value::number(0), value::number(-0.0), value::number(1.5),
            value::number(-123456.25), value::number(std::numeric_limits<double>::denorm_min()),
            value::number(std::numeric_limits<double>::max())})}
    };
}
static void check_cbor() {
    for (const auto& g : goldens()) {
        const auto encoded = encode_cbor(g.second);
        require(encode_cbor(decode_cbor(encoded)) == encoded, "golden CBOR did not roundtrip");
    }
    require(decode_cbor(unhex("4100")).type == value::kind::binary, "binary became text");
    require(decode_cbor(unhex("6100")).type == value::kind::text, "text became binary");
    require(decode_cbor(unhex("3bffffffffffffffff")).integer == UINT64_MAX, "negative domain narrowed");
    require(hex(encode_cbor(decode_cbor(unhex("1b0000000000000001")))) == "01", "nonpreferred uint rejected");
    require(hex(encode_cbor(decode_cbor(unhex("580100")))) == "4100", "nonpreferred length rejected");
    require(hex(encode_cbor(decode_cbor(unhex("b800")))) == "a0", "nonpreferred map rejected");
    require(hex(encode_cbor(decode_cbor(unhex("a2627a7a02616101")))) == "a2616101627a7a02", "map order not normalized");
    for (const auto* s : {"f93e00", "fa3fc00000", "fb3ff8000000000000"})
        require(decode_cbor(unhex(s)).real == 1.5, "float width decode mismatch");
    for (const auto* s : {"f98000", "fa80000000", "fb8000000000000000"}) {
        const auto v = decode_cbor(unhex(s));
        require(v.real == 0 && std::signbit(v.real), "negative zero lost during decode");
        require(hex(encode_cbor(v)) == "fb8000000000000000", "negative zero lost during encode");
    }
    require(decode_cbor(unhex("f90001")).real == std::ldexp(1.0, -24), "half subnormal mismatch");
    require(decode_cbor(unhex("f97bff")).real == 65504, "half max mismatch");
    require(decode_cbor(unhex("fa00000001")).real == std::ldexp(1.0, -149), "single subnormal mismatch");
    const std::vector<std::string> bad = {
        "", "1817ff", "18", "19ff", "1affffff", "1bffffffffffffff", "1c", "1d", "1e", "1f",
        "5bffffffffffffffff", "7bffffffffffffffff", "9bffffffffffffffff", "bbffffffffffffffff",
        "5f40ff", "7f60ff", "9fff", "bfff", "c000", "d9d9f7f6", "ff", "f0", "f7", "f814", "f818",
        "f93c", "fa3f8000", "fb3ff00000000000", "f97c00", "f9fc00", "f97e00", "f97c01",
        "fa7f800000", "faff800000", "fa7fc00000", "fb7ff0000000000000", "fbfff0000000000000",
        "fb7ff8000000000000", "fb7ff0000000000001", "f6f6", "8100f6", "81", "a16161", "a10100",
        "a2616100616101", "a261610078016101", "6180", "62c080", "62c1bf", "62c241", "62e282",
        "63eda080", "64f4908080", "64f5808080", "65f888808080", "61ff"
    };
    for (const auto& b : bad) rejects([&] { decode_cbor(unhex(b)); }, "invalid CBOR accepted");
    rejects([] { encode_cbor(value::number(std::numeric_limits<double>::infinity())); }, "infinite encode accepted");
    rejects([] { encode_cbor(value::number(std::numeric_limits<double>::quiet_NaN())); }, "NaN encode accepted");
    rejects([] { encode_cbor(value::string(std::string("\xed\xa0\x80", 3))); }, "invalid text encode accepted");
    rejects([] { encode_cbor(value::object({{"x", {}}, {"x", {}}})); }, "duplicate encode accepted");
    rejects([] { encode_cbor(value::object({{std::string("\x80", 1), {}}})); }, "invalid key encode accepted");
    // UTF-8 is not normalized: these two keys must remain distinct.
    require(decode_cbor(encode_cbor(value::object({{u8"\u00e9", {}}, {u8"e\u0301", {}}}))).map.size() == 2,
        "text keys were normalized");
}
static void check_limits() {
    auto nested = value{};
    for (unsigned i = 0; i < 32; ++i) nested = value::list({std::move(nested)});
    const auto accepted = encode_cbor(nested); decode_cbor(accepted);
    nested = value::list({std::move(nested)});
    rejects([&] { encode_cbor(nested); }, "encode depth bound absent");
    bytes deep(33, 0x81); deep.push_back(0xf6);
    rejects([&] { decode_cbor(deep); }, "decode depth bound absent");
    auto container = value::list(std::vector<value>(1024));
    decode_cbor(encode_cbor(container)); container.array.push_back({});
    rejects([&] { encode_cbor(container); }, "encode container bound absent");
    bytes many{0x99, 0x04, 0x01}; many.insert(many.end(), 1025, 0xf6);
    rejects([&] { decode_cbor(many); }, "decode container bound absent");
    limits small; small.max_nodes = 4;
    auto map = value::object({{"a", {}}, {"b", {}}});
    rejects([&] { encode_cbor(map, small); }, "encode map keys not counted as nodes");
    rejects([&] { decode_cbor(encode_cbor(map), small); }, "decode map keys not counted as nodes");
    small.max_nodes = 5; decode_cbor(encode_cbor(map, small), small);
    // The node budget is global, not reset for each individually valid container.
    auto nodes = value::list({value::list(std::vector<value>(1023)),
        value::list(std::vector<value>(1023)), value::list(std::vector<value>(1023)),
        value::list(std::vector<value>(1022))});
    const auto exact_nodes = encode_cbor(nodes); // root + 4 lists + 4091 leaves = 4096.
    decode_cbor(exact_nodes);
    nodes.array.back().array.push_back({});
    rejects([&] { encode_cbor(nodes); }, "global encode node bound absent");
    bytes too_many_nodes{0x84};
    for (unsigned i = 0; i < 4; ++i) {
        too_many_nodes.insert(too_many_nodes.end(), {0x99, 0x03, 0xff});
        too_many_nodes.insert(too_many_nodes.end(), 1023, 0xf6);
    }
    rejects([&] { decode_cbor(too_many_nodes); }, "global decode node bound absent");
    small.max_depth = 0;
    decode_cbor(encode_cbor(value::object(), small), small);
    rejects([&] { decode_cbor(unhex("a16161f6"), small); }, "map child depth ignored");
    auto exact = value::blob(bytes(65533, 0xff));
    const auto maximum = encode_cbor(exact);
    require(maximum.size() == 65536, "incorrect exact payload bound");
    decode_cbor(maximum); decode_one(encode_frame({0xff, exact}));
    exact.binary.push_back(0);
    rejects([&] { encode_cbor(exact); }, "encode payload bound absent");
    bytes large(65537, 0);
    rejects([&] { decode_cbor(large); }, "decode payload bound absent");
    limits tight; tight.max_payload = 1;
    require(encode_cbor({}, tight).size() == 1, "tight scalar cap failed");
    rejects([&] { encode_cbor(value::uint(24), tight); }, "tight encode cap ignored");
    rejects([&] { decode_cbor(unhex("1818"), tight); }, "tight decode cap ignored");
    for (unsigned which = 0; which < 6; ++which) {
        limits invalid;
        switch (which) {
        case 0: invalid.max_payload = 0; break; case 1: invalid.max_payload = 65537; break;
        case 2: invalid.max_depth = 33; break; case 3: invalid.max_container = 1025; break;
        case 4: invalid.max_nodes = 0; break; default: invalid.max_nodes = 4097;
        }
        rejects([&] { encode_cbor({}, invalid); }, "invalid limits accepted");
        rejects([&] { frame_decoder d(invalid); }, "invalid frame limits accepted");
    }
}
static void check_frames() {
    const auto first = encode_frame({0xa5, value::string(u8"caf\u00e9")});
    const auto second = encode_frame({0x00, value::blob({0, 255})});
    require(hex(first) == "06000000a565636166c3a9", "payload-only LE frame changed");
    bytes both = first; both.insert(both.end(), second.begin(), second.end());
    for (std::size_t split = 0; split <= both.size(); ++split) {
        frame_decoder decoder;
        std::vector<bytes> seen;
        auto receive = [&](frame f) { seen.push_back(encode_frame(f)); };
        decoder.feed(both.data(), split, receive);
        decoder.feed(both.data() + split, both.size() - split, receive);
        decoder.finish();
        require(seen == std::vector<bytes>({first, second}), "split/coalesced frames changed");
    }
    frame_decoder singles;
    unsigned count = 0;
    for (const auto b : both) singles.feed(&b, 1, [&](frame) { ++count; });
    singles.finish(); require(count == 2, "bytewise frames failed");
    for (std::size_t length = 1; length < first.size(); ++length) {
        frame_decoder d; unsigned emitted = 0;
        d.feed(first.data(), length, [&](frame) { ++emitted; });
        rejects([&] { d.finish(); }, "truncated frame accepted at EOF");
        require(emitted == 0, "truncated frame emitted");
        rejects([&] { d.feed(first, [](frame) {}); }, "decoder recovered after truncated EOF");
    }
    for (const auto* bad : {"0000000000", "0100010000", "ffffffff00", "0100000000ff", "0200000000f6f6"}) {
        frame_decoder d;
        rejects([&] { d.feed(unhex(bad), [](frame) {}); }, "invalid frame accepted");
        rejects([&] { d.feed(first, [](frame) {}); }, "decoder recovered after invalid frame");
    }
    frame_decoder prefix;
    auto valid_then_bad = first;
    valid_then_bad.insert(valid_then_bad.end(), {0, 0, 0, 0, 0});
    unsigned delivered = 0;
    rejects([&] { prefix.feed(valid_then_bad, [&](frame) { ++delivered; }); }, "bad suffix accepted");
    require(delivered == 1, "valid prefix delivery changed on bad suffix");
    frame_decoder ended;
    ended.feed(nullptr, 0, [](frame) {}); ended.finish();
    rejects([&] { ended.feed(first, [](frame) {}); }, "decoder accepted feed after EOF");
    frame_decoder callback;
    bool threw = false;
    try { callback.feed(first, [](frame) { throw std::runtime_error("test callback"); }); }
    catch (const std::runtime_error&) { threw = true; }
    require(threw, "callback exception lost");
    rejects([&] { callback.feed(first, [](frame) {}); }, "decoder recovered after callback failure");
    frame_decoder reentrant;
    rejects([&] { reentrant.feed(first, [&](frame) {
        try { reentrant.feed(nullptr, 0, [](frame) {}); } catch (const codec_error&) {}
    }); }, "caught callback reentry did not poison decoder");
    frame_decoder premature_finish;
    rejects([&] { premature_finish.feed(first, [&](frame) {
        try { premature_finish.finish(); } catch (const codec_error&) {}
    }); }, "caught callback EOF did not poison decoder");
    frame_decoder null_input;
    rejects([&] { null_input.feed(nullptr, 1, [](frame) {}); }, "null nonempty feed accepted");
    // Large coalesced input delivers incrementally, with no retained-frame queue.
    const auto unit = encode_frame({0xfe, {}});
    bytes batch;
    for (unsigned i = 0; i < 10000; ++i) batch.insert(batch.end(), unit.begin(), unit.end());
    frame_decoder streaming; count = 0;
    streaming.feed(batch, [&](frame f) { require(f.type == 0xfe, "opaque type changed"); ++count; });
    streaming.finish(); require(count == 10000, "coalesced frame count changed");
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--golden") {
            for (const auto& g : goldens()) std::cout << g.first << ' ' << hex(encode_frame({0xa5, g.second})) << '\n';
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--roundtrip-hex") {
            std::cout << hex(encode_frame(decode_one(unhex(argv[2])))) << '\n'; return 0;
        }
        if (argc != 1) return 3;
        check_cbor(); check_limits(); check_frames();
        std::cout << "ETP codec tests PASS\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << "ETP codec: " << error.what() << '\n'; return 2;
    }
}
