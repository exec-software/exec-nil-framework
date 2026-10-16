fn reject_handle_multiply(value: text) -> i32
    let invalid = value * value
    return 0
end fn

fn main() -> i32
    reject_handle_multiply("value")
    return 0
end fn
