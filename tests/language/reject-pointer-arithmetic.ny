module ny.tests.reject_pointer_arithmetic

fn main() -> i32
    let words: ptr<i32> = allocate<i32>(2)
    let wrong_rhs = words + 1.5
    let reversed = 1 + words
    let paired = words - words
    deallocate(words)
    return 0
end fn
