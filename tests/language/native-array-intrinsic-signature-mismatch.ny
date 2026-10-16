@intrinsic("array.get")
fn bad_get(values: array<i32>, index: i32) -> i64
    return 0
end fn

fn main() -> i32
    let values: array<i32> = [1]
    println(bad_get(values, 0))
    return 0
end fn
