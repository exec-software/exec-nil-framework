import nil.std.math

const global_scale: f64 = 1e3

fn half(value: f64) -> f64
    return value / 2e0
end fn

fn classify(value: f64) -> i32
    match value
        1e3 -> return 1
        1.5e-3 -> return 2
        _ -> return 0
    end match
    return 0
end fn

fn main() -> i32
    let inferred = 1e0
    println(half(inferred) == 0.5)
    println(global_scale == 1000.0)
    println(1E+2 == 100.0 and 1.25e-2 == 0.0125)
    println(-2.5e+2 == -250.0)
    println(1e3-2.0 == 998.0)
    println(0xDEAD == 57005 and 0x1E3 == 483 and 0xfeed == 65261)
    let values = [1e0, 2.5e1, 3E-1]
    println(values[0] == 1.0 and values[1] == 25.0 and values[2] == 0.3)
    println(classify(1000.0) == 1 and classify(0.0015) == 2)
    println(match 1e3 { 1E+3 -> true; _ -> false; })
    println(1.7976931348623157e308 > 1.7e308)
    println(2.2250738585072014e-308 > 2.225073858507201e-308)
    println(5e-324 > 0.0 and 5e-324 + 5e-324 == 1e-323)
    println(5e-324 / 5e-324 == 1.0)
    println(0e999999999999999999999 == 0.0 and 0e-999999999999999999999 == 0.0)
    println(atan2(-0e3, -1.0) < 0.0 and atan2(0E-99999999999999, -1.0) > 0.0)
    println(1.25 + 2.5 == 3.75)
    return 0
end fn
