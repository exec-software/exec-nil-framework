module nil.tests.byte_file_unicode_paths

import nil.std.io.file
import nil.std.system.std_sys
import nil.std.text

fn reject_malformed_path(path: text) -> bool
    match nil::std::io::read_regular_bytes(path, 256)
        Result.Ok(_) -> return false
        Result.Error(message) ->
            if message != "file.read_regular_bytes: invalid path" then return false end if
    end match
    match nil::std::io::write_bytes_atomic(path, [], false)
        Result.Ok(_) -> return false
        Result.Error(message) ->
            if message != "file.write_bytes_atomic: invalid destination path" then return false end if
    end match
    match nil::std::io::write_bytes_atomic(path, [], true)
        Result.Ok(_) -> return false
        Result.Error(message) ->
            if message != "file.write_bytes_atomic: invalid destination path" then return false end if
    end match
    return true
end fn

# The host supplies independently created Unicode filenames and all-byte
# payloads. Never construct the expected filename by asking Ny to round-trip it.
fn main() -> i32
    if nil::std::system::arg_count() != 32 then return 64 end if
    let mut item: i64 = 0
    while item < 8
        let input = nil::std::system::arg(item * 4)
        let created = nil::std::system::arg(item * 4 + 1)
        let protected = nil::std::system::arg(item * 4 + 2)
        let replaced = nil::std::system::arg(item * 4 + 3)
        match nil::std::io::read_regular_bytes(input, 256)
            Result.Error(message) ->
                println(message)
                return 65
            Result.Ok(bytes) ->
                if bytes.len() != 256 then return 66 end if
                let mut offset: i64 = 0
                while offset < 256
                    if bytes[offset] != (((offset + item * 17) % 256) as u8) then return 67 end if
                    offset = offset + 1
                end while
                match nil::std::io::write_bytes_atomic(created, bytes, false)
                    Result.Error(message) ->
                        println(message)
                        return 68
                    Result.Ok(written) ->
                        if not written then return 69 end if
                end match
                match nil::std::io::write_bytes_atomic(protected, bytes, false)
                    Result.Ok(_) -> return 70
                    Result.Error(message) ->
                        if message != "file.write_bytes_atomic: destination already exists" then return 73 end if
                        println("no-clobber")
                end match
                bytes[0] = 42 as u8
                bytes[255] = 7 as u8
                match nil::std::io::write_bytes_atomic(replaced, bytes, true)
                    Result.Error(message) ->
                        println(message)
                        return 71
                    Result.Ok(written) ->
                        if not written then return 72 end if
                end match
        end match
        item = item + 1
    end while
    # Text is byte-indexed: all literals below are valid Unicode, while their
    # checked slices deliberately form invalid UTF-8. No malformed source or
    # argv decoding is involved, and raw invalid bytes are never printed.
    let continuation = try_substring("é", 1, 1).unwrap()
    let truncated_two = try_substring("é", 0, 1).unwrap()
    let truncated_three = try_substring("日", 0, 2).unwrap()
    let e0 = try_substring("ࠀ", 0, 1).unwrap()
    let ed = try_substring("퟿", 0, 1).unwrap()
    let f4 = try_substring("􀀀", 0, 1).unwrap()
    let a0 = try_substring("ࠀ", 1, 1).unwrap()
    let af = try_substring("¯", 1, 1).unwrap()
    let continuation_80 = try_substring("", 1, 1).unwrap()
    let continuation_90 = try_substring("𐀀", 1, 1).unwrap()
    let malformed: array<text> = [continuation, truncated_two, truncated_three,
        e0 + continuation_80 + af, ed + a0 + continuation_80,
        f4 + continuation_90 + continuation_80 + continuation_80]
    for bad in malformed
        if not reject_malformed_path(nil::std::system::arg(0) + "-invalid-" + bad) then return 74 end if
        if not reject_malformed_path(nil::std::system::arg(16) + "-invalid-" + bad) then return 75 end if
    end for
    println("malformed-paths-rejected")
    println("byte-file-unicode-paths-ok")
    return 0
end fn
