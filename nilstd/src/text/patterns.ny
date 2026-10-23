@pragma once
module nil.std.text

import nil.std.core

# Patterns use bounded byte-oriented matching. Offsets are byte offsets;
# embedded NUL, unsupported syntax and exhausted budgets are errors.
pub struct Pattern
    source: text
end struct

pub struct PatternMatch
    input: text
    pattern: text
    spans: array<i64>
end struct

@intrinsic("regex.validate")
fn _pattern_validate(source: text) -> text
end fn

@intrinsic("regex.search")
fn _pattern_search(source: text, input: text, start: i64, full: bool) -> array<i64>
end fn

@intrinsic("regex.group_index")
fn _pattern_group(source: text, name: text) -> i64
end fn

pub fn compile_pattern(source: text) -> Result<Pattern, text>
    let error = _pattern_validate(source)
    if error != "" then return Result.Error(error) end if
    return Result.Ok(Pattern { source: source })
end fn

fn _pattern_find(pattern: Pattern, input: text, start: i64, full: bool) -> Result<Option<PatternMatch>, text>
    let spans = _pattern_search(pattern.source, input, start, full)
    let status = spans[0]
    if status == 0 then return Result.Ok(Option.None) end if
    if status == 1 then
        return Result.Ok(Option.Some(PatternMatch { input: input, pattern: pattern.source, spans: spans }))
    end if
    if status == 2 then return Result.Error("pattern is invalid or unsupported") end if
    if status == 3 then return Result.Error("pattern search offset is invalid") end if
    if status == 4 then return Result.Error("pattern matching budget exceeded") end if
    return Result.Error("pattern matching failed: input must be NUL-free and within the supported limits")
end fn

pub fn search_pattern(pattern: Pattern, input: text, start: i64) -> Result<Option<PatternMatch>, text>
    return _pattern_find(pattern, input, start, false)
end fn

pub fn first_match(pattern: Pattern, input: text) -> Result<Option<PatternMatch>, text>
    return _pattern_find(pattern, input, 0, false)
end fn

pub fn full_match(pattern: Pattern, input: text) -> Result<Option<PatternMatch>, text>
    return _pattern_find(pattern, input, 0, true)
end fn

# Public records are writable. Validate both the encoded shape and the byte
# window before returning a capture, including values constructed by callers.
fn _capture_slot(found: PatternMatch, index: i64) -> i64
    let count = found.spans.len() as i64
    if count < 3 or count % 2 != 1 or found.spans[0] != 1 then return -1 end if
    if index < 0 or index >= (count - 1) / 2 then return -1 end if
    let slot = 1 + index * 2
    let start = found.spans[slot as i32]
    let length = found.spans[(slot + 1) as i32]
    let size = found.input.len() as i64
    if start < 0 or start > size or length < 0 then return -1 end if
    if length > size - start then return -1 end if
    return slot
end fn

pub fn capture_start(found: PatternMatch, index: i64) -> Option<i64>
    let slot = _capture_slot(found, index)
    if slot < 0 then return Option.None end if
    return Option.Some(found.spans[slot as i32])
end fn

pub fn capture_length(found: PatternMatch, index: i64) -> Option<i64>
    let slot = _capture_slot(found, index)
    if slot < 0 then return Option.None end if
    return Option.Some(found.spans[(slot + 1) as i32])
end fn

pub fn capture_index(found: PatternMatch, index: i64) -> Option<text>
    let slot = _capture_slot(found, index)
    if slot < 0 then return Option.None end if
    return Option.Some(found.input.substr(found.spans[slot as i32], found.spans[(slot + 1) as i32]))
end fn

pub fn capture_named(found: PatternMatch, name: text) -> Option<text>
    return capture_index(found, _pattern_group(found.pattern, name))
end fn

# Next is non-overlapping. Empty matches consume one byte of search progress,
# while an empty match at EOF terminates iteration without repeating forever.
pub fn next_match(found: PatternMatch) -> Result<Option<PatternMatch>, text>
    let slot = _capture_slot(found, 0)
    if slot < 0 then return Result.Error("next_match requires a valid whole-match span") end if
    let start = found.spans[slot as i32]
    let length = found.spans[(slot + 1) as i32]
    let mut next = start + length
    if length == 0 then
        if next == found.input.len() as i64 then return Result.Ok(Option.None) end if
        next = next + 1
    end if
    return search_pattern(Pattern { source: found.pattern }, found.input, next)
end fn
