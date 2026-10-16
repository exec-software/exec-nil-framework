fn square(value: i64) -> i64
    return value * value
end fn

fn transform(value: i64) -> i64
    return square(value) + 3
end fn

fn increment(value: i32) -> i32
    return value + 1
end fn

fn invert(value: bool) -> bool
    return not value
end fn

fn half(value: f64) -> f64
    return value / 2.0
end fn

fn byte_mask(value: u8) -> u8
    return value ^ (255 as u8)
end fn

fn widen(value: u8) -> i64
    return (value as i64) * 2
end fn

fn recursive_sum(value: i64) -> i64
    if value < 2 then
        return value
    end if
    return value + recursive_sum(value - 1)
end fn

fn main() -> i32
    let inputs: array<i64> = [1 as i64, 2 as i64, 3 as i64, 4 as i64]
    let outputs = parallel_map(transform, inputs)
    println(outputs[0])
    println(outputs[1])
    println(outputs[2])
    println(outputs[3])
    println(inputs[2])
    let integers: array<i32> = [7, 11, 19]
    let incremented = parallel_map(increment, integers)
    println(incremented[2])
    let flags: array<bool> = [true, false, true]
    let inverted = parallel_map(invert, flags)
    println(inverted[0])
    println(inverted[1])
    let values: array<f64> = [2.5, 7.0]
    let halves = parallel_map(half, values)
    println(halves[0])
    println(halves[1])
    let bytes: array<u8> = [0 as u8, 127 as u8, 255 as u8]
    let masked = parallel_map(byte_mask, bytes)
    println(masked[0])
    println(masked[1])
    println(masked[2])
    let widened = parallel_map(widen, bytes)
    println(widened[2])
    let sums = parallel_map(recursive_sum, inputs)
    println(sums[3])
    let empty: array<i64> = []
    let empty_output = parallel_map(transform, empty)
    println(empty_output.len())
    println(transform(5))
    return 0
end fn
