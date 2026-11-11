# Deterministic random generators

Import `nil.std.math` from Ny or Matrix; `ny.lang.math` remains an alias.
`random_generator` owns mutable MINSTD state. Separate instances can be
interleaved without advancing each other or the global generator. The algorithm
is deterministic and is not suitable for security-sensitive randomness.

| Ny API | Behavior |
| --- | --- |
| `random_generator.new(seed_value: i64)` | Allocate a generator with a normalized seed. |
| `reseed(seed_value: i64)` | Reset this generator and its aliases. |
| `clone() -> random_generator` | Allocate a separate generator at the current position. |
| `next_i32() -> i32` | Advance once and return a value in `[1, 2147483646]`. |
| `next_f64() -> f64` | Advance once and return a value strictly in `(0, 1)`. |
| `next_bool() -> bool` | Advance once; true when the next integer is even. |
| `range_i32(min: i32, max: i32) -> i32` | Inclusive bounds; consume two draws per attempt. |
| `range_f64(min: f64, max: f64) -> f64` | Finite bounds, inclusive minimum and exclusive maximum; consume one draw. |

Both range methods return `min` without consuming state when the finite bounds
are equal. Inverted bounds panic. Floating ranges reject NaN and either
infinity before checking equality, so equal infinities also panic. Equal signed
zeros preserve the sign of `min`.

Matrix uses the corresponding hyphenated names: `random-generator`,
`seed-value`, `next-i32`, `next-f64`, `next-bool`, `range-i32` and `range-f64`.
Its default `number` is the seed's `i64`; integer range bounds use
`override integer` (`i32`).

```ny
import nil.std.math

let terrain = random_generator.new(1)
println(terrain.next_i32())     # 48271
let checkpoint = terrain.clone()
println(terrain.next_i32())     # 182605794
println(checkpoint.next_i32())  # 182605794, independently
```

```text
read everything from nil.std.math

value terrain is random-generator.new with seed-value being 1
say terrain.next-i32 with nothing
value checkpoint is terrain.clone with nothing
say terrain.next-i32 with nothing
say checkpoint.next-i32 with nothing
```

Records have shared-handle semantics: `let alias = terrain` in Ny or
`value alias is terrain` in Matrix shares state. Use `clone()` for a separate
continuation. The public `state: i64` field holds the recurrence state; it is
normalized before each draw, so directly assigned negative, zero, and extreme
values cannot overflow the multiplication or leave the generator stuck at zero.

## Reproducibility and range limits

Seeds are reduced modulo `2147483647`, negative remainders are shifted into the
nonnegative range, and zero becomes one. Each draw then computes
`state = (48271 * state) % 2147483647`. Seed one begins with `48271`,
`182605794`, `1291394886`, `1914720637`. Clones and equal seeds reproduce the
same stream. Distinct objects provide separate state, without a guarantee of
statistically independent streams.

The existing global functions `seed`, `random_i32`, `random_f64`, `random_bool`,
`random_range` and `random_range_f64` remain available. Their default seed is
`12345`; seeding or drawing an instance leaves that global state unchanged.
The integer recurrence and existing integer range mapping are preserved.
The global integer-range wrapper allocates a temporary generator to share the
same bounded-sampling implementation. Reuse a `random_generator` instance when
per-call allocation matters.

Integer ranges retain the two-draw mapping and rejection rule used by the
global API. They accept every ordered pair of `i32` bounds without arithmetic
overflow, but do **not** promise uniform sampling or that every value in a wide
interval can occur. MINSTD has only `2147483646` nonzero states; combining two
successive draws does not create additional independent entropy or cover all
`2^32` possible integer outputs.

Floating ranges preserve `min + t * (max - min)` when the span is finite. When
opposite finite bounds overflow that subtraction, they use a weighted sum of
the endpoints. If rounding reaches the excluded maximum or falls below the
minimum, the result is set to the minimum. This maintains the range contract,
including adjacent and subnormal bounds, without a uniformity guarantee. These
range repairs intentionally change the old overflowing or out-of-range results.

The paired `random-generators` fixtures cover exact recurrence values,
normalization, aliasing, cloning, stream isolation, rejection consuming multiple
draws, and extreme floating ranges. The negative contract runner exercises
inverted and nonfinite bounds for both frontends and both APIs. Route checks
cover textual/binary NIL, bundles, LLVM JIT and AOT; installed consumers also
exercise source and binary-library imports and both assembler/linker pipelines.

---

# Secure random values

Import `nil.std.security.random`. These are a different thing from the
generators above and the distinction is the whole point: MINSTD is
deterministic and seeded at a known value, so it is the right generator for a
simulation, a shuffle or a jittered retry, and the wrong one for anything a
user holds.

A session identifier, a CSRF token, a password-reset link and a nonce are all
bearer credentials. One drawn from MINSTD is one anyone can compute: two draws
reveal the stream.

| Ny API | Behavior |
| --- | --- |
| `random_hex(byte_count: i64) -> Result<text, text>` | Random bytes as lower-case hex, two characters per byte. |
| `random_bytes(byte_count: i64) -> Result<text, text>` | Random bytes as they are, carried as text. |
| `random_below(bound: i64) -> Result<i64, text>` | A uniform whole number in `[0, bound)`. |

Interpreter-backed. The native route does not carry these yet.

`random_hex` is the one to reach for. A credential wants to be text anyway, and
hex survives a cookie, a URL, a header and a log line with no further escaping;
it is also the form Matrix can use, because a Matrix page cannot index an Ny
byte array. Sixteen bytes — thirty-two characters — is a reasonable session
identifier.

`random_bytes` returns arbitrary bytes carried in a text value. It is not valid
UTF-8 in general, so read it with `nil.std.text`'s `byte_at`, never with
`substring` or a character count.

`random_below` uses rejection sampling rather than a remainder. Taking a draw
modulo the bound skews towards the low values whenever the bound does not
divide the range evenly — a bias small enough never to be noticed and large
enough to matter to anything that was counting on uniformity.

These ask the operating system and do nothing else: `getrandom(2)` on Linux
falling back to `/dev/urandom`, and the CRT's `rand_s` on Windows. There is no
userspace pool, no seeding and no state. There is also no fallback to a weak
source — if the kernel cannot provide entropy the answer is an error, because
quietly returning something guessable is how this goes wrong in a way nobody
notices until it matters.

A single draw is limited to 1024 bytes. A key is 32 and a generous token is 64;
asking for a megabyte is a program that wants a stream cipher seeded from here
instead.

```matrix
read nil.std.core
read nil.std.security.random

new-session-id returns text begin paragraph
    value drawn is nil.std.security::random_hex with byte_count being 16
    consider drawn
        when Result.Ok holding token then give token
        !!! No fallback. A predictable identifier is worse than no session.
        when Result.Error holding why then raise entropy-unavailable with reason being why
        otherwise raise entropy-unavailable with reason being "no entropy"
    end consider
end paragraph
```
