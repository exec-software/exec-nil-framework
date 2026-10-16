import ny.lang.core.*
import ny.lang.system.*

fn main() -> i32
    let scenario = arg(0)
    if scenario == "i64-overflow" then
        println(Convert.to_i64("9223372036854775808"))
    else if scenario == "nonfinite-f64" then
        println(Convert.to_f64("inf"))
    else if scenario == "invalid-base" then
        println(Convert.from_base("10", 1))
    else if scenario == "invalid-scalar" then
        println(Convert.i32_to_char(55296))
    else if scenario == "multiple-scalars" then
        println(Convert.char_to_i32("ab"))
    else if scenario == "odd-hex" then
        println(Convert.hex_to_bytes("abc").length())
    else
        println(Convert.f64_to_text_precision(1.0, 101))
    end if
    return 0
end fn
