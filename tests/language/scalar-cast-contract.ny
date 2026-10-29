@intrinsic("math.sqrt")
fn square_root(value: f64) -> f64
    return 0.0
end fn

@intrinsic("math.exp")
fn exponential(value: f64) -> f64
    return 0.0
end fn

fn main() -> i32
    println(0 as bool)
    println(1 as bool)
    println(1.0 as bool)
    println(1.0 as bool)
    println((-0.0) as bool)
    println(true as i32)
    println(false as i64)
    println(4294967297 as i32)
    println(2147483648 as i32)
    println(4294967295 as i32)
    println((-2147483649) as i32)
    println((4294967295 as i32) as i64)
    println(4294967297.9 as i32)
    println((-4294967297.9) as i32)
    println(42.9 as i64)
    println((-42.9) as i64)
    println(511.75 as u8)
    println((-256.75) as u8)
    println((-1) as u8)
    println(256 as u8)
    println(((-9223372036854775808.0) as i64))
    println(9223372036854774784.0 as i64)
    println((true as f64) == 1.0)
    println(((-7) as f64) == -7.0)
    println(4294967297 as int)
    println(square_root(1.0) as bool)
    println(exponential(0.0) as bool)
    return 0
end fn
