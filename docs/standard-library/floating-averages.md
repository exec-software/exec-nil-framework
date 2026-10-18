# Floating-point averages

`average_f64(items: array<f64>) -> Option<f64>` is available from
`nil.std.core` in both Ny and Matrix. The legacy `ny.lang.core` import resolves
to the same implementation.

| Input | Result |
| --- | --- |
| Empty array | `None`. |
| Nonempty array of finite values | `Some(mean)`, with a finite approximation to the arithmetic mean inside the input's minimum/maximum interval. |
| Any NaN or infinity | The original ordered `sum_f64(items) / count` computation, wrapped in `Some`. |

The function leaves the input and its aliases unchanged. It takes O(n) time
and O(1) auxiliary storage. `sum_f64` retains its existing ordered, uncompensated
addition behavior, including intermediate overflow.

```ny
import nil.std.core

fn main() -> i32
    let mean = average_f64([1e308, 1e308]).unwrap()
    println(mean == 1e308)
    return 0
end fn
```

```matrix
book examples.floating-averages

read everything from nil.std.core

main returns override integer begin paragraph
    value mean is (average-f64 with arr being [1e308, 1e308]).unwrap with nothing
    say mean equals 1e308
    give 0
end paragraph
```

The previous implementation overflowed while summing the two values in these
examples. The updated implementation keeps two compensated subtotals: values
of magnitude at most one remain unscaled, while larger values are divided by
a power of two greater than the array length. This leaves headroom for the
large subtotal without underflowing the small inputs. The final merge retains
addition residuals and restores the original scale when cancellation makes
that safe. A final range clamp prevents rounding from escaping the input
interval, including at the largest finite value.

The correction uses a magnitude-ordered addition residual, as described in
[Ogita, Rump and Oishi, *Accurate Sum and Dot Product*, section 1.1](https://www.tuhh.de/ti3/paper/rump/OgRuOi05.pdf).
This implementation does not provide an exact accumulator, guaranteed correct
rounding, a universal ULP bound, or order-independent results. Severe
cancellation can still erase a representable residual: for example,
`[1e100, 1e308, 1.7976931348623157e308, -1e308, -1.7976931348623157e308]`
can produce zero instead of a value near `2e99`. That accuracy limit remains
follow-up work. Values close to zero can also round to zero normally.

For nonfinite inputs, retaining ordered summation is deliberate. A NaN
propagates; opposite infinities produce NaN. Intermediate finite overflow can
also affect the result: `[1e308, 1e308, -infinity]` still produces NaN after
the first two terms overflow. This compatibility path does not use the finite
averaging algorithm.

Paired Ny and Matrix fixtures cover ordinary means, extreme magnitudes,
cancellation, subnormal values and rounding ties, empty arrays, signed zeros,
nonfinite inputs and source preservation. Finite reference values are derived
from exact rational sums of the binary64 inputs. These fixtures run through
the textual/binary interpreter, bundles, LLVM JIT/AOT, standalone
assembler/linker pipelines, and installed source/binary libraries. Their
individual tolerances establish the tested cases, not a general error bound.
