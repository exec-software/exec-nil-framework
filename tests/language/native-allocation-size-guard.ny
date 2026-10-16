fn main() -> i32
    let values = allocate<i64>(9223372036854775807)
    values[0] = 1
    deallocate(values)
    return 0
end fn
