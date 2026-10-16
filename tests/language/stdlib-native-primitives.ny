import ny.lang.collections
import ny.lang.core.*

fn main() -> i32
    let bytes: array<u8> = [0 as u8, 255 as u8]
    bytes.set(0, 128 as u8)
    bytes.set(-1, 3 as u8)
    bytes.set(2, 4 as u8)
    bytes.push(511 as u8)
    println(bytes.length())
    println(bytes.get(0))
    println(bytes.get(1))
    println(bytes.get(2))
    println(bytes.get(-1))
    println(bytes.get(3))
    let integers: array<i32> = [-42]
    integers.set(0, -17)
    println(integers.get(0))
    let numbers: array<f64> = [1.25]
    numbers.set(0, -2.5)
    println(numbers.get(0) == -2.5)
    println(numbers.get(1) == 0.0)
    let flags: array<bool> = [false]
    flags.set(0, true)
    println(flags.get(0))
    let words: array<text> = ["first"]
    words.set(0, "word" + "-value")
    println(words.get(0))
    println(words.get(1) == "")

    let mut data: vec<u8> = vec<u8>.new()
    data.push(1 as u8)
    data.push(255 as u8)
    data.clear()
    data.push(128 as u8)
    data.set(0, 254 as u8)
    println(data.length())
    println(data.get(0).unwrap())
    println(data.get(-1).is_none())
    println(data.get(1).is_none())

    let source: text = "bytes" + "-value"
    println(source.len())
    println(source.length())
    println(source.substr(1, 3))
    println(source.substr(-7, 2))
    println(source.substr(6, 1000))
    println(source.substr(1000, 2) == "")
    println(source.substr(0, -1) == "")
    println(source.substr(11, 1) == "")
    println(source.substr(6, 9223372036854775807))
    println("é".len())
    println("é!".substr(0, 2))
    let failure: Result<array<u8>, text> = Result.Error("invalid input")
    match failure
        Result.Ok(_) -> return 99
        Result.Error(message) -> println(message)
    end match
    let success: Result<i64, text> = Result.Ok(123456789)
    println(success.unwrap())
    return 0
end fn
