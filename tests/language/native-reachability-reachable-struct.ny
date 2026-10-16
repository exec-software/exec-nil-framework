struct reachable_byte_buffer
    bytes: array<u8>
end struct

fn construct_reachable_buffer() -> reachable_byte_buffer
    let mut bytes: array<u8> = []
    bytes.push(1)
    bytes.push(2)
    bytes.push(3)
    return reachable_byte_buffer { bytes: bytes }
end fn

fn main() -> i32
    let value: reachable_byte_buffer = construct_reachable_buffer()
    println(value.bytes.len())
    println(value.bytes[0])
    println(value.bytes[1])
    println(value.bytes[2])
    return 0
end fn
