module ny.tests.byte_io

import ny.lang.io.file
import ny.lang.system.std_sys

fn main() -> i32
    if ny::lang::system::arg_count() != 2 then
        println("expected input and output paths")
        return 64
    end if

    let input_path = ny::lang::system::arg(0)
    let output_path = ny::lang::system::arg(1)
    let loaded = ny::lang::io::read_bytes(input_path)
    match loaded
        Result.Ok(bytes) ->
            println(bytes.len())
            println(bytes[0])
            println(bytes[127])
            println(bytes[128])
            println(bytes[255])
            bytes[128] = 42
            println(bytes[128])
            bytes[128] = 128
            let written = ny::lang::io::write_bytes(output_path, bytes)
            match written
                Result.Ok(_) ->
                    let measured = ny::lang::io::size(output_path)
                    match measured
                        Result.Ok(byte_count) -> println(byte_count)
                        Result.Error(_) -> return 76
                    end match

                    let copy_path = output_path + ".copy"
                    let moved_path = output_path + ".moved"
                    let copied = ny::lang::io::copy(output_path, copy_path)
                    match copied
                        Result.Ok(_) -> println("copied")
                        Result.Error(_) -> return 77
                    end match
                    let moved = ny::lang::io::move(copy_path, moved_path)
                    match moved
                        Result.Ok(_) -> println("moved")
                        Result.Error(_) -> return 78
                    end match
                    if not ny::lang::io::exists(moved_path) then return 79 end if
                    let deleted = ny::lang::io::delete(moved_path)
                    match deleted
                        Result.Ok(_) -> println("deleted")
                        Result.Error(_) -> return 80
                    end match
                    if ny::lang::io::exists(moved_path) then return 81 end if

                    let missing = ny::lang::io::read_bytes(output_path + ".missing")
                    match missing
                        Result.Ok(_) -> return 75
                        Result.Error(_) ->
                            println("missing-file-error")
                            return 0
                    end match
                Result.Error(message) ->
                    println(message)
                    return 74
            end match
        Result.Error(message) ->
            println(message)
            return 66
    end match
end fn
