fn reject_handle_negation(value: ptr<i32>) -> i32
    let invalid = -value
    return 0
end fn

fn main() -> i32
    reject_handle_negation(alloc(4))
    return 0
end fn
