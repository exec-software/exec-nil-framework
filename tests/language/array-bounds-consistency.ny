fn main() -> i32
    let values: array<i32> = [11, 22]
    values[-1] = 90
    values[2] = 91
    values[-2147483648] = 92
    values[2147483647] = 93
    println(values[-1])
    println(values[2])
    println(values[-2147483648])
    println(values[2147483647])
    println(values[0])
    println(values[1])
    println(values.len())

    let empty: array<i32> = []
    empty[-2147483648] = 94
    empty[0] = 95
    empty[2147483647] = 96
    println(empty[-2147483648])
    println(empty[0])
    println(empty[2147483647])
    println(empty.len())
    return 0
end fn
