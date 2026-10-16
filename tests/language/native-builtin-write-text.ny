fn reject_write_text(value: text)
    write_i32(value, 1)
end fn

fn main() -> i32
    reject_write_text("value")
    return 0
end fn
