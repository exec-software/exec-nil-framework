module language_server
# Language Server Protocol for NY Programming Language
# Provides code completion, Go to Definition, and error diagnostics

import string_utils
import winplat
import theme

// Symbol types
pub function SYM_FUNCTION() -> i32
    return 1
end function

pub function SYM_VARIABLE() -> i32
    return 2
end function

pub function SYM_TYPE() -> i32
    return 3
end function

pub function SYM_MODULE() -> i32
    return 4
end function

pub function SYM_STRUCT() -> i32
    return 5
end function

pub function SYM_ENUM() -> i32
    return 6
end function

// Symbol structure
pub struct symbol_info
    name: text
    symbol_type: i32
    line: i32
    column: i32
    scope: text
    definition: text
    signature: text
    documentation: text
end struct

// Module export structure for module-qualified completions
pub struct module_export
    module_name: text
    symbols: array<symbol_info>
    symbol_count: i32
end struct

// Symbol table (global and per-buffer)
let mut global_symbols: array<symbol_info> = []
let mut buffer_symbols: array<symbol_info> = []
let mut imported_modules: array<text> = []

// Known module exports (built-in knowledge of standard library)
let mut known_modules: array<module_export> = []

// Error diagnostics
pub struct diagnostic
    line: i32
    column: i32
    severity: i32
    message: text
    code: text
end struct

let mut diagnostics: array<diagnostic> = []

// Initialize language server
pub function initialize_server()
    global_symbols = []
    buffer_symbols = []
    imported_modules = []
    diagnostics = []
    initialize_known_modules()
end function

// Initialize known module exports (standard library)
pub function initialize_known_modules()
    known_modules = []

    // string_utils module
    let mut su_symbols: array<symbol_info> = []
    su_symbols = su_symbols + [create_symbol("substring", SYM_FUNCTION(), "substring(s: text, start: i32, end: i32) -> text", "Extract substring")]
    su_symbols = su_symbols + [create_symbol("length", SYM_FUNCTION(), "length(s: text) -> i32", "Get string length")]
    su_symbols = su_symbols + [create_symbol("char_at", SYM_FUNCTION(), "char_at(s: text, index: i32) -> text", "Get character at index")]
    su_symbols = su_symbols + [create_symbol("index_of", SYM_FUNCTION(), "index_of(s: text, needle: text) -> i32", "Find substring position")]
    su_symbols = su_symbols + [create_symbol("contains", SYM_FUNCTION(), "contains(s: text, needle: text) -> bool", "Check if contains substring")]
    su_symbols = su_symbols + [create_symbol("trim", SYM_FUNCTION(), "trim(s: text) -> text", "Remove leading/trailing whitespace")]
    su_symbols = su_symbols + [create_symbol("split", SYM_FUNCTION(), "split(s: text, delimiter: text) -> split_result", "Split string by delimiter")]
    su_symbols = su_symbols + [create_symbol("starts_with", SYM_FUNCTION(), "starts_with(s: text, prefix: text) -> bool", "Check if starts with prefix")]
    su_symbols = su_symbols + [create_symbol("ends_with", SYM_FUNCTION(), "ends_with(s: text, suffix: text) -> bool", "Check if ends with suffix")]
    su_symbols = su_symbols + [create_symbol("to_upper", SYM_FUNCTION(), "to_upper(s: text) -> text", "Convert to uppercase")]
    su_symbols = su_symbols + [create_symbol("to_lower", SYM_FUNCTION(), "to_lower(s: text) -> text", "Convert to lowercase")]
    su_symbols = su_symbols + [create_symbol("int_to_text", SYM_FUNCTION(), "int_to_text(value: i32) -> text", "Convert integer to string")]
    su_symbols = su_symbols + [create_symbol("text_to_int", SYM_FUNCTION(), "text_to_int(s: text) -> i32", "Convert string to integer")]
    su_symbols = su_symbols + [create_symbol("char_code", SYM_FUNCTION(), "char_code(c: text) -> i32", "Get ASCII code of character")]
    su_symbols = su_symbols + [create_symbol("concat", SYM_FUNCTION(), "concat(s1: text, s2: text) -> text", "Concatenate strings")]
    su_symbols = su_symbols + [create_symbol("pad_left", SYM_FUNCTION(), "pad_left(s: text, width: i32, pad: text) -> text", "Pad string on left")]
    su_symbols = su_symbols + [create_symbol("pad_right", SYM_FUNCTION(), "pad_right(s: text, width: i32, pad: text) -> text", "Pad string on right")]
    su_symbols = su_symbols + [create_symbol("repeat_char", SYM_FUNCTION(), "repeat_char(c: text, count: i32) -> text", "Repeat character")]
    known_modules = known_modules + [module_export { module_name: "string_utils", symbols: su_symbols, symbol_count: su_symbols.count }]

    // winplat module
    let mut wp_symbols: array<symbol_info> = []
    wp_symbols = wp_symbols + [create_symbol("create_window", SYM_FUNCTION(), "create_window(title: text, w: i32, h: i32) -> i32", "Create window")]
    wp_symbols = wp_symbols + [create_symbol("show_window", SYM_FUNCTION(), "show_window(handle: i32)", "Show window")]
    wp_symbols = wp_symbols + [create_symbol("destroy_window", SYM_FUNCTION(), "destroy_window(handle: i32)", "Destroy window")]
    wp_symbols = wp_symbols + [create_symbol("fill_rect", SYM_FUNCTION(), "fill_rect(h: i32, x: i32, y: i32, w: i32, h: i32, color: i32)", "Fill rectangle")]
    wp_symbols = wp_symbols + [create_symbol("draw_text", SYM_FUNCTION(), "draw_text(h: i32, x: i32, y: i32, text: text, color: i32)", "Draw text")]
    wp_symbols = wp_symbols + [create_symbol("get_next_event", SYM_FUNCTION(), "get_next_event() -> event_data", "Get next input event")]
    wp_symbols = wp_symbols + [create_symbol("begin_draw", SYM_FUNCTION(), "begin_draw(handle: i32) -> i32", "Begin drawing")]
    wp_symbols = wp_symbols + [create_symbol("end_draw", SYM_FUNCTION(), "end_draw(dc: i32)", "End drawing")]
    wp_symbols = wp_symbols + [create_symbol("clear_window", SYM_FUNCTION(), "clear_window(dc: i32, color: i32)", "Clear window")]
    wp_symbols = wp_symbols + [create_symbol("read_file", SYM_FUNCTION(), "read_file(path: text) -> text", "Read file contents")]
    wp_symbols = wp_symbols + [create_symbol("write_file", SYM_FUNCTION(), "write_file(path: text, content: text) -> bool", "Write file")]
    wp_symbols = wp_symbols + [create_symbol("list_directory", SYM_FUNCTION(), "list_directory(path: text) -> text", "List directory entries")]
    known_modules = known_modules + [module_export { module_name: "winplat", symbols: wp_symbols, symbol_count: wp_symbols.count }]

    // theme module
    let mut th_symbols: array<symbol_info> = []
    th_symbols = th_symbols + [create_symbol("set_theme", SYM_FUNCTION(), "set_theme(theme_id: i32)", "Set current theme")]
    th_symbols = th_symbols + [create_symbol("get_theme", SYM_FUNCTION(), "get_theme() -> i32", "Get current theme")]
    th_symbols = th_symbols + [create_symbol("THEME_DARK", SYM_FUNCTION(), "THEME_DARK() -> i32", "Dark theme constant")]
    th_symbols = th_symbols + [create_symbol("THEME_LIGHT", SYM_FUNCTION(), "THEME_LIGHT() -> i32", "Light theme constant")]
    th_symbols = th_symbols + [create_symbol("bg_primary", SYM_FUNCTION(), "bg_primary() -> i32", "Primary background color")]
    th_symbols = th_symbols + [create_symbol("text_primary", SYM_FUNCTION(), "text_primary() -> i32", "Primary text color")]
    th_symbols = th_symbols + [create_symbol("syntax_keyword", SYM_FUNCTION(), "syntax_keyword() -> i32", "Keyword syntax color")]
    th_symbols = th_symbols + [create_symbol("syntax_function", SYM_FUNCTION(), "syntax_function() -> i32", "Function syntax color")]
    th_symbols = th_symbols + [create_symbol("syntax_string", SYM_FUNCTION(), "syntax_string() -> i32", "String syntax color")]
    th_symbols = th_symbols + [create_symbol("syntax_comment", SYM_FUNCTION(), "syntax_comment() -> i32", "Comment syntax color")]
    known_modules = known_modules + [module_export { module_name: "theme", symbols: th_symbols, symbol_count: th_symbols.count }]

    // widget module
    let mut wd_symbols: array<symbol_info> = []
    wd_symbols = wd_symbols + [create_symbol("create_button", SYM_FUNCTION(), "create_button(x, y, w, h: i32, label: text, id: i32) -> widget_data", "Create button widget")]
    wd_symbols = wd_symbols + [create_symbol("create_text_editor", SYM_FUNCTION(), "create_text_editor(x, y, w, h: i32, text: text, id: i32) -> widget_data", "Create text editor")]
    wd_symbols = wd_symbols + [create_symbol("create_tabbar", SYM_FUNCTION(), "create_tabbar(x, y, w, h: i32, labels: text, id: i32) -> widget_data", "Create tab bar")]
    wd_symbols = wd_symbols + [create_symbol("create_treeview", SYM_FUNCTION(), "create_treeview(x, y, w, h: i32, data: text, id: i32) -> widget_data", "Create tree view")]
    wd_symbols = wd_symbols + [create_symbol("create_statusbar", SYM_FUNCTION(), "create_statusbar(x, y, w, h: i32, text: text, id: i32) -> widget_data", "Create status bar")]
    wd_symbols = wd_symbols + [create_symbol("draw_widget", SYM_FUNCTION(), "draw_widget(dc: i32, w: widget_data)", "Draw widget")]
    wd_symbols = wd_symbols + [create_symbol("process_key_event", SYM_FUNCTION(), "process_key_event(event, widget) -> bool", "Process key event")]
    wd_symbols = wd_symbols + [create_symbol("process_mouse_click_event", SYM_FUNCTION(), "process_mouse_click_event(event, widget) -> bool", "Process mouse click")]
    known_modules = known_modules + [module_export { module_name: "widget", symbols: wd_symbols, symbol_count: wd_symbols.count }]
end function

// Helper to create a symbol_info
pub function create_symbol(name: text, sym_type: i32, signature: text, doc: text) -> symbol_info
    return symbol_info {
        name: name,
        symbol_type: sym_type,
        line: 0,
        column: 0,
        scope: "module",
        definition: "",
        signature: signature,
        documentation: doc
    }
end function

// Get module completions for a specific module
pub function get_module_completions(module_name: text) -> array<symbol_info>
    let mut i = 0
    while i < known_modules.count do
        let module_info = known_modules.items[i]
        if module_info.module_name == module_name then
            return module_info.symbols
        end if
        i = i + 1
    end while
    return []
end function

// Check if a module is known
pub function is_known_module(module_name: text) -> bool
    let mut i = 0
    while i < known_modules.count do
        if known_modules.items[i].module_name == module_name then
            return true
        end if
        i = i + 1
    end while
    return false
end function

// Get list of all known module names
pub function get_known_module_names() -> array<text>
    let mut names: array<text> = []
    let mut i = 0
    while i < known_modules.count do
        names = names + [known_modules.items[i].module_name]
        i = i + 1
    end while
    return names
end function

// Parse function definition
pub function parse_function(line_text: text, line_num: i32)
    let mut symbol = symbol_info {
        name: "",
        symbol_type: SYM_FUNCTION(),
        line: line_num,
        column: 0,
        scope: "global",
        definition: line_text,
        signature: "",
        documentation: ""
    }

    // Pattern: function name(params) -> return_type
    let func_pos = string_utils.index_of(line_text, "function ")
    if func_pos >= 0 then
        let after_func = string_utils.substring(line_text, func_pos + 9, string_utils.length(line_text))
        let paren_pos = string_utils.index_of(after_func, "(")
        if paren_pos >= 0 then
            symbol.name = string_utils.trim(string_utils.substring(after_func, 0, paren_pos))
            symbol.signature = string_utils.trim(after_func)
            symbol.column = func_pos
            buffer_symbols = buffer_symbols + [symbol]
        end if
    end if
end function

// Parse struct definition
pub function parse_struct(line_text: text, line_num: i32)
    let mut symbol = symbol_info {
        name: "",
        symbol_type: SYM_STRUCT(),
        line: line_num,
        column: 0,
        scope: "global",
        definition: line_text,
        signature: "",
        documentation: ""
    }

    // Pattern: struct name or pub struct name
    let struct_pos = string_utils.index_of(line_text, "struct ")
    let mut pub_pos = string_utils.index_of(line_text, "pub struct ")

    if pub_pos >= 0 then
        let after_struct = string_utils.substring(line_text, pub_pos + 11, string_utils.length(line_text))
        let brace_pos = string_utils.index_of(after_struct, "{")
        if brace_pos >= 0 then
            symbol.name = string_utils.trim(string_utils.substring(after_struct, 0, brace_pos))
            symbol.column = pub_pos
            buffer_symbols = buffer_symbols + [symbol]
        end if
    elseif struct_pos >= 0 then
        let after_struct = string_utils.substring(line_text, struct_pos + 7, string_utils.length(line_text))
        let brace_pos = string_utils.index_of(after_struct, "{")
        if brace_pos >= 0 then
            symbol.name = string_utils.trim(string_utils.substring(after_struct, 0, brace_pos))
            symbol.column = struct_pos
            buffer_symbols = buffer_symbols + [symbol]
        end if
    end if
end function

// Parse module definition
pub function parse_module(line_text: text, line_num: i32)
    let mut symbol = symbol_info {
        name: "",
        symbol_type: SYM_MODULE(),
        line: line_num,
        column: 0,
        scope: "global",
        definition: line_text,
        signature: "",
        documentation: ""
    }

    // Pattern: module name
    let module_pos = string_utils.index_of(line_text, "module ")
    if module_pos >= 0 then
        let after_module = string_utils.substring(line_text, module_pos + 7, string_utils.length(line_text))
        let comment_pos = string_utils.index_of(after_module, "#")
        if comment_pos >= 0 then
            symbol.name = string_utils.trim(string_utils.substring(after_module, 0, comment_pos))
        else
            symbol.name = string_utils.trim(after_module)
        end if
        symbol.column = module_pos
        buffer_symbols = buffer_symbols + [symbol]
    end if
end function

// Parse let/mut variable declarations
pub function parse_variable(line_text: text, line_num: i32)
    let mut symbol = symbol_info {
        name: "",
        symbol_type: SYM_VARIABLE(),
        line: line_num,
        column: 0,
        scope: "local",
        definition: line_text,
        signature: "",
        documentation: ""
    }

    // Pattern: let name: type or let mut name: type
    let let_pos = string_utils.index_of(line_text, "let ")
    if let_pos >= 0 then
        let after_let = string_utils.substring(line_text, let_pos + 4, string_utils.length(line_text))
        let colon_pos = string_utils.index_of(after_let, ":")
        if colon_pos >= 0 then
            let before_colon = string_utils.trim(string_utils.substring(after_let, 0, colon_pos))
            let mut name = before_colon
            let is_mut = string_utils.contains(before_colon, "mut")
            if is_mut then
                let space_pos = string_utils.index_of(before_colon, " ")
                if space_pos >= 0 then
                    name = string_utils.substring(before_colon, space_pos + 1, string_utils.length(before_colon))
                end if
            end if
            symbol.name = name
            symbol.column = let_pos

            // Extract type
            let after_colon = string_utils.substring(after_let, colon_pos + 1, string_utils.length(after_let))
            let eq_pos = string_utils.index_of(after_colon, "=")
            if eq_pos >= 0 then
                symbol.signature = string_utils.trim(string_utils.substring(after_colon, 0, eq_pos))
            else
                symbol.signature = string_utils.trim(after_colon)
            end if

            buffer_symbols = buffer_symbols + [symbol]
        end if
    end if
end function

// Parse all symbols in a buffer
pub function parse_buffer(content: text)
    buffer_symbols = []

    let lines = string_utils.split(content, "\n")
    let mut i = 0
    while i < lines.count do
        let line = string_utils.trim(lines.items[i])
        if string_utils.length(line) > 0 then
            if string_utils.index_of(line, "module ") == 0 then
                parse_module(line, i)
            elseif string_utils.index_of(line, "function ") >= 0 or string_utils.index_of(line, "pub function ") >= 0 then
                parse_function(line, i)
            elseif string_utils.index_of(line, "struct ") >= 0 or string_utils.index_of(line, "pub struct ") >= 0 then
                parse_struct(line, i)
            elseif string_utils.index_of(line, "let ") >= 0 then
                parse_variable(line, i)
            end if
        end if
        i = i + 1
    end while
end function

// Get symbols for code completion (returns array of text for now to avoid circular dependency)
pub function get_symbols() -> array<text>
    let mut names: array<text> = []
    let mut i = 0
    while i < buffer_symbols.count do
        let sym = buffer_symbols.items[i]
        names = names + [sym.name]
        i = i + 1
    end while
    return names
end function

// Go to Definition
pub function go_to_definition(word: text, line_num: i32) -> symbol_info
    let mut result = symbol_info {
        name: "",
        symbol_type: 0,
        line: -1,
        column: 0,
        scope: "",
        definition: "",
        signature: "",
        documentation: ""
    }

    let mut i = 0
    while i < buffer_symbols.count do
        let sym = buffer_symbols.items[i]
        if sym.name == word then
            result = sym
            break
        end if
        i = i + 1
    end while

    return result
end function

// Add diagnostic
pub function add_diagnostic(line: i32, column: i32, severity: i32, message: text, code: text)
    let diag = diagnostic {
        line: line,
        column: column,
        severity: severity,
        message: message,
        code: code
    }
    diagnostics = diagnostics + [diag]
end function

// Clear diagnostics
pub function clear_diagnostics()
    diagnostics = []
end function

// Get all diagnostics
pub function get_diagnostics() -> array<diagnostic>
    return diagnostics
end function

