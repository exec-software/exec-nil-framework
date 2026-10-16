module ny.tests.math_runtime

import ny.lang.math.*

fn main() -> i32
    println(abs_i32(-9))
    println(abs(-2.5) as i32)
    println(floor(2.9) as i32)
    println(ceil(2.1) as i32)
    println(round(2.5) as i32)
    println(trunc(-2.9) as i32)
    println(pow(2.0, 5.0) as i32)
    println(sqrt(81.0) as i32)
    println(cbrt(27.0) as i32)
    println(exp(0.0) as i32)
    println(log(1.0) as i32)
    println(log10(1000.0) as i32)
    println(log2(8.0) as i32)
    println(min_i32(7, -2))
    println(max_i32(7, -2))
    println(min(7.0, -2.0) as i32)
    println(max(7.0, -2.0) as i32)
    println(sign(-0.5))
    println(sign(0.0))
    println(sign(0.5))
    println(sin(0.0) as i32)
    println(cos(0.0) as i32)
    println(tan(0.0) as i32)
    println(asin(0.0) as i32)
    println(acos(1.0) as i32)
    println(atan(0.0) as i32)
    println(atan2(0.0, 1.0) as i32)
    println(sinh(0.0) as i32)
    println(cosh(0.0) as i32)
    println(tanh(0.0) as i32)
    println(tanh(100.0) as i32)
    println(tanh(-100.0) as i32)
    let tiny = 0.00000001
    if not approx_eq(sinh(tiny), tiny, 0.000000000000001) then return 9 end if
    if not approx_eq(cosh(tiny), 1.0, 0.000000000000001) then return 9 end if
    if not approx_eq(tanh(tiny), tiny, 0.000000000000001) then return 9 end if
    println("tiny-hyperbolic-stable")
    println(radians_to_degrees(PI) as i32)
    println(degrees_to_radians(180.0) as i32)
    println(clamp(12.0, -2.0, 7.0) as i32)
    println(clamp_i32(-8, -3, 10))

    if approx_eq(1.0, 1.125, 0.125) then
        println("inclusive-epsilon")
    else
        return 10
    end if
    if approx_eq(1.0, 1.0, -0.1) then
        return 11
    else
        println("negative-epsilon-rejected")
    end if

    seed(-1)
    println(random_i32())
    seed(2147483647)
    println(random_i32())
    println(random_range(-5, -5))
    let bounded = random_range(-10, 10)
    if bounded < -10 or bounded > 10 then return 12 end if
    println("range-bounded")
    seed(1)
    println(random_range(-2147483648, 2147483647))
    seed(1)
    let unit = random_f64()
    if unit <= 0.0 or unit >= 1.0 then return 13 end if
    println("unit-random-bounded")
    let ranged = random_range_f64(-2.0, 3.0)
    if ranged < -2.0 or ranged >= 3.0 then return 14 end if
    println("f64-range-bounded")

    let a = vec2.new(3.0, 4.0)
    println(a.length() as i32)
    println(a.dot(a) as i32)
    println(a.distance(vec2.zero()) as i32)
    let a_plus_one = a.add(vec2.one())
    println(a_plus_one.x as i32)
    println(a_plus_one.y as i32)
    let b = vec3.new(1.0, 0.0, 0.0)
    let c = vec3.new(0.0, 1.0, 0.0)
    println(b.cross(c).z as i32)
    println(b.dot(c) as i32)
    println(b.add(c).length_sq() as i32)
    return 0
end fn
