@intrinsic("io.console.write")
@interop("ny_tests_missing_native_library")
fn conflicting_native_dispatch(value: text)
end fn

fn main() -> i32
    conflicting_native_dispatch("must not silently dispatch")
    return 0
end fn
