#!/usr/bin/env python3
"""Independent stdlib ETP/CBOR golden oracle; never opens a socket or pipe.

Usage: python etp_golden_frames.py --codec /path/to/nil_etp_codec_tests
The C++ executable is a bounded test driver, not an interpreter/server. Exact
vectors anchor the Python encoder to RFC 8949 Appendix A; Python struct supplies
independent IEEE16/32/64 conversion. This is the documented NIL subset, not a
general CBOR package or proof that Liz's application protocol is implemented.
"""
import argparse
import math
import struct
import subprocess
import sys
import time


def argument(major, number):
    assert 0 <= number < 2**64
    prefix = major << 5
    if number < 24:
        return bytes([prefix + number])
    for ai, width in ((24, 1), (25, 2), (26, 4), (27, 8)):
        if number < 1 << (8 * width):
            return bytes([prefix + ai]) + number.to_bytes(width, "big")
    raise AssertionError("integer outside test domain")


def encode(item):
    if item is None:
        return b"\xf6"
    if isinstance(item, bool):
        return b"\xf5" if item else b"\xf4"
    if isinstance(item, int):
        return argument(0, item) if item >= 0 else argument(1, -1 - item)
    if isinstance(item, float):
        assert math.isfinite(item)
        return b"\xfb" + struct.pack(">d", item)
    if isinstance(item, bytes):
        return argument(2, len(item)) + item
    if isinstance(item, str):
        utf8 = item.encode("utf-8", errors="strict")
        return argument(3, len(utf8)) + utf8
    if isinstance(item, list):
        return argument(4, len(item)) + b"".join(map(encode, item))
    assert isinstance(item, dict) and all(isinstance(k, str) for k in item)
    ordered = sorted((encode(k), encode(v)) for k, v in item.items())
    return argument(5, len(item)) + b"".join(k + v for k, v in ordered)


def decode(payload):
    """Independent small test-only decoder, with fixed resource bounds."""
    assert 0 < len(payload) <= 65536
    position, nodes = 0, 0

    def read(length):
        nonlocal position
        assert 0 <= length <= len(payload) - position, "truncated item"
        result = payload[position:position + length]
        position += length
        return result

    def item(depth=0):
        nonlocal nodes
        nodes += 1
        assert depth <= 32 and nodes <= 4096
        lead = read(1)[0]
        major, extra = divmod(lead, 32)
        if major == 7:
            if extra in (20, 21, 22):
                return {20: False, 21: True, 22: None}[extra]
            assert extra in (25, 26, 27), "unsupported simple value"
            length, form = {25: (2, ">e"), 26: (4, ">f"), 27: (8, ">d")}[extra]
            result = struct.unpack(form, read(length))[0]
            assert math.isfinite(result)
            return result
        assert major != 6 and extra <= 27, "unsupported tag/indefinite/reserved"
        number = extra if extra < 24 else int.from_bytes(read(1 << (extra - 24)), "big")
        if major == 0:
            return number
        if major == 1:
            return -1 - number
        if major == 2:
            return read(number)
        if major == 3:
            return read(number).decode("utf-8", errors="strict")
        assert number <= 1024
        if major == 4:
            return [item(depth + 1) for _ in range(number)]
        assert major == 5
        result = {}
        for _ in range(number):
            key = item(depth + 1)
            assert isinstance(key, str) and key not in result
            result[key] = item(depth + 1)
        return result

    result = item()
    assert position == len(payload), "trailing CBOR"
    return result


def framed(payload, kind=0xA5):
    return struct.pack("<IB", len(payload), kind) + payload


def check_frame(wire):
    assert len(wire) >= 5
    length, kind = struct.unpack("<IB", wire[:5])
    assert length == len(wire) - 5 and length > 0
    return kind, decode(wire[5:])


def equivalent(left, right):
    assert type(left) is type(right), "CBOR value type changed"
    if isinstance(left, float):
        assert struct.pack(">d", left) == struct.pack(">d", right), "float/sign changed"
    elif isinstance(left, list):
        assert len(left) == len(right)
        for a, b in zip(left, right):
            equivalent(a, b)
    elif isinstance(left, dict):
        assert left.keys() == right.keys()
        for key in left:
            equivalent(left[key], right[key])
    else:
        assert left == right


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--codec", required=True)
    args = parser.parse_args()
    deadline = time.monotonic() + 25

    def run(*arguments, success=True):
        remaining = deadline - time.monotonic()
        assert remaining > 0, "golden oracle exceeded its overall deadline"
        result = subprocess.run([args.codec, *arguments], stdin=subprocess.DEVNULL,
                                capture_output=True, timeout=min(5, remaining), check=False)
        assert len(result.stdout) < 1024 * 1024 and len(result.stderr) < 4096
        if success:
            assert result.returncode == 0 and not result.stderr, (arguments, result.returncode, result.stderr)
        else:
            assert result.returncode == 2 and not result.stdout and b"ETP codec:" in result.stderr, result
        return result.stdout.decode("ascii", errors="strict").strip()

    expected = {
        "null": None, "false": False, "true": True,
        "integers": [0, 23, 24, 255, 256, 65535, 65536, 2**32-1, 2**32, 2**64-1,
                     -1, -24, -25, -2**64],
        "binary": bytes(range(256)), "empty_binary": b"",
        "text": "caf\u00e9 \u4e2d\u6587 \U0001f642 e\u0301", "nul_text": "a\0b", "empty_text": "",
        "map": {"zz": 2, "b": b"\0\xff", "a": [True, None], "\u00e9": "utf8"},
        "empty_map": {}, "empty_array": [],
        "floats": [0.0, -0.0, 1.5, -123456.25, float.fromhex("0x0.0000000000001p-1022"), sys.float_info.max],
    }
    # Independent, literal RFC-style vectors prevent two encoders merely agreeing
    # on a shared mistaken width, sign, string count or integer byte order.
    anchors = [(0, "00"), (23, "17"), (24, "1818"), (255, "18ff"), (256, "190100"),
               (65536, "1a00010000"), (2**32, "1b0000000100000000"),
               (2**64-1, "1bffffffffffffffff"), (-1, "20"), (-25, "3818"),
               (-2**64, "3bffffffffffffffff"), (b"\x00\xff", "4200ff"),
               ("\u6c34", "63e6b0b4"), ([1, 2, 3], "83010203"),
               ({"a": 1, "b": [2, 3]}, "a26161016162820203"),
               (1.5, "fb3ff8000000000000"), (-0.0, "fb8000000000000000")]
    for item, literal in anchors:
        assert encode(item).hex() == literal
        equivalent(decode(bytes.fromhex(literal)), item)
    assert framed(b"\xf6").hex() == "01000000a5f6", "ETP length must exclude type/header"

    observed = {}
    for line in run("--golden").splitlines():
        name, literal = line.split(" ")
        assert name not in observed
        observed[name] = bytes.fromhex(literal)
    assert observed.keys() == expected.keys()
    for name, value in expected.items():
        wire = observed[name]
        assert wire == framed(encode(value)), name
        kind, decoded = check_frame(wire)
        assert kind == 0xA5
        equivalent(decoded, value)
        # Independent Python -> C++ -> Python typed-value and exact encoding proof.
        returned = bytes.fromhex(run("--roundtrip-hex", framed(encode(value)).hex()))
        assert returned == wire

    alternatives = [
        ("1800", 0), ("190017", 23), ("1b0000000000000018", 24), ("390000", -1),
        ("580200ff", b"\0\xff"), ("790003e6b0b4", "\u6c34"), ("980101", [1]),
        ("b800", {}), ("a2627a7a02616101", {"a": 1, "zz": 2}),
        ("f90000", 0.0), ("f98000", -0.0), ("f93e00", 1.5),
        ("f90001", 2.0**-24), ("f90400", 2.0**-14), ("f97bff", 65504.0),
        ("fa00000000", 0.0), ("fa80000000", -0.0), ("fa3fc00000", 1.5),
        ("fa00000001", 2.0**-149), ("fa7f7fffff", float.fromhex("0x1.fffffep127")),
    ]
    for literal, value in alternatives:
        equivalent(decode(bytes.fromhex(literal)), value)
        normalized = bytes.fromhex(run("--roundtrip-hex", framed(bytes.fromhex(literal), 0x7D).hex()))
        assert normalized == framed(encode(value), 0x7D)
        equivalent(check_frame(normalized)[1], value)

    malformed = ["", "18", "1bffffffffffffff", "5bffffffffffffffff", "9bffffffffffffffff",
                 "5f40ff", "9fff", "c000", "f7", "f814", "ff", "f97c00", "f97e00",
                 "fa7f800000", "fb7ff8000000000000", "f6f6", "a201000101",
                 "a261610078016101", "62c080", "63eda080", "64f4908080", "62e282"]
    for literal in malformed:
        run("--roundtrip-hex", framed(bytes.fromhex(literal)).hex(), success=False)
    for literal in ("ffffffff00", "0100010000", "01000000a5", "01000000a5f601"):
        run("--roundtrip-hex", literal, success=False)
    print(f"ETP independent oracle PASS: {len(expected)} typed goldens, {len(anchors)} literal anchors, "
          f"{len(alternatives)} interoperable forms, {len(malformed) + 4} rejection controls")


if __name__ == "__main__":
    main()
