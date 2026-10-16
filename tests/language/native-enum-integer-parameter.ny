enum native_mode
    ready
end enum

fn mode_code(value: native_mode) -> i32
    return 1
end fn

fn main() -> i32
    return mode_code(0)
end fn
