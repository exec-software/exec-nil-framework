@schema
@pragma once

module nil.std.text

# =============================================================================
# StringBuilder - Efficient string concatenation (mirrors System.Text.StringBuilder)
# =============================================================================

pub struct StringBuilder
    pub buffer: string
    pub capacity: i32
    pub length: i32
end struct

impl StringBuilder
    # Constructor with initial capacity
    pub fn new() -> StringBuilder
        return StringBuilder { buffer: string.new(), capacity: 16, length: 0 }
    end fn

    pub fn new_with_capacity(capacity: i32) -> StringBuilder
        if capacity <= 0 then
            return StringBuilder.new()
        end if
        return StringBuilder { buffer: string.new(), capacity: capacity, length: 0 }
    end fn

    pub fn new_from_string(value: string) -> StringBuilder
        let len = value.length()
        return StringBuilder { buffer: value, capacity: len + 16, length: len }
    end fn

    # Append operations
    pub fn append(self: ref<Self>, value: string) -> ref<StringBuilder>
        self.buffer = self.buffer.concat(value)
        self.length = self.buffer.length()
        return self
    end fn

    pub fn append_text(self: ref<Self>, value: text) -> ref<StringBuilder>
        let str = string.from(value)
        return self.append(str)
    end fn

    pub fn append_char(self: ref<Self>, value: i8) -> ref<StringBuilder>
        let char_str = string.from(value as text)
        return self.append(char_str)
    end fn

    pub fn append_int(self: ref<Self>, value: i32) -> ref<StringBuilder>
        let int_str = string.from(value.to_text())
        return self.append(int_str)
    end fn

    pub fn append_line(self: ref<Self>) -> ref<StringBuilder>
        return self.append_text("\n")
    end fn

    pub fn append_line_text(self: ref<Self>, value: text) -> ref<StringBuilder>
        self.append_text(value)
        return self.append_line()
    end fn

    # Insert operations
    pub fn insert(self: ref<Self>, index: i32, value: string) -> ref<StringBuilder>
        if index < 0 then index = 0 end if
        if index > self.length then index = self.length end if

        if index == 0 then
            # Prepend
            self.buffer = value.concat(self.buffer)
        else if index == self.length then
            # Append
            self.buffer = self.buffer.concat(value)
        else
            # Insert in middle
            let prefix = self.buffer.substr(0, index)
            let suffix = self.buffer.substr(index, self.length)
            self.buffer = prefix.concat(value).concat(suffix)
        end if

        self.length = self.buffer.length()
        return self
    end fn

    # Remove operations
    pub fn remove(self: ref<Self>, start_index: i32, length: i32) -> ref<StringBuilder>
        if start_index < 0 || start_index >= self.length || length <= 0 then
            return self
        end if

        let end_index = start_index + length
        if end_index > self.length then
            end_index = self.length
        end if

        let prefix = self.buffer.substr(0, start_index)
        let suffix = self.buffer.substr(end_index, self.length)
        self.buffer = prefix.concat(suffix)
        self.length = self.buffer.length()
        return self
    end fn

    # Replace operations
    pub fn replace(self: ref<Self>, old_value: string, new_value: string) -> ref<StringBuilder>
        # Simple implementation - for full regex replacement, use regex module
        let mut result = string.new()
        let mut i = 0
        let old_len = old_value.length()

        while i <= self.length - old_len
            let substring = self.buffer.substr(i, i + old_len)
            if substring.eq(old_value) then
                result = result.concat(new_value)
                i = i + old_len
            else
                let char = self.buffer.substr(i, i + 1)
                result = result.concat(char)
                i = i + 1
            end if
        end while

        # Append remaining characters
        if i < self.length then
            let remaining = self.buffer.substr(i, self.length)
            result = result.concat(remaining)
        end if

        self.buffer = result
        self.length = self.buffer.length()
        return self
    end fn

    # Clear operations
    pub fn clear(self: ref<Self>)
        self.buffer = string.new()
        self.length = 0
    end fn

    # Query operations
    pub fn to_string(self: ref<Self>) -> string
        return self.buffer
    end fn

    pub fn to_text(self: ref<Self>) -> text
        return self.buffer.fmt()
    end fn

    pub fn length(self: ref<Self>) -> i32
        return self.length
    end fn

    pub fn capacity(self: ref<Self>) -> i32
        return self.capacity
    end fn

    pub fn is_empty(self: ref<Self>) -> bool
        return self.length == 0
    end fn

    # Character operations
    pub fn char_at(self: ref<Self>, index: i32) -> option<i8>
        if index < 0 || index >= self.length then
            return option.none
        end if
        let char_str = self.buffer.substr(index, index + 1)
        # Convert single character string to i8 (implementation detail)
        return option.some(0)  # Placeholder - need actual char extraction
    end fn

    pub fn set_char_at(self: ref<Self>, index: i32, value: i8) -> ref<StringBuilder>
        if index < 0 || index >= self.length then
            return self
        end if

        let prefix = self.buffer.substr(0, index)
        let suffix = self.buffer.substr(index + 1, self.length)
        let new_char = string.from(value as text)
        self.buffer = prefix.concat(new_char).concat(suffix)
        return self
    end fn

    # Utility operations
    pub fn ensure_capacity(self: ref<Self>, capacity: i32) -> ref<StringBuilder>
        if capacity > self.capacity then
            self.capacity = capacity
        end if
        return self
    end fn
end impl

# =============================================================================
# Encoding - Text encoding support (mirrors System.Text.Encoding)
# =============================================================================

pub enum EncodingType
    utf8
    utf16
    utf32
    ascii
    latin1
end enum

pub struct Encoding
    pub encoding_type: EncodingType
    pub code_page: i32
    pub display_name: string
end struct

impl Encoding
    # Predefined encodings
    pub fn utf8() -> Encoding
        return Encoding { encoding_type: EncodingType.utf8, code_page: 65001, display_name: string.from("UTF-8") }
    end fn

    pub fn utf16() -> Encoding
        return Encoding { encoding_type: EncodingType.utf16, code_page: 1200, display_name: string.from("UTF-16") }
    end fn

    pub fn utf32() -> Encoding
        return Encoding { encoding_type: EncodingType.utf32, code_page: 12000, display_name: string.from("UTF-32") }
    end fn

    pub fn ascii() -> Encoding
        return Encoding { encoding_type: EncodingType.ascii, code_page: 20127, display_name: string.from("ASCII") }
    end fn

    pub fn latin1() -> Encoding
        return Encoding { encoding_type: EncodingType.latin1, code_page: 28591, display_name: string.from("Latin-1") }
    end fn

    # Get byte count for string
    pub fn get_byte_count(self: ref<Self>, chars: string) -> i32
        match self.encoding_type
            EncodingType.utf8 -> return chars.length()
            EncodingType.utf16 -> return chars.length() * 2
            EncodingType.utf32 -> return chars.length() * 4
            EncodingType.ascii -> return chars.length()
            EncodingType.latin1 -> return chars.length()
        end match
    end fn

    # Get char count for byte array
    pub fn get_char_count(self: ref<Self>, bytes: vec<i8>) -> i32
        match self.encoding_type
            EncodingType.utf8 -> return bytes.length()
            EncodingType.utf16 -> return bytes.length() / 2
            EncodingType.utf32 -> return bytes.length() / 4
            EncodingType.ascii -> return bytes.length()
            EncodingType.latin1 -> return bytes.length()
        end match
    end fn

    # Convert string to bytes
    pub fn get_bytes(self: ref<Self>, s: string) -> vec<i8>
        let mut result = vec<i8>.new()
        let text_data = s.fmt()

        match self.encoding_type
            EncodingType.utf8 ->
                let mut i = 0
                while i < text_data.len()
                    result.push(text_data[i] as i8)
                    i = i + 1
                end while
            EncodingType.utf16 ->
                # UTF-16 encoding (simplified)
                let mut i = 0
                while i < text_data.len()
                    result.push(text_data[i] as i8)
                    result.push(0)  # High byte
                    i = i + 1
                end while
            EncodingType.ascii ->
                # ASCII encoding - filter non-ASCII characters
                let mut i = 0
                while i < text_data.len()
                    if (text_data[i] as i32) < 128 then
                        result.push(text_data[i] as i8)
                    else
                        result.push(63)  # '?' replacement character
                    end if
                    i = i + 1
                end while
            else ->
                # Default to UTF-8 for other encodings
                let mut i = 0
                while i < text_data.len()
                    result.push(text_data[i] as i8)
                    i = i + 1
                end while
        end match

        return result
    end fn

    # Convert bytes to string
    pub fn get_string(self: ref<Self>, bytes: vec<i8>) -> string
        let mut result_text = ""

        match self.encoding_type
            EncodingType.utf8 ->
                let mut i = 0
                while i < bytes.length()
                    result_text = result_text + (bytes.get(i).unwrap() as i8).to_text()
                    i = i + 1
                end while
            EncodingType.ascii ->
                let mut i = 0
                while i < bytes.length()
                    result_text = result_text + (bytes.get(i).unwrap() as i8).to_text()
                    i = i + 1
                end while
            else ->
                # Default to UTF-8 for other encodings
                let mut i = 0
                while i < bytes.length()
                    result_text = result_text + (bytes.get(i).unwrap() as i8).to_text()
                    i = i + 1
                end while
        end match

        return string.from(result_text)
    end fn

    # Encoding properties
    pub fn encoding_name(self: ref<Self>) -> string
        return self.display_name
    end fn

    pub fn code_page(self: ref<Self>) -> i32
        return self.code_page
    end fn

    pub fn is_single_byte(self: ref<Self>) -> bool
        match self.encoding_type
            EncodingType.ascii -> return true
            EncodingType.latin1 -> return true
            else -> return false
        end match
    end fn

    pub fn is_utf8(self: ref<Self>) -> bool
        return self.encoding_type == EncodingType.utf8
    end fn
end impl

# =============================================================================
# Enhanced Regular Expressions (mirrors System.Text.RegularExpressions)
# =============================================================================

pub struct Regex
    pub pattern: string
    pub options: RegexOptions
end struct

pub struct RegexOptions
    pub ignore_case: bool
    pub multiline: bool
    pub singleline: bool
    pub ignore_whitespace: bool
    pub compiled: bool
end struct

impl RegexOptions
    pub fn new() -> RegexOptions
        return RegexOptions {
            ignore_case: false,
            multiline: false,
            singleline: false,
            ignore_whitespace: false,
            compiled: false
        }
    end fn

    pub fn with_ignore_case() -> RegexOptions
        let mut opts = RegexOptions.new()
        opts.ignore_case = true
        return opts
    end fn

    pub fn with_multiline() -> RegexOptions
        let mut opts = RegexOptions.new()
        opts.multiline = true
        return opts
    end fn

    pub fn with_singleline() -> RegexOptions
        let mut opts = RegexOptions.new()
        opts.singleline = true
        return opts
    end fn
end impl

impl Regex
    # Constructors
    pub fn new(pattern: string) -> Regex
        return Regex { pattern: pattern, options: RegexOptions.new() }
    end fn

    pub fn new_with_options(pattern: string, options: RegexOptions) -> Regex
        return Regex { pattern: pattern, options: options }
    end fn

    # Core matching operations
    pub fn is_match(self: ref<Self>, input: string) -> bool
        return self.is_match_at(input, 0)
    end fn

    pub fn is_match_at(self: ref<Self>, input: string, startat: i32) -> bool
        # Simplified implementation - basic pattern matching
        return self.simple_match(input, startat)
    end fn

    pub fn matches(self: ref<Self>, input: string) -> vec<Match>
        let mut results = vec<Match>.new()
        let mut start = 0

        while start <= input.length()
            let match_result = self.match_at(input, start)
            match match_result
                option.some(m) ->
                    results.push(m)
                    start = m.index + m.length
                option.none ->
                    start = start + 1
            end match
        end while

        return results
    end fn

    pub fn match(self: ref<Self>, input: string) -> option<Match>
        return self.match_at(input, 0)
    end fn

    pub fn match_at(self: ref<Self>, input: string, startat: i32) -> option<Match>
        # Simplified pattern matching implementation
        let pattern_str = self.pattern.fmt()
        let input_str = input.fmt()

        # For now, implement basic substring matching
        if startat < 0 || startat >= input.length() then
            return option.none
        end if

        # Simple pattern matching for demonstration
        # In a real implementation, this would use a proper regex engine
        if self.simple_match_at(input, startat) then
            return option.some(Match {
                value: self.pattern,
                index: startat,
                length: self.pattern.length(),
                groups: vec<Group>.new()
            })
        else
            return option.none
        end if
    end fn

    # Replace operations
    pub fn replace(self: ref<Self>, input: string, replacement: string) -> string
        return self.replace_with_count(input, replacement, -1)
    end fn

    pub fn replace_with_count(self: ref<Self>, input: string, replacement: string, count: i32) -> string
        let mut result = input
        let mut replacements = 0
        let mut start = 0

        while start <= result.length() && (count < 0 || replacements < count)
            let match_result = self.match_at(result, start)
            match match_result
                option.some(m) ->
                    let prefix = result.substr(0, m.index)
                    let suffix = result.substr(m.index + m.length, result.length())
                    result = prefix.concat(replacement).concat(suffix)
                    start = m.index + replacement.length()
                    replacements = replacements + 1
                option.none ->
                    start = start + 1
            end match
        end while

        return result
    end fn

    # Split operations
    pub fn split(self: ref<Self>, input: string) -> vec<string>
        return self.split_with_count(input, -1)
    end fn

    pub fn split_with_count(self: ref<Self>, input: string, count: i32) -> vec<string>
        let mut result = vec<string>.new()
        let mut current = 0
        let mut splits = 0

        while current <= input.length() && (count < 0 || splits < count - 1)
            let match_result = self.match_at(input, current)
            match match_result
                option.some(m) ->
                    if m.index > current then
                        let segment = input.substr(current, m.index)
                        result.push(segment)
                    end if
                    current = m.index + m.length
                    splits = splits + 1
                option.none ->
                    current = current + 1
            end match
        end while

        # Add remaining part
        if current < input.length() then
            result.push(input.substr(current, input.length()))
        else
            result.push(string.new())
        end if

        return result
    end fn

    # Helper methods for simplified pattern matching
    private fn simple_match(self: ref<Self>, input: string, start: i32) -> bool
        return self.simple_match_at(input, start)
    end fn

    private fn simple_match_at(self: ref<Self>, input: string, start: i32) -> bool
        let pattern_str = self.pattern.fmt()
        let input_str = input.fmt()
        let pat_len = pattern_str.len()

        if start + pat_len > input_str.len() then
            return false
        end if

        let mut i = 0
        while i < pat_len
            let pattern_char = pattern_str[i]
            let input_char = input_str[start + i]

            if self.options.ignore_case then
                if (pattern_char as i32).to_lower() != (input_char as i32).to_lower() then
                    return false
                end if
            else
                if pattern_char != input_char then
                    return false
                end if
            end if

            i = i + 1
        end while

        return true
    end fn
end impl

# =============================================================================
# Match and Group structures
# =============================================================================

pub struct Match
    pub value: string
    pub index: i32
    pub length: i32
    pub groups: vec<Group>
end struct

impl Match
    pub fn value(self: ref<Self>) -> string
        return self.value
    end fn

    pub fn index(self: ref<Self>) -> i32
        return self.index
    end fn

    pub fn length(self: ref<Self>) -> i32
        return self.length
    end fn

    pub fn success(self: ref<Self>) -> bool
        return self.length > 0
    end fn
end impl

pub struct Group
    pub value: string
    pub index: i32
    pub length: i32
    pub success: bool
end struct

impl Group
    pub fn new(value: string, index: i32, length: i32, success: bool) -> Group
        return Group { value: value, index: index, length: length, success: success }
    end fn

    pub fn value(self: ref<Self>) -> string
        return self.value
    end fn

    pub fn success(self: ref<Self>) -> bool
        return self.success
    end fn
end impl

# =============================================================================
# Utility functions
# =============================================================================

pub fn escape_string(s: string) -> string
    let mut result = StringBuilder.new()
    let text_data = s.fmt()
    let mut i = 0

    while i < text_data.len()
        let c = text_data[i]
        match c
            '\\' -> result.append_text("\\\\")
            '\"' -> result.append_text("\\\"")
            '\n' -> result.append_text("\\n")
            '\r' -> result.append_text("\\r")
            '\t' -> result.append_text("\\t")
            else -> result.append_char(c)
        end match
        i = i + 1
    end while

    return result.to_string()
end fn

pub fn unescape_string(s: string) -> string
    let mut result = StringBuilder.new()
    let text_data = s.fmt()
    let mut i = 0

    while i < text_data.len()
        if text_data[i] == '\\' && i + 1 < text_data.len() then
            let next_char = text_data[i + 1]
            match next_char
                '\\' -> result.append_char('\\')
                '\"' -> result.append_char('\"')
                'n' -> result.append_char('\n')
                'r' -> result.append_char('\r')
                't' -> result.append_char('\t')
                else ->
                    result.append_char('\\')
                    result.append_char(next_char)
            end match
            i = i + 2
        else
            result.append_char(text_data[i])
            i = i + 1
        end if
    end while

    return result.to_string()
end fn