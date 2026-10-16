@pragma once
# ny.lang.system - Simple YAML/config file parser

module nil.std.system

import nil.std.core

@interop("runtime")
fn string_trim(s: text) -> text
end fn

@interop("runtime")
fn string_find(s: text, substr: text) -> i32
end fn

@interop("runtime")
fn string_substr(s: text, start: i32, len: i32) -> text
end fn

@interop("runtime")
fn string_len(s: text) -> i32
end fn

@interop("runtime")
fn string_split(s: text, delim: text) -> vec<text>
end fn

@interop("runtime")
fn file_read_text(filename: text) -> result<text, text>
end fn

pub struct yaml_value
    key: text
    value: text
end struct

pub fn parse_yaml_line(line: text) -> yaml_value?
    let trimmed = string_trim(line)
    if trimmed == "" or string_find(trimmed, "#") == 0
        return none
    end if

    let colon_pos = string_find(trimmed, ":")
    if colon_pos == -1
        return none
    end if

    let key = string_trim(string_substr(trimmed, 0, colon_pos))
    let value_part = string_trim(string_substr(trimmed, colon_pos + 1, string_len(trimmed) - colon_pos - 1))

    # Remove quotes if present
    if string_len(value_part) >= 2 and string_substr(value_part, 0, 1) == "\"" and string_substr(value_part, string_len(value_part) - 1, 1) == "\""
        value_part = string_substr(value_part, 1, string_len(value_part) - 2)
    end if

    return some(yaml_value { key: key, value: value_part })
end fn

pub fn load_yaml_config(filename: text) -> map<text, text>
    let content_result = file_read_text(filename)
    if content_result.is_err()
        return map{}
    end if

    let content = content_result.unwrap()
    let lines = string_split(content, "\n")
    let config = map{}

    for line in lines
        let parsed = parse_yaml_line(line)
        if parsed.is_some()
            let kv = parsed.unwrap()
            config.insert(kv.key, kv.value)
        end if
    end for

    return config
end fn