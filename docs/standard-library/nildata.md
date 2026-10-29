# NILData

`nil.std.nildata` provides versioned structured data for Ny and Matrix, JSON
interchange, and explicit migration from NyData. A shared runtime engine serves
the command-line converter and every language execution route.

## Format

```nildata
nildata 1
object
  name is "Anomalous"
  enabled is true
  scale is -1.6
  modules is an array
    item is "shell"
    item is "desktop"
  end array
  metadata is an object
    "display name" is "NIL framework"
  end object
end object
```

A document is UTF-8, begins with `nildata 1`, and contains exactly one root
value. The version header is required. Keywords are case-sensitive. Statements
occupy separate lines; indentation is for readability. Blank lines and `#`
comments outside quoted text are allowed. Writers use LF and two-space
indentation. Readers also accept CRLF. Root containers may also use `an object` and
`an array`; writers use the shorter root forms.

| Value | Syntax |
| --- | --- |
| Null | `null` |
| Boolean | `true`, `false` |
| Integer | `0`, `-17`, `9007199254740993` |
| Decimal | `-1.6`, `1e400`, `-0.0` |
| Text | `"hello"`, `"line one\nline two"` |
| Object | root `object`, nested `an object`, closed by `end object` |
| Array | root `array`, nested `an array`, closed by `end array` |

Object statements are `key is value`; array statements are `item is value`.
Keys may be ASCII identifiers (`[A-Za-z_][A-Za-z0-9_]*`) or quoted text. Quoted
keys allow spaces, punctuation, empty names and Unicode. Object names are
case-sensitive. Duplicate names, including equivalent escaped names, are errors.
Array entries are ordered, may contain different kinds, and may nest arrays or
objects. Empty blocks are valid. `list` is not a NILData type or keyword.

Numbers use the JSON decimal grammar: optional `-`, integer digits without
leading zeros, an optional fractional part, and an optional signed exponent.
A fraction or exponent identifies a decimal; otherwise the value is an integer.
The exact token is retained, including large integers, exponent spelling and
negative zero. No implicit range limit or floating-point rounding is imposed;
conversion to a language numeric type is a separate checked operation. NaN,
infinity, hexadecimal, a leading `+`, `.5`, and `1.` are rejected.

Text and quoted keys use JSON escapes: `\"`, `\\`, `\/`, `\b`, `\f`, `\n`,
`\r`, `\t` and `\uXXXX` with valid surrogate pairs. Raw control characters and
literal newlines inside quotes are rejected. Escaped newlines preserve exact
content, including blank lines, indentation and text such as `end object`.
`"true"`, `"42"` and `"null"` remain text. There is no interpolation or execution.

Serialization sorts object keys by their UTF-8 byte spelling, preserves array
order and number tokens, and normalizes quoting and indentation. Comments and
original source formatting are not retained. Numeric-key objects are never
reinterpreted as arrays. Repeated normalization is stable; this is not a
cryptographic canonicalization specification.

## Library API

```ny
import nil.std.nildata

fn main() -> i32
    let source = "nildata 1\nobject\n  name is \"Anomalous\"\nend object\n"
    let parsed = loads(source)
    if parsed.is_error() then
        println(parsed.unwrap_error())
        return 1
    end if
    println(to_json(parsed.unwrap()).unwrap())
    return 0
end fn
```

Matrix imports the same module with `read everything from nil.std.nildata`.
Public function names use the usual Matrix hyphen spelling, such as `from-json`
and `array-get`.

`Value` holds `encoded: text`. Treat it as opaque and use the checked functions;
all operations validate it even when a caller constructs a `Value` directly.
Functions return `Result<..., text>` with a diagnostic on failure. They never
return a partially parsed document or silently coerce one kind to another.

| Operation | Successful result |
| --- | --- |
| `loads(text)`, `from_json(text)` | `Value` |
| `dumps(Value)`, `to_json(Value)` | serialized `text` |
| `kind(Value)` | `null`, `boolean`, `integer`, `decimal`, `text`, `object` or `array` as text |
| `numeric_text(Value)` | exact integer/decimal token |
| `as_i64(Value)` | checked signed 64-bit integer; fractions/exponents and overflow fail |
| `text_value(Value)`, `bool_value(Value)` | decoded `text` or `bool`, respectively |
| `size(Value)` | array length or object member count as `i64` |
| `new_object()`, `new_array()`, `null_value()` | new `Value` |
| `string_value(text)`, `boolean_value(bool)` | new `Value` |
| `integer_value(text)`, `decimal_value(text)` | new value from an exact numeric token of that kind |
| `object_has(Value, text)` | `bool` |
| `object_get(Value, text)` | `Option<Value>`; missing key is `None`, explicit null is `Some` |
| `object_key(Value, i64)` | `Option<text>` in sorted key order |
| `object_set(Value, text, Value)` | new object with the member added or replaced |
| `array_get(Value, i64)` | `Option<Value>` |
| `array_append(Value, Value)` | new array with one item appended |
| `array_set(Value, i64, Value)`, `array_remove(Value, i64)` | new array with one existing item replaced or removed |
| `object_remove(Value, text)` | new object with a member removed; absent key is a successful no-op |

Updates return a new value and leave the input unchanged. Missing keys and
nonnegative indexes beyond the end yield `None` for lookup. Negative indexes,
out-of-range update/removal indexes, wrong kinds and malformed values are errors. There is no mutable native handle
to close or keep alive.

Encoded U+0000 can round-trip through documents and JSON. Extracting it as
language text or an enumerated key fails explicitly on both routes, because the
native text ABI cannot represent embedded NUL. Raw NUL arguments are rejected.

## Bounds and diagnostics

Inputs, operation arguments and serialized outputs are limited to 1 MiB each.
A tree contains at most 65,536 values and supports depth 32 below its root.
Combined results of updates are checked against the same limits. JSON and
NILData reject invalid UTF-8, duplicate keys, trailing input and malformed
numbers or escapes. NILData syntax errors identify the failing source line; nested
JSON scalar errors also include their local byte offset. Diagnostic wording
is not a stable machine protocol. Encoding, resource-limit and operation
errors may have no source line.

Values are stored as encoded documents; operations parse and copy them. This
initial API is bounded and deterministic, with no streaming, zero-copy or
constant-time indexing guarantee. For hot loops, extract ordinary language
values once and operate on them directly.

## CLI and migration

```sh
nilcompiler --to-nildata input.json
nilcompiler --to-json input.nildata
nilcompiler --import-nydata legacy-library.nydata
nilcompiler --import-nydata-compiler legacy-compiler.nydata
```

Commands emit a complete document to stdout only after successful conversion.
Inputs must be regular files; symlinks and special files are rejected.
Errors use stderr and a nonzero exit status. `--to-nydata` is retired and reports
the replacement command. New files use `.nildata`; VS Code supplies syntax
highlighting, comments and folding without treating data as executable Matrix.

`import_nydata(text)` reads the old library's single named object/list/field
root. `import_nydata_compiler(text)` wraps the old compiler's top-level named
blocks as object members. Both return `Result<Value, text>`. Legacy fields stay
text; the library importer turns explicit list blocks into arrays, while the
compiler importer rejects list blocks. Numbered objects stay objects. Empty
compiler input represents an empty object. Legacy `#` text is literal, not a
comment.
The compiler dialect never had an unambiguous root-array or scalar encoding,
so a member named `root` remains a member. Legacy indentation/whitespace and
scalar types already discarded by old writers cannot be reconstructed.

Legacy importers require matching complete block endings, reject duplicate
members and reject trailing or unexpected tokens. `loads` accepts versioned
NILData only. Old module imports `nil.std.nydata` and `ny.lang.nydata` are aliases
for `nil.std.nildata`; old `nydata_value` records and `dumps(value, name, indent)`
signatures must migrate to the checked API. No legacy writer is shipped.
