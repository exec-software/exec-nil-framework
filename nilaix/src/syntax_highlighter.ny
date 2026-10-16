module syntax_highlighter
# Syntax highlighting system for NY programming language
# Supports keywords, strings, comments, numbers, operators

import winplat
import theme
import string_utils

// Token types for syntax highlighting
pub function TOKEN_DEFAULT() -> i32
    return 0
end function

pub function TOKEN_KEYWORD() -> i32
    return 1
end function

pub function TOKEN_STRING() -> i32
    return 2
end function

pub function TOKEN_COMMENT() -> i32
    return 3
end function

pub function TOKEN_NUMBER() -> i32
    return 4
end function

pub function TOKEN_OPERATOR() -> i32
    return 5
end function

pub function TOKEN_FUNCTION() -> i32
    return 6
end function

pub function TOKEN_TYPE() -> i32
    return 7
end function

pub function TOKEN_BUILTIN() -> i32
    return 8
end function

pub function TOKEN_DIRECTIVE() -> i32
    return 9
end function

// Syntax token structure
pub struct syntax_token
    token_type: i32
    start_pos: i32
    end_pos: i32
    color: i32
end struct

// Token list structure
pub struct token_list
    tokens: array<syntax_token>
    count: i32
end struct

// NY language keywords
let mut keywords: text = "if,then,elseif,else,end,while,do,function,return,module,import,export,public,private,pub,function,struct,let,mut,not,and,or,in,break,continue,for"

// Function-like keywords
let mut functions: text = "print,write,read,open,close,length,split,trim,index_of,replace,int_to_text,text_to_int"

// Type keywords
let mut types: text = "i32,text,bool,array,i64,f32,f64"

// Built-in functions and constants
let mut builtins: text = "true,false,null"

// Operators checked inline in is_operator function

// Directives
let mut directives: text = "@import,@export,@pub,@private,@struct,@function,@module,@interop,@native"

pub function get_token_color(token_type: i32) -> i32
    if token_type == TOKEN_KEYWORD() then
        return theme.syntax_keyword()
    elseif token_type == TOKEN_STRING() then
        return theme.syntax_string()
    elseif token_type == TOKEN_COMMENT() then
        return theme.syntax_comment()
    elseif token_type == TOKEN_NUMBER() then
        return theme.syntax_number()
    elseif token_type == TOKEN_OPERATOR() then
        return theme.syntax_operator()
    elseif token_type == TOKEN_FUNCTION() then
        return theme.syntax_function()
    elseif token_type == TOKEN_TYPE() then
        return theme.syntax_type()
    elseif token_type == TOKEN_BUILTIN() then
        return theme.syntax_builtin()
    elseif token_type == TOKEN_DIRECTIVE() then
        return theme.syntax_directive()
    else
        return theme.text_primary()
    end if
end function

pub function is_keyword(word: text) -> bool
    let parts = string_utils.split(keywords, ",")
    let mut i = 0
    while i < parts.count do
        if string_utils.trim(parts.items[i]) == word then
            return true
        end if
        i = i + 1
    end while
    return false
end function

pub function is_function(word: text) -> bool
    let parts = string_utils.split(functions, ",")
    let mut i = 0
    while i < parts.count do
        if string_utils.trim(parts.items[i]) == word then
            return true
        end if
        i = i + 1
    end while
    return false
end function

pub function is_type(word: text) -> bool
    let parts = string_utils.split(types, ",")
    let mut i = 0
    while i < parts.count do
        if string_utils.trim(parts.items[i]) == word then
            return true
        end if
        i = i + 1
    end while
    return false
end function

pub function is_builtin(word: text) -> bool
    let parts = string_utils.split(builtins, ",")
    let mut i = 0
    while i < parts.count do
        if string_utils.trim(parts.items[i]) == word then
            return true
        end if
        i = i + 1
    end while
    return false
end function

pub function is_operator(char: text) -> bool
    if char == "+" or char == "-" or char == "*" or char == "/" then
        return true
    end if
    if char == "%" or char == "=" or char == "!" or char == "<" then
        return true
    end if
    if char == ">" or char == "&" or char == "^" or char == "~" then
        return true
    end if
    if char == "." or char == ":" or char == ";" then
        return true
    end if
    return false
end function

pub function is_directive(word: text) -> bool
    if string_utils.length(word) > 0 and string_utils.char_at(word, 0) == "@" then
        let directive_word = string_utils.substring(word, 1, string_utils.length(word))
        let parts = string_utils.split(directives, ",")
        let mut i = 0
        while i < parts.count do
            if string_utils.trim(parts.items[i]) == directive_word then
                return true
            end if
            i = i + 1
        end while
    end if
    return false
end function

pub function is_identifier_char(char: text) -> bool
    // Allow letters, digits, and underscore
    let code = string_utils.char_code(char)
    if (code >= 65 and code <= 90) or (code >= 97 and code <= 122) then
        return true  // A-Z, a-z
    elseif (code >= 48 and code <= 57) then
        return true  // 0-9
    elseif code == 95 then
        return true  // underscore
    end if
    return false
end function

pub function is_whitespace(char: text) -> bool
    let code = string_utils.char_code(char)
    return code == 32 or code == 9 or code == 10 or code == 13  // space, tab, newline, carriage return
end function

pub function parse_line(line: text) -> token_list
    let mut tokens: array<syntax_token> = []
    let mut token_count = 0
    let mut pos = 0
    let len = string_utils.length(line)

    while pos < len do
        let char = string_utils.char_at(line, pos)

        // Skip whitespace
        if is_whitespace(char) then
            pos = pos + 1
            continue
        end if

        // Check for comments
        if char == "#" and (pos == 0 or string_utils.char_at(line, pos - 1) == " ") then
            let token = syntax_token {
                token_type: TOKEN_COMMENT(),
                start_pos: pos,
                end_pos: len,
                color: get_token_color(TOKEN_COMMENT())
            }
            tokens = tokens + [token]
            token_count = token_count + 1
            break
        end if

        // Check for strings
        if char == "\"" or char == "'" then
            let string_start = pos
            let quote_char = char
            pos = pos + 1
            while pos < len do
                let current = string_utils.char_at(line, pos)
                if current == quote_char then
                    pos = pos + 1
                    break
                elseif current == "\\" and pos + 1 < len then
                    pos = pos + 2  // Skip escaped character
                else
                    pos = pos + 1
                end if
            end while
            let token = syntax_token {
                token_type: TOKEN_STRING(),
                start_pos: string_start,
                end_pos: pos,
                color: get_token_color(TOKEN_STRING())
            }
            tokens = tokens + [token]
            token_count = token_count + 1
            continue
        end if

        // Check for numbers
        if (char >= "0" and char <= "9") then
            let num_start = pos
            pos = pos + 1
            // Parse integer part
            while pos < len and (string_utils.char_at(line, pos) >= "0" and string_utils.char_at(line, pos) <= "9") do
                pos = pos + 1
            end while
            // Parse decimal part
            if pos < len and string_utils.char_at(line, pos) == "." then
                pos = pos + 1
                while pos < len and (string_utils.char_at(line, pos) >= "0" and string_utils.char_at(line, pos) <= "9") do
                    pos = pos + 1
                end while
            end if
            let token = syntax_token {
                token_type: TOKEN_NUMBER(),
                start_pos: num_start,
                end_pos: pos,
                color: get_token_color(TOKEN_NUMBER())
            }
            tokens = tokens + [token]
            token_count = token_count + 1
            continue
        end if

        // Check for identifiers and keywords
        if is_identifier_char(char) then
            let ident_start = pos
            while pos < len and is_identifier_char(string_utils.char_at(line, pos)) do
                pos = pos + 1
            end while
            let word = string_utils.substring(line, ident_start, pos)
            let mut token_type = TOKEN_DEFAULT()

            if is_keyword(word) then
                token_type = TOKEN_KEYWORD()
            elseif is_function(word) then
                token_type = TOKEN_FUNCTION()
            elseif is_type(word) then
                token_type = TOKEN_TYPE()
            elseif is_builtin(word) then
                token_type = TOKEN_BUILTIN()
            elseif is_directive(word) then
                token_type = TOKEN_DIRECTIVE()
            end if

            let token = syntax_token {
                token_type: token_type,
                start_pos: ident_start,
                end_pos: pos,
                color: get_token_color(token_type)
            }
            tokens = tokens + [token]
            token_count = token_count + 1
            continue
        end if

        // Check for operators
        if is_operator(char) then
            let mut op_end = pos + 1
            // Check for multi-character operators
            if pos + 1 < len then
                let two_char = char + string_utils.char_at(line, pos + 1)
                if two_char == "==" or two_char == "!=" or two_char == "<=" or two_char == ">=" or two_char == "&&" or two_char == "||" or two_char == "<<" or two_char == ">>" then
                    op_end = pos + 2
                end if
            end if
            let token = syntax_token {
                token_type: TOKEN_OPERATOR(),
                start_pos: pos,
                end_pos: op_end,
                color: get_token_color(TOKEN_OPERATOR())
            }
            tokens = tokens + [token]
            token_count = token_count + 1
            pos = op_end
            continue
        end if

        // Default case - advance one character
        pos = pos + 1
    end while

    return token_list { tokens: tokens, count: token_count }
end function

pub function highlight_line(line: text, handle: i32, x: i32, y: i32)
    let tokens = parse_line(line)
    let mut i = 0
    while i < tokens.count do
        let token = tokens.tokens[i]
        let text_segment = string_utils.substring(line, token.start_pos, token.end_pos)
        let segment_x = x + (token.start_pos * 8)  // Assuming 8px per character
        winplat::draw_text(handle, segment_x, y, text_segment, token.color)
        i = i + 1
    end while
end function

pub function get_line_highlight_color(line_num: i32) -> i32
    // Different background colors for different line types
    if line_num % 2 == 0 then
        return theme.editor_line_even()
    else
        return theme.editor_line_odd()
    end if
end function