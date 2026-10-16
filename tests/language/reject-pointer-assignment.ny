module ny.tests.reject_pointer_assignment

fn main() -> i32
    let words: ptr<i32> = allocate<i32>(1)
    let mut bytes: ptr<u8> = allocate<u8>(1)
    bytes = words
    let mut address: i64 = 0
    address = words
    *words = bytes
    words[0] = bytes
    let mut byte_values: array<u8> = [(0 as u8)]
    byte_values[0] = words
    deallocate(bytes)
    deallocate(words)
    return 0
end fn
