# Checked decimal integer conversion

`Convert.try_to_i32(value: text) -> Result<i32, text>` and
`Convert.try_to_i64(value: text) -> Result<i64, text>` parse signed decimal
integers without panicking on malformed input or overflow. Import
`nil.std.core` from either language. Matrix uses the method spellings
`Convert.try-to-i32` and `Convert.try-to-i64`.

Accepted input contains an optional leading `+` or `-` followed by one or more
ASCII decimal digits. Leading zeros and signed zero are valid. Whitespace,
radix prefixes, digit separators, decimal points, exponent notation, Unicode
digits and trailing characters are rejected. The whole text must match;
there is no trimming or partial parse.

`Result.Ok` contains the exact signed value, including either width's minimum
and maximum. `Result.Error` contains text prefixed by the called conversion's
intrinsic name, such as `convert.try_to_i64: value is outside the i64 range`.
Error details distinguish empty input, a sign without digits, an invalid
digit, and overflow. Parsing stops at the first error; an overflowing prefix
can therefore be reported before a later invalid character. `try_to_i32`
first parses within `i64` bounds, then checks the narrower destination.

```ny
import nil.std.core

fn main() -> i32
    match Convert.try_to_i64("9223372036854775807")
        Result.Ok(value) -> println(value)
        Result.Error(message) -> println(message)
    end match
    return 0
end fn
```

The interpreter and native runtime share the integer parser. LLVM lowering
constructs the same `Result<i64, text>` value used by the interpreter, with a
64-bit success payload and managed error text. The additive `nyrt_try_i64`
runtime entry point follows the existing checked-i32 convention: success
returns `1`, writes the value, and clears the error; failure returns `0`,
zeros an available value output and supplies an error when requested.

The native C entry point accepts NUL-terminated text. It has no length
parameter and cannot represent embedded NUL bytes; native language text
retains the framework's NUL-free contract. The interpreter's length-aware
parser rejects an embedded NUL as an invalid digit.

Direct ABI tests and paired Ny/Matrix fixtures cover boundaries, malformed
input, exact errors, returned results and error-text lifetime. The fixtures
run through text/binary NIL, bundles, LLVM JIT/AOT, standalone assembler/linker
execution and installed source/binary libraries. This is a native claim for
these checked decimal integer APIs; other `Convert` operations retain their
separate support boundaries.
