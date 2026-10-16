module ny.tests.pointer_one_past_identity

fn retain_first_one_past() -> ptr<i32>
    let first: ptr<i32> = allocate<i32>(2)
    first[0] = 731
    first[1] = 732
    return first + 2
end fn

fn main() -> i32
    # Keep only A's logical one-past pointer, then allocate B. Walking back
    # from the retained pointer must still recover A rather than aliasing B.
    let first_end = retain_first_one_past()
    let second: ptr<i32> = allocate<i32>(2)
    second[0] = 901
    second[1] = 902

    let first_last = first_end - 1
    println(*first_last)
    println(second[0])

    let first_base = first_end - 2
    println(*first_base)
    deallocate(first_base)
    deallocate(second)
    return 0
end fn
