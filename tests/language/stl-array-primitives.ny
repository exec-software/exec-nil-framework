import ny.lang.collections

fn round_trip<T>(value: T) -> T
    let values: array<T> = new_with_value<T>(2, value)
    return get<T>(values, 1)
end fn

fn main() -> i32
    let samples: array<f64> = new_with_value<f64>(3, 1.25)
    println(length<f64>(samples))
    println(get<f64>(samples, 1) == 1.25)
    array_set<f64>(samples, 1, -2.5)
    println(get<f64>(samples, 1) == -2.5)
    array_set<f64>(samples, -1, 99.0)
    array_set<f64>(samples, 3, 99.0)
    println(get<f64>(samples, 0) == 1.25)
    println(get<f64>(samples, -1) == 0.0)
    println(get<f64>(samples, 3) == 0.0)
    println(round_trip<f64>(-0.125) == -0.125)

    let words: array<text> = new_with_value<text>(2, "filled" + "-word")
    println(get<text>(words, 1))
    array_set<text>(words, 0, "changed")
    println(get<text>(words, 0))
    println(get<text>(words, -1) == "")
    println(get<text>(words, 2) == "")
    let empty: array<text> = new_with_value<text>(0, "unused")
    println(length<text>(empty))
    println(get<text>(empty, 0) == "")
    println(round_trip<text>("generic-text"))

    let integers: array<i32> = new_with_value<i32>(2, -42)
    println(get<i32>(integers, 0))
    array_set<i32>(integers, 1, 73)
    println(get<i32>(integers, 1))
    println(get<i32>(integers, -1))
    let bytes: array<u8> = new_u8(2, 255 as u8)
    println(get_u8(bytes, 0))
    set_u8(bytes, 1, 128 as u8)
    println(get_u8(bytes, 1))
    let flags: array<bool> = new_with_value<bool>(2, true)
    array_set<bool>(flags, 0, false)
    println(get<bool>(flags, 0))
    println(get<bool>(flags, 1))
    return 0
end fn
