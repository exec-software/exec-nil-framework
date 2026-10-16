@intrinsic("array.get")
fn native_get(values: array<i32>, index: i32) -> i32
    return 0
end fn

@intrinsic("array.set")
fn native_set(values: array<i32>, index: i32, value: i32)
end fn

@intrinsic("array.get")
fn native_get_i64(values: array<i64>, index: i32) -> i64
    return 0
end fn

@intrinsic("array.set")
fn native_set_i64(values: array<i64>, index: i32, value: i64)
end fn

@intrinsic("array.get")
fn native_get_bool(values: array<bool>, index: i32) -> bool
    return false
end fn

@intrinsic("array.set")
fn native_set_bool(values: array<bool>, index: i32, value: bool)
end fn

fn main() -> i32
    let values: array<i32> = [10, 20, 30]
    println(native_get(values, 1))
    native_set(values, 1, 42)
    println(native_get(values, 1))
    native_set(values, -1, 90)
    native_set(values, 3, 91)
    native_set(values, -2147483648, 92)
    native_set(values, 2147483647, 93)
    println(native_get(values, -1))
    println(native_get(values, 3))
    println(native_get(values, -2147483648))
    println(native_get(values, 2147483647))
    println(native_get(values, 0))
    println(native_get(values, 2))
    println(values.len())

    let empty: array<i32> = []
    native_set(empty, -2147483648, 94)
    native_set(empty, 0, 95)
    native_set(empty, 2147483647, 96)
    println(native_get(empty, -2147483648))
    println(native_get(empty, 0))
    println(native_get(empty, 2147483647))
    println(empty.len())

    let wide: array<i64> = [40 as i64, 2 as i64]
    native_set_i64(wide, 0, 41 as i64)
    println(native_get_i64(wide, 0) + native_get_i64(wide, 1))

    let flags: array<bool> = [true, false]
    if native_get_bool(flags, 0) then
        println(1)
    end if
    native_set_bool(flags, 1, true)
    if native_get_bool(flags, 1) then
        println(1)
    end if
    return 0
end fn
