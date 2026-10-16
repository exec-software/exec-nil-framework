@intrinsic("array.new_with_value")
fn native_fill(size: i32, value: i32) -> array<i32>
    return []
end fn

fn main() -> i32
    let values: array<i32> = native_fill(3, 7)
    println(values.len())
    println(values[0] + values[2])
    return 0
end fn
