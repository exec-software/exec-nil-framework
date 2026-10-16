module ny.tests.file_stream_runtime

import ny.lang.core.*
import ny.lang.io.*

fn main() -> i32
    let path = "ny-file-stream-runtime.bin"
    let copy_path = "ny-file-stream-runtime.copy.bin"
    let new_read_write_path = "ny-file-stream-runtime.new-rw.bin"
    let created = File.create(path)
    match created
        Result.Error(_) -> return 10
        Result.Ok(writer) ->
            let buffer = allocate<u8>(5)
            buffer[0] = 0
            buffer[1] = 127
            buffer[2] = 128
            buffer[3] = 255
            buffer[4] = 42

            match writer.write(buffer, -1)
                Result.Ok(_) -> return 12
                Result.Error(_) -> println("negative-length-error")
            end match
            match writer.write(buffer, 6)
                Result.Ok(_) -> return 13
                Result.Error(_) -> println("buffer-bounds-error")
            end match
            match writer.read(buffer, 1)
                Result.Ok(_) -> return 14
                Result.Error(_) -> println("write-only-error")
            end match
            match writer.write(buffer, 5)
                Result.Error(_) -> return 15
                Result.Ok(count) -> println(count)
            end match
            match writer.seek(2, 0)
                Result.Error(_) -> return 16
                Result.Ok(position) -> println(position)
            end match
            buffer[0] = 99
            match writer.write(buffer, 1)
                Result.Error(_) -> return 17
                Result.Ok(count) -> println(count)
            end match
            match writer.flush()
                Result.Error(_) -> return 18
                Result.Ok(_) -> println("flushed")
            end match
            match writer.seek(0, 9)
                Result.Ok(_) -> return 19
                Result.Error(_) -> println("invalid-origin-error")
            end match
            match writer.close()
                Result.Error(_) -> return 20
                Result.Ok(_) -> println("closed-writer")
            end match
            match writer.write(buffer, 1)
                Result.Ok(_) -> return 21
                Result.Error(_) -> println("closed-handle-error")
            end match
            deallocate(buffer)
    end match

    let opened = File.open(path)
    match opened
        Result.Error(_) -> return 21
        Result.Ok(reader) ->
            let buffer = allocate<u8>(5)
            match reader.flush()
                Result.Ok(_) -> return 23
                Result.Error(_) -> println("read-only-flush-error")
            end match
            match reader.write(buffer, 1)
                Result.Ok(_) -> return 24
                Result.Error(_) -> println("read-only-error")
            end match
            match reader.read(buffer, 5)
                Result.Error(_) -> return 25
                Result.Ok(count) -> println(count)
            end match
            println(buffer[0])
            println(buffer[1])
            println(buffer[2])
            println(buffer[3])
            println(buffer[4])
            match reader.read(buffer, 1)
                Result.Error(_) -> return 26
                Result.Ok(count) -> println(count)
            end match
            match reader.seek(-2, 2)
                Result.Error(_) -> return 27
                Result.Ok(position) -> println(position)
            end match
            match reader.read(buffer, 5)
                Result.Error(_) -> return 26
                Result.Ok(count) -> println(count)
            end match
            println(buffer[0])
            println(buffer[1])
            match reader.seek(-1, 0)
                Result.Ok(_) -> return 28
                Result.Error(_) -> println("negative-seek-error")
            end match
            match reader.close()
                Result.Error(_) -> return 29
                Result.Ok(_) -> println("closed-reader")
            end match
            match reader.close()
                Result.Error(_) -> return 30
                Result.Ok(_) -> println("idempotent-close")
            end match
            deallocate(buffer)
    end match

    let source = ny::lang::io::open_file(path)
    let destination = ny::lang::io::create_file(copy_path)
    match source
        Result.Error(_) -> return 30
        Result.Ok(reader) ->
            match destination
                Result.Error(_) -> return 31
                Result.Ok(writer) ->
                    match ny::lang::io::stream_copy(reader, writer)
                        Result.Error(_) -> return 32
                        Result.Ok(count) -> println(count)
                    end match
                    match reader.close()
                        Result.Error(_) -> return 33
                        Result.Ok(_) -> println("copy-reader-closed")
                    end match
                    match writer.close()
                        Result.Error(_) -> return 34
                        Result.Ok(_) -> println("copy-writer-closed")
                    end match
            end match
    end match

    let appended = File.append(copy_path)
    match appended
        Result.Error(_) -> return 35
        Result.Ok(writer) ->
            let buffer = allocate<u8>(1)
            buffer[0] = 77
            match writer.seek(0, 0)
                Result.Error(_) -> return 36
                Result.Ok(position) -> println(position)
            end match
            match writer.write(buffer, 1)
                Result.Error(_) -> return 37
                Result.Ok(count) -> println(count)
            end match
            match writer.close()
                Result.Error(_) -> return 38
                Result.Ok(_) -> println("append-closed")
            end match
            deallocate(buffer)
    end match

    let read_write = File.open_read_write(copy_path)
    match read_write
        Result.Error(_) -> return 39
        Result.Ok(file) ->
            let buffer = allocate<u8>(6)
            match file.read(buffer, 6)
                Result.Error(_) -> return 40
                Result.Ok(count) -> println(count)
            end match
            println(buffer[5])
            buffer[0] = 11
            match file.seek(0, 0)
                Result.Error(_) -> return 41
                Result.Ok(position) -> println(position)
            end match
            match file.write(buffer, 1)
                Result.Error(_) -> return 42
                Result.Ok(count) -> println(count)
            end match
            match file.seek(0, 0)
                Result.Error(_) -> return 43
                Result.Ok(_) -> println("rewound")
            end match
            buffer[0] = 0
            match file.read(buffer, 1)
                Result.Error(_) -> return 44
                Result.Ok(count) -> println(count)
            end match
            println(buffer[0])
            match file.close()
                Result.Error(_) -> return 45
                Result.Ok(_) -> println("read-write-closed")
            end match
            deallocate(buffer)
    end match

    let new_read_write = ny::lang::io::open_read_write_file(new_read_write_path)
    match new_read_write
        Result.Error(_) -> return 48
        Result.Ok(file) ->
            let buffer = allocate<u8>(1)
            buffer[0] = 201
            match file.write(buffer, 1)
                Result.Error(_) -> return 49
                Result.Ok(count) -> println(count)
            end match
            match file.seek(0, 0)
                Result.Error(_) -> return 50
                Result.Ok(_) -> println("new-read-write-rewound")
            end match
            buffer[0] = 0
            match file.read(buffer, 1)
                Result.Error(_) -> return 51
                Result.Ok(count) -> println(count)
            end match
            println(buffer[0])
            match file.close()
                Result.Error(_) -> return 52
                Result.Ok(_) -> println("new-read-write-closed")
            end match
            deallocate(buffer)
    end match

    match ny::lang::io::delete(path)
        Result.Error(_) -> return 46
        Result.Ok(_) -> println("deleted-source")
    end match
    match ny::lang::io::delete(copy_path)
        Result.Error(_) -> return 47
        Result.Ok(_) -> println("deleted")
    end match
    match ny::lang::io::delete(new_read_write_path)
        Result.Error(_) -> return 53
        Result.Ok(_) -> println("deleted-new-read-write")
    end match
    return 0
end fn
