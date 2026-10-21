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
