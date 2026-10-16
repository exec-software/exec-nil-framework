fn reject_read_text(value: text) -> i32
    return read_i32(value)
end fn

fn main() -> i32
    reject_read_text("value")
    return 0
end fn
