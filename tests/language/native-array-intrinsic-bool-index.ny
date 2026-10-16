@intrinsic("array.set")
fn bad_set(values: array<i32>, index: bool, value: i32)
end fn

fn main() -> i32
    let values: array<i32> = [7]
    bad_set(values, false, 1)
    return 0
end fn
