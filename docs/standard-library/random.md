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
