@schema
@pragma once
module nil.std.io

import nil.std.core
import nil.std.collections

# The separator every function in this module produces: always "/".
#
# Path-producing functions accept "\\" as a separator on every host, including
# in a single-component join. Drive and root interpretation follows the host;
# this does not turn Windows drive paths into absolute POSIX paths.
#
# This is not the host's own spelling. Use it to split or rebuild a path, not
# to decide how one should look to a user or to cmd.exe.
@intrinsic("path.separator")
pub fn separator() -> text
end fn

@intrinsic("path.combine")
pub fn combine(part1: text, part2: text) -> text
end fn

pub fn join(parts: vec<text>) -> text
    let mut result: text = ""
    let mut i: i32 = 0
    while i < parts.length() do
        match parts.get(i)
            Option.None -> return result
            Option.Some(part) ->
                let part_text = part as text
                result = combine(result, part_text)
        end match
        i = i + 1
    end while
    return result
end fn

@intrinsic("path.dirname")
pub fn dirname(path: text) -> text
end fn

pub fn parent(path: text) -> text
    return dirname(path)
end fn

@intrinsic("path.basename")
pub fn basename(path: text) -> text
end fn

@intrinsic("path.stem")
pub fn stem(path: text) -> text
end fn

@intrinsic("path.extension")
pub fn extension(path: text) -> text
end fn

@intrinsic("path.extension_no_dot")
pub fn extension_no_dot(path: text) -> text
end fn

@intrinsic("path.absolute")
pub fn absolute(path: text) -> text
end fn

@intrinsic("path.normalize")
pub fn normalize(path: text) -> text
end fn

@intrinsic("path.is_absolute")
pub fn is_absolute(path: text) -> bool
end fn

pub fn is_relative(path: text) -> bool
    return not is_absolute(path)
end fn

@intrinsic("path.relative")
pub fn relative(from: text, to: text) -> text
end fn

pub fn has_extension(path: text) -> bool
    let ext = extension(path)
    return ext.len() > 0
end fn

# Exact byte suffix comparison, not component matching or separator folding.
@intrinsic("path.ends_with")
pub fn ends_with(path: text, suffix: text) -> bool
end fn

@intrinsic("path.with_extension")
pub fn with_extension(path: text, new_ext: text) -> text
end fn

pub fn with_prefix(path: text, prefix: text) -> text
    let d = dirname(path)
    let b = basename(path)
    return combine(d, prefix + b)
end fn

pub fn with_suffix(path: text, suffix: text) -> text
    let d = dirname(path)
    let b = basename(path)
    return combine(d, b + suffix)
end fn
