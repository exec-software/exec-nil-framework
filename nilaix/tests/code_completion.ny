module code_completion_test
# Test application for code completion system
# Demonstrates intelligent code suggestions and auto-completion

import widget
import theme
import winplat
import string_utils
import code_completion

let mut window_handle: i32 = -1
let mut running: bool = true
let mut text_editor: widget::widget_data
let mut status_text: text = "Code Completion Test - Type to see suggestions"

pub function main()
    window_handle = winplat.create_window("Code Completion Test", 800, 600)
    if window_handle == -1 then
        print("Failed to create window")
        return
    end if

    theme.set_theme(theme.THEME_DARK())

    // Initialize code completion system
    code_completion.initialize_completion()

    // Create text editor with code completion enabled
    let initial_content = "# Code Completion Test\n# Type below to see code suggestions\n# Try typing: if, func, let, str, etc.\n\nmodule completion_demo\n\n"
    text_editor = widget.create_text_editor(20, 60, 760, 480, initial_content, 100)

    while running do
        process_events()
        render_frame()
        winplat.wait_events()
    end while

    winplat.destroy_window(window_handle)
end function

pub function process_events()
    let mut event = winplat::event { event_type: 0, key_code: 0, mouse_x: 0, mouse_y: 0 }

    while winplat.poll_event(event) do
        if event.event_type == winplat::EVENT_QUIT() then
            running = false
        elseif event.event_type == winplat::EVENT_MOUSE_PRESS() then
            widget.check_widget_click(event.mouse_x, event.mouse_y, text_editor)
        elseif event.event_type == winplat::EVENT_KEY_PRESS() then
            handle_key_press(event)
        end if
    end while
end function

pub function handle_key_press(event: winplat::event)
    let focused_id = widget.get_focused_widget_id()

    if focused_id == text_editor.id then
        widget.process_key_event(event, text_editor)

        // Update status based on completion state
        if code_completion.is_completion_visible() then
            let selected = code_completion.get_selected_completion()
            status_text = "Completion: " + selected.text + " | " + selected.description + " | Tab to accept, Esc to cancel"
        else
            let cursor_line = widget.editor_cursor_to_line(text_editor.text, text_editor.cursor_pos)
            let cursor_col = widget.editor_cursor_to_col(text_editor.text, text_editor.cursor_pos)
            status_text = "Line: " + string_utils.int_to_text(cursor_line + 1) + ", Col: " + string_utils.int_to_text(cursor_col + 1) + " | Type to trigger completion"
        end if
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

pub function render_frame()
    // Clear background
    winplat.fill_rect(window_handle, 0, 0, 800, 600, theme.bg_primary())

    // Draw title
    winplat.draw_text(window_handle, 20, 10, "NY Code Completion Test", theme.text_primary())
    winplat.draw_text(window_handle, 20, 30, "Features: Keywords, Functions, Types, Built-ins, Snippets | T=Toggle theme", theme.text_secondary())

    // Draw completion legend
    draw_completion_legend()

    // Draw text editor
    widget.draw_widget(window_handle, text_editor)

    // Draw status bar
    winplat.fill_rect(window_handle, 0, 570, 800, 30, theme.bg_status_bar())
    winplat.fill_rect(window_handle, 0, 570, 800, 1, theme.border_primary())
    winplat.draw_text(window_handle, 10, 575, status_text, theme.text_primary())

    winplat.present(window_handle)
end function

pub function draw_completion_legend()
    let legend_x = 20
    let legend_y = 35

    winplat.draw_text(window_handle, legend_x, legend_y, "Completion Types: ", theme.text_secondary())
    winplat.draw_text(window_handle, legend_x + 100, legend_y, "Keywords", theme.syntax_keyword())
    winplat.draw_text(window_handle, legend_x + 180, legend_y, "Functions", theme.syntax_function())
    winplat.draw_text(window_handle, legend_x + 260, legend_y, "Types", theme.syntax_type())
    winplat.draw_text(window_handle, legend_x + 320, legend_y, "Built-ins", theme.syntax_builtin())
    winplat.draw_text(window_handle, legend_x + 400, legend_y, "Snippets", theme.syntax_directive())
end function