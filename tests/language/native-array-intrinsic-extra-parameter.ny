@intrinsic("array.get")
fn bad_get(values: array<i32>, index: i32, ignored: i32) -> i32
    return 0
end fn

fn main() -> i32
    let values: array<i32> = [7]
    println(bad_get(values, 0, 0))
    return 0
end fn
