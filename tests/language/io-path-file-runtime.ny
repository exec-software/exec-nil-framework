module ny.tests.io_path_file_runtime

import ny.lang.core.*
import ny.lang.io.*

fn main() -> i32
    let path = "ny-io-path-file-runtime.txt"
    let copied_path = "ny-io-path-file-runtime.copy.txt"
    let moved_path = "ny-io-path-file-runtime.moved.txt"

    ny::lang::io::write_error("io-stderr\n")
    println(ny::lang::io::basename("/tmp/archive.tar.gz"))
    println(ny::lang::io::dirname("/tmp/archive.tar.gz"))
    println(ny::lang::io::stem("/tmp/archive.tar.gz"))
    println(ny::lang::io::extension("/tmp/archive.tar.gz"))
    println(ny::lang::io::extension_no_dot("/tmp/archive.tar.gz"))
    println(ny::lang::io::with_extension("/tmp/archive.tar.gz", "ny"))
    println(ny::lang::io::with_prefix("/tmp/file.ny", "pre-"))
    println(ny::lang::io::with_suffix("/tmp/file.ny", ".bak"))
    println(ny::lang::io::normalize("alpha/./beta/../gamma"))
    println(ny::lang::io::relative("/tmp/one", "/tmp/two/file"))
    let mut parts: vec<text> = vec<text>.new()
    parts.push("/tmp")
    parts.push("ny")
    parts.push("file")
    println(ny::lang::io::join(parts))
    if not ny::lang::io::has_extension("file.ny") then return 9 end if
    if ny::lang::io::has_extension("README") then return 9 end if
    if ny::lang::io::ends_with("/tmp/archive.tar.gz", ".tar.gz") then
        println("suffix-match")
    else
        return 10
    end if
    if ny::lang::io::ends_with("file.ny", "") then
        println("empty-suffix-match")
    else
        return 11
    end if
    if ny::lang::io::ends_with("file.ny", ".cpp") then return 12 end if

    match ny::lang::io::write_text(path, "alpha")
        Result.Error(_) -> return 13
        Result.Ok(_) -> println("wrote")
    end match
    if not ny::lang::io::exists(path) then return 14 end if
    match ny::lang::io::append_text(path, "-beta")
        Result.Error(_) -> return 15
        Result.Ok(_) -> println("appended")
    end match
    match ny::lang::io::read_text(path)
        Result.Error(_) -> return 16
        Result.Ok(contents) -> println(contents)
    end match
    match ny::lang::io::size(path)
        Result.Error(_) -> return 17
        Result.Ok(byte_count) -> println(byte_count)
    end match
    match ny::lang::io::read_bytes(path)
        Result.Error(_) -> return 18
        Result.Ok(bytes) ->
            println(bytes.len())
            println(bytes[0])
            println(bytes[9])
    end match

    match ny::lang::io::copy(path, copied_path)
        Result.Error(_) -> return 19
        Result.Ok(_) -> println("copied")
    end match
    match ny::lang::io::move(copied_path, moved_path)
        Result.Error(_) -> return 20
        Result.Ok(_) -> println("moved")
    end match
    if ny::lang::io::exists(copied_path) then return 21 end if
    if not ny::lang::io::exists(moved_path) then return 22 end if

    match ny::lang::io::read_text(path + ".missing")
        Result.Ok(_) -> return 23
        Result.Error(_) -> println("read-error")
    end match
    match ny::lang::io::size(path + ".missing")
        Result.Ok(_) -> return 24
        Result.Error(_) -> println("size-error")
    end match
    match ny::lang::io::open_file(path + ".missing")
        Result.Ok(_) -> return 25
        Result.Error(_) -> println("open-error")
    end match

    match ny::lang::io::delete(path)
        Result.Error(_) -> return 26
        Result.Ok(_) -> println("deleted-source")
    end match
    match ny::lang::io::delete(moved_path)
        Result.Error(_) -> return 27
        Result.Ok(_) -> println("deleted-moved")
    end match
    match ny::lang::io::delete(moved_path)
        Result.Error(_) -> return 28
        Result.Ok(removed) ->
            if removed then return 29 end if
            println("missing-delete-false")
    end match
    return 0
end fn
