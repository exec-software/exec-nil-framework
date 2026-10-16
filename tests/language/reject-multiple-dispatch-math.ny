@intrinsic("math.sqrt")
@interop("ny_tests_missing_native_library")
fn conflicting_math_dispatch(value: f64) -> f64
end fn

fn main() -> i32
    let ignored: f64 = conflicting_math_dispatch(4.0)
    return 0
end fn
