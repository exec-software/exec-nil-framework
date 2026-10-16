fn main() -> i32
    let typed = allocate<i32>(1)
    write_i32(typed, 41)
    println(read_i32(typed))
    free(typed)

    let legacy = alloc(1)
    write_i32(legacy, 42)
    println(*legacy)
    deallocate(legacy)
    return 0
end fn
