module ny.tests.u8_canonicalization

import ny.lang.io.file

fn main() -> i32
    println((-1 as u8))
    println((255 as u8))
    println((256 as u8))
    println((511 as u8))

    let mut values: array<u8> = [
        (-1 as u8),
        (255 as u8),
        (256 as u8),
        (511 as u8)
    ]
    values[0] = 256
    values[1] = 511
    values.push(-1)
    println(values.len())
    let mut value_index: i32 = 0
    while value_index < values.len()
        println(values[value_index])
        value_index = value_index + 1
    end while

    let raw: ptr<u8> = allocate<u8>(4)
    raw[0] = -1
    raw[1] = 255
    raw[2] = 256
    raw[3] = 511
    println(raw[0])
    println(raw[1])
    println(raw[2])
    println(raw[3])
    deallocate(raw)

    let path = "u8-canonicalization.bin"
    match ny::lang::io::write_bytes_atomic(path, values, false)
        Result.Error(_) -> return 40
        Result.Ok(written) ->
            if not written then return 41 end if
    end match
    match ny::lang::io::read_regular_bytes(path, 5)
        Result.Error(_) -> return 42
        Result.Ok(bytes) ->
            if bytes.len() != 5 then return 43 end if
            let mut byte_index: i32 = 0
            while byte_index < bytes.len()
                println(bytes[byte_index])
                byte_index = byte_index + 1
            end while
    end match
    return 0
end fn
