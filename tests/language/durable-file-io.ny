# Shared public durability and lock contract, paired with the Matrix fixture.
module nil.tests.durable_file_io
import nil.std.core
import nil.std.io
import nil.std.system
import nil.std.text

fn main() -> i32
    if nil::std::system::arg_count() != 1 then return 64 end if
    if nil::std::text::byte_at("é", 0) != 195 then return 20 end if
    if nil::std::text::byte_at("é", 1) != 169 then return 21 end if
    if nil::std::text::byte_at("é", -1) != -1 then return 22 end if
    if nil::std::text::byte_at("é", 2) != -1 then return 23 end if
    if nil::std::text::from_byte(195) + nil::std::text::from_byte(169) != "é" then return 24 end if
    if nil::std::text::from_byte(-1) != "" or nil::std::text::from_byte(256) != "" then return 25 end if
    let root = nil::std::system::arg(0)
    let output = root + "/checkpoint.bin"
    let lock = root + "/writer.lock"
    let first: array<u8> = [0 as u8, 10 as u8, 13 as u8, 128 as u8, 255 as u8]
    let second: array<u8> = [255 as u8, 0 as u8, 1 as u8]
    let empty: array<u8> = []
    if not nil::std::io::write_bytes_durable(output, first, false).is_ok() then return 1 end if
    if nil::std::io::write_bytes_durable(output, second, false).is_ok() then return 2 end if
    match nil::std::io::read_bytes(output)
        Result.Error(_) -> return 3
        Result.Ok(bytes) ->
            if bytes.len() != 5 or bytes[0] != 0 as u8 or bytes[4] != 255 as u8 then return 4 end if
    end match
    if not nil::std::io::write_bytes_durable(output, second, true).is_ok() then return 5 end if
    if not nil::std::io::write_bytes_durable(root + "/empty.bin", empty, false).is_ok() then return 6 end if
    if not nil::std::io::sync_directory(root).is_ok() then return 7 end if
    if nil::std::io::sync_directory(output).is_ok() then return 8 end if
    println("durable-write-ok")

    let acquired = nil::std::io::acquire_file_lock(lock, true)
    if acquired.is_error() then return 10 end if
    let token = acquired.unwrap()
    if token <= 0 then return 11 end if
    if not nil::std::io::file_lock_is_held(token).unwrap() then return 31 end if
    if nil::std::io::file_lock_is_held(0).unwrap() or
       nil::std::io::file_lock_is_held(-1).unwrap() then return 32 end if
    if nil::std::io::acquire_file_lock(lock, false).is_ok() then return 12 end if
    if nil::std::io::release_file_lock(0).is_ok() then return 13 end if
    if not nil::std::io::release_file_lock(token).is_ok() then return 14 end if
    if nil::std::io::release_file_lock(token).is_ok() then return 15 end if
    if nil::std::io::file_lock_is_held(token).unwrap() then return 33 end if
    let again = nil::std::io::acquire_file_lock(lock, false)
    if again.is_error() then return 16 end if
    if again.unwrap() <= token then return 17 end if
    if not nil::std::io::file_lock_is_held(again.unwrap()).unwrap() then return 34 end if
    if nil::std::io::file_lock_is_held(token).unwrap() then return 35 end if
    # Deliberately retain this lock until invocation completion. The host oracle
    # independently acquires the backing file after every execution route.
    if not nil::std::io::sync_directory(root).is_ok() then return 18 end if
    println("locks-ok")

    # RFC 6234 vectors, so a wrong digest fails rather than merely staying stable.
    match nil::std::io::sha256_hex(empty)
        Result.Error(_) -> return 20
        Result.Ok(digest) ->
            if digest != "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" then return 21 end if
    end match
    let abc: array<u8> = [97 as u8, 98 as u8, 99 as u8]
    match nil::std::io::sha256_hex(abc)
        Result.Error(_) -> return 22
        Result.Ok(digest) ->
            if digest != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" then return 23 end if
    end match

    # The harness creates the working directory owner-only.
    match nil::std::io::is_private_directory(root)
        Result.Error(_) -> return 24
        Result.Ok(private) -> if not private then return 25 end if
    end match
    let nested = root + "/private"
    if not nil::std::io::create_private_directory(nested).is_ok() then return 26 end if
    # An existing entry is reported, never adopted.
    if nil::std::io::create_private_directory(nested).is_ok() then return 27 end if
    match nil::std::io::is_private_directory(nested)
        Result.Error(_) -> return 28
        Result.Ok(private) -> if not private then return 29 end if
    end match
    # A 0600 published file is a private regular file; a directory is not, and
    # that is a false verdict rather than an error.
    match nil::std::io::is_private_regular_file(output)
        Result.Error(_) -> return 30
        Result.Ok(private) -> if not private then return 31 end if
    end match
    match nil::std::io::is_private_regular_file(nested)
        Result.Error(_) -> return 32
        Result.Ok(private) -> if private then return 33 end if
    end match
    # An absent entry cannot be inspected, which is distinct from either verdict.
    if nil::std::io::is_private_regular_file(nested + "/absent").is_ok() then return 34 end if
    if nil::std::io::is_private_directory(nested + "/absent").is_ok() then return 35 end if
    if not nil::std::io::sync_directory(nested).is_ok() then return 36 end if
    println("metadata-ok")
    return 0
end fn
