import nil.std.text

fn main() -> i32
    let bytes: array<u8> = [65 as u8, 66 as u8]
    println(nil::std::text::from_bytes(bytes))
    return 0
end fn
