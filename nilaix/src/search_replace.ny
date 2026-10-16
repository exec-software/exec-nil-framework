module search_replace
# Search and replace system for text editor
# Supports text search, regex patterns, and replacement operations

import widget
import theme
import winplat
import string_utils

// Search options
pub function SEARCH_CASE_SENSITIVE() -> i32
    return 1
end function

pub function SEARCH_WHOLE_WORD() -> i32
    return 2
end function

pub function SEARCH_REGEX() -> i32
    return 4
end function

pub function SEARCH_BACKWARDS() -> i32
    return 8
end function

// Search state
pub struct search_state
    search_text: text
    replace_text: text
    options: i32
    current_match: i32
    total_matches: array<i32>
    match_count: i32
    is_active: bool
    show_replace: bool
end struct

// Search result
pub struct search_result
    start_pos: i32
    end_pos: i32
    line: i32
    column: i32
    matched_text: text
end struct

// Global search state
let mut current_search: search_state

pub function initialize_search()
    current_search = search_state {
        search_text: "",
        replace_text: "",
        options: 0,
        current_match: -1,
        total_matches: [],
        match_count: 0,
        is_active: false,
        show_replace: false
    }
end function

pub function start_search(search_text: text, options: i32)
    current_search.search_text = search_text
    current_search.options = options
    current_search.is_active = true
    current_search.current_match = -1
    current_search.total_matches = []
    current_search.match_count = 0

    if string_utils.length(search_text) > 0 then
        perform_search()
    end if
end function

pub function start_replace(search_text: text, replace_text: text, options: i32)
    current_search.search_text = search_text
    current_search.replace_text = replace_text
    current_search.options = options
    current_search.show_replace = true
    current_search.is_active = true
    current_search.current_match = -1
    current_search.total_matches = []
    current_search.match_count = 0

    if string_utils.length(search_text) > 0 then
        perform_search()
    end if
end function

pub function stop_search()
    current_search.is_active = false
    current_search.current_match = -1
    current_search.match_count = 0
    current_search.total_matches = []
end function

pub function is_search_active() -> bool
    return current_search.is_active
end function

pub function get_search_text() -> text
    return current_search.search_text
end function

pub function get_replace_text() -> text
    return current_search.replace_text
end function

pub function get_match_count() -> i32
    return current_search.match_count
end function

pub function get_current_match_index() -> i32
    return current_search.current_match
end function

pub function perform_search()
    if string_utils.length(current_search.search_text) == 0 then
        current_search.total_matches = []
        current_search.match_count = 0
        current_search.current_match = -1
        return
    end if

    // Clear previous matches
    current_search.total_matches = []
    current_search.match_count = 0

    // Perform search based on options
    if (current_search.options and SEARCH_REGEX()) != 0 then
        perform_regex_search()
    else
        perform_text_search()
    end if

    // Set current match to first result
    if current_search.match_count > 0 then
        current_search.current_match = 0
    else
        current_search.current_match = -1
    end if
end function

pub function perform_text_search()
    // Simple text search implementation
    // In a real implementation, this would search the editor content
    // For now, we'll create dummy matches for demonstration
    let mut dummy_matches = [100, 200, 300, 400, 500]  // Dummy positions

    let mut i = 0
    while i < dummy_matches.count do
        current_search.total_matches = current_search.total_matches + [dummy_matches.items[i]]
        i = i + 1
    end while

    current_search.match_count = dummy_matches.count
end function

pub function perform_regex_search()
    // Regex search implementation placeholder
    // In a real implementation, this would use regex patterns
    let mut regex_matches = [150, 250, 350, 450, 550]  // Dummy regex positions

    let mut i = 0
    while i < regex_matches.count do
        current_search.total_matches = current_search.total_matches + [regex_matches.items[i]]
        i = i + 1
    end while

    current_search.match_count = regex_matches.count
end function

pub function find_next() -> i32
    if not current_search.is_active or current_search.match_count == 0 then
        return -1
    end if

    if current_search.current_match < current_search.match_count - 1 then
        current_search.current_match = current_search.current_match + 1
    else
        current_search.current_match = 0  // Wrap around
    end if

    return current_search.current_match
end function

pub function find_previous() -> i32
    if not current_search.is_active or current_search.match_count == 0 then
        return -1
    end if

    if current_search.current_match > 0 then
        current_search.current_match = current_search.current_match - 1
    else
        current_search.current_match = current_search.match_count - 1  // Wrap around
    end if

    return current_search.current_match
end function

pub function replace_current() -> bool
    if not current_search.is_active or current_search.show_replace then
        return false
    end if

    if current_search.current_match >= 0 and current_search.current_match < current_search.match_count then
        // In a real implementation, this would replace the current match
        // For now, we'll simulate a successful replacement
        return true
    end if

    return false
end function

pub function replace_all() -> i32
    if not current_search.is_active or current_search.show_replace then
        return 0
    end if

    let mut replaced_count = 0

    // In a real implementation, this would replace all matches
    // For now, we'll simulate replacing all matches
    replaced_count = current_search.match_count

    if replaced_count > 0 then
        current_search.match_count = 0
        current_search.current_match = -1
    end if

    return replaced_count
end function

// Search dialog widget creation
pub function create_search_dialog(x: i32, y: i32, width: i32, height: i32) -> widget_data
    let dialog = widget.create_panel(x, y, width, height, "Find and Replace", 4000)
    return dialog
end function

pub function create_search_input_field(x: i32, y: i32, width: i32, height: i32, text: text, id: i32) -> widget_data
    return widget.create_text_field(x, y, width, height, text, id)
end function

pub function create_replace_input_field(x: i32, y: i32, width: i32, height: i32, text: text, id: i32) -> widget_data
    return widget.create_text_field(x, y, width, height, text, id)
end function

pub function create_search_button(x: i32, y: i32, width: i32, height: i32, text: text, id: i32) -> widget_data
    return widget.create_button(x, y, width, height, text, id)
end function

pub function create_search_checkbox(x: i32, y: i32, width: i32, height: i32, text: text, checked: bool, id: i32) -> widget_data
    return widget.create_checkbox(x, y, width, height, text, checked, id)
end function

pub function draw_search_dialog(handle: i32, dialog: widget_data,
                           search_field: widget::widget_data, replace_field: widget::widget_data,
                           find_button: widget::widget_data, replace_button: widget::widget_data,
                           replace_all_button: widget::widget_data,
                           case_checkbox: widget::widget_data, whole_word_checkbox: widget::widget_data,
                           results_label: widget::widget_data)
    // Draw dialog background
    widget.draw_widget(handle, dialog)

    // Draw dialog title
    winplat.draw_text(handle, dialog.x + 10, dialog.y + 5, "Find and Replace", theme.text_primary())

    // Draw search section
    winplat.draw_text(handle, dialog.x + 10, dialog.y + 30, "Find:", theme.text_secondary())
    widget.draw_widget(handle, search_field)

    // Draw replace section
    winplat.draw_text(handle, dialog.x + 10, dialog.y + 60, "Replace:", theme.text_secondary())
    widget.draw_widget(handle, replace_field)

    // Draw options
    widget.draw_widget(handle, case_checkbox)
    winplat.draw_text(handle, dialog.x + 150, dialog.y + 90, "Case sensitive", theme.text_secondary())

    widget.draw_widget(handle, whole_word_checkbox)
    winplat.draw_text(handle, dialog.x + 150, dialog.y + 115, "Whole word", theme.text_secondary())

    // Draw buttons
    widget.draw_widget(handle, find_button)
    widget.draw_widget(handle, replace_button)
    widget.draw_widget(handle, replace_all_button)

    // Draw results
    widget.draw_widget(handle, results_label)
end function

pub function highlight_search_matches(handle: i32, editor: widget::widget_data, start_pos: i32, end_pos: i32)
    if not is_search_active() or get_match_count() == 0 then
        return
    end if

    // Draw highlight overlay for the current match
    // In a real implementation, this would highlight the matched text in the editor
    let highlight_color = theme.text_selection_bg()  // Yellow highlight for matches
    let highlight_alpha = 0x40FFFF00  // Semi-transparent yellow

    // Draw a simple rectangle to indicate the highlight area
    winplat.fill_rect(handle, editor.x + start_pos, editor.y + 50, end_pos - start_pos, 20, highlight_alpha)

    // Draw match information
    let match_text = "Match " + string_utils.int_to_text(get_current_match_index() + 1) + " of " + string_utils.int_to_text(get_match_count())
    winplat.draw_text(handle, editor.x, editor.y + 30, match_text, theme.text_secondary())
end function

pub function update_search_result_text(results_label: widget::widget_data)
    let mut result_text = "No matches found"

    if is_search_active() then
        if get_match_count() > 0 then
            result_text = string_utils.int_to_text(get_match_count()) + " matches found"
        else
            result_text = "No matches found"
        end if
    else
        result_text = "Enter search text"
    end if

    // Update the label's text (in a real implementation)
    // results_label.text = result_text
end function