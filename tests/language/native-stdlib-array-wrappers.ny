module ny.tests.native_stdlib_array_wrappers

import ny.lang.collections.array

fn first<T>(values: array<T>) -> T
    return values[0]
end fn

fn main() -> i32
    let values: array<u8> = ny::lang::collections::new_u8(4, 17 as u8)
    println(ny::lang::collections::length(values))
    println(values[0])
    values[3] = 255 as u8
    println(values[3])
    println(ny::lang::collections::length(values))
    println(first(values))
    return 0
end fn
