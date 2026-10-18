# Values and arithmetic distinguish inferred i32 from i64 on every route.
import nil.std.core

const global_wide = 4294967296
const global_minimum = -9223372036854775808

fn wide_sum(items: array<i64>) -> i64
    return items[0] + items[1]
end fn

fn narrow_first(items: array<i32>) -> i32
    return items[0]
end fn

fn literal_return() -> i64
    return 4294967296
end fn

fn increment(value: i64) -> i64
    return value + 1
end fn

fn classify(value: i64) -> i32
    match value
        4294967296 -> return 2
        -9223372036854775808 -> return 1
        0x7FFFFFFFFFFFFFFF -> return 3
        _ -> return 0
    end match
    return 0
end fn

fn classify_mask(value: i32) -> i32
    match value
        0 -> return 0
        -0xFFFFFFFF -> return 6
        _ -> return 0
    end match
    return 0
end fn

fn main() -> i32
    println(2147483647)
    println(2147483647 + 1)
    println(-2147483648)
    println(-2147483648 - 1)
    println(-(2147483648))
    println(-(-2147483648))
    println(2147483648)
    println(2147483648 + 1)
    println(4294967295 + 1)
    println(-2147483649 - 1)
    println(9223372036854775807)
    println(9223372036854775807 + 1)
    println(-9223372036854775808)
    println(-9223372036854775808 - 1)
    println(-(-9223372036854775808))
    println(-0x8000000000000000)
    println(0x7FFFFFFF + 1)
    println(0x80000000)
    println(0xFFFFFFFF)
    let sign_mask = 0x80000000
    let all_bits = 0xFFFFFFFF
    println(sign_mask)
    println(all_bits)
    println(0xFFFFFFFF as i64)
    println(-0x80000000)
    println(0x100000000)
    println(0x7FFFFFFFFFFFFFFF)
    println(0xFFFFFFFF & 0x80000000)
    println(0x100000000 | 0x00000001)
    println(0x100000000 >> 32)
    println(4294967295 >> 32)
    println(global_wide)
    println(global_minimum)
    println(increment(literal_return()))

    let low_first = [1, 2147483648]
    let high_first = [2147483648, 1]
    println(wide_sum(low_first))
    println(wide_sum(high_first))
    let small = 7
    let wide = 4294967296
    let variables_low_first = [small, wide]
    let variables_high_first = [wide, small]
    println(wide_sum(variables_low_first))
    println(wide_sum(variables_high_first))
    let signed_masks = [all_bits, 4294967296]
    println(wide_sum(signed_masks))
    println(wide_sum([0xFFFFFFFF, 4294967296]))
    println(wide_sum([4294967296, 0xFFFFFFFF]))
    let minimums = [-2147483648, -9223372036854775808]
    println(minimums[0])
    println(minimums[1])
    println(narrow_first([0xFFFFFFFF, 0x80000000]))

    let narrowed: i32 = 4294967297
    let narrow_items: array<i32> = [2147483648 as i32, 4294967297 as i32]
    println(narrowed)
    println(narrow_items[0])
    println(narrow_items[1])
    println(4294967295 as i32)
    println((-2147483649) as i32)
    println((4294967295 as i32) as i64)

    println(classify(-9223372036854775808))
    println(classify(4294967296))
    println(classify(9223372036854775807))
    println(match -9223372036854775808 { -9223372036854775808 -> 4; _ -> 0; })
    println(match 4294967296 { 0x100000000 -> 5; _ -> 0; })
    println(classify_mask(1))
    println(match 1 { -0xFFFFFFFF -> 7; _ -> 0; })
    println(abs_i64(0))
    println(abs_i64(-4294967296))
    println(abs_i32(0))
    println(match 1 { 2 - 1 -> 8; _ -> 0; })
    println(match 1 { 1 when 2 == 3 - 1 -> 9; _ -> 0; })
    match 7
        -1 -> println(0)
        7 -> println(match 1 { 2 - 1 -> 10; _ -> 0; })
        -2 -> println(0)
        _ -> println(0)
    end match
    return 0
end fn
