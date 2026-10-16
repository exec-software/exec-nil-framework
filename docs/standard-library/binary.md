# Checked binary data and hexadecimal conversion

Ny provides bounded fixed-width byte operations in `ny.lang.io` and strict
hexadecimal decoding in `ny.lang.core`. Their validation, endian conversion,
and decoding logic are written in Ny. Native support supplies the underlying
scalar-array, byte-indexed text, and panic mechanisms; it does not replace the
library algorithms with a separate native codec.

For variable-width integers and CRC-32, see
[Canonical varints and CRC-32](varint-checksum.md).

```ny
import ny.lang.core
import ny.lang.io.*
```

## Fixed-width reads and writes

The 16 public functions operate on `array<u8>`. Each format has a little-endian
(`_le`, least-significant byte first) and big-endian (`_be`, most-significant
byte first) read and write function.

| Format | Read functions | Read result | Write functions | Accepted write value |
| --- | --- | --- | --- | --- |
| Unsigned 16-bit, 2 bytes | `read_u16_le`, `read_u16_be` | `Result<i32, text>` | `write_u16_le`, `write_u16_be` | 0 through 65,535 |
| Unsigned 32-bit, 4 bytes | `read_u32_le`, `read_u32_be` | `Result<i64, text>` | `write_u32_le`, `write_u32_be` | 0 through 4,294,967,295 |
| Signed 32-bit, 4 bytes | `read_i32_le`, `read_i32_be` | `Result<i32, text>` | `write_i32_le`, `write_i32_be` | −2,147,483,648 through 2,147,483,647 |
| Signed 64-bit, 8 bytes | `read_i64_le`, `read_i64_be` | `Result<i64, text>` | `write_i64_le`, `write_i64_be` | Entire `i64` range |

Every read takes `(data: array<u8>, offset: i64, limit: i64)`. Every write
takes `(data: array<u8>, offset: i64, limit: i64, value: i64)` and returns
`Result<i64, text>`. On success, a read returns its decoded value; a write
returns the next offset, `offset + width`.

`u16` and `u32` in these names describe encoded formats, not additional
cross-route unsigned scalar types. `i32` can represent every unsigned 16-bit
value, and `i64` can represent every unsigned 32-bit value. The signed 64-bit
functions preserve all 64 raw bits using a two's-complement `i64` carrier:
eight `ff` bytes read as `-1`, and writing `-1` produces eight `ff` bytes.
There is no separate unsigned 64-bit numeric API in this subset.

### Window and failure contract

The caller supplies the half-open byte window `[offset, limit)`, where `limit`
is an absolute exclusive array index, not a byte count. A smaller limit can
confine an operation to a field or record inside a larger array. The library
requires `0 <= offset <= limit <= data.len()` and enough bytes for the entire
encoded width. It checks the remaining length by subtraction after validating
the window, avoiding overflow from an unchecked `offset + width`.

| Condition | `Result.Error` text |
| --- | --- |
| Negative offset/limit, offset after limit, or limit after the array end | `binary: invalid byte window` |
| Valid window with fewer than the required bytes | `binary: truncated byte window` |
| Valid write window but value outside the encoded format's range | `binary: value is outside the encoded range` |

Window validation precedes value validation. Reads never modify the array.
Writes validate the complete window and value before changing any byte, so a
failed write leaves the whole array unchanged. Successful writes change only
the selected bytes and are visible through aliases of that array. No operation
grows the array or updates a hidden cursor; advance your own cursor only after
`Result.Ok`. Unlike an explicit wrapping `as u8` cast, the checked narrow
writes never silently truncate an out-of-range value.

For example, this reads a two-byte field without allowing access past its
declared end:

```ny
fn main() -> i32
    let data: array<u8> = [1 as u8, 35 as u8, 255 as u8]
    match read_u16_be(data, 0, 2)
        Result.Ok(value) -> println(value) # 291
        Result.Error(error) -> panic(error)
    end match
    return 0
end fn
```

See the [Ny implementation](../../nilstd/src/io/binary.ny) and
[exact-byte fixture](../../tests/language/checked-binary-runtime.ny). This is
a fixed-width encoding subset, not a streaming parser, allocation limiter,
general serialization framework, or complete codec.

## Checked hexadecimal decoding

`Convert.try_hex_to_bytes(input: text) -> Result<array<u8>, text>` accepts only
an even number of ASCII hexadecimal digits, `0`–`9`, `a`–`f`, or `A`–`F`.
Empty input is valid and returns an empty array. Each pair becomes exactly one
byte, so successful output length is half the input's byte length. Decoding is
case-insensitive; `Convert.bytes_to_hex(data)` encodes lowercase digits.

Signs, `0x` prefixes, whitespace, separators, embedded NULs, and non-ASCII
digits are not part of the accepted syntax. A malformed suffix cannot turn a
valid prefix into a partial success; the checked API returns no partial array.

| Condition | `Result.Error` text |
| --- | --- |
| Odd input byte length | `hex_to_bytes: input must contain an even number of digits` |
| Even byte length containing any non-hexadecimal byte | `hex_to_bytes: input contains a non-hexadecimal digit` |

Length validation runs first. `Convert.hex_to_bytes(input: text) -> array<u8>`
is the convenience form for callers that treat malformed input as fatal. It
delegates to the checked decoder and calls `panic` with the same error text on
failure. It no longer substitutes bytes or returns partial data for malformed
input. Use `try_hex_to_bytes` for recoverable validation of external input.

The [conversion source](../../nilstd/src/core/convert.ny) and
[checked-hex fixture](../../tests/language/checked-hex-runtime.ny) define the
exercised subset; this is not a blanket native-support claim for other
`Convert` methods.

## Text and native-runtime boundary

The text length/substr primitives used here index bytes, not Unicode scalar
values or grapheme clusters. A substring can split a UTF-8 sequence. Negative
substring starts and counts clamp to zero; a start past the end gives an empty
substring, and an oversized count clips to the remaining bytes without signed
overflow. These operations do not validate or normalize Unicode text.

The current native text ABI is NUL-free. Native compilation rejects embedded
NUL literals before execution; it is not valid to treat native `text` as an
arbitrary byte container. Use `array<u8>` for binary data, including zero
bytes. The separate [embedded-NUL fixture](../../tests/language/checked-hex-nul-runtime.ny)
exercises the decoder's rejection through interpreter routes, where such text
is representable. It is not evidence of native embedded-NUL text support.

## Evidence and remaining limits

`checked-hex-runtime` and `checked-binary-runtime` pass all six source execution
routes using `build/cmake` tools: `text-interpreter`, `binary-interpreter`,
`bundle-text`, `bundle-binary`, `llvm-jit`, and `llvm-aot`. Their exact-output
fixtures cover every byte value in hex round trips, malformed hex, all 16
binary functions, endian byte patterns, signed/unsigned boundaries, truncated
and invalid windows, unchanged arrays after failed writes, and cursor results.
The supported native platform for this evidence is Linux.

Focused `stdlib-native-primitives` fixtures additionally exercise scalar-array
operations, `vec<u8>`, substring bounds, managed text lifetimes, panic paths,
and real-function precedence over primitive aliases. These prerequisites do
not establish general native collection or enum support: native lowering of
`Option<userstruct>` remains a known gap. The checked APIs use only the narrower
`Result<array<u8>, text>`, `Result<i32, text>`, and `Result<i64, text>` shapes.

The clean-source SDK checkpoint packages all 15 modules with normal type
checking and passes 288 installed consumer executions. That includes both new
fixtures through `nilstd` and `nillib`, each on all six routes and the separate
text/binary-NIL assembler/linker pipelines. This is an isolated next-candidate
SDK, not a replacement of the published RC or broad standard-library native
certification. See the
[standard-library status](index.md) for the wider capability boundary.
