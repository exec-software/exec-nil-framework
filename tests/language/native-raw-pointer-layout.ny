module ny.tests.native_raw_pointer_layout

fn main() -> i32
    let bytes: ptr<u8> = allocate<u8>(4)
    bytes[0] = 0
    bytes[1] = 127
    bytes[2] = 128
    bytes[3] = 255
    println(bytes[0])
    println(bytes[1])
    println(bytes[2])
    println(bytes[3])
    *bytes = 42
    println(*bytes)
    deallocate(bytes)

    let flags: ptr<bool> = allocate<bool>(1)
    flags[0] = true
    if *flags then
        println(1)
    end if
    deallocate(flags)

    let words: ptr<i32> = allocate<i32>(2)
    words[0] = -2147483647 - 1
    words[1] = 2147483647
    println(words[0])
    println(words[1])
    deallocate(words)
    return 0
end fn
