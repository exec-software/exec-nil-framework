module nil.tests.rooted_file_snapshot

import nil.std.io.file
import nil.std.system.std_sys
import nil.std.text

fn inspect(snapshot: nil::std::io::FileSnapshot, stamp: i64, digest: text, seed: i64) -> bool
    let boxed: anything = snapshot
    if not anything_is<nil::std::io::FileSnapshot>(boxed) then return false end if
    let recovered = boxed as nil::std::io::FileSnapshot
    let boxed_content: anything = recovered.content
    if not anything_is<array<u8>>(boxed_content) or anything_is<array<i64>>(boxed_content) then return false end if
    let recovered_content = boxed_content as array<u8>
    if recovered.content.len() != 256 then return false end if
    if recovered.modified_ms != stamp then return false end if
    if recovered.sha256 != digest then return false end if
    let mut index: i64 = 0
    while index < 256
        if recovered_content[index] != (((index * 73 + seed) % 256) as u8) then return false end if
        index = index + 1
    end while
    return true
end fn

fn reject(root: text, relative: text, maximum: i64, expected: text) -> bool
    match nil::std::io::read_rooted_snapshot(root, relative, maximum)
        Result.Ok(_) -> return false
        Result.Error(message) ->
            if not nil::std::text::starts_with(message, "file.read_rooted_snapshot:") then return false end if
            if nil::std::text::index_of(message, root) >= 0 then return false end if
            if expected != "" and message != expected then return false end if
    end match
    return true
end fn

fn main() -> i32
    if nil::std::system::arg_count() != 4 and nil::std::system::arg_count() != 5 then return 60 end if
    let root = nil::std::system::arg(0)
    let stamp = nil::std::core::Convert.try_to_i64(nil::std::system::arg(1)).unwrap()
    let digest = nil::std::system::arg(2)
    let seed = nil::std::core::Convert.try_to_i64(nil::std::system::arg(3)).unwrap()
    match nil::std::io::read_rooted_snapshot(root, "nested/café-λ.bin", 256)
        Result.Error(message) ->
            println(message)
            return 61
        Result.Ok(snapshot) ->
            if not inspect(snapshot, stamp, digest, seed) then return 62 end if
            if nil::std::system::arg_count() == 5 then
                if nil::std::system::arg(4) != "wrong-element" then return 74 end if
                let boxed_content: anything = snapshot.content
                let invalid_content = boxed_content as array<i64>
                println(invalid_content.len())
                return 75
            end if
    end match
    match nil::std::io::read_rooted_snapshot(root, "empty.bin", 0)
        Result.Error(message) ->
            println(message)
            return 63
        Result.Ok(snapshot) ->
            if snapshot.content.len() != 0 or snapshot.modified_ms != stamp then return 64 end if
            if snapshot.sha256 != "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" then return 65 end if
    end match
    println("snapshot-content-ok")
    if not reject(root, "nested/café-λ.bin", 255,
        "file.read_rooted_snapshot: input exceeds maximum_bytes") then return 66 end if
    if not reject(root, "empty.bin", -1, "") then return 67 end if
    if not reject(root, "empty.bin", 536870913, "") then return 68 end if
    let invalid: array<text> = ["", ".", "..", "../outside/sentinel.bin", "nested/../empty.bin",
        "/empty.bin", "nested//café-λ.bin", "nested/", "nested\\café-λ.bin", "C:empty.bin",
        "missing.bin", "nested"]
    for path in invalid
        if not reject(root, path, 256, "") then return 69 end if
    end for
    if not reject(root + "/missing-root", "empty.bin", 256, "") then return 70 end if
    if not reject(root + "/empty.bin", "empty.bin", 256, "") then return 71 end if
    println("snapshot-errors-ok")
    if not reject(root, "linked.bin", 256, "") then return 72 end if
    if not reject(root, "via-link/sentinel.bin", 256, "") then return 73 end if
    println("snapshot-links-ok")
    return 0
end fn
