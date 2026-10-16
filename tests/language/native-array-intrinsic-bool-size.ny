@intrinsic("array.new_with_value")
fn bad_new(size: bool, value: i32) -> array<i32>
    return []
end fn

fn main() -> i32
    bad_new(false, 1)
    return 0
end fn
