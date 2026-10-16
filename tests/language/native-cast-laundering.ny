fn main() -> i32
    let source = allocate<i32>(1)
    let address = source as i64
    let wrong = address as ptr<i64>
    wrong[0] = 7
    deallocate(wrong)
    return 0
end fn
