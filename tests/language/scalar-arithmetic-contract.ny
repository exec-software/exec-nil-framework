struct ScalarBox<T>
    value: T
end struct

enum ScalarOption<T>
    Some(T)
    None
end enum

fn increment(value: i32) -> i32
    return value + (1 as i32)
end fn

fn narrow_return() -> i32
    return 4294967297
end fn

fn echo_byte(value: u8) -> u8
    return value
end fn

fn main() -> i32
    let maximum: i32 = 2147483647
    println(maximum + (1 as i32))
    let mut wrapped: i32 = maximum + (1 as i32)
    println(wrapped)
    wrapped = maximum + (1 as i32)
    println(wrapped)
    println(increment(maximum))
    println((maximum + (1 as i32)) as i64)
    println(((-2147483647 - 1) as i32) - (1 as i32))
    let maximum64: i64 = 9223372036854775807
    let minimum64: i64 = maximum64 + (1 as i64)
    println(minimum64)
    println(-minimum64)
    println((255 as u8) + (1 as u8))
    println((255 as u8) + (1 as i32))
    println((65536 as i32) * (65536 as i32))
    println((128 as u8) * (2 as u8))
    println((-8 as i32) >> 1)
    println((248 as u8) >> 1)
    println((1 as i32) << 31)
    println((1 as u8) << 7)
    println((1 as i64) << 63)
    println((1 as u8) << (7 as i64))
    println((5 as i32) + (7 as i64))
    println(((5 as u8) + 0.5) == 5.5)
    println((255 as u8) & (15 as u8))
    println((240 as u8) | (15 as u8))
    println((255 as u8) ^ (15 as u8))
    println(~(0 as u8))
    println(~(0 as i32))
    println(((-7) as i32) / (3 as i32))
    println(((-7) as i32) % (3 as i32))
    println((255 as u8) / (2 as u8))
    println(narrow_return())
    let mut narrowed: i32 = 4294967297
    println(narrowed)
    narrowed = 4294967298
    println(narrowed)
    println(echo_byte(511 as u8))
    let box: ScalarBox<i32> = ScalarBox<i32> { value: 4294967297 }
    println(box.value)
    box.value = 4294967298
    println(box.value)
    let flag: ScalarBox<bool> = ScalarBox<bool> { value: 3 }
    println(flag.value)
    flag.value = 0
    println(flag.value)
    let optional: ScalarOption<i32> = ScalarOption.Some(4294967297)
    match optional
        ScalarOption.Some(value) -> println(value)
        ScalarOption.None -> return 1
    end match
    let byte_option: ScalarOption<u8> = ScalarOption.Some(511 as u8)
    match byte_option
        ScalarOption.Some(value) -> println(value)
        ScalarOption.None -> return 2
    end match
    for index in range(2147483647, 2147483649)
        println(index)
        println(index + 0)
    end for
    return 0
end fn
