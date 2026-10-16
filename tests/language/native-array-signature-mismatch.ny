fn retain_i32(values: array<i32>) -> array<i32>
    return values
end fn

fn require_i64(values: array<i64>) -> array<i64>
    return values
end fn

fn main() -> i32
    let source: array<i32> = [1]
    let typed_i32 = retain_i32(source)
    let wrong = require_i64(typed_i32)
    println(wrong.len())
    return 0
end fn
