@pragma once
# =============================================================================
# ny.lang.nydata.parser — NyData text-format parser (loads)
# =============================================================================
# Parses NyData Cognitive Block Format text into nydata_value trees.
#
# Grammar (simplified):
#   block  ::= "object" NAME NL fields "end" NAME NL
#            | "list"   NAME NL items  "end" NAME NL
#            | "field"  NAME NL body   "end" NAME NL
#   fields ::= block*
#   items  ::= ("item" NL block "end" "item" NL)*
#   body   ::= (line NL)*          -- raw text, zero escaping
# =============================================================================

module nil.std.nydata

import nil.std.core
import nil.std.nydata.nydata

# -------------------------------------------------------------------------
# Parser state
# -------------------------------------------------------------------------

struct parse_state
    lines: array<text>
    pos: i32
    total: i32
end struct

fn ps_peek(ps: parse_state) -> text
    if ps.pos >= ps.total then
        return ""
    end if
    return ps.lines[ps.pos]
end fn

fn ps_consume(mut ps: parse_state) -> text
    let line = ps_peek(ps)
    ps.pos = ps.pos + 1
    return line
end fn

fn trim_line(s: text) -> text
    return s.trim()
end fn

fn starts_with(s: text, prefix: text) -> bool
    return s.starts_with(prefix)
end fn

fn after_prefix(s: text, prefix_len: i32) -> text
    return s.substr(prefix_len, s.len() - prefix_len)
end fn

fn split_lines(src: text) -> array<text>
    let mut lines: array<text> = []
    let mut start = 0
    let mut i = 0
    while i < src.len() do
        if src.substr(i, 1) == "\n" then
            lines = lines + [src.substr(start, i - start)]
            start = i + 1
        end if
        i = i + 1
    end while
    if start < src.len() then
        lines = lines + [src.substr(start, src.len() - start)]
    end if
    return lines
end fn

fn array_len(a: array<text>) -> i32
    return a.len()
end fn

fn parse_object(mut ps: parse_state, name: text) -> nydata_value
    let mut obj = nydata_object { name: name, fields: [], count: 0 }
    let end_marker = "end " + name
    while ps.pos < ps.total do
        let raw = ps_peek(ps)
        let line = trim_line(raw)
        if line == end_marker then
            ps.pos = ps.pos + 1
            return nydata_value {
                kind: NYDATA_OBJECT(),
                string_value: "",
                object_value: obj,
                list_value: nydata_list { name: "", items: [], count: 0 }
            }
        end if
        let child = parse_value(ps)
        if child.kind != NYDATA_ERROR() then
            # derive field name from child (already named inside parse_value)
            let nf = nydata_field { name: child.object_value.name, value: child }
            if obj.count == 0 then
                obj.fields = [nf]
            else
                obj.fields = obj.fields + [nf]
            end if
            obj.count = obj.count + 1
        else
            ps.pos = ps.pos + 1
        end if
    end while
    return nydata_value {
        kind: NYDATA_OBJECT(),
        string_value: "",
        object_value: obj,
        list_value: nydata_list { name: "", items: [], count: 0 }
    }
end fn

fn parse_list(mut ps: parse_state, name: text) -> nydata_value
    let mut lst = nydata_list { name: name, items: [], count: 0 }
    let end_marker = "end " + name
    while ps.pos < ps.total do
        let raw = ps_peek(ps)
        let line = trim_line(raw)
        if line == end_marker then
            ps.pos = ps.pos + 1
            return nydata_value {
                kind: NYDATA_LIST(),
                string_value: "",
                object_value: nydata_object { name: name, fields: [], count: 0 },
                list_value: lst
            }
        end if
        if line == "item" then
            ps.pos = ps.pos + 1
            # peek to determine if item contains a keyword block or a bare string
            let next = trim_line(ps_peek(ps))
            let mut elem: nydata_value
            if starts_with(next, "object ") or starts_with(next, "list ") or starts_with(next, "field ") then
                elem = parse_value(ps)
            else
                # bare string item: collect lines until "end item"
                let mut body = ""
                let mut scanning = true
                while ps.pos < ps.total and scanning do
                    let iline = trim_line(ps_peek(ps))
                    if iline == "end item" then
                        scanning = false
                    else
                        if body == "" then
                            body = iline
                        else
                            body = body + "\n" + iline
                        end if
                        ps.pos = ps.pos + 1
                    end if
                end while
                elem = nydata_value {
                    kind: NYDATA_STRING(),
                    string_value: body,
                    object_value: nydata_object { name: "", fields: [], count: 0 },
                    list_value: nydata_list { name: "", items: [], count: 0 }
                }
            end if
            if lst.count == 0 then
                lst.items = [elem]
            else
                lst.items = lst.items + [elem]
            end if
            lst.count = lst.count + 1
            # consume "end item"
            if trim_line(ps_peek(ps)) == "end item" then
                ps.pos = ps.pos + 1
            end if
        else
            ps.pos = ps.pos + 1
        end if
    end while
    return nydata_value {
        kind: NYDATA_LIST(),
        string_value: "",
        object_value: nydata_object { name: name, fields: [], count: 0 },
        list_value: lst
    }
end fn

fn parse_field(mut ps: parse_state, name: text) -> nydata_value
    let end_marker = "end " + name
    let mut body = ""
    while ps.pos < ps.total do
        let raw = ps_peek(ps)
        let line = trim_line(raw)
        if line == end_marker then
            ps.pos = ps.pos + 1
            return nydata_value {
                kind: NYDATA_STRING(),
                string_value: body,
                object_value: nydata_object { name: name, fields: [], count: 0 },
                list_value: nydata_list { name: "", items: [], count: 0 }
            }
        end if
        if body == "" then
            body = line
        else
            body = body + "\n" + line
        end if
        ps.pos = ps.pos + 1
    end while
    return nydata_value {
        kind: NYDATA_STRING(),
        string_value: body,
        object_value: nydata_object { name: name, fields: [], count: 0 },
        list_value: nydata_list { name: "", items: [], count: 0 }
    }
end fn

fn parse_value(mut ps: parse_state) -> nydata_value
    while ps.pos < ps.total do
        let raw = ps_peek(ps)
        let line = trim_line(raw)
        if line == "" then
            ps.pos = ps.pos + 1
        elseif starts_with(line, "object ") then
            ps.pos = ps.pos + 1
            let name = after_prefix(line, 7)
            return parse_object(ps, name)
        elseif starts_with(line, "list ") then
            ps.pos = ps.pos + 1
            let name = after_prefix(line, 5)
            return parse_list(ps, name)
        elseif starts_with(line, "field ") then
            ps.pos = ps.pos + 1
            let name = after_prefix(line, 6)
            return parse_field(ps, name)
        else
            return nydata_value {
                kind: NYDATA_ERROR(),
                string_value: "unexpected token: " + line,
                object_value: nydata_object { name: "", fields: [], count: 0 },
                list_value: nydata_list { name: "", items: [], count: 0 }
            }
        end if
    end while
    return nydata_value {
        kind: NYDATA_NULL(),
        string_value: "",
        object_value: nydata_object { name: "", fields: [], count: 0 },
        list_value: nydata_list { name: "", items: [], count: 0 }
    }
end fn

# -------------------------------------------------------------------------
# Public entry point
# -------------------------------------------------------------------------

pub fn loads(src: text) -> nydata_value
    let lines = split_lines(src)
    let mut ps = parse_state { lines: lines, pos: 0, total: array_len(lines) }
    return parse_value(ps)
end fn
