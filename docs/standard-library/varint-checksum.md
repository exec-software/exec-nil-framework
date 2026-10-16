# Canonical varints and CRC-32

Ny provides canonical base-128 integer encoding and bounded CRC-32 checksums in
`ny.lang.io`. Both implementations are pure Ny code over `array<u8>`; they do
not call an FFI codec or keep hidden cursor or checksum state.

```ny
import ny.lang.core
import ny.lang.io.*
```

## Canonical variable-width integers

| Function | Contract |
| --- | --- |
| `var_u32_size(value: i64) -> Result<i32, text>` | Return the one-to-five-byte encoded size, rejecting values outside the unsigned 32-bit range. |
| `var_i64_size(value: i64) -> i32` | Return the one-to-ten-byte size after ZigZag conversion. Every `i64` is accepted. |
| `read_var_u32(data: array<u8>, offset: i64, limit: i64) -> Result<i64, text>` | Read the first canonical unsigned 32-bit value in the window. |
| `read_var_i64(data: array<u8>, offset: i64, limit: i64) -> Result<i64, text>` | Read the first canonical ZigZag-encoded signed 64-bit value in the window. |
| `write_var_u32(data: array<u8>, offset: i64, limit: i64, value: i64) -> Result<i64, text>` | Write an unsigned 32-bit value and return the next offset. |
| `write_var_i64(data: array<u8>, offset: i64, limit: i64, value: i64) -> Result<i64, text>` | ZigZag-encode any `i64`, write it, and return the next offset. |

`u32` describes the wire width. Ny uses an `i64` carrier so the API can
represent the complete range from 0 through 4,294,967,295. The signed API uses
ZigZag over all 64 bits: `0`, `-1`, `1`, and `-2` map to unsigned bit patterns
`0`, `1`, `2`, and `3`. This is not signed LEB128, and it is not the
two's-complement `int64` varint representation.

Each encoded byte contributes seven low-order payload bits; the high bit says
that another byte follows. An unsigned 32-bit value uses at most five bytes,
with at most four payload bits in the fifth. A ZigZag `i64` uses at most ten
bytes, with at most one payload bit in the tenth. The implementation checks
those final-byte limits before shifting, including at the signed boundaries.

Readers enforce the shortest form. A multi-byte encoding whose final payload
is zero is rejected instead of being accepted as an alternate spelling of a
smaller value. This is a Ny validation policy stricter than merely recognizing
the general base-128 wire shape. The byte layout and ZigZag mapping follow the
official [Protocol Buffers encoding reference](https://protobuf.dev/programming-guides/encoding/),
but these functions do not parse field tags, wire types, length-delimited
records, messages, or a general Protocol Buffers stream.

### Cursor and window contract

All operations use a caller-owned half-open window `[offset, limit)`, where
`limit` is an absolute exclusive array index. They require
`0 <= offset <= limit <= data.len()`.

A read returns only the decoded value. Because accepted input is canonical,
its matching size function gives the consumed byte count; advance a cursor by
that size only after `Result.Ok`. A read stops after the first value and does
not require the rest of the supplied window to be empty. A successful write
returns `offset + encoded_size` directly.

Writes determine the complete size and verify that it fits before changing the
first byte. A failed write therefore leaves the array unchanged. Successful
writes mutate only the selected bytes, do not grow the array, and are visible
through aliases.

| Condition | `Result.Error` text |
| --- | --- |
| Negative endpoint, offset after limit, or limit after the array end | `binary: invalid byte window` |
| Input ends before a terminating byte, or output lacks room for the complete value | `varint: truncated input` |
| A fifth/tenth byte exceeds its width, or continuation exceeds the maximum byte count | `varint: encoded value overflows its width` |
| A multi-byte value is not in its shortest form | `varint: non-canonical encoding` |
| `var_u32_size` or `write_var_u32` receives a value outside 0 through 4,294,967,295 | `varint: value is outside the encoded range` |

Window validation occurs before value validation. On reads, truncation and
width checks follow the bytes in encounter order. See the
[Ny implementation](../../nilstd/src/io/varint.ny).

## Bounded and incremental CRC-32

| Function | Contract |
| --- | --- |
| `crc32(data: array<u8>, offset: i64, limit: i64) -> Result<i64, text>` | Compute CRC-32 over exactly `[offset, limit)`. |
| `crc32_update(previous: i64, data: array<u8>, offset: i64, limit: i64) -> Result<i64, text>` | Continue from a previously finalized checksum and return the new finalized checksum. |

The checksum is CRC-32/ISO-HDLC (the IEEE CRC-32 used by gzip): reflected
polynomial `0xedb88320`, initial state `0xffffffff`, and final XOR
`0xffffffff`. [RFC 1952 section 8](https://www.rfc-editor.org/rfc/rfc1952.html#section-8)
provides the primary algorithm reference.

Results use an `i64` carrier for the unsigned 32-bit range 0 through
4,294,967,295. Start an incremental calculation with `previous = 0`; pass each
successful, finalized result into the next call. An empty valid window preserves
the previous checksum. The implementation uses constant working state with no
hidden state, explicit scratch buffer, lookup table, input copy, or input
mutation.

Window validation runs before previous-state validation. The window error is
`binary: invalid byte window`, shared with the other bounded byte APIs. A
`previous` value outside the unsigned 32-bit range returns
`crc32: previous value is outside the unsigned 32-bit range`.

CRC-32 is suitable for detecting accidental corruption. It is linear,
unkeyed, and not cryptographic authentication; do not use it to establish
integrity against an attacker. See the
[Ny implementation](../../nilstd/src/io/checksum.ny).

## Evidence boundary

These APIs are part of the active `ny.lang.io` module. The
[CRC-32 fixture](../../tests/language/checked-crc-runtime.ny) and
[varint fixture](../../tests/language/checked-varint-runtime.ny) pass all six
build-tree execution routes, including LLVM JIT and AOT. The latter also
composes varints, fixed-width encoding, and CRC in a guarded binary record.
Both fixtures also pass from installed `nilstd` and `nillib`, through those six
routes plus text/binary-NIL standalone assembler/linker pipelines. The isolated
clean-source SDK passed 370 CTests and 336 installed consumer executions in
total, with 15 strictly compiled modules. These are fixture-scoped results, not a
whole-library native or performance certification; the published RC and active
SDK are unchanged. The existing fixed-width and hexadecimal evidence remains
separately documented in
[Checked binary data and hexadecimal conversion](binary.md).
