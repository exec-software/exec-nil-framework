module ny.tests.native_pointer_arithmetic

fn main() -> i32
    # This integer intentionally equals nilinterpreter's first virtual heap
    # address. It must remain ordinary arithmetic while that block is live.
    let collision_guard: ptr<i32> = allocate<i32>(2)
    let ordinary_address_value: i64 = 17179869184
    println(ordinary_address_value + 1)
    deallocate(collision_guard)

    let bytes: ptr<u8> = allocate<u8>(4)
    bytes[0] = 11
    bytes[1] = 22
    bytes[2] = 33
    bytes[3] = 44
    let byte_tail = bytes + 3
    println(*byte_tail)
    let byte_middle = byte_tail - 2
    println(byte_middle[0])
    byte_middle[1] = 55
    println(bytes[2])
    deallocate(bytes)

    let flags: ptr<bool> = allocate<bool>(3)
    let second_flag = flags + 1
    *second_flag = 9
    if flags[1] then
        println(1)
    end if
    let first_flag = second_flag - 1
    if *first_flag then
        println(0)
    else
        println(2)
    end if
    deallocate(flags)

    let words: ptr<i32> = allocate<i32>(3)
    words[0] = 4294967295
    println(words[0])
    words[0] = 100
    words[1] = 200
    words[2] = 300
    let word_offset: i64 = 1
    let second_word = words + word_offset
    *second_word = 4294967296
    println(*second_word)
    *second_word = 200
    println(*second_word)
    let end_words = second_word + 2
    let last_word = end_words - 1
    println(*last_word)
    let first_word = second_word - word_offset
    println(first_word[0])
    deallocate(words)

    let wide: ptr<i64> = allocate<i64>(2)
    wide[0] = 4294967296
    wide[1] = 8589934592
    let wide_second = wide + 1
    println(*wide_second)
    println(*(wide_second - 1))
    deallocate(wide)
    return 0
end fn
