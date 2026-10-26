# Text patterns

Import `nil.std.text` from Ny or read it from Matrix. `compile_pattern(source)`
accepts the **NIL byte-pattern profile** and returns `Result<Pattern, text>`.
There is no separate dialect selector in this version. Familiar regex notation
is supported only where described below; unsupported extensions are errors.

## Readable declarations

```matrix
dollar-amount begin pattern
    capture amount matching one or more ascii digits
    require next to match literal " USD"
end pattern

value compiled is (compile-pattern with dollar-amount).unwrap with nothing
value maybe-match is (first-match with compiled, "price: 12 USD").unwrap with nothing
```

Ny declares the same source with `pattern dollar_amount` followed by the same
body and `end pattern`. A declaration creates an immutable text constant;
`compile_pattern` turns it into a library `Pattern`. Local and public module
bindings are supported. The compiler validates the declaration before emitting
NIL. An ordinary conventional equivalent is `(?<amount>[0-9]+)(?= USD)`.

The example consumes only `12`. Lookahead tests the next bytes at the current
position without consuming them. Consequently a full match of `12 USD` fails.
Use `first_match` to search, or add `literal " USD"` to consume the suffix.

| Phrase | Meaning |
| --- | --- |
| `literal "a.b"` | Match these literal bytes; regex punctuation is escaped |
| `literal newline`, `literal tab`, `literal quote` | Named literal characters |
| `ascii digits`, `ascii letters`, `ascii whitespace` | ASCII classes (`digit` and `letter` also accepted) |
| `character from "0" through "9"` | Inclusive ordered single-byte range |
| `any character` | Any non-NUL byte, including newline |
| `start of input`, `end of input` | Absolute input anchors |
| `one or more A`, `zero or more A`, `optional A` | Greedy repetition of one atom |
| `exactly N A`, `between N and M A` | Counted repetition, with bounds at most 1000 |
| `fewest one or more A` | Lazy repetition; also accepts zero-or-more, optional and between |
| `capture name matching A` | Named capture |
| `require next to match A` | Positive lookahead |
| `require next not to match A` | Negative lookahead |
| `begin pattern ... end pattern` | A sequence as one atom |
| `either A or B` | Ordered alternatives; additional `or C` arms allowed |

An atom is one phrase from the table, including a nested block. Put a sequence
inside a nested block to repeat, capture, assert or alternate the whole sequence.
Adjacent phrases form a sequence. Empty blocks match empty text. Pattern text
literals never interpolate braces; Ny still applies its ordinary backslash string
escapes, while Matrix strings remain raw. Capture labels are ASCII identifiers;
Matrix lowercases them and changes hyphens to underscores. Lookup uses the
resulting case-sensitive name. Declaration names follow normal language rules.

## Conventional notation

Supported: literals, escaped punctuation, `.`, `[abc]`, `[^abc]`, ranges,
`\d \D \w \W \s \S`, `\n \r \t \f \v`, and exactly two hexadecimal digits in
`\xHH`; sequence and `|`; `()`, `(?:)`, `(?<name>)`; greedy and lazy `? * +
{n} {n,} {n,m}`; `^ $`; `(?=)` and `(?!)`.

Matching is byte-oriented and case-sensitive. `\d` is `[0-9]`, `\w` includes
ASCII letters, digits and underscore, and `\s` is ASCII whitespace. Dot excludes
LF. `^` and `$` anchor absolute input boundaries; `$` does not special-case a
trailing newline. Search is leftmost-first, with alternatives tried in source
order. Positive lookahead is atomic and preserves successful captures. Negative
lookahead discards captures. A repeated capture retains its last successful
participation. Empty iterations stop unbounded repetition.

Quantifying an anchor or lookahead assertion is rejected, including a noncapturing
wrapper containing only that assertion (for example `(?:(?=a))*`).

Backreferences, lookbehind, flags, word boundaries, Unicode properties, atomic
groups and possessive quantifiers are unsupported. They fail validation instead
of silently acquiring another meaning. UTF-8 text is preserved as bytes; a match
or capture can split a multibyte character. Embedded NUL is outside the shared
native/VM text profile. The VM rejects NUL-bearing pattern/input text; native
source constants and supported text ingress reject NUL before reaching the
engine. The C ABI accepts NUL-terminated strings and cannot inspect hidden bytes
after the terminator.

## Results and captures

| API | Result |
| --- | --- |
| `compile_pattern(source)` | `Result<Pattern, text>` |
| `first_match(pattern, input)` | Search from byte zero |
| `search_pattern(pattern, input, start)` | Search from a checked byte offset, including EOF |
| `full_match(pattern, input)` | Require the whole input to match |
| `next_match(found)` | Next non-overlapping match on the same input and pattern |
| `capture_index(found, index)`, `capture_named(found, name)` | `Option<text>` |
| `capture_start(found, index)`, `capture_length(found, index)` | `Option<i64>` byte spans |

Every matching operation returns `Result<Option<PatternMatch>, text>`:
`Ok(Some(...))` is a match, `Ok(None)` is absence, and `Error(...)` is invalid
syntax/input/offset or an exhausted budget. Group zero is the whole match.
Missing/unmatched captures return `None`; participating empty captures return
`Some("")`. Unknown names and out-of-range indices return `None`.

After an empty match, `next_match` advances the search start by one byte. An
empty match at EOF terminates iteration. This is a byte progression rule, not
Unicode grapheme iteration. Public records are ordinary values; matching
revalidates pattern sources and capture APIs check span bounds even for records
constructed by the caller.

## Collection, splitting and replacement

| API | Result |
| --- | --- |
| `find_all_matches(pattern, input)` | `Result<array<text>, text>` of whole matches |
| `split_matches(pattern, input)` | `Result<array<text>, text>` of fields between matches |
| `replace_matches(pattern, input, replacement)` | `Result<text, text>` with literal replacements |

All three use the same non-overlapping sequence as `first_match`/`next_match`.
Capturing groups do not change the shape of the result: collection always returns
group zero, and splitting never inserts capture values. Splitting preserves
leading, trailing and consecutive empty fields. With no matches, collection
returns `[]`, splitting returns `[input]`, and replacement returns `input`.

Replacement text has no template syntax. `$1`, backslashes and braces are
inserted literally. Use the single-match capture APIs when captured values need
custom processing.

An empty match consumes no output bytes even though the next search advances
one byte. For example, an empty pattern on `"ab"` collects `["", "", ""]`,
splits into `["", "a", "b", ""]`, and replacing with `"-"` produces `"-a-b-"`.
On empty input it collects `[""]`, splits into `["", ""]`, and replaces with
`"-"`. These are byte operations: empty matches can fall inside UTF-8 characters.

```matrix
digits begin pattern
    one or more ascii digits
end pattern
value pattern is (compile-pattern with digits).unwrap with nothing
value numbers is (find-all-matches with pattern, "item 12, batch 300").unwrap with nothing
value selected is collect begin query
    from each item in numbers
    where (the length of item) is greater than 2
    give item
end query
value redacted is (replace-matches with pattern, "item 12", "#").unwrap with nothing
```

The collection helpers accept at most **4096 matches**, including empty matches;
splitting can therefore return at most 4097 fields. A search beyond the last
accepted match establishes completeness. More matches or any later search error
return `Error`, never a partial successful result. Use first/next matching for
incremental processing beyond this collection limit.

Replacement text and the complete replacement output are each limited to
**1 MiB** and the shared NUL-free text profile. Replacement text is validated
even when no match exists. The complete output size is checked before allocating
replacement pieces; balanced concatenation avoids repeatedly copying a growing
prefix. These checks bound returned data, not exact host memory usage. Runtime
allocation failures remain runtime failures.

## Resource and portability profile

The engine limits pattern sources to 4096 bytes, input to 1 MiB, capture groups
to 64, group nesting to 32, counted repetitions to 1000, and compiled instructions
to 16384. It limits compilation work, loop slots, total search execution to
8 million instruction steps, and accounted backtracking state to 8 MiB / 65536 states.
The step budget covers every candidate search position, rather than resetting
at each offset. Compiled programs use an immutable, thread-local cache bounded
by 32 entries and 8 MiB of accounted program storage. Container/allocator
bookkeeping means these amounts are not exact process RSS bounds. These limits are part of this initial profile and can
reject otherwise valid expressions. Cache storage is native engine memory,
not a public managed allocation or an Aperture-attributed language object.
Each search in a collection helper has its own execution budget; the helpers do
not impose a single instruction budget across all matches.

Pattern values retain their source text and require no explicit disposal. Match
values retain input, source and a managed span array. The same engine is linked
into the interpreter and native runtime, including standalone SDK executables.
There is no external regex dependency. Replacement templates, reusable prose
fragments and Unicode-aware matching remain future library work.
