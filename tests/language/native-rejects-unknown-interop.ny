@interop("ny_tests_missing_native_library")
fn unknown_native_interop(value: i32) -> i32
    return value
end fn

fn main() -> i32
    println(unknown_native_interop(42))
    return 0
end fn
