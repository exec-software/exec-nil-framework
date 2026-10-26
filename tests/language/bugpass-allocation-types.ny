type Wide = i64
type Byte = u8

fn make<T>(count: i32) -> ptr<T>
    return allocate<T>(count)
end fn

fn make_offset<T>(count: i32) -> ptr<T>
    return allocate<T>(count) + 0
end fn

fn amount() -> i32
    println("size once")
    return 2
end fn

fn main() -> i32
    let direct = allocate<i32>(amount()) + 0
    direct[0] = 17
    direct[1] = 29
    println(direct[0])
    println(direct[1])
    deallocate(direct)

    let alias = allocate<Wide>(2)
    alias[0] = 4294967297
    alias[1] = 8589934594
    println(alias[0])
    println(alias[1])
    deallocate(alias)

    let specialized = make<Wide>(2)
    specialized[0] = 4294967297
    specialized[1] = 8589934594
    println(specialized[0])
    println(specialized[1])
    deallocate(specialized)

    let bytes = make_offset<Byte>(2)
    bytes[0] = 255 as u8
    bytes[1] = 129 as u8
    println(bytes[0])
    println(bytes[1])
    deallocate(bytes)
    return 0
end fn
