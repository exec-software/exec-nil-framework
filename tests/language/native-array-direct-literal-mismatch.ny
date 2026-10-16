fn require_i64(values: array<i64>) -> i32
    return values.len()
end fn

fn main() -> i32
    return require_i64([1, 2])
end fn
