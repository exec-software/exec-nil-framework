fn byte_count(values: array<u8>) -> i32
    return values.len()
end fn

fn main() -> i32
    let mut values: array<u8> = []
    values.push(1)
    values.push(2)
    values.push(3)
    println(byte_count(values))
    return 0
end fn
