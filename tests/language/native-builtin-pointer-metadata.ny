fn main() -> i32
    let memory = alloc(4)
    write_i32(memory, 42)
    println(read_i32(memory))
    free(memory)
    return 0
end fn
