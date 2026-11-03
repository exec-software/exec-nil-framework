# Portable public scalar-I/O contract. Named-struct environment enumeration is
# intentionally separate until the native aggregate-array ABI is implemented.
module nil.tests.ordinary_io_native
import nil.std.core
import nil.std.io
import nil.std.system

fn environment_value_is(name: text, expected: text) -> bool
    match nil::std::system::get_environment_variable(name)
        Option.None -> return false
        Option.Some(value) -> return value == expected
    end match
    return false
end fn

fn main() -> i32
    if nil::std::system::arg_count() != 4 then return 64 end if
    let seed = nil::std::system::arg(0)
    let target = nil::std::system::arg(1)
    let cwd = nil::std::system::arg(2)
    let name = nil::std::system::arg(3)
    if nil::std::io::combine("alpha\\beta", "file.ny") != "alpha/beta/file.ny" then return 1 end if
    if nil::std::io::basename("café\\日本語\\🦊.ny") != "🦊.ny" then return 2 end if
    if nil::std::io::dirname("café\\日本語\\🦊.ny") != "café/日本語" then return 3 end if
    if nil::std::io::normalize("alpha\\beta/../gamma") != "alpha/gamma" then return 4 end if
    if nil::std::io::relative("alpha\\one", "alpha\\two\\file.ny") != "../two/file.ny" then return 5 end if
    if nil::std::io::ends_with("a\\file.ny", "/file.ny") then return 6 end if
    println("paths-ok")

    if not nil::std::io::exists(seed) then return 10 end if
    match nil::std::io::read_bytes(seed)
        Result.Error(_) -> return 11
        Result.Ok(bytes) ->
            if bytes.len() != 5 or bytes[0] != 0 as u8 or bytes[4] != 255 as u8 then return 12 end if
            if not nil::std::io::write_bytes(target + ".bytes", bytes).is_ok() then return 13 end if
    end match
    if not nil::std::io::write_text(target + ".text", "café日本語🦊\r\n").is_ok() then return 14 end if
    if not nil::std::io::append_text(target + ".text", "tail\n").is_ok() then return 15 end if
    match nil::std::io::read_text(target + ".text")
        Result.Error(_) -> return 16
        Result.Ok(value) -> if value != "café日本語🦊\r\ntail\n" then return 17 end if
    end match
    match nil::std::io::size(target + ".bytes")
        Result.Error(_) -> return 18
        Result.Ok(size) -> if size != 5 then return 19 end if
    end match
    if not nil::std::io::copy(seed, target + ".copy").is_ok() then return 20 end if
    if not nil::std::io::move(target + ".copy", target + ".moved").is_ok() then return 21 end if
    if nil::std::io::exists(target + ".copy") then return 22 end if
    if not nil::std::io::delete(target + ".moved").is_ok() then return 23 end if
    match nil::std::io::delete(target + ".moved")
        Result.Error(_) -> return 24
        Result.Ok(removed) -> if removed then return 25 end if
    end match
    if nil::std::io::read_bytes(target + ".missing").is_ok() then return 26 end if
    println("files-ok")

    let bytes = allocate<u8>(5)
    match nil::std::io::File.open(seed)
        Result.Error(_) -> return 30
        Result.Ok(reader) ->
            match reader.read(bytes, 5)
                Result.Error(_) -> return 31
                Result.Ok(size) -> if size != 5 then return 32 end if
            end match
            if bytes[0] != 0 as u8 or bytes[4] != 255 as u8 then return 33 end if
            match reader.read(bytes, 1)
                Result.Error(_) -> return 34
                Result.Ok(size) -> if size != 0 then return 35 end if
            end match
            match reader.seek(-1, 2)
                Result.Error(_) -> return 36
                Result.Ok(position) -> if position != 4 then return 37 end if
            end match
            if not reader.close().is_ok() then return 38 end if
    end match
    match nil::std::io::File.create(target + ".stream")
        Result.Error(_) -> return 40
        Result.Ok(writer) ->
            match writer.write(bytes, 5)
                Result.Error(_) -> return 41
                Result.Ok(size) -> if size != 5 then return 42 end if
            end match
            if not writer.flush().is_ok() then return 43 end if
            if not writer.close().is_ok() then return 44 end if
    end match
    deallocate(bytes)
    println("streams-ok")

    if not environment_value_is(name, "host-café日本語🦊") then return 50 end if
    if not nil::std::system::set_environment_variable(name, "ny-café日本語🦊").is_ok() then return 51 end if
    if not environment_value_is(name, "ny-café日本語🦊") then return 52 end if
    if not nil::std::system::set_environment_variable(name, "").is_ok() then return 53 end if
    if not environment_value_is(name, "") then return 54 end if
    if not nil::std::system::unset_environment_variable(name).is_ok() then return 55 end if
    if not nil::std::system::get_environment_variable(name).is_none() then return 56 end if
    if nil::std::system::set_environment_variable("bad=name", "x").is_ok() then return 57 end if
    let before = nil::std::system::get_current_directory()
    if not nil::std::system::set_current_directory(cwd).is_ok() then return 58 end if
    if nil::std::system::get_current_directory() != cwd then return 59 end if
    if not nil::std::system::set_current_directory(before).is_ok() then return 60 end if
    if nil::std::system::get_current_directory() != before then return 61 end if
    if nil::std::system::get_machine_name().len() == 0 then return 62 end if
    if nil::std::system::get_user_name().len() == 0 then return 63 end if
    let newline = nil::std::system::newline()
    if newline != "\n" and newline != "\r\n" then return 65 end if
    println("environment-ok")
    return 0
end fn
