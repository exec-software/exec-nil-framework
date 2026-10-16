module ny.tests.native_cli_secure_byte_io

import ny.lang.io.file

fn main() -> i32
    # The route harness runs each backend in a new private working directory.
    let path = "native-secure-byte-io.bin"
    let mut initial: array<u8> = []
    initial.push(0)
    initial.push(128)
    initial.push(255)

    match ny::lang::io::write_bytes_atomic(path, initial, false)
        Result.Error(_) -> return 30
        Result.Ok(written) ->
            if not written then return 31 end if
            println("created")
    end match

    match ny::lang::io::read_regular_bytes(path, 3)
        Result.Error(_) -> return 32
        Result.Ok(bytes) ->
            if bytes.len() != 3 then return 33 end if
            if bytes[0] != (0 as u8) then return 34 end if
            if bytes[1] != (128 as u8) then return 35 end if
            if bytes[2] != (255 as u8) then return 36 end if
            println("read-first")
    end match

    match ny::lang::io::write_bytes_atomic(path, initial, false)
        Result.Ok(_) -> return 37
        Result.Error(_) -> println("no-clobber")
    end match

    match ny::lang::io::read_regular_bytes(path, 2)
        Result.Ok(_) -> return 38
        Result.Error(_) -> println("bounded-read")
    end match

    let mut replacement: array<u8> = []
    replacement.push(42)
    replacement.push(42)
    replacement[1] = 7
    match ny::lang::io::write_bytes_atomic(path, replacement, true)
        Result.Error(_) -> return 39
        Result.Ok(written) ->
            if not written then return 40 end if
            println("replaced")
    end match

    match ny::lang::io::read_regular_bytes(path, 2)
        Result.Error(_) -> return 41
        Result.Ok(bytes) ->
            if bytes.len() != 2 then return 42 end if
            if bytes[0] != (42 as u8) then return 43 end if
            if bytes[1] != (7 as u8) then return 44 end if
            println("read-replacement")
    end match

    match ny::lang::io::write_bytes_atomic("", replacement, false)
        Result.Ok(_) -> return 45
        Result.Error(_) -> println("invalid-path")
    end match
    return 0
end fn
