import nil.std.core
import nil.std.math

fn require(ok: bool, label: text)
    if not ok then panic(label) end if
end fn

# Oracles are nearest binary64 values of exact Fraction sums of the binary64
# inputs divided by count. Nonzero tolerances below are four oracle ULPs;
# zero tolerances deliberately cover exact results and subnormal rounding.
fn near(items: array<f64>, expected: f64, tolerance: f64, label: text)
    let result = average_f64(items)
    require(result.is_some(), label)
    let actual = result.unwrap()
    require(is_finite(actual), label)
    let mut lower = items[0]
    let mut upper = items[0]
    for index in range(items.len())
        if items[index] < lower then lower = items[index] end if
        if items[index] > upper then upper = items[index] end if
    end for
    require(actual >= lower and actual <= upper, label)
    require(abs(actual - expected) <= tolerance, label)
end fn

fn nan_mean(items: array<f64>, label: text)
    let result = average_f64(items)
    require(result.is_some(), label)
    let actual = result.unwrap()
    require(actual != actual, label)
end fn

fn infinite_mean(items: array<f64>, expected: f64, label: text)
    let result = average_f64(items)
    require(result.is_some() and result.unwrap() == expected, label)
end fn

fn main() -> i32
    require(average_f64([]).is_none(), "empty mean is absent")
    near([2.0, 4.0, 6.0], 4.0, 0.0, "ordinary exact mean")
    near([0.1, 0.2, 0.3], 0.2, 1.1102230246251565e-16, "fractional mean")
    near([-3.0, 0.0, 3.0], 0.0, 0.0, "ordinary cancellation")
    near([0.0, -0.0], 0.0, 0.0, "zero mean")
    require(atan2(average_f64([-0.0, -0.0]).unwrap(), -1.0) > 0.0, "negative-zero mean retains positive zero")
    let original = [2.0, -0.0, 8.0]
    let alias = original
    near(original, 3.3333333333333335, 1.7763568394002505e-15, "input alias mean")
    require(alias.len() == 3 and alias[0] == 2.0 and alias[2] == 8.0, "input values preserved")
    require(atan2(alias[1], -1.0) < 0.0, "input negative zero preserved")
    println("ordinary means and source aliases")

    let maximum = 1.7976931348623157e308
    near([maximum], maximum, 0.0, "maximum singleton")
    near([-maximum], -maximum, 0.0, "minimum singleton")
    near([1e308, 1e308], 1e308, 7.98336123813888e292, "positive intermediate overflow")
    near([-1e308, -1e308], -1e308, 7.98336123813888e292, "negative intermediate overflow")
    near([maximum, maximum, maximum], maximum, 7.98336123813888e292, "maximum repeated")
    near([maximum, maximum, maximum, maximum, maximum], maximum, 0.0, "constant maximum endpoint")
    near([-maximum, -maximum, -maximum], -maximum, 7.98336123813888e292, "minimum repeated")
    near([1e308, 1e308, -1e308], 3.333333333333333e307, 1.99584030953472e292, "positive mixed overflow")
    near([1e308, -1e308, 1e308], 3.333333333333333e307, 1.99584030953472e292, "mixed permutation")
    near([-1e308, -1e308, 1e308], -3.333333333333333e307, 1.99584030953472e292, "negative mixed overflow")
    near([1e308, 1e308, -1e308, -1e308], 0.0, 0.0, "balanced overflowing groups")
    near([maximum, -maximum], 0.0, 0.0, "opposite maxima")
    println("finite overflow and mixed signs")

    near([1e16, 1.0, -1e16], 0.3333333333333333, 2.220446049250313e-16, "moderate cancellation residue")
    near([1e16, 1.0, 1.0, -1e16], 0.5, 0.0, "two cancellation units")
    near([3.0, 1.0000000000000002, -2.0, -1.0, -1.0], 4.4408920985006264e-17, 2.465190328815662e-32, "compensation tail before small terms")
    near([2.0000000000000004, 1.9999999999999998, -1.0, -1.0, -1.0, -1.0], 3.700743415417188e-17, 2.465190328815662e-32, "opposite compensation tails")
    near([-1.0, 2.0000000000000004, -1.0, 1.9999999999999998, -1.0, -1.0], 3.700743415417188e-17, 2.465190328815662e-32, "compensation tail permutation")
    near([-2.0000000000000004, -1.9999999999999998, 1.0, 1.0, 1.0, 1.0], -3.700743415417188e-17, 2.465190328815662e-32, "negative compensation tails")
    near([1e308, 1.0, -1e308], 0.3333333333333333, 2.220446049250313e-16, "extreme cancellation residue")
    near([1e308, 1e308, -1e308, -1e308, 3e-300], 6.000000000000001e-301, 3.3156184e-316, "tiny residue after overflow")
    near([3e-300, -1e308, 1e308, -1e308, 1e308], 6.000000000000001e-301, 3.3156184e-316, "tiny residue permutation")
    near([1e308, 1e-308, -1e308], 3.33333333333333e-309, 2e-323, "subnormal cancellation residue")
    near([maximum, -maximum, 2.2250738585072014e-308], 7.41691286169067e-309, 2e-323, "minimum normal residue")
    near([1e308, 1e308, -1e308, -1e308, 5e-324], 0.0, 0.0, "unrepresentable final residue rounds to zero")
    println("compensated cancellation")

    let tiny = 5e-324
    near([tiny, tiny], tiny, 0.0, "repeated minimum subnormal")
    near([-tiny, -tiny], -tiny, 0.0, "negative minimum subnormal")
    near([tiny, 0.0, tiny], tiny, 0.0, "subnormal thirds")
    near([tiny, 0.0], 0.0, 0.0, "subnormal tie rounds to even zero")
    near([1.5e-323, 0.0], 1e-323, 0.0, "subnormal tie rounds to even nonzero")
    near([tiny, 1e-323, 1.5e-323], 1e-323, 0.0, "mixed subnormal mean")
    near([2.2250738585072014e-308, 2.2250738585072014e-308, 2.2250738585072014e-308], 2.2250738585072014e-308, 0.0, "minimum normal repeated")
    near([2.2250738585072014e-308, 2.225073858507201e-308], 2.2250738585072014e-308, 0.0, "normal boundary tie")
    println("subnormal means and rounding")

    let mut large_values: array<f64> = []
    let mut small_values: array<f64> = []
    for index in range(257)
        large_values.push(maximum)
        small_values.push(tiny)
    end for
    near(large_values, maximum, 7.98336123813888e292, "257 maximum values")
    near(small_values, tiny, 0.0, "257 minimum subnormals")
    println("repeated maximum and subnormal means")

    let infinity = exp(1000.0)
    let nan = sqrt(-1.0)
    infinite_mean([infinity], infinity, "positive infinity singleton")
    infinite_mean([-infinity], -infinity, "negative infinity singleton")
    infinite_mean([infinity, 1e308, 1e308], infinity, "positive infinity propagates")
    infinite_mean([-infinity, 1e308, 1e308], -infinity, "negative infinity precedes large terms")
    nan_mean([nan], "NaN singleton")
    nan_mean([1.0, nan, 2.0], "NaN inside input")
    nan_mean([infinity, -infinity], "opposite infinities")
    nan_mean([infinity, nan, -infinity], "NaN and infinities")
    nan_mean([1e308, 1e308, -infinity], "historical overflow before opposite infinity")
    nan_mean([-1e308, -1e308, infinity], "historical negative overflow before infinity")
    require(sum_f64([1e308, 1e308]) == infinity, "sum retains ordinary overflowing arithmetic")
    println("nonfinite input contracts")
    return 0
end fn
