import ny.lang.core.*

fn main() -> i32
    println(Convert.to_i32("-2147483648"))
    println(Convert.to_i64("9223372036854775807"))
    println(Convert.to_f64("1.25e2"))
    println(Convert.to_bool("On"))
    println(Convert.i32_to_text(-42))
    println(Convert.f64_to_text_precision(12.3456, 2))
    println(Convert.to_hex(-42))
    println(Convert.to_hex_upper(48879))
    println(Convert.to_hex_padded(-42, 4))
    println(Convert.from_hex("-0x2A"))
    println(Convert.to_binary(-42))
    println(Convert.from_binary("-0b101010"))
    println(Convert.to_octal(511))
    println(Convert.from_octal("0o777"))
    println(Convert.from_base("z", 36))
    println(Convert.char_to_i32("λ"))
    println(Convert.i32_to_char(955))
    println(Convert.bytes_to_hex([0, 15, 16, 255]))
    let decoded: array<u8> = Convert.hex_to_bytes("000f10ff")
    println(decoded.length())
    println(decoded[0])
    println(decoded[3])

    let bad_i32 = Convert.try_to_i32("2147483648")
    let bad_f64 = Convert.try_to_f64("1.0junk")
    let bad_bool = Convert.try_to_bool("maybe")
    match bad_i32
        Result.Ok(_) -> println("unexpected")
        Result.Error(message) -> println(message)
    end match
    match bad_f64
        Result.Ok(_) -> println("unexpected")
        Result.Error(message) -> println(message)
    end match
    match bad_bool
        Result.Ok(_) -> println("unexpected")
        Result.Error(message) -> println(message)
    end match
    return 0
end fn
