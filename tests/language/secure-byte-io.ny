module ny.tests.secure_byte_io

import ny.lang.io.file
import ny.lang.system.std_sys
import ny.lang.threading

fn main() -> i32
    let argument_count = ny::lang::system::arg_count()
    if argument_count == 4 then
        let race_input_path = ny::lang::system::arg(0)
        let race_output_path = ny::lang::system::arg(1)
        let race_ready_path = ny::lang::system::arg(2)
        let race_gate_path = ny::lang::system::arg(3)

        match ny::lang::io::read_regular_bytes(race_input_path, 1048576)
            Result.Error(message) ->
                println(message)
                return 90
            Result.Ok(bytes) ->
                match ny::lang::io::write_bytes_atomic(race_ready_path, [], false)
                    Result.Error(message) ->
                        println(message)
                        return 91
                    Result.Ok(ready) ->
                        if not ready then return 92 end if
                end match

                while not ny::lang::io::exists(race_gate_path)
                    ny::lang::threading::sleep_ms(1)
                end while

                match ny::lang::io::write_bytes_atomic(race_output_path, bytes, false)
                    Result.Error(message) -> println(message)
                    Result.Ok(written) ->
                        if not written then return 93 end if
                        println("race-winner")
                end match
        end match
        return 0
    end if

    if argument_count != 12 then
        println("expected twelve filesystem paths")
        return 64
    end if

    let input_path = ny::lang::system::arg(0)
    let output_path = ny::lang::system::arg(1)
    let existing_path = ny::lang::system::arg(2)
    let symlink_input_path = ny::lang::system::arg(3)
    let special_input_path = ny::lang::system::arg(4)
    let symlink_output_path = ny::lang::system::arg(5)
    let dynamic_output_path = ny::lang::system::arg(6)
    let empty_input_path = ny::lang::system::arg(7)
    let dynamic_size_input_path = ny::lang::system::arg(8)
    let replace_absent_path = ny::lang::system::arg(9)
    let directory_output_path = ny::lang::system::arg(10)
    let metadata_replace_path = ny::lang::system::arg(11)

    match ny::lang::io::read_regular_bytes(input_path, 256)
        Result.Error(message) ->
            println(message)
            return 65
        Result.Ok(bytes) ->
            if bytes.len() != 256 then return 66 end if
            if bytes[0] != (0 as u8) then return 67 end if
            if bytes[127] != (127 as u8) then return 68 end if
            if bytes[128] != (128 as u8) then return 69 end if
            if bytes[255] != (255 as u8) then return 70 end if
            println("read-exact")

            match ny::lang::io::write_bytes_atomic(output_path, bytes, false)
                Result.Error(message) ->
                    println(message)
                    return 71
                Result.Ok(written) ->
                    if not written then return 88 end if
                    println("write-new")
            end match

            match ny::lang::io::write_bytes_atomic(existing_path, bytes, false)
                Result.Ok(_) -> return 72
                Result.Error(message) -> println(message)
            end match

            bytes[0] = 42
            match ny::lang::io::write_bytes_atomic(output_path, bytes, true)
                Result.Error(message) ->
                    println(message)
                    return 73
                Result.Ok(written) ->
                    if not written then return 89 end if
                    println("write-replace")
            end match
    end match

    match ny::lang::io::read_regular_bytes(input_path, 255)
        Result.Ok(_) -> return 74
        Result.Error(message) -> println(message)
    end match
    match ny::lang::io::read_regular_bytes(empty_input_path, 0)
        Result.Error(message) ->
            println(message)
            return 86
        Result.Ok(bytes) ->
            if bytes.len() != 0 then return 87 end if
            println("empty-read")
    end match
    match ny::lang::io::read_regular_bytes(input_path, -1)
        Result.Ok(_) -> return 75
        Result.Error(message) -> println(message)
    end match
    match ny::lang::io::read_regular_bytes(input_path, 536870913)
        Result.Ok(_) -> return 76
        Result.Error(message) -> println(message)
    end match
    match ny::lang::io::read_regular_bytes(symlink_input_path, 256)
        Result.Ok(_) -> return 77
        Result.Error(_) -> println("symlink-read-rejected")
    end match
    match ny::lang::io::read_regular_bytes(special_input_path, 256)
        Result.Ok(_) -> return 78
        Result.Error(_) -> println("special-read-rejected")
    end match
    match ny::lang::io::read_regular_bytes(dynamic_size_input_path, 4096)
        Result.Ok(_) -> return 85
        Result.Error(_) -> println("dynamic-size-read-rejected")
    end match

    let invalid_path = "visible-secret\0hidden-secret"
    match ny::lang::io::read_regular_bytes(invalid_path, 256)
        Result.Ok(_) -> return 79
        Result.Error(message) -> println(message)
    end match
    match ny::lang::io::write_bytes_atomic(invalid_path, [], false)
        Result.Ok(_) -> return 80
        Result.Error(message) -> println(message)
    end match
    match ny::lang::io::write_bytes_atomic("", [], false)
        Result.Ok(_) -> return 81
        Result.Error(message) -> println(message)
    end match

    match ny::lang::io::write_bytes_atomic(symlink_output_path, [], false)
        Result.Ok(_) -> return 82
        Result.Error(_) -> println("symlink-output-no-clobber")
    end match
    match ny::lang::io::write_bytes_atomic(symlink_output_path, [], true)
        Result.Ok(_) -> return 83
        Result.Error(_) -> println("symlink-output-replace-rejected")
    end match

    match ny::lang::io::write_bytes_atomic(replace_absent_path, [], true)
        Result.Error(message) ->
            println(message)
            return 94
        Result.Ok(written) ->
            if not written then return 95 end if
            println("write-replace-absent")
    end match
    match ny::lang::io::write_bytes_atomic(directory_output_path, [], true)
        Result.Ok(_) -> return 96
        Result.Error(_) -> println("directory-output-rejected")
    end match
    match ny::lang::io::write_bytes_atomic(metadata_replace_path, [], true)
        Result.Error(message) ->
            println(message)
            return 97
        Result.Ok(written) ->
            if not written then return 98 end if
            println("write-metadata-replace")
    end match

    let mut generated: array<u8> = []
    generated.push(0)
    generated.push(128)
    generated.push(255)
    match ny::lang::io::write_bytes_atomic(dynamic_output_path, generated, false)
        Result.Error(message) ->
            println(message)
            return 84
        Result.Ok(written) ->
            if not written then return 99 end if
            println("dynamic-array-write")
    end match
    return 0
end fn
