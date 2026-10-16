module search_replace_test
# Test application for search and replace functionality
# Demonstrates text search, navigation, and replacement operations

import widget
import theme
import winplat
import string_utils
import search_replace

let mut window_handle: i32 = -1
let mut running: bool = true
let mut status_text: text = "Search and Replace Test - Use search dialog or shortcuts"

// Search dialog components
let mut search_dialog: widget::widget_data
let mut search_field: widget::widget_data
let mut replace_field: widget::widget_data
let mut find_button: widget::widget_data
let mut replace_button: widget::widget_data
let mut replace_all_button: widget::widget_data
let mut case_checkbox: widget::widget_data
let mut whole_word_checkbox: widget::widget_data
let mut results_label: widget::widget_data
let mut editor: widget::widget_data

// Search dialog visibility
let mut search_dialog_visible: bool = false

pub function main()
    window_handle = winplat.create_window("Search and Replace Test", 900, 700)
    if window_handle == -1 then
        print("Failed to create window")
        return
    end if

    theme.set_theme(theme.THEME_DARK())

    // Initialize search system
    search_replace.initialize_search()

    create_search_interface()
    create_test_editor()

    while running do
        process_events()
        render_frame()
        winplat.wait_events()
    end while

    winplat.destroy_window(window_handle)
end function

pub function create_search_interface()
    // Search dialog (centered)
    let dialog_x = 250
    let dialog_y = 80
    let dialog_w = 400
    let dialog_h = 180

    search_dialog = widget.create_panel(dialog_x, dialog_y, dialog_w, dialog_h, "", 4000)

    // Search field
    search_field = widget.create_text_field(dialog_x + 60, dialog_y + 25, 320, 25, "", 4001)

    // Replace field
    replace_field = widget.create_text_field(dialog_x + 60, dialog_y + 55, 320, 25, "", 4002)

    // Checkboxes
    case_checkbox = widget.create_checkbox(dialog_x + 60, dialog_y + 85, 120, 20, "Case sensitive", false, 4003)
    whole_word_checkbox = widget.create_checkbox(dialog_x + 200, dialog_y + 85, 120, 20, "Whole word", false, 4004)

    // Buttons
    find_button = widget.create_button(dialog_x + 60, dialog_y + 120, 70, 25, "Find Next", 4005)
    replace_button = widget.create_button(dialog_x + 140, dialog_y + 120, 70, 25, "Replace", 4006)
    replace_all_button = widget.create_button(dialog_x + 220, dialog_y + 120, 100, 25, "Replace All", 4007)

    // Results label
    results_label = widget.create_label(dialog_x + 60, dialog_y + 155, 320, 20, "Enter search text", 4008)

    // Hide dialog initially
    search_dialog.visible = false
    search_field.visible = false
    replace_field.visible = false
    case_checkbox.visible = false
    whole_word_checkbox.visible = false
    find_button.visible = false
    replace_button.visible = false
    replace_all_button.visible = false
    results_label.visible = false
end function

pub function create_test_editor()
    let sample_text = "# Search and Replace Test Editor\n\n# This editor contains sample text for testing search functionality.\n\nfunction hello_world()\n    print(\"Hello, world!\")\n    print(\"Hello again, world!\")\n    print(\"Another hello message\")\n    return true\nend function\n\n# Search terms to try:\n# - \"hello\" (multiple matches)\n# - \"function\" (single match)\n# - \"print\" (multiple matches)\n# - \"world\" (multiple matches)\n# - \"xyz\" (no matches)\n\n# Keyboard shortcuts:\n# - Ctrl+F: Show search dialog\n# - F3: Find next\n# - Shift+F3: Find previous\n# - Ctrl+H: Show replace dialog\n# - Escape: Close dialog\n\n# Features demonstrated:\n# - Case sensitive search\n# - Whole word search\n# - Navigation through matches\n# - Individual replace\n# - Replace all functionality\n# - Match highlighting in editor"

    editor = widget.create_text_editor(50, 300, 800, 350, sample_text, 1000)
end function

pub function process_events()
    let mut event = winplat::event { event_type: 0, key_code: 0, mouse_x: 0, mouse_y: 0 }

    while winplat.poll_event(event) do
        if event.event_type == winplat::EVENT_QUIT() then
            running = false
        elseif event.event_type == winplat::EVENT_MOUSE_PRESS() then
            handle_mouse_press(event.mouse_x, event.mouse_y)
        elseif event.event_type == winplat::EVENT_KEY_PRESS() then
            handle_key_press(event)
        end if
    end while
end function

pub function handle_mouse_press(mouse_x: i32, mouse_y: i32)
    // Handle search dialog interactions first
    if search_dialog_visible then
        if widget.check_widget_click(mouse_x, mouse_y, search_field) then
            // Text entered in search field
            update_search()
        elseif widget.check_widget_click(mouse_x, mouse_y, replace_field) then
            // Text entered in replace field
            status_text = "Replace text updated"
        elseif widget.check_widget_click(mouse_x, mouse_y, case_checkbox) then
            status_text = "Case sensitive option toggled"
        elseif widget.check_widget_click(mouse_x, mouse_y, whole_word_checkbox) then
            status_text = "Whole word option toggled"
        elseif widget.check_widget_click(mouse_x, mouse_y, find_button) then
            find_next_match()
        elseif widget.check_widget_click(mouse_x, mouse_y, replace_button) then
            replace_current_match()
        elseif widget.check_widget_click(mouse_x, mouse_y, replace_all_button) then
            replace_all_matches()
        end if
        return
    end if

    // Handle editor click
    widget.check_widget_click(mouse_x, mouse_y, editor)
end function

pub function handle_key_press(event: winplat::event)
    let focused_id = widget.get_focused_widget_id()

    // Handle search shortcuts
    if event.key_code == 0x10046 then  // Ctrl+F
        show_search_dialog()
    elseif event.key_code == 0x10043 then  // F3
        find_next_match()
    elseif event.key_code == 0x10042 then  // Shift+F3
        find_previous_match()
    elseif event.key_code == 0x10048 then  // Ctrl+H
        show_replace_dialog()
    elseif event.key_code == 0x1001B then  // Escape
        hide_search_dialog()
    end if

    // Route to focused widget
    if focused_id == search_field.id or focused_id == replace_field.id then
        widget.process_key_event(event, search_field)
    elseif focused_id == editor.id then
        widget.process_key_event(event, editor)
    end if

    // Handle theme toggle
    if event.key_code == 84 or event.key_code == 116 then  // T or t
        if theme.get_theme() == theme.THEME_DARK() then
            theme.set_theme(theme.THEME_LIGHT())
            status_text = "Switched to Light theme"
        else
            theme.set_theme(theme.THEME_DARK())
            status_text = "Switched to Dark theme"
        end if
    end if
end function

pub function show_search_dialog()
    search_dialog_visible = true
    search_dialog.visible = true
    search_field.visible = true
    replace_field.visible = true
    case_checkbox.visible = true
    whole_word_checkbox.visible = true
    find_button.visible = true
    replace_button.visible = true
    replace_all_button.visible = true
    results_label.visible = true

    // Focus search field
    widget.set_focused_widget_id(search_field.id)

    status_text = "Search dialog opened - Enter search text"
end function

pub function hide_search_dialog()
    search_dialog_visible = false
    search_dialog.visible = false
    search_field.visible = false
    replace_field.visible = false
    case_checkbox.visible = false
    whole_word_checkbox.visible = false
    find_button.visible = false
    replace_button.visible = false
    replace_all_button.visible = false
    results_label.visible = false

    status_text = "Search dialog closed"
end function

pub function update_search()
    let search_text = search_field.text
    let options = 0

    if case_checkbox.checked then
        options = options or search_replace.SEARCH_CASE_SENSITIVE()
    end if

    if whole_word_checkbox.checked then
        options = options or search_replace.SEARCH_WHOLE_WORD()
    end if

    search_replace.start_search(search_text, options)
    update_results_label()
end function

pub function find_next_match()
    if search_replace.is_search_active() then
        let match_index = search_replace.find_next()
        if match_index >= 0 then
            status_text = "Found match " + string_utils.int_to_text(match_index + 1) + " of " + string_utils.int_to_text(search_replace.get_match_count())
        else
            status_text = "No more matches found"
        end if
        update_results_label()
    end if
end function

pub function find_previous_match()
    if search_replace.is_search_active() then
        let match_index = search_replace.find_previous()
        if match_index >= 0 then
            status_text = "Found match " + string_utils.int_to_text(match_index + 1) + " of " + string_utils.int_to_text(search_replace.get_match_count())
        else
            status_text = "No more matches found"
        end if
        update_results_label()
    end if
end function

pub function replace_current_match()
    if search_replace.replace_current() then
        status_text = "Replaced current match"
        update_results_label()
    else
        status_text = "No match to replace"
    end if
end function

pub function replace_all_matches()
    let replaced_count = search_replace.replace_all()
    if replaced_count > 0 then
        status_text = "Replaced " + string_utils.int_to_text(replaced_count) + " matches"
    else
        status_text = "No matches to replace"
    end if
    update_results_label()
end function

pub function update_results_label()
    let mut result_text = "Enter search text"

    if search_replace.is_search_active() then
        if search_replace.get_match_count() > 0 then
            result_text = string_utils.int_to_text(search_replace.get_match_count()) + " matches found"
        else
            result_text = "No matches found"
        end if
    end if

    // Update label text (in a real implementation, this would update the widget)
    // results_label.text = result_text
end function

pub function render_frame()
    // Clear background
    winplat.fill_rect(window_handle, 0, 0, 900, 700, theme.bg_primary())

    // Draw title
    winplat.draw_text(window_handle, 20, 10, "Search and Replace Test", theme.text_primary())
    winplat.draw_text(window_handle, 20, 30, "Ctrl+F: Find | Ctrl+H: Replace | F3/Shift+F3: Navigate | Escape: Close", theme.text_secondary())

    // Draw editor
    widget.draw_widget(window_handle, editor)

    // Draw search dialog if visible
    if search_dialog_visible then
        search_replace.draw_search_dialog(window_handle, search_dialog, search_field, replace_field,
                                          find_button, replace_button, replace_all_button,
                                          case_checkbox, whole_word_checkbox, results_label)

        // Highlight current match in editor
        if search_replace.is_search_active() and search_replace.get_match_count() > 0 then
            let current_match = search_replace.get_current_match_index()
            if current_match >= 0 then
                // Draw highlight for current match (simplified)
                let match_pos = 100 + (current_match * 50)  // Dummy position for demo
                search_replace.highlight_search_matches(window_handle, editor, match_pos, match_pos + 30)
            end if
        end if
    end if

    // Draw status bar
    winplat.fill_rect(window_handle, 0, 670, 900, 30, theme.bg_status_bar())
    winplat.fill_rect(window_handle, 0, 670, 900, 1, theme.border_primary())
    winplat.draw_text(window_handle, 10, 675, status_text, theme.text_primary())

    winplat.present(window_handle)
end function