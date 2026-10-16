fn echo_text(value: text) -> text
    return value
end fn

fn main() -> i32
    let message: text = echo_text("typed ABI")
    println(message)
    return 0
end fn
