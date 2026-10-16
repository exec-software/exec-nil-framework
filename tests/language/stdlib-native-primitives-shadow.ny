fn array__get(left: i64, right: i64) -> i64
    return left * 10 + right
end fn

fn array__set(first: i64, second: i64, third: i64) -> i64
    return first * 100 + second * 10 + third
end fn

fn array__length(left: i64, right: i64) -> i64
    return left + right
end fn

fn String__substr(first: i64, second: i64, third: i64) -> i64
    return first + second + third
end fn

fn panic(message: text) -> i64
    return message.length()
end fn

fn main() -> i32
    println(array__get(2, 3))
    println(array__set(1, 2, 3))
    println(array__length(4, 5))
    println(String__substr(6, 7, 8))
    println(panic("ordinary user function"))
    return 0
end fn
