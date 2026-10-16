@intrinsic("ny.tests.native.unknown")
fn unknown_native_intrinsic(value: i32) -> i32
    return value + 1
end fn

fn main() -> i32
    println(unknown_native_intrinsic(41))
    return 0
end fn
