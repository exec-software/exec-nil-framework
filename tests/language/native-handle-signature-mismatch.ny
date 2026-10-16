fn wrong_pointer_width(value: ptr<i64>) -> ptr<i64>
    return value
end fn

fn main() -> i32
    let values = allocate<i32>(1)
    let wrong = wrong_pointer_width(values)
    deallocate(wrong)
    return 0
end fn
