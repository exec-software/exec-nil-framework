# Public ordinary-I/O regression. No raw intrinsic calls.
# Modes are independent processes so a lexical failure cannot hide files/env.
# Drive-relative, UNC, reserved-device, malformed-UTF8, and containment policies
# are deliberately outside this ordinary valid-path probe.
module nil.tests.ordinary_io_parity

import nil.std.core
import nil.std.collections
import nil.std.io
import nil.std.system

fn observe(label: text, actual: text, expected: text) -> bool
    println(label + "=" + actual)
    return actual == expected
end fn

fn observe_bool(label: text, actual: bool) -> bool
    if actual then
        println(label + "=true")
    else
        println(label + "=false")
    end if
    return actual
end fn

fn lexical() -> i32
    let mut ok = true
    ok = observe("lexical.separator", nil::std::io::separator(), "/") and ok
    ok = observe("lexical.combine.slash", nil::std::io::combine("alpha", "beta/file.ny"), "alpha/beta/file.ny") and ok
    ok = observe("lexical.combine.backslash", nil::std::io::combine("alpha\\beta", "file.ny"), "alpha/beta/file.ny") and ok
    ok = observe("lexical.normalize.slash", nil::std::io::normalize("alpha/./beta/../gamma"), "alpha/gamma") and ok
    ok = observe("lexical.normalize.backslash", nil::std::io::normalize("alpha\\beta\\..\\gamma"), "alpha/gamma") and ok
    ok = observe("lexical.normalize.mixed", nil::std::io::normalize("alpha\\beta/../gamma"), "alpha/gamma") and ok
    ok = observe("lexical.basename.backslash", nil::std::io::basename("alpha\\beta\\file.ny"), "file.ny") and ok
    ok = observe("lexical.dirname.backslash", nil::std::io::dirname("alpha\\beta\\file.ny"), "alpha/beta") and ok
    ok = observe("lexical.stem.backslash", nil::std::io::stem("alpha\\beta\\file.ny"), "file") and ok
    ok = observe("lexical.extension.backslash", nil::std::io::extension("alpha\\beta\\file.ny"), ".ny") and ok
    ok = observe("lexical.relative.backslash", nil::std::io::relative("alpha\\one", "alpha\\two\\file.ny"), "../two/file.ny") and ok
    let mut empty: vec<text> = vec<text>.new()
    ok = observe("lexical.join.empty", nil::std::io::join(empty), "") and ok
    let mut one: vec<text> = vec<text>.new()
    one.push("alpha\\beta")
    ok = observe("lexical.join.one", nil::std::io::join(one), "alpha/beta") and ok
    let mut two: vec<text> = vec<text>.new()
    two.push("alpha\\beta")
    two.push("file.ny")
    ok = observe("lexical.join.two", nil::std::io::join(two), "alpha/beta/file.ny") and ok
    ok = observe("lexical.dotfile.stem", nil::std::io::stem("alpha/.config"), ".config") and ok
    ok = observe("lexical.dotfile.extension", nil::std::io::extension("alpha/.config"), "") and ok
    ok = observe("lexical.multiext.stem", nil::std::io::stem("alpha/archive.tar.gz"), "archive.tar") and ok
    ok = observe("lexical.multiext.extension", nil::std::io::extension_no_dot("alpha/archive.tar.gz"), "gz") and ok
    ok = observe("lexical.with_extension.mixed", nil::std::io::with_extension("alpha\\beta/file.ny", "txt"), "alpha/beta/file.txt") and ok
    ok = observe("lexical.prefix.mixed", nil::std::io::with_prefix("alpha\\beta/file.ny", "pre-"), "alpha/beta/pre-file.ny") and ok
    ok = observe("lexical.suffix.mixed", nil::std::io::with_suffix("alpha/beta\\file.ny", ".bak"), "alpha/beta/file.ny.bak") and ok
    ok = observe("lexical.unicode.basename", nil::std::io::basename("café/日/😀.txt"), "😀.txt") and ok
    ok = observe("lexical.unicode.dirname", nil::std::io::dirname("café/日/😀.txt"), "café/日") and ok
    ok = observe("lexical.unicode.normalize", nil::std::io::normalize("café/./日/../😀.txt"), "café/😀.txt") and ok
    ok = observe("lexical.unicode.nfd", nil::std::io::basename("café/é.txt"), "é.txt") and ok
    observe_bool("lexical.ok", ok)
    if not ok then return 1 end if
    return 0
end fn

fn equal_bytes(actual: array<u8>, expected: array<u8>) -> bool
    if actual.len() != expected.len() then return false end if
    let mut i: i64 = 0
    while i < expected.len()
        if actual[i] != expected[i] then return false end if
        i = i + 1
    end while
    return true
end fn

# All handles and allocations are explicitly closed even when an operation
# reports an error. The host independently inspects final names and raw bytes.
fn stream_case(path: text, expected: array<u8>) -> i32
    let buffer = allocate<u8>(5)
    let mut i: i64 = 0
    while i < 5
        buffer[i] = expected[i]
        i = i + 1
    end while
    let mut code: i32 = 0
    match nil::std::io::File.create(path)
        Result.Error(_) -> code = 51
        Result.Ok(writer) ->
            match writer.write(buffer, 5)
                Result.Error(_) -> code = 52
                Result.Ok(count) -> if count != 5 then code = 52 end if
            end match
            if code == 0 then
                match writer.flush()
                    Result.Error(_) -> code = 53
                    Result.Ok(flushed) -> if not flushed then code = 53 end if
                end match
            end if
            if code == 0 then
                match writer.seek(0, 0)
                    Result.Error(_) -> code = 54
                    Result.Ok(position) -> if position != 0 then code = 54 end if
                end match
            end if
            match writer.close()
                Result.Error(_) -> if code == 0 then code = 55 end if
                Result.Ok(closed) -> if not closed and code == 0 then code = 55 end if
            end match
    end match
    if code == 0 then
        match nil::std::io::File.open(path)
            Result.Error(_) -> code = 56
            Result.Ok(reader) ->
                match reader.read(buffer, 5)
                    Result.Error(_) -> code = 57
                    Result.Ok(count) -> if count != 5 then code = 57 end if
                end match
                if code == 0 then
                    i = 0
                    while i < 5
                        if buffer[i] != expected[i] then code = 58 end if
                        i = i + 1
                    end while
                end if
                if code == 0 then
                    match reader.read(buffer, 1)
                        Result.Error(_) -> code = 59
                        Result.Ok(count) -> if count != 0 then code = 59 end if
                    end match
                end if
                if code == 0 then
                    match reader.seek(-1, 2)
                        Result.Error(_) -> code = 60
                        Result.Ok(position) -> if position != 4 then code = 60 end if
                    end match
                end if
                match reader.close()
                    Result.Error(_) -> if code == 0 then code = 61 end if
                    Result.Ok(closed) -> if not closed and code == 0 then code = 61 end if
                end match
        end match
    end if
    deallocate(buffer)
    return code
end fn

fn file_case(index: i64, seed: text, stem: text) -> i32
    let expected: array<u8> = [index as u8, 10 as u8, 13 as u8, 128 as u8, 255 as u8]
    match nil::std::io::read_bytes(seed)
        Result.Error(_) -> return 10
        Result.Ok(bytes) -> if not equal_bytes(bytes, expected) then return 11 end if
    end match
    if not nil::std::io::exists(seed) then return 12 end if
    let text_path = stem + ".text"
    let byte_path = stem + ".bytes"
    let copy_path = stem + ".copy"
    let move_path = stem + ".moved"
    let stream_path = stem + ".stream"
    let expected_text = "café日😀\r\ntail\n"
    match nil::std::io::write_text(text_path, "café日😀\r\n")
        Result.Error(_) -> return 20
        Result.Ok(written) -> if not written then return 20 end if
    end match
    match nil::std::io::append_text(text_path, "tail\n")
        Result.Error(_) -> return 21
        Result.Ok(written) -> if not written then return 21 end if
    end match
    match nil::std::io::read_text(text_path)
        Result.Error(_) -> return 22
        Result.Ok(value) -> if value != expected_text then return 22 end if
    end match
    match nil::std::io::size(text_path)
        Result.Error(_) -> return 23
        Result.Ok(count) -> if count != expected_text.len() then return 23 end if
    end match
    match nil::std::io::write_bytes(byte_path, expected)
        Result.Error(_) -> return 30
        Result.Ok(written) -> if not written then return 30 end if
    end match
    if not nil::std::io::exists(byte_path) then return 31 end if
    match nil::std::io::size(byte_path)
        Result.Error(_) -> return 32
        Result.Ok(count) -> if count != 5 then return 32 end if
    end match
    match nil::std::io::copy(byte_path, copy_path)
        Result.Error(_) -> return 40
        Result.Ok(copied) -> if not copied then return 40 end if
    end match
    match nil::std::io::move(copy_path, move_path)
        Result.Error(_) -> return 41
        Result.Ok(moved) -> if not moved then return 41 end if
    end match
    if nil::std::io::exists(copy_path) then return 42 end if
    match nil::std::io::read_bytes(move_path)
        Result.Error(_) -> return 43
        Result.Ok(bytes) -> if not equal_bytes(bytes, expected) then return 43 end if
    end match
    match nil::std::io::copy(move_path, copy_path)
        Result.Error(_) -> return 44
        Result.Ok(copied) -> if not copied then return 44 end if
    end match
    match nil::std::io::delete(copy_path)
        Result.Error(_) -> return 45
        Result.Ok(deleted) -> if not deleted then return 45 end if
    end match
    match nil::std::io::delete(copy_path)
        Result.Error(_) -> return 46
        Result.Ok(deleted) -> if deleted then return 46 end if
    end match
    return stream_case(stream_path, expected)
end fn

fn files() -> i32
    if nil::std::system::arg_count() != 13 then return 64 end if
    let mut index: i64 = 0
    let mut ok = true
    while index < 6
        let code = file_case(index, nil::std::system::arg(index * 2 + 1), nil::std::system::arg(index * 2 + 2))
        println("files." + Convert.i64_to_text(index) + ".code=" + Convert.i32_to_text(code))
        if code != 0 then ok = false end if
        index = index + 1
    end while
    observe_bool("files.ok", ok)
    if not ok then return 2 end if
    return 0
end fn

fn environment_value_is(name: text, expected: text) -> bool
    match nil::std::system::get_environment_variable(name)
        Option.None -> return false
        Option.Some(value) -> return value == expected
    end match
    return false
end fn

fn environment_entry_is(name: text, expected: text) -> bool
    let entries = nil::std::system::get_environment_variables()
    let mut count = 0
    let mut correct = true
    let mut i: i64 = 0
    while i < entries.length()
        if entries[i].name == name then
            count = count + 1
            if entries[i].value != expected then correct = false end if
        end if
        i = i + 1
    end while
    return count == 1 and correct
end fn

fn environment() -> i32
    if nil::std::system::arg_count() != 4 then return 64 end if
    let target = nil::std::system::arg(1)
    let expected_cwd = nil::std::system::arg(2)
    let name = nil::std::system::arg(3)
    let mut ok = true
    ok = observe_bool("environment.host_get", environment_value_is(name, "host-café日😀")) and ok
    ok = observe_bool("environment.host_list", environment_entry_is(name, "host-café日😀")) and ok
    ok = observe_bool("environment.set", nil::std::system::set_environment_variable(name, "ny-café日😀").is_ok()) and ok
    ok = observe_bool("environment.get", environment_value_is(name, "ny-café日😀")) and ok
    ok = observe_bool("environment.list", environment_entry_is(name, "ny-café日😀")) and ok
    ok = observe_bool("environment.empty_set", nil::std::system::set_environment_variable(name, "").is_ok()) and ok
    ok = observe_bool("environment.empty_get", environment_value_is(name, "")) and ok
    ok = observe_bool("environment.empty_list", environment_entry_is(name, "")) and ok
    let previous_cwd = nil::std::system::get_current_directory()
    ok = observe_bool("environment.cwd_set", nil::std::system::set_current_directory(target).is_ok()) and ok
    ok = observe_bool("environment.cwd_get", nil::std::system::get_current_directory() == expected_cwd) and ok
    ok = observe_bool("environment.cwd_restore", nil::std::system::set_current_directory(previous_cwd).is_ok()) and ok
    ok = observe_bool("environment.cwd_restored", nil::std::system::get_current_directory() == previous_cwd) and ok
    ok = observe_bool("environment.unset", nil::std::system::unset_environment_variable(name).is_ok()) and ok
    ok = observe_bool("environment.absent", nil::std::system::get_environment_variable(name).is_none()) and ok
    observe_bool("environment.ok", ok)
    if not ok then return 3 end if
    return 0
end fn

fn main() -> i32
    if nil::std::system::arg_count() < 1 then return 64 end if
    let mode = nil::std::system::arg(0)
    if mode == "lexical" then
        if nil::std::system::arg_count() != 1 then return 64 end if
        return lexical()
    end if
    if mode == "files" then return files() end if
    if mode == "environment" then return environment() end if
    return 64
end fn
