# Vector norms and checked normalization

Import `nil.std.math` from Ny or Matrix. The legacy `ny.lang.math` name resolves
to the same library. These APIs use `f64` (`floating` or `override double` in
Matrix).

| Ny API | Contract |
| --- | --- |
| `is_finite(value: f64) -> bool` | True for finite values, including signed zero and subnormal values; false for NaN and either infinity. |
| `hypot2(x: f64, y: f64) -> f64` | Scaled Euclidean norm of two components. |
| `hypot3(x: f64, y: f64, z: f64) -> f64` | Scaled Euclidean norm of three components. |
| `vec2.is_finite()`, `vec3.is_finite()` | True only when every component is finite. |
| `vec2.try_normalize() -> Option<vec2>` | A unit direction for finite, nonzero input; `None` otherwise. |
| `vec3.try_normalize() -> Option<vec3>` | The corresponding three-component operation. |

The norm helpers and existing `length()` methods scale components before
squaring them. Large or tiny coordinates therefore do not overflow or underflow
solely because of the intermediate squares. The final length can overflow when
the norm exceeds the `f64` range, and rounding near the upper finite boundary
can also produce infinity. NaN propagates, including when
another component is infinite; otherwise infinite components yield positive
infinity. The all-zero norm is positive zero. These are approximate floating-point
calculations, without a correctly rounded result guarantee.

Checked normalization works directly on scaled components, so it can normalize
a finite vector even when its length is unrepresentable. It also handles
subnormal nonzero vectors without taking an overflowing reciprocal. A successful
result is a fresh vector; the input and its aliases are preserved. Components
too small relative to the largest component may round to zero.

The existing `normalize()` methods use the same stable calculation for valid
directions and return the original vector unchanged for zero or non-finite
input. This preserves the zero-vector fallback and defines a consistent fallback
for non-finite components, which previously could produce a mixture of zero and
NaN components. Use `try_normalize()` when the caller needs to reject invalid directions.
`length_sq()`, dot and cross products still use ordinary multiplication and
addition; they may overflow or underflow. `distance()` uses the improved norm,
but its component subtraction can still overflow first.

```ny
import nil.std.math
import nil.std.core

let direction = vec3.new(2.0, -3.0, 6.0)
match direction.try_normalize()
    Option.Some(unit) -> println(unit.z)
    Option.None -> println("No finite direction")
end match
```

```text
read everything from nil.std.math
read everything from nil.std.core

value direction is vec3.new with x being 2.0, y being minus 3.0, z being 6.0
consider direction.try-normalize with nothing
    when Option.Some holding unit then say unit.z
    when Option.None then say "No finite direction"
end consider
```

The norm helpers are implemented in Matrix and the existing vector methods in
Ny. The paired `vector-norms` and `vector-norms-matrix` fixtures verify ordinary,
large, tiny, subnormal, zero, NaN and infinite inputs through textual and binary
NIL, both bundle routes, LLVM JIT and native AOT. They check independent expected
coordinates and input preservation. Both are also registered for installed
source and binary-library consumption. This is a vector API contract, not a
package-wide native math or cross-platform qualification.
