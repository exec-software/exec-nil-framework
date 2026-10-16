enum native_numeric_mode
    ready
end enum

fn reject_enum_multiply(value: native_numeric_mode) -> i32
    let invalid = value * value
    return 0
end fn

fn main() -> i32
    reject_enum_multiply(0)
    return 0
end fn
