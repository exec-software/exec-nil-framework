@schema
@pragma once

module nil.std.text

import nil.std.core
import nil.std.collections

# =============================================================================
# String Utilities - Comprehensive text manipulation (mirrors System.String methods)
# =============================================================================
# Provides text manipulation functions that operate on the built-in `text` type:
#   - Trim, pad, case conversion
#   - Search (contains, starts_with, ends_with, index_of, last_index_of)
#   - Split, join, replace
#   - Character classification
#   - Repeat, reverse, substring
#
# Usage:
#   import ny.lang.text
#   let trimmed = trim("  hello  ")
#   let parts = split("a,b,c", ",")
#   let upper = to_upper("hello")
# =============================================================================

# =============================================================================
# Platform Intrinsics
# =============================================================================

@interop("runtime")
fn _char_code(c: text) -> i32
end fn

@interop("runtime")
fn _char_from_code(code: i32) -> text
end fn

fn to_upper_impl(s: text) -> text
    let mut result = ""
    let len = s.len()
    let mut i = 0
    while i < len do
        let c = s.substr(i, 1)
        if c >= "a" and c <= "z" then
            let code = _char_code(c) - 32
            result = result + _char_from_code(code)
        else
            result = result + c
        end if
        i = i + 1
    end while
    return result
end fn

fn to_lower_impl(s: text) -> text
    let mut result = ""
    let len = s.len()
    let mut i = 0
    while i < len do
        let c = s.substr(i, 1)
        if c >= "A" and c <= "Z" then
            let code = _char_code(c) + 32
            result = result + _char_from_code(code)
        else
            result = result + c
        end if
        i = i + 1
    end while
    return result
end fn

fn trim_impl(s: text) -> text
    let len = s.len()
    if len == 0 then return "" end if
    let mut start = 0
    while start < len do
        let c = s.substr(start, 1)
        if c != " " and c != "\t" and c != "\n" and c != "\r" and c != "\f" and c != "\v" then
            break
        end if
        start = start + 1
    end while

    let mut end_idx = len
    while end_idx > start do
        let c = s.substr(end_idx - 1, 1)
        if c != " " and c != "\t" and c != "\n" and c != "\r" and c != "\f" and c != "\v" then
            break
        end if
        end_idx = end_idx - 1
    end while

    return s.substr(start, end_idx - start)
end fn

fn trim_start_impl(s: text) -> text
    let len = s.len()
    if len == 0 then return "" end if
    let mut start = 0
    while start < len do
        let c = s.substr(start, 1)
        if c != " " and c != "\t" and c != "\n" and c != "\r" and c != "\f" and c != "\v" then
            break
        end if
        start = start + 1
    end while
    return s.substr(start, len - start)
end fn

fn trim_end_impl(s: text) -> text
    let len = s.len()
    if len == 0 then return "" end if
    let mut end_idx = len
    while end_idx > 0 do
        let c = s.substr(end_idx - 1, 1)
        if c != " " and c != "\t" and c != "\n" and c != "\r" and c != "\f" and c != "\v" then
            break
        end if
        end_idx = end_idx - 1
    end while
    return s.substr(0, end_idx)
end fn

fn trim_chars_impl(s: text, chars: text) -> text
    let len = s.len()
    if len == 0 then return "" end if

    let mut start = 0
    while start < len do
        let c = s.substr(start, 1)
        if index_of_impl(chars, c, 0) < 0 then
            break
        end if
        start = start + 1
    end while

    let mut end_idx = len
    while end_idx > start do
        let c = s.substr(end_idx - 1, 1)
        if index_of_impl(chars, c, 0) < 0 then
            break
        end if
        end_idx = end_idx - 1
    end while

    return s.substr(start, end_idx - start)
end fn

fn index_of_impl(s: text, needle: text, start_index: i64) -> i64
    let s_len = s.len() as i64
    let n_len = needle.len() as i64

    let mut start = start_index
    if start < 0 then start = 0 end if
    if start > s_len then start = s_len end if
    if n_len == 0 then return start end if
    if n_len > s_len - start then return -1 end if

    let mut i = start
    while i <= s_len - n_len do
        if s.substr(i as i32, n_len as i32) == needle then
            return i
        end if
        i = i + 1
    end while
    return -1
end fn

fn last_index_of_impl(s: text, needle: text, start_index: i64) -> i64
    let s_len = s.len() as i64
    let n_len = needle.len() as i64

    let mut start = start_index
    if n_len == 0 then
        if start < 0 or start > s_len then return s_len end if
        return start
    end if
    if start < 0 or start >= s_len then start = s_len - 1 end if
    if n_len > s_len then return -1 end if

    # Adjust start index if needle would go past end
    if start + n_len > s_len then
        start = s_len - n_len
    end if

    let mut i = start
    while i >= 0 do
        if s.substr(i as i32, n_len as i32) == needle then
            return i
        end if
        i = i - 1
    end while
    return -1
end fn

fn replace_all_impl(s: text, old_value: text, new_value: text) -> text
    let old_len = old_value.len()
    if old_len == 0 then return s end if

    let mut result = ""
    let mut pos: i64 = 0
    let mut found: i64 = 0

    while true do
        found = index_of_impl(s, old_value, pos)
        if found == -1 then
            result = result + s.substr(pos as i32, (s.len() as i64 - pos) as i32)
            break
        end if

        result = result + s.substr(pos as i32, (found - pos) as i32)
        result = result + new_value
        pos = found + old_len as i64
    end while

    return result
end fn

fn replace_first_impl(s: text, old_value: text, new_value: text) -> text
    let pos = index_of_impl(s, old_value, 0)
    if pos == -1 then return s end if

    let prefix = s.substr(0, pos as i32)
    let suffix = s.substr((pos + old_value.len() as i64) as i32, (s.len() as i64 - (pos + old_value.len() as i64)) as i32)
    return prefix + new_value + suffix
end fn

fn repeat_impl(s: text, count: i64) -> text
    if count <= 0 then return "" end if
    let mut result = ""
    let mut i = 0
    while i < count do
        result = result + s
        i = i + 1
    end while
    return result
end fn

fn reverse_impl(s: text) -> text
    let len = s.len()
    if len <= 1 then return s end if
    let mut result = ""
    let mut i = len - 1
    while i >= 0 do
        result = result + s.substr(i, 1)
        i = i - 1
    end while
    return result
end fn

fn capitalize_impl(s: text) -> text
    if s.len() == 0 then return "" end if
    let first = s.substr(0, 1)
    let rest = s.substr(1, s.len() - 1)
    return to_upper_impl(first) + rest
end fn

fn title_case_impl(s: text) -> text
    let mut result = ""
    let len = s.len()
    let mut capitalize_next = true
    let mut i = 0

    while i < len do
        let c = s.substr(i, 1)
        if c == " " or c == "\t" or c == "\n" or c == "_" or c == "-" then
            result = result + c
            capitalize_next = true
        else if capitalize_next then
            result = result + to_upper_impl(c)
            capitalize_next = false
        else
            result = result + to_lower_impl(c)
            capitalize_next = false
        end if
        i = i + 1
    end while
    return result
end fn

fn starts_with_impl(s: text, prefix: text) -> bool
    let p_len = prefix.len()
    if p_len > s.len() then return false end if
    return s.substr(0, p_len) == prefix
end fn

fn ends_with_impl(s: text, suffix: text) -> bool
    let s_len = s.len()
    let sf_len = suffix.len()
    if sf_len > s_len then return false end if
    return s.substr(s_len - sf_len, sf_len) == suffix
end fn

fn contains_impl(s: text, needle: text) -> bool
    return index_of_impl(s, needle, 0) != -1
end fn

fn split_impl(s: text, delimiter: text) -> vec<text>
    let mut parts = vec<text>.new()
    let delim_len = delimiter.len()

    if delim_len == 0 then
        let mut i = 0
        while i < s.len() do
            parts.push(s.substr(i, 1))
            i = i + 1
        end while
        return parts
    end if

    let mut pos: i64 = 0
    let mut found: i64 = 0
    while true do
        found = index_of_impl(s, delimiter, pos)
        if found == -1 then
            parts.push(s.substr(pos as i32, (s.len() as i64 - pos) as i32))
            break
        end if

        parts.push(s.substr(pos as i32, (found - pos) as i32))
        pos = found + delim_len as i64
    end while
    return parts
end fn

fn split_n_impl(s: text, delimiter: text, max_parts: i64) -> vec<text>
    if max_parts <= 0 then return split_impl(s, delimiter) end if

    let mut parts = vec<text>.new()
    let delim_len = delimiter.len()
    if delim_len == 0 then return split_impl(s, delimiter) end if

    let mut pos: i64 = 0
    let mut found: i64 = 0
    let mut splits = 0

    while splits < max_parts - 1 do
        found = index_of_impl(s, delimiter, pos)
        if found == -1 then break end if

        parts.push(s.substr(pos as i32, (found - pos) as i32))
        pos = found + delim_len as i64
        splits = splits + 1
    end while

    parts.push(s.substr(pos as i32, (s.len() as i64 - pos) as i32))
    return parts
end fn

fn pad_left_impl(s: text, width: i64, fill_char: text) -> text
    let s_len = s.len() as i64
    if width <= s_len then return s end if
    let pad_len = width - s_len
    let fill = if fill_char.len() == 0 then " " else fill_char.substr(0, 1)
    return repeat_impl(fill, pad_len) + s
end fn

fn pad_right_impl(s: text, width: i64, fill_char: text) -> text
    let s_len = s.len() as i64
    if width <= s_len then return s end if
    let pad_len = width - s_len
    let fill = if fill_char.len() == 0 then " " else fill_char.substr(0, 1)
    return s + repeat_impl(fill, pad_len)
end fn

fn count_occurrences_impl(s: text, needle: text) -> i64
    let needle_len = needle.len()
    if needle_len == 0 then return 0 end if

    let mut count = 0
    let mut pos: i64 = 0
    while true do
        let found = index_of_impl(s, needle, pos)
        if found == -1 then break end if
        count = count + 1
        pos = found + needle_len as i64
    end while
    return count
end fn

fn is_whitespace_impl(s: text) -> bool
    if s.len() == 0 then return false end if
    let mut i = 0
    while i < s.len() do
        let c = s.substr(i, 1)
        if c != " " and c != "\t" and c != "\n" and c != "\r" and c != "\f" and c != "\v" then
            return false
        end if
        i = i + 1
    end while
    return true
end fn

fn is_numeric_impl(s: text) -> bool
    if s.len() == 0 then return false end if
    let mut i = 0
    while i < s.len() do
        let c = s.substr(i, 1)
        if c < "0" or c > "9" then
            return false
        end if
        i = i + 1
    end while
    return true
end fn

fn is_alpha_impl(s: text) -> bool
    if s.len() == 0 then return false end if
    let mut i = 0
    while i < s.len() do
        let c = s.substr(i, 1)
        let is_lower = c >= "a" and c <= "z"
        let is_upper = c >= "A" and c <= "Z"
        if not is_lower and not is_upper then
            return false
        end if
        i = i + 1
    end while
    return true
end fn

fn is_alphanumeric_impl(s: text) -> bool
    if s.len() == 0 then return false end if
    let mut i = 0
    while i < s.len() do
        let c = s.substr(i, 1)
        let is_lower = c >= "a" and c <= "z"
        let is_upper = c >= "A" and c <= "Z"
        let is_digit = c >= "0" and c <= "9"
        if not is_lower and not is_upper and not is_digit then
            return false
        end if
        i = i + 1
    end while
    return true
end fn

fn is_lower_impl(s: text) -> bool
    if s.len() == 0 then return false end if
    let mut i = 0
    while i < s.len() do
        let c = s.substr(i, 1)
        # Ny treats a string as lowercase when it contains no ASCII uppercase
        # letters; non-alphabetic characters do not affect the result.
        if c >= "A" and c <= "Z" then
            return false
        end if
        i = i + 1
    end while
    return true
end fn

fn is_upper_impl(s: text) -> bool
    if s.len() == 0 then return false end if
    let mut i = 0
    while i < s.len() do
        let c = s.substr(i, 1)
        # Ny treats a string as uppercase when it contains no ASCII lowercase
        # letters; non-alphabetic characters do not affect the result.
        if c >= "a" and c <= "z" then
            return false
        end if
        i = i + 1
    end while
    return true
end fn

# =============================================================================
# Public API - Case Conversion
# =============================================================================

pub fn to_upper(s: text) -> text
    return to_upper_impl(s)
end fn

pub fn to_lower(s: text) -> text
    return to_lower_impl(s)
end fn

pub fn capitalize(s: text) -> text
    return capitalize_impl(s)
end fn

pub fn title_case(s: text) -> text
    return title_case_impl(s)
end fn

# =============================================================================
# Public API - Trimming
# =============================================================================

pub fn trim(s: text) -> text
    return trim_impl(s)
end fn

pub fn trim_start(s: text) -> text
    return trim_start_impl(s)
end fn

pub fn trim_end(s: text) -> text
    return trim_end_impl(s)
end fn

pub fn trim_chars(s: text, chars: text) -> text
    return trim_chars_impl(s, chars)
end fn

# =============================================================================
# Public API - Search
# =============================================================================

pub fn contains(s: text, needle: text) -> bool
    return contains_impl(s, needle)
end fn

pub fn starts_with(s: text, prefix: text) -> bool
    return starts_with_impl(s, prefix)
end fn

pub fn ends_with(s: text, suffix: text) -> bool
    return ends_with_impl(s, suffix)
end fn

pub fn index_of(s: text, needle: text) -> i64
    return index_of_impl(s, needle, 0)
end fn

pub fn index_of_from(s: text, needle: text, start: i64) -> i64
    return index_of_impl(s, needle, start)
end fn

pub fn last_index_of(s: text, needle: text) -> i64
    return last_index_of_impl(s, needle, -1)
end fn

pub fn last_index_of_from(s: text, needle: text, start: i64) -> i64
    return last_index_of_impl(s, needle, start)
end fn

pub fn count(s: text, needle: text) -> i64
    return count_occurrences_impl(s, needle)
end fn

# =============================================================================
# Public API - Replace
# =============================================================================

pub fn replace(s: text, old_value: text, new_value: text) -> text
    return replace_all_impl(s, old_value, new_value)
end fn

pub fn replace_first(s: text, old_value: text, new_value: text) -> text
    return replace_first_impl(s, old_value, new_value)
end fn

# =============================================================================
# Public API - Split & Join
# =============================================================================

pub fn split(s: text, delimiter: text) -> vec<text>
    return split_impl(s, delimiter)
end fn

pub fn split_n(s: text, delimiter: text, max_parts: i64) -> vec<text>
    return split_n_impl(s, delimiter, max_parts)
end fn

pub fn split_whitespace(s: text) -> vec<text>
    # Split on any whitespace, filtering empty parts
    let mut parts = vec<text>.new()
    let trimmed = trim_impl(s)
    if trimmed.len() == 0 then
        return parts
    end if
    let mut start = 0
    let mut i = 0
    let mut in_word = false
    while i < trimmed.len() do
        let ch = trimmed.substr(i, 1)
        if ch == " " or ch == "\t" or ch == "\n" or ch == "\r" or ch == "\f" or ch == "\v" then
            if in_word then
                parts.push(trimmed.substr(start, i - start))
                in_word = false
            end if
        else
            if not in_word then
                start = i
                in_word = true
            end if
        end if
        i = i + 1
    end while
    if in_word then
        parts.push(trimmed.substr(start, trimmed.len() - start))
    end if
    return parts
end fn

pub fn split_lines(s: text) -> vec<text>
    let normalized = replace_all_impl(replace_all_impl(s, "\r\n", "\n"), "\r", "\n")
    return split_impl(normalized, "\n")
end fn

pub fn join(parts: vec<text>, separator: text) -> text
    let mut result = ""
    let mut first = true
    let mut i = 0
    while i < parts.length() do
        if first then
            first = false
        else
            result = result + separator
        end if
        result = result + parts.get(i).unwrap()
        i = i + 1
    end while
    return result
end fn

# =============================================================================
# Public API - Padding
# =============================================================================

pub fn pad_left(s: text, width: i64) -> text
    return pad_left_impl(s, width, " ")
end fn

pub fn pad_left_char(s: text, width: i64, fill: text) -> text
    return pad_left_impl(s, width, fill)
end fn

pub fn pad_right(s: text, width: i64) -> text
    return pad_right_impl(s, width, " ")
end fn

pub fn pad_right_char(s: text, width: i64, fill: text) -> text
    return pad_right_impl(s, width, fill)
end fn

pub fn center(s: text, width: i64) -> text
    let slen = s.len() as i64
    if width <= slen then
        return s
    end if
    let total_pad = width - slen
    let left_pad = total_pad / 2
    let right_pad = total_pad - left_pad
    let mut result = ""
    let mut i = 0
    while i < left_pad do
        result = result + " "
        i = i + 1
    end while
    result = result + s
    i = 0
    while i < right_pad do
        result = result + " "
        i = i + 1
    end while
    return result
end fn

# =============================================================================
# Public API - Transform
# =============================================================================

pub fn repeat(s: text, count: i64) -> text
    if count <= 0 then
        return ""
    end if
    return repeat_impl(s, count)
end fn

pub fn reverse(s: text) -> text
    return reverse_impl(s)
end fn

pub fn substring(s: text, start: i64, length: i64) -> text
    let size: i64 = s.len() as i64
    if start < 0 or start >= size or length <= 0 then
        return ""
    end if
    # Subtract after validating start so a large length cannot wrap the bound.
    let available: i64 = size - start
    if length > available then
        return s.substr(start as i32, available as i32)
    end if
    return s.substr(start as i32, length as i32)
end fn

pub fn char_at(s: text, index: i64) -> text
    if index < 0 or index >= s.len() as i64 then
        return ""
    end if
    return s.substr(index as i32, 1)
end fn

pub fn length(s: text) -> i64
    return s.len() as i64
end fn

pub fn remove_prefix(s: text, prefix: text) -> text
    if starts_with_impl(s, prefix) then
        return s.substr(prefix.len(), s.len() - prefix.len())
    end if
    return s
end fn

pub fn remove_suffix(s: text, suffix: text) -> text
    if ends_with_impl(s, suffix) then
        return s.substr(0, s.len() - suffix.len())
    end if
    return s
end fn

pub fn truncate(s: text, max_len: i64, ellipsis: text) -> text
    if max_len <= 0 then
        return ""
    end if
    if s.len() as i64 <= max_len then
        return s
    end if
    let cut = max_len - ellipsis.len() as i64
    if cut <= 0 then
        return ellipsis.substr(0, max_len as i32)
    end if
    return s.substr(0, cut as i32) + ellipsis
end fn

# =============================================================================
# Public API - Character Classification
# =============================================================================

pub fn is_empty(s: text) -> bool
    return s.len() == 0
end fn

pub fn is_whitespace(s: text) -> bool
    if s.len() == 0 then
        return false
    end if
    return is_whitespace_impl(s)
end fn

pub fn is_blank(s: text) -> bool
    return s.len() == 0 or is_whitespace_impl(s)
end fn

pub fn is_numeric(s: text) -> bool
    return is_numeric_impl(s)
end fn

pub fn is_alpha(s: text) -> bool
    return is_alpha_impl(s)
end fn

pub fn is_alphanumeric(s: text) -> bool
    return is_alphanumeric_impl(s)
end fn

pub fn is_lower(s: text) -> bool
    return is_lower_impl(s)
end fn

pub fn is_upper(s: text) -> bool
    return is_upper_impl(s)
end fn

# =============================================================================
# Conversion Utilities
# =============================================================================

@interop("runtime")
fn _int_to_string(n: i32) -> text
end fn

@interop("runtime")
fn _int64_to_string(n: i64) -> text
end fn

pub fn int_to_string(n: i32) -> text
    return _int_to_string(n)
end fn

pub fn int_to_string_i64(n: i64) -> text
    return _int64_to_string(n)
end fn
