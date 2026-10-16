@schema
@pragma once
module nil.std.json

# JSON parsing and serialization for Ny
# Rewritten to use module-level g_pos so position advances across recursive calls.

import string_utils

// JSON value types
pub fn JSON_NULL() -> i32 return 0 end fn
pub fn JSON_OBJECT() -> i32 return 1 end fn
pub fn JSON_ARRAY() -> i32 return 2 end fn
pub fn JSON_STRING() -> i32 return 3 end fn
pub fn JSON_NUMBER() -> i32 return 4 end fn
pub fn JSON_BOOLEAN() -> i32 return 5 end fn

// JSON value structure
pub struct json_value
    kind: i32
    string_value: text
    number_value: f64
    boolean_value: bool
    object_value: json_object
    array_value: json_array
end struct

// JSON object structure
pub struct json_object
    fields: array<json_field>
    count: i32
end struct

// JSON field structure
pub struct json_field
    name: text
    value: json_value
end struct

// JSON array structure
pub struct json_array
    items: array<json_value>
    count: i32
end struct

// Module-level parse position cursor shared across all internal parse functions
let mut g_pos: i32 = 0

// Internal null value factory
fn null_val() -> json_value
    return json_value { type: JSON_NULL(), string_value: "", number_value: 0.0, boolean_value: false, object_value: json_object { fields: [], count: 0 }, array_value: json_array { items: [], count: 0 } }
end fn

// Internal: skip whitespace, advancing g_pos
fn skip_ws(json_str: text) -> void
    let len = string_utils.length(json_str)
    while g_pos < len do
        let ch = string_utils.char_at(json_str, g_pos)
        if ch == " " or ch == "\t" or ch == "\n" or ch == "\r" then
            g_pos = g_pos + 1
        else
            break
        end if
    end while
end fn

// Internal: parse a quoted JSON string starting at g_pos (points to '"')
fn parse_string_g(json_str: text) -> json_value
    let len = string_utils.length(json_str)
    g_pos = g_pos + 1
    let mut result = ""
    while g_pos < len do
        let ch = string_utils.char_at(json_str, g_pos)
        if ch == "\"" then
            g_pos = g_pos + 1
            return json_value { type: JSON_STRING(), string_value: result, number_value: 0.0, boolean_value: false, object_value: json_object { fields: [], count: 0 }, array_value: json_array { items: [], count: 0 } }
        elseif ch == "\\" then
            g_pos = g_pos + 1
            if g_pos < len then
                let esc = string_utils.char_at(json_str, g_pos)
                if esc == "n" then
                    result = result + "\n"
                elseif esc == "t" then
                    result = result + "\t"
                elseif esc == "r" then
                    result = result + "\r"
                elseif esc == "\"" then
                    result = result + "\""
                elseif esc == "\\" then
                    result = result + "\\"
                elseif esc == "/" then
                    result = result + "/"
                elseif esc == "u" then
                    g_pos = g_pos + 4
                else
                    result = result + esc
                end if
                g_pos = g_pos + 1
            end if
        else
            result = result + ch
            g_pos = g_pos + 1
        end if
    end while
    return json_value { type: JSON_STRING(), string_value: result, number_value: 0.0, boolean_value: false, object_value: json_object { fields: [], count: 0 }, array_value: json_array { items: [], count: 0 } }
end fn

// Internal: parse a JSON number starting at g_pos
fn parse_number_g(json_str: text) -> json_value
    let len = string_utils.length(json_str)
    let start = g_pos
    while g_pos < len do
        let ch = string_utils.char_at(json_str, g_pos)
        if ch >= "0" and ch <= "9" then
            g_pos = g_pos + 1
        elseif ch == "-" or ch == "+" then
            g_pos = g_pos + 1
        elseif ch == "." then
            g_pos = g_pos + 1
            while g_pos < len do
                let fch = string_utils.char_at(json_str, g_pos)
                if fch >= "0" and fch <= "9" then
                    g_pos = g_pos + 1
                else
                    break
                end if
            end while
        elseif ch == "e" or ch == "E" then
            g_pos = g_pos + 1
            if g_pos < len then
                let ech = string_utils.char_at(json_str, g_pos)
                if ech == "-" or ech == "+" then
                    g_pos = g_pos + 1
                end if
                while g_pos < len do
                    let en = string_utils.char_at(json_str, g_pos)
                    if en >= "0" and en <= "9" then
                        g_pos = g_pos + 1
                    else
                        break
                    end if
                end while
            end if
        else
            break
        end if
    end while
    let num_str = string_utils.substring(json_str, start, g_pos - start)
    let num = string_utils.text_to_f64(num_str)
    return json_value { type: JSON_NUMBER(), string_value: "", number_value: num, boolean_value: false, object_value: json_object { fields: [], count: 0 }, array_value: json_array { items: [], count: 0 } }
end fn

// Internal: parse boolean at g_pos
fn parse_boolean_g(json_str: text) -> json_value
    let seg4 = string_utils.substring(json_str, g_pos, 4)
    if seg4 == "true" then
        g_pos = g_pos + 4
        return json_value { type: JSON_BOOLEAN(), string_value: "", number_value: 0.0, boolean_value: true, object_value: json_object { fields: [], count: 0 }, array_value: json_array { items: [], count: 0 } }
    end if
    let seg5 = string_utils.substring(json_str, g_pos, 5)
    if seg5 == "false" then
        g_pos = g_pos + 5
        return json_value { type: JSON_BOOLEAN(), string_value: "", number_value: 0.0, boolean_value: false, object_value: json_object { fields: [], count: 0 }, array_value: json_array { items: [], count: 0 } }
    end if
    return null_val()
end fn

// Internal: parse null at g_pos
fn parse_null_g(json_str: text) -> json_value
    let seg = string_utils.substring(json_str, g_pos, 4)
    if seg == "null" then
        g_pos = g_pos + 4
    end if
    return null_val()
end fn

// Internal: parse value at g_pos (dispatcher)
fn parse_value_g(json_str: text) -> json_value
    skip_ws(json_str)
    let len = string_utils.length(json_str)
    if g_pos >= len then
        return null_val()
    end if
    let ch = string_utils.char_at(json_str, g_pos)
    if ch == "{" then
        return parse_object_g(json_str)
    elseif ch == "[" then
        return parse_array_g(json_str)
    elseif ch == "\"" then
        return parse_string_g(json_str)
    elseif ch == "t" or ch == "f" then
        return parse_boolean_g(json_str)
    elseif ch == "n" then
        return parse_null_g(json_str)
    else
        return parse_number_g(json_str)
    end if
end fn

// Internal: parse JSON object at g_pos
fn parse_object_g(json_str: text) -> json_value
    g_pos = g_pos + 1
    skip_ws(json_str)
    let len = string_utils.length(json_str)
    let mut fields: array<json_field> = []
    let mut count = 0

    if g_pos < len and string_utils.char_at(json_str, g_pos) == "}" then
        g_pos = g_pos + 1
        return json_value { type: JSON_OBJECT(), string_value: "", number_value: 0.0, boolean_value: false, object_value: json_object { fields: fields, count: 0 }, array_value: json_array { items: [], count: 0 } }
    end if

    while g_pos < len do
        skip_ws(json_str)
        if g_pos >= len or string_utils.char_at(json_str, g_pos) != "\"" then
            break
        end if
        let fname_val = parse_string_g(json_str)
        let fname = fname_val.string_value
        skip_ws(json_str)
        if g_pos >= len or string_utils.char_at(json_str, g_pos) != ":" then
            break
        end if
        g_pos = g_pos + 1
        skip_ws(json_str)
        let fval = parse_value_g(json_str)
        skip_ws(json_str)
        fields = fields + [json_field { name: fname, value: fval }]
        count = count + 1
        if g_pos < len and string_utils.char_at(json_str, g_pos) == "," then
            g_pos = g_pos + 1
        elseif g_pos < len and string_utils.char_at(json_str, g_pos) == "}" then
            g_pos = g_pos + 1
            break
        else
            break
        end if
    end while

    return json_value { type: JSON_OBJECT(), string_value: "", number_value: 0.0, boolean_value: false, object_value: json_object { fields: fields, count: count }, array_value: json_array { items: [], count: 0 } }
end fn

// Internal: parse JSON array at g_pos
fn parse_array_g(json_str: text) -> json_value
    g_pos = g_pos + 1
    skip_ws(json_str)
    let len = string_utils.length(json_str)
    let mut items: array<json_value> = []
    let mut count = 0

    if g_pos < len and string_utils.char_at(json_str, g_pos) == "]" then
        g_pos = g_pos + 1
        return json_value { type: JSON_ARRAY(), string_value: "", number_value: 0.0, boolean_value: false, object_value: json_object { fields: [], count: 0 }, array_value: json_array { items: items, count: 0 } }
    end if

    while g_pos < len do
        skip_ws(json_str)
        let item = parse_value_g(json_str)
        items = items + [item]
        count = count + 1
        skip_ws(json_str)
        if g_pos < len and string_utils.char_at(json_str, g_pos) == "," then
            g_pos = g_pos + 1
        elseif g_pos < len and string_utils.char_at(json_str, g_pos) == "]" then
            g_pos = g_pos + 1
            break
        else
            break
        end if
    end while

    return json_value { type: JSON_ARRAY(), string_value: "", number_value: 0.0, boolean_value: false, object_value: json_object { fields: [], count: 0 }, array_value: json_array { items: items, count: count } }
end fn

// -------------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------------

pub fn parse(json_str: text) -> json_value
    let trimmed = string_utils.trim(json_str)
    if string_utils.length(trimmed) == 0 then
        return null_val()
    end if
    g_pos = 0
    return parse_value_g(trimmed)
end fn

pub fn serialize(value: json_value) -> text
    return stringify(value)
end fn

pub fn stringify(value: json_value) -> text
    if value.kind == JSON_OBJECT() then
        return stringify_object(value.object_value)
    elseif value.kind == JSON_ARRAY() then
        return stringify_array(value.array_value)
    elseif value.kind == JSON_STRING() then
        return stringify_string(value.string_value)
    elseif value.kind == JSON_NUMBER() then
        return string_utils.f64_to_text(value.number_value)
    elseif value.kind == JSON_BOOLEAN() then
        if value.boolean_value then
            return "true"
        else
            return "false"
        end if
    else
        return "null"
    end if
end fn

pub fn stringify_object(obj: json_object) -> text
    let mut result = "{"
    let mut i = 0
    while i < obj.count do
        if i > 0 then
            result = result + ","
        end if
        result = result + "\"" + obj.fields[i].name + "\":" + stringify(obj.fields[i].value)
        i = i + 1
    end while
    return result + "}"
end fn

pub fn stringify_array(arr: json_array) -> text
    let mut result = "["
    let mut i = 0
    while i < arr.count do
        if i > 0 then
            result = result + ","
        end if
        result = result + stringify(arr.items[i])
        i = i + 1
    end while
    return result + "]"
end fn

pub fn stringify_string(s: text) -> text
    return "\"" + s + "\""
end fn

pub fn object_get(obj: json_object, name: text) -> json_value
    let mut i = 0
    while i < obj.count do
        if obj.fields[i].name == name then
            return obj.fields[i].value
        end if
        i = i + 1
    end while
    return null_val()
end fn

pub fn array_get(arr: json_array, index: i32) -> json_value
    if index >= 0 and index < arr.count then
        return arr.items[index]
    end if
    return null_val()
end fn

pub fn is_null(value: json_value) -> bool
    return value.kind == JSON_NULL()
end fn

pub fn is_object(value: json_value) -> bool
    return value.kind == JSON_OBJECT()
end fn

pub fn is_array(value: json_value) -> bool
    return value.kind == JSON_ARRAY()
end fn

pub fn is_string(value: json_value) -> bool
    return value.kind == JSON_STRING()
end fn

pub fn is_number(value: json_value) -> bool
    return value.kind == JSON_NUMBER()
end fn

pub fn is_boolean(value: json_value) -> bool
    return value.kind == JSON_BOOLEAN()
end fn

// Legacy API shims (set g_pos then call internal)
pub fn parse_object(json_str: text, pos: i32) -> json_value
    g_pos = pos
    return parse_object_g(json_str)
end fn

pub fn parse_array(json_str: text, pos: i32) -> json_value
    g_pos = pos
    return parse_array_g(json_str)
end fn

pub fn parse_value(json_str: text, pos: i32) -> json_value
    g_pos = pos
    return parse_value_g(json_str)
end fn

pub fn parse_string(json_str: text, pos: i32) -> json_value
    g_pos = pos
    return parse_string_g(json_str)
end fn

pub fn parse_number(json_str: text, pos: i32) -> json_value
    g_pos = pos
    return parse_number_g(json_str)
end fn

pub fn parse_boolean(json_str: text, pos: i32) -> json_value
    g_pos = pos
    return parse_boolean_g(json_str)
end fn

pub fn parse_null(json_str: text, pos: i32) -> json_value
    g_pos = pos
    return parse_null_g(json_str)
end fn

pub fn skip_whitespace(json_str: text, pos: i32) -> i32
    let len = string_utils.length(json_str)
    let mut p = pos
    while p < len do
        let ch = string_utils.char_at(json_str, p)
        if ch == " " or ch == "\t" or ch == "\n" or ch == "\r" then
            p = p + 1
        else
            break
        end if
    end while
    return p
end fn

pub fn empty() -> json_value
    return null_val()
end fn
