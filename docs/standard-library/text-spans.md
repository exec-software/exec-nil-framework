# Checked text spans

`nil.std.text` provides checked access to text bytes. Both Ny and Matrix use
the same Matrix-authored implementation in `string_checked.matrix`.

| Function | Result |
| --- | --- |
| `try_substring(s: text, start: i64, length: i64) -> Option<text>` | `Some` containing the entire requested byte span, or `None` if any part is outside the text. |
| `try_char_at(s: text, index: i64) -> Option<text>` | `Some` containing one byte at an existing index, or `None`. |

A valid span requires `0 <= start <= s.len()` and
`0 <= length <= s.len() - start`. Bounds are checked at their original `i64`
width before conversion to the text runtime's index type. Validation uses
subtraction after checking the start, so extreme inputs cannot overflow an
addition and appear valid.

Empty spans are valid: `try_substring("", 0, 0)` and
`try_substring("abc", 3, 0)` both return `Some("")`. A negative length, a
start beyond the end even with zero length, or a span extending past the end
returns `None`. `try_char_at("abc", 3)` returns `None` because there is no
byte at the end position. Neither operation changes the source text.

```ny
import nil.std.text

fn main() -> i32
    match try_substring("frame:42", 6, 2)
        Option.Some(value) -> println(value)
        Option.None -> println("missing field")
    end match
    return 0
end fn
```

```matrix
book text-span-example
read everything from nil.std.text

main returns number begin paragraph
    consider try-substring with s being "frame:42", start being 6, length being 2
        when Option.Some holding span then say span
        when Option.None then say "missing field"
    end consider
    give 0
end paragraph
```

Both examples print `42`. Legacy `ny.lang.text` imports remain aliases of the
same module.

These APIs use byte positions, as do the existing text runtime primitives.
They do not validate UTF-8 boundaries or count Unicode scalars or grapheme
clusters. A one-byte result can be part of a multibyte sequence. Native text
remains NUL-free; these helpers do not change its representation.

The compatibility helpers keep their existing policy: `substring` returns
empty text for an invalid start or nonpositive count and clips an oversized
positive count to the remaining bytes. Its clamp now handles even the maximum
`i64` count without overflow. `char_at` returns empty text for an invalid
index. Use the checked forms when an invalid request must be distinguishable
from an empty result.

Paired Ny/Matrix fixtures cover exact spans, empty results, invalid bounds,
signed extrema, UTF-8 byte positions, aliases, and the compatibility clamp.
They are registered for text/binary NIL, bundles, LLVM JIT/AOT, standalone
assembler/linker execution, and installed source/binary libraries.

## Byte values

`nil.std.text` measures and slices by byte — `length` of `"café"` is 5 — but
until these two existed there was no way to read a byte *as a number* or write
one as text.

| Function | Result |
| --- | --- |
| `byte_at(s: text, index: i64) -> i64` | The byte at an index, `0..255`, or minus one when the index is outside the text. |
| `from_byte(code: i64) -> text` | One byte as text; empty text for a code outside `0..255`. |

The conversion that looks like it would do this is
`nil.std.core`'s `convert.char_to_i32`, and it will not: it takes a whole
Unicode scalar and **aborts the process** on a partial one. That is a fatal
runtime error, not a fault `attempt` can catch. So a byte in the middle of a
multi-byte character could not be inspected, and such a character could not be
isolated in order to inspect it whole, because isolating it needed the same
test. Percent-encoding anything outside ASCII was unreachable because of this,
and so was reading arbitrary bytes off a socket.

These two are about bytes and say so in their names. They know nothing about
UTF-8; assembling characters from bytes is the caller's job.

`from_byte(195)` is the single byte `0xC3`, where `convert.i32_to_char(195)` is
the *two* bytes that encode U+00C3. Joining text joins bytes, so a multi-byte
character can be built one byte at a time:

```matrix
read nil.std.text.string_utils

!!! The two bytes that spell "é", built from their values.
value accented is (nil.std.text::from_byte with code being 195)
    plus (nil.std.text::from_byte with code being 169)
```

`byte_at` answers rather than raising when the index is out of range, because
reading past the end is how a scan over bytes discovers it has finished, and a
loop that must check the length separately is a loop that will sometimes
forget to.

`try_char_at` above gives the same byte as a one-byte text wrapped in an
`Option`. Use it when the byte is wanted as text and `byte_at` when it is
wanted as a number.
