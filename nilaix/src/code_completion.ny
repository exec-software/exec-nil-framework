module code_completion
# Code completion system for NY programming language
# Provides intelligent code suggestions and auto-completion

import string_utils
import winplat
import theme
import language_server

// Completion item types
pub function COMPLETION_KEYWORD() -> i32
    return 0
end function

pub function COMPLETION_FUNCTION() -> i32
    return 1
end function

pub function COMPLETION_TYPE() -> i32
    return 2
end function

pub function COMPLETION_VARIABLE() -> i32
    return 3
end function

pub function COMPLETION_BUILTIN() -> i32
    return 4
end function

pub function COMPLETION_SNIPPET() -> i32
    return 5
end function

// Completion item structure
pub struct completion_item
    text: text
    item_type: i32
    description: text
    insert_text: text
end struct

// Completion list structure
pub struct completion_list
    items: array<completion_item>
    count: i32
    selected_index: i32
end struct

// Completion popup widget data
pub struct completion_popup
    visible: bool
    x: i32
    y: i32
    width: i32
    height: i32
    max_items: i32
    item_height: i32
    completions: completion_list
    trigger_text: text
    cursor_pos: i32
end struct

// Global completion popup
let mut popup_state: completion_popup

// NY language completion data
let mut completion_keywords: text = "if,then,elseif,else,end,while,do,function,return,module,import,export,public,private,pub,struct,let,mut,not,and,or,in,break,continue,for"

let mut completion_functions: text = "print,write,read,open,close,length,split,trim,index_of,replace,int_to_text,text_to_int,f64_to_text,text_to_f64,join,contains,char_at,char_code"

let mut completion_types: text = "i32,i64,f32,f64,text,bool,array"

let mut completion_builtins: text = "true,false,null"

let mut completion_snippets: text = "if,while,function,module,struct"

pub function initialize_completion()
    popup_state = completion_popup {
        visible: false,
        x: 0,
        y: 0,
        width: 300,
        height: 200,
        max_items: 10,
        item_height: 20,
        completions: completion_list { items: [], count: 0, selected_index: 0 },
        trigger_text: "",
        cursor_pos: 0
    }
end function

pub function get_completion_color(item_type: i32) -> i32
    if item_type == COMPLETION_KEYWORD() then
        return theme.syntax_keyword()
    elseif item_type == COMPLETION_FUNCTION() then
        return theme.syntax_function()
    elseif item_type == COMPLETION_TYPE() then
        return theme.syntax_type()
    elseif item_type == COMPLETION_VARIABLE() then
        return theme.text_primary()
    elseif item_type == COMPLETION_BUILTIN() then
        return theme.syntax_builtin()
    elseif item_type == COMPLETION_SNIPPET() then
        return theme.syntax_directive()
    else
        return theme.text_primary()
    end if
end function

pub function create_completion_item(text: text, item_type: i32, description: text, insert_text: text) -> completion_item
    let item = completion_item {
        text: text,
        item_type: item_type,
        description: description,
        insert_text: insert_text
    }
    return item
end function

pub function get_keyword_completions() -> array<completion_item>
    let mut items: array<completion_item> = []
    let parts = string_utils.split(completion_keywords, ",")
    let mut i = 0
    while i < parts.count do
        let keyword = string_utils.trim(parts.items[i])
        let item = create_completion_item(keyword, COMPLETION_KEYWORD(), "NY keyword", keyword)
        items = items + [item]
        i = i + 1
    end while
    return items
end function

pub function get_function_completions() -> array<completion_item>
    let mut items: array<completion_item> = []
    let parts = string_utils.split(completion_functions, ",")
    let mut i = 0
    while i < parts.count do
        let func_name = string_utils.trim(parts.items[i])
        let description = "Function: " + func_name + "()"
        let insert_text = func_name + "()"
        let item = create_completion_item(func_name, COMPLETION_FUNCTION(), description, insert_text)
        items = items + [item]
        i = i + 1
    end while
    return items
end function

pub function get_type_completions() -> array< completion_item>
    let mut items: array<completion_item> = []
    let parts = string_utils.split(completion_types, ",")
    let mut i = 0
    while i < parts.count do
        let type_name = string_utils.trim(parts.items[i])
        let description = "Type: " + type_name
        let item = create_completion_item(type_name, COMPLETION_TYPE(), description, type_name)
        items = items + [item]
        i = i + 1
    end while
    return items
end function

pub function get_builtin_completions() -> array<completion_item>
    let mut items: array<completion_item> = []
    let parts = string_utils.split(completion_builtins, ",")
    let mut i = 0
    while i < parts.count do
        let builtin = string_utils.trim(parts.items[i])
        let description = "Built-in: " + builtin
        let item = create_completion_item(builtin, COMPLETION_BUILTIN(), description, builtin)
        items = items + [item]
        i = i + 1
    end while
    return items
end function

pub function get_snippet_completions() -> array<completion_item>
    let mut items: array<completion_item> = []
    let parts = string_utils.split(completion_snippets, ",")
    let mut i = 0
    while i < parts.count do
        let snippet_name = string_utils.trim(parts.items[i])
        let mut insert_text = ""
        let mut description = ""

        if snippet_name == "if" then
            insert_text = "if  then\n    \nend if"
            description = "Snippet: if statement"
        elseif snippet_name == "while" then
            insert_text = "while  do\n    \nend while"
            description = "Snippet: while loop"
        elseif snippet_name == "function" then
            insert_text = "function () -> \n    \nend function"
            description = "Snippet: function definition"
        elseif snippet_name == "module" then
            insert_text = "module \n# Module description\n\nend module"
            description = "Snippet: module definition"
        elseif snippet_name == "struct" then
            insert_text = "struct \n    : \n    : \nend struct"
            description = "Snippet: struct definition"
        end if

        let item = create_completion_item(snippet_name, COMPLETION_SNIPPET(), description, insert_text)
        items = items + [item]
        i = i + 1
    end while
    return items
end function

pub function get_word_at_cursor(text: text, cursor_pos: i32) -> text
    if cursor_pos <= 0 or string_utils.length(text) == 0 then
        return ""
    end if

    let mut start = cursor_pos - 1
    let mut end_pos = cursor_pos

    // Find start of word (go backwards until non-word character)
    while start >= 0 do
        let char = string_utils.char_at(text, start)
        let code = string_utils.char_code(char)
        if not ((code >= 65 and code <= 90) or (code >= 97 and code <= 122) or (code >= 48 and code <= 57) or code == 95) then
            start = start + 1
            break
        end if
        start = start - 1
    end while

    if start < 0 then start = 0 end if

    // Find end of word (go forwards until non-word character)
    while end_pos < string_utils.length(text) do
        let char = string_utils.char_at(text, end_pos)
        let code = string_utils.char_code(char)
        if not ((code >= 65 and code <= 90) or (code >= 97 and code <= 122) or (code >= 48 and code <= 57) or code == 95) then
            break
        end if
        end_pos = end_pos + 1
    end while

    return string_utils.substring(text, start, end_pos)
end function

pub function filter_completions(all_items: array<completion_item>, filter: text) -> completion_list
    let mut filtered: array<completion_item> = []
    let mut count = 0
    let filter_len = string_utils.length(filter)

    if filter_len == 0 then
        // No filter - return all items
        filtered = all_items
        count = all_items.count
    else
        // Apply filter
        let mut i = 0
        while i < all_items.count do
            let item = all_items.items[i]
            if string_utils.contains(item.text, filter) then
                filtered = filtered + [item]
                count = count + 1
            end if
            i = i + 1
        end while
    end if

    return completion_list { items: filtered, count: count, selected_index: 0 }
end function

pub function trigger_completion(text: text, cursor_pos: i32, x: i32, y: i32)
    // Check for module-qualified completion (e.g., "string_utils.")
    let module_context = get_module_context(text, cursor_pos)
    let word = get_word_at_cursor(text, cursor_pos)

    let mut all_items: array<completion_item> = []

    if string_utils.length(module_context) > 0 then
        // Module-qualified completion - show only that module's symbols
        let module_symbols = language_server::get_module_completions(module_context)
        let mut i = 0
        while i < module_symbols.count do
            let sym = module_symbols.items[i]
            let item = create_completion_item(sym.name, COMPLETION_FUNCTION(), sym.signature, sym.name)
            all_items = all_items + [item]
            i = i + 1
        end while
    else
        // General completion - show keywords, functions, types, etc.
        all_items = all_items + get_keyword_completions()
        all_items = all_items + get_function_completions()
        all_items = all_items + get_type_completions()
        all_items = all_items + get_builtin_completions()
        all_items = all_items + get_snippet_completions()

        // Add known module names for import suggestions
        let module_names = language_server::get_known_module_names()
        let mut j = 0
        while j < module_names.count do
            let mod_name = module_names.items[j]
            let desc = string_utils.concat("Module: ", mod_name)
            let item = create_completion_item(mod_name, COMPLETION_TYPE(), desc, mod_name)
            all_items = all_items + [item]
            j = j + 1
        end while

        // Add symbol completions from language server (current buffer)
        let symbols = language_server::get_symbols()
        let mut i = 0
        while i < symbols.count do
            let sym = symbols.items[i]
            let item = create_completion_item(sym, COMPLETION_FUNCTION(), "Buffer symbol", sym)
            all_items = all_items + [item]
            i = i + 1
        end while
    end if

    // Filter completions by current word
    popup_state.completions = filter_completions(all_items, word)
    popup_state.trigger_text = word
    popup_state.cursor_pos = cursor_pos
    popup_state.x = x
    popup_state.y = y

    // Only show popup if we have completions
    if popup_state.completions.count > 0 then
        popup_state.visible = true
        // Calculate popup height based on item count
        let mut visible_items = popup_state.completions.count
        if visible_items > popup_state.max_items then
            visible_items = popup_state.max_items
        end if
        popup_state.height = visible_items * popup_state.item_height + 4
    else
        popup_state.visible = false
    end if
end function

// Get module context if cursor is after "module_name."
pub function get_module_context(text: text, cursor_pos: i32) -> text
    if cursor_pos <= 0 then
        return ""
    end if

    // Look backwards for a dot
    let mut dot_pos = cursor_pos - 1
    while dot_pos >= 0 do
        let ch = string_utils.char_at(text, dot_pos)
        if ch == "." then
            // Found a dot - extract the word before it
            let mut word_start = dot_pos - 1
            while word_start >= 0 do
                let c = string_utils.char_at(text, word_start)
                let code = string_utils.char_code(c)
                if not ((code >= 65 and code <= 90) or (code >= 97 and code <= 122) or (code >= 48 and code <= 57) or code == 95) then
                    word_start = word_start + 1
                    break
                end if
                word_start = word_start - 1
            end while
            if word_start < 0 then
                word_start = 0
            end if
            let module_name = string_utils.substring(text, word_start, dot_pos)
            // Check if it's a known module
            if language_server::is_known_module(module_name) then
                return module_name
            end if
            return ""
        elseif ch == " " or ch == "\n" or ch == "\t" or ch == "(" or ch == ")" or ch == "," then
            // Stop searching - not a module context
            return ""
        end if
        dot_pos = dot_pos - 1
    end while
    return ""
end function

// Check if we should auto-trigger completion based on typed character
pub function should_auto_trigger(text: text, cursor_pos: i32, typed_char: text) -> bool
    // Trigger on dot for module-qualified completion
    if typed_char == "." then
        // Check if the word before the dot is a known module
        let module_name = get_word_before_cursor(text, cursor_pos - 1)
        return language_server::is_known_module(module_name)
    end if

    // Trigger after typing 2+ characters of a word
    let word = get_word_at_cursor(text, cursor_pos)
    if string_utils.length(word) >= 2 then
        return true
    end if

    return false
end function

// Get the word immediately before cursor position
pub function get_word_before_cursor(text: text, cursor_pos: i32) -> text
    if cursor_pos <= 0 then
        return ""
    end if

    let mut end_pos = cursor_pos
    let mut start = cursor_pos - 1

    // Find start of word
    while start >= 0 do
        let ch = string_utils.char_at(text, start)
        let code = string_utils.char_code(ch)
        if not ((code >= 65 and code <= 90) or (code >= 97 and code <= 122) or (code >= 48 and code <= 57) or code == 95) then
            start = start + 1
            break
        end if
        start = start - 1
    end while

    if start < 0 then
        start = 0
    end if

    return string_utils.substring(text, start, end_pos)
end function

pub function hide_completion()
    popup_state.visible = false
end function

pub function is_completion_visible() -> bool
    return popup_state.visible
end function

pub function select_next_completion()
    if popup_state.visible and popup_state.completions.count > 0 then
        popup_state.completions.selected_index = (popup_state.completions.selected_index + 1) % popup_state.completions.count
    end if
end function

pub function select_previous_completion()
    if popup_state.visible and popup_state.completions.count > 0 then
        popup_state.completions.selected_index = popup_state.completions.selected_index - 1
        if popup_state.completions.selected_index < 0 then
            popup_state.completions.selected_index = popup_state.completions.count - 1
        end if
    end if
end function

pub function get_selected_completion() -> completion_item
    if popup_state.visible and popup_state.completions.count > 0 and popup_state.completions.selected_index >= 0 then
        return popup_state.completions.items[popup_state.completions.selected_index]
    end if
    return completion_item { text: "", item_type: 0, description: "", insert_text: "" }
end function

pub function apply_completion(text: text, cursor_pos: i32) -> text
    let selected = get_selected_completion()
    if string_utils.length(selected.text) > 0 then
        let word = get_word_at_cursor(text, cursor_pos)
        let word_start = cursor_pos - string_utils.length(word)
        let before = string_utils.substring(text, 0, word_start)
        let after = string_utils.substring(text, cursor_pos, string_utils.length(text))
        return before + selected.insert_text + after
    end if
    return text
end function

pub function draw_completion_popup(handle: i32)
    if not popup_state.visible then
        return
    end if

    // Draw popup background
    winplat.fill_rect(handle, popup_state.x, popup_state.y, popup_state.width, popup_state.height, theme.popup_bg())
    winplat.fill_rect(handle, popup_state.x, popup_state.y, popup_state.width, 1, theme.border_primary())
    winplat.fill_rect(handle, popup_state.x, popup_state.y + popup_state.height - 1, popup_state.width, 1, theme.border_primary())
    winplat.fill_rect(handle, popup_state.x, popup_state.y, 1, popup_state.height, theme.border_primary())
    winplat.fill_rect(handle, popup_state.x + popup_state.width - 1, popup_state.y, 1, popup_state.height, theme.border_primary())

    // Draw completion items
    let mut i = 0
    let mut max_display = popup_state.completions.count
    if max_display > popup_state.max_items then
        max_display = popup_state.max_items
    end if

    while i < max_display do
        let item = popup_state.completions.items[i]
        let item_y = popup_state.y + 2 + (i * popup_state.item_height)

        // Highlight selected item
        if i == popup_state.completions.selected_index then
            winplat.fill_rect(handle, popup_state.x + 1, item_y, popup_state.width - 2, popup_state.item_height, theme.listbox_selected_bg())
        end if

        // Draw item text
        let color = get_completion_color(item.item_type)
        winplat.draw_text(handle, popup_state.x + 6, item_y + 2, item.text, color)

        // Draw description (if space allows)
        if popup_state.width > 150 then
            let desc_x = popup_state.x + 100
            winplat.draw_text(handle, desc_x, item_y + 2, item.description, theme.text_secondary())
        end if

        i = i + 1
    end while
end function