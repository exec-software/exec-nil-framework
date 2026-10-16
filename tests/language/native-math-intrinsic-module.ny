module ny.tests.native_math_intrinsic_module

import ny.lang.math.math

fn main() -> i32
    println(ny::lang::math::sqrt(81.0) as i32)
    println(ny::lang::math::pow(2.0, 10.0) as i32)
    println(ny::lang::math::cbrt(27.0) as i32)
    return 0
end fn
