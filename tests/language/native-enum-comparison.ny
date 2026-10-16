enum native_comparison_mode
    ready
end enum

fn reject_enum_comparison(value: native_comparison_mode) -> i32
    let invalid = value == value
    return 0
end fn

fn main() -> i32
    reject_enum_comparison(0)
    return 0
end fn
