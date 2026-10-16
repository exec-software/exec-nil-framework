module ny.tests.pointer_comparisons

fn report(value: bool)
    if value then
        println(1)
    else
        println(0)
    end if
end fn

fn main() -> i32
    let bytes: ptr<u8> = allocate<u8>(2)
    let next_byte = bytes + 1
    report(bytes == bytes)
    report(bytes != next_byte)
    report(bytes < next_byte)
    report(bytes <= bytes)
    report(next_byte > bytes)
    report(next_byte >= next_byte)
    deallocate(bytes)

    let flags: ptr<bool> = allocate<bool>(2)
    let next_flag = flags + 1
    report(flags == flags)
    report(flags < next_flag)
    deallocate(flags)

    let words: ptr<i32> = allocate<i32>(2)
    let next_word = words + 1
    report(words != next_word)
    report(next_word > words)
    deallocate(words)

    let wide: ptr<i64> = allocate<i64>(2)
    let next_wide = wide + 1
    report(wide <= next_wide)
    report(next_wide >= wide)
    deallocate(wide)
    return 0
end fn
