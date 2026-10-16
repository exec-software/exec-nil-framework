fn main() -> i32
    let stale = alloc(1)
    write_i32(stale, 43)
    free(stale)
    println(read_i32(stale))
    free(stale)
    return 0
end fn
