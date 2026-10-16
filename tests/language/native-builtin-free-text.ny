fn reject_free_text(value: text)
    free(value)
end fn

fn main() -> i32
    reject_free_text("value")
    return 0
end fn
