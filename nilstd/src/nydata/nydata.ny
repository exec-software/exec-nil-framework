@pragma once

module nil.std.nydata

import nil.std.core

pub fn NYDATA_NULL() -> i32
    return 0
end fn

pub fn NYDATA_OBJECT() -> i32
    return 1
end fn

pub fn NYDATA_LIST() -> i32
    return 2
end fn

pub fn NYDATA_STRING() -> i32
    return 3
end fn

pub fn NYDATA_ERROR() -> i32
    return -1
end fn

# -------------------------------------------------------------------------
# Core types
# -------------------------------------------------------------------------

pub struct nydata_value
    kind: i32
    string_value: text
    object_value: nydata_object
    list_value: nydata_list
end struct

pub struct nydata_object
    name: text
    fields: array<nydata_field>
    count: i32
end struct

pub struct nydata_field
    name: text
    value: nydata_value
end struct

pub struct nydata_list
    name: text
    items: array<nydata_value>
    count: i32
end struct

# -------------------------------------------------------------------------
# Internal helpers
# -------------------------------------------------------------------------

fn make_null() -> nydata_value
    return nydata_value {
        kind: NYDATA_NULL(),
        string_value: "",
        object_value: nydata_object { name: "", fields: [], count: 0 },
        list_value: nydata_list { name: "", items: [], count: 0 }
    }
end fn

fn make_error(msg: text) -> nydata_value
    return nydata_value {
        kind: NYDATA_ERROR(),
        string_value: msg,
        object_value: nydata_object { name: "", fields: [], count: 0 },
        list_value: nydata_list { name: "", items: [], count: 0 }
    }
end fn

fn make_string(s: text) -> nydata_value
    return nydata_value {
        kind: NYDATA_STRING(),
        string_value: s,
        object_value: nydata_object { name: "", fields: [], count: 0 },
        list_value: nydata_list { name: "", items: [], count: 0 }
    }
end fn

# -------------------------------------------------------------------------
# Serialization (dumps)
# -------------------------------------------------------------------------

pub fn dumps(val: nydata_value, name: text, indent: i32) -> text
    let mut indent_str = ""
    let mut i = 0
    while i < indent do
        indent_str = indent_str + "  "
        i = i + 1
    end while

    if val.kind == NYDATA_OBJECT() then
        let mut out = indent_str + "object " + name + "\n"
        let mut j = 0
        while j < val.object_value.count do
            let field = val.object_value.fields[j]
            out = out + dumps(field.value, field.name, indent + 1)
            j = j + 1
        end while
        out = out + indent_str + "end " + name + "\n"
        return out
    elseif val.kind == NYDATA_LIST() then
        let mut out = indent_str + "list " + name + "\n"
        let mut j = 0
        while j < val.list_value.count do
            let item = val.list_value.items[j]
            out = out + indent_str + "  item\n"
            if item.kind == NYDATA_STRING() then
                out = out + indent_str + "    " + item.string_value + "\n"
            else
                out = out + dumps(item, item.object_value.name, indent + 2)
            end if
            out = out + indent_str + "  end item\n"
            j = j + 1
        end while
        out = out + indent_str + "end " + name + "\n"
        return out
    elseif val.kind == NYDATA_STRING() then
        let mut out = indent_str + "field " + name + "\n"
        out = out + indent_str + "  " + val.string_value + "\n"
        out = out + indent_str + "end " + name + "\n"
        return out
    elseif val.kind == NYDATA_NULL() then
        let mut out = indent_str + "field " + name + "\n"
        out = out + indent_str + "  null\n"
        out = out + indent_str + "end " + name + "\n"
        return out
    end if
    return ""
end fn

# -------------------------------------------------------------------------
# Public API utilities
# -------------------------------------------------------------------------

pub fn object_get(obj: nydata_object, name: text) -> nydata_value
    let mut i = 0
    while i < obj.count do
        if obj.fields[i].name == name then
            return obj.fields[i].value
        end if
        i = i + 1
    end while
    return make_null()
end fn

pub fn object_set(mut obj: nydata_object, name: text, val: nydata_value)
    let mut i = 0
    while i < obj.count do
        if obj.fields[i].name == name then
            obj.fields[i] = nydata_field { name: name, value: val }
            return
        end if
        i = i + 1
    end while
    let nf = nydata_field { name: name, value: val }
    if obj.count == 0 then
        obj.fields = [nf]
    else
        obj.fields = obj.fields + [nf]
    end if
    obj.count = obj.count + 1
end fn

pub fn list_add(mut lst: nydata_list, val: nydata_value)
    if lst.count == 0 then
        lst.items = [val]
    else
        lst.items = lst.items + [val]
    end if
    lst.count = lst.count + 1
end fn

# -------------------------------------------------------------------------
# Convenience constructors
# -------------------------------------------------------------------------

pub fn string(s: text) -> nydata_value
    return make_string(s)
end fn

pub fn object(name: text) -> nydata_value
    return nydata_value {
        kind: NYDATA_OBJECT(),
        string_value: "",
        object_value: nydata_object { name: name, fields: [], count: 0 },
        list_value: nydata_list { name: "", items: [], count: 0 }
    }
end fn

pub fn list(name: text) -> nydata_value
    return nydata_value {
        kind: NYDATA_LIST(),
        string_value: "",
        object_value: nydata_object { name: "", fields: [], count: 0 },
        list_value: nydata_list { name: name, items: [], count: 0 }
    }
end fn
