fn make_byte_buffer() -> array<u8>
    let mut values: array<u8> = []
    values.push(1)
    values.push(128)
    values.push(255)
    return values
end fn

fn middle_byte(values: array<u8>) -> u8
    return values[1]
end fn

fn main() -> i32
    let values: array<u8> = make_byte_buffer()
    println(values.len())
    println(middle_byte(values))
    println(values[2])
    return 0
end fn
