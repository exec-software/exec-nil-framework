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

# Collect only whole-match byte windows. The private flat layout avoids arrays
# of records, which are outside the current native array profile. A final
# search establishes completeness even when exactly 4096 matches were found.
fn _pattern_collect_spans(pattern: Pattern, input: text) -> Result<array<i64>, text>
    let mut spans: array<i64> = []
    let mut pending = first_match(pattern, input)
    while true do
        if pending.is_error() then return Result.Error(pending.unwrap_error()) end if
        let maybe = pending.unwrap()
        if maybe.is_none() then return Result.Ok(spans) end if
        if spans.len() == 8192 then return Result.Error("pattern match count exceeds 4096") end if
        let found = maybe.unwrap()
        spans.push(capture_start(found, 0).unwrap())
        spans.push(capture_length(found, 0).unwrap())
        pending = next_match(found)
    end while
end fn

# Whole matches are returned regardless of capturing groups. Empty matches are
# retained; no match is a successful empty array. Failure never returns a prefix.
pub fn find_all_matches(pattern: Pattern, input: text) -> Result<array<text>, text>
    let scan = _pattern_collect_spans(pattern, input)
    if scan.is_error() then return Result.Error(scan.unwrap_error()) end if
    let spans = scan.unwrap()
    let mut matches: array<text> = []
    let mut index = 0
    while index < spans.len() do
        matches.push(input.substr(spans[index], spans[index + 1]))
        index = index + 2
    end while
    return Result.Ok(matches)
end fn

# Keep every empty field, including at input boundaries. Captures are not
# inserted into the result. A zero-width delimiter moves the search cursor in
# next_match, but consumes no bytes from this independent output cursor.
pub fn split_matches(pattern: Pattern, input: text) -> Result<array<text>, text>
    let scan = _pattern_collect_spans(pattern, input)
    if scan.is_error() then return Result.Error(scan.unwrap_error()) end if
    let spans = scan.unwrap()
    let mut parts: array<text> = []
    let mut cursor: i64 = 0
    let mut index = 0
    while index < spans.len() do
        let start = spans[index]
        parts.push(input.substr(cursor, start - cursor))
        cursor = start + spans[index + 1]
        index = index + 2
    end while
    parts.push(input.substr(cursor, (input.len() as i64) - cursor))
    return Result.Ok(parts)
end fn

# Balanced concatenation copies each byte at most once per tree level instead
# of repeatedly copying a growing prefix. The bounded scan gives depth <= 14.
fn _pattern_join_parts(parts: array<text>, first: i32, limit: i32) -> text
    if first == limit then return "" end if
    if limit - first == 1 then return parts[first] end if
    let middle = first + (limit - first) / 2
    let left = _pattern_join_parts(parts, first, middle)
    let right = _pattern_join_parts(parts, middle, limit)
    return left + right
end fn

# Replacement is literal text: dollar signs, backslashes and braces have no
# template meaning. Check the complete output size before allocating pieces.
pub fn replace_matches(pattern: Pattern, input: text, replacement: text) -> Result<text, text>
    let scan = _pattern_collect_spans(pattern, input)
    if scan.is_error() then return Result.Error(scan.unwrap_error()) end if
    let spans = scan.unwrap()
    # An empty-pattern search validates NUL-free text and its byte limit in
    # both runtimes without scanning replacement as a regex or interpolating it.
    let checked = first_match(Pattern { source: "" }, replacement)
    if checked.is_error() then return Result.Error("pattern replacement must be NUL-free and at most 1048576 bytes") end if
    let count = (spans.len() / 2) as i64
    let replacement_size = replacement.len() as i64
    let mut kept = input.len() as i64
    let mut index = 0
    while index < spans.len() do
        kept = kept - spans[index + 1]
        index = index + 2
    end while
    if replacement_size > 0 then
        if count > (1048576 - kept) / replacement_size then
            return Result.Error("pattern replacement output exceeds 1048576 bytes")
        end if
    end if
    if count == 0 then return Result.Ok(input) end if
    let mut parts: array<text> = []
    let mut cursor: i64 = 0
    index = 0
    while index < spans.len() do
        let start = spans[index]
        parts.push(input.substr(cursor, start - cursor))
        parts.push(replacement)
        cursor = start + spans[index + 1]
        index = index + 2
    end while
    parts.push(input.substr(cursor, (input.len() as i64) - cursor))
    return Result.Ok(_pattern_join_parts(parts, 0, parts.len()))
end fn
