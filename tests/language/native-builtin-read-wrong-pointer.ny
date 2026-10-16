fn reject_read_pointer_kind(value: ptr<i64>) -> i32
    return read_i32(value)
end fn

fn main() -> i32
    reject_read_pointer_kind(alloc(8))
    return 0
end fn
