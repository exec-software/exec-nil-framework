module ny.tests.native_pointer_comparison_mismatch

fn main() -> i32
    let words: ptr<i32> = allocate<i32>(1)
    let bytes: ptr<u8> = allocate<u8>(1)
    if words == bytes then
        println(1)
    end if
    deallocate(words)
    deallocate(bytes)
    return 0
end fn
