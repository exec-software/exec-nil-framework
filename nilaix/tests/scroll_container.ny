module scroll_container_test
# Test application for scroll container widget implementation
# Demonstrates clipping, mouse wheel support, and scroll bars

import widget
import theme
import winplat
import string_utils

let mut window_handle: i32 = -1
let mut running: bool = true
let mut scroll_container: widget::widget_data
let mut content_scroll_y: i32 = 0
let mut max_scroll_y: i32 = 0

// Large content that requires scrolling
let mut large_content = "Scroll Container Test Content\n\n"
let mut i = 0
while i < 50 do
    large_content = large_content + "This is line " + string_utils.int_to_text(i + 1) + " of the scrollable content. "
    large_content = large_content + "Scroll containers are essential for displaying large amounts of content in a limited space.\n"
    i = i + 1
end while

pub function main()
    window_handle = winplat.create_window("Scroll Container Test", 800, 600)
    if window_handle == -1 then
        print("Failed to create window")
        return
    end if

    theme.set_theme(theme.THEME_DARK())

    // Create scroll container (uses text editor as base with scroll features)
    scroll_container = widget.create_text_editor(50, 50, 700, 500, large_content, 200)
    scroll_container.options = "line_numbers,scrollable"

    // Calculate max scroll
    let total_lines = widget.editor_get_line_count(large_content)
    let visible_lines = widget.editor_visible_lines(500)
    max_scroll_y = total_lines - visible_lines
    if max_scroll_y < 0 then max_scroll_y = 0 end if

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
            widget.check_widget_click(event.mouse_x, event.mouse_y, scroll_container)
        elseif event.event_type == winplat::EVENT_MOUSE_WHEEL() then
            handle_mouse_wheel(event.mouse_y)  // mouse_y contains wheel delta
        elseif event.event_type == winplat::EVENT_KEY_PRESS() then
            widget.process_key_event(event, scroll_container)
        end if
    end while
end function

pub function handle_mouse_wheel(wheel_delta: i32)
    // Scroll up/down based on wheel delta
    let scroll_amount = wheel_delta / 120  // Standard wheel delta
    content_scroll_y = content_scroll_y - scroll_amount

    if content_scroll_y < 0 then content_scroll_y = 0 end if
    if content_scroll_y > max_scroll_y then content_scroll_y = max_scroll_y end if

    // Update scroll container's scroll offset
    scroll_container.selected_index = content_scroll_y
end function

pub function render_frame()
    winplat.fill_rect(window_handle, 0, 0, 800, 600, theme.bg_primary())

    // Draw title
    winplat.draw_text(window_handle, 50, 20, "Scroll Container Test - Use mouse wheel or arrow keys", theme.text_primary())

    // Draw scroll container
    widget.draw_widget(window_handle, scroll_container)

    // Draw scroll info
    draw_scroll_info()

    winplat.present(window_handle)
end function

pub function draw_scroll_info()
    let info_x = 50
    let info_y = 560

    let total_lines = widget.editor_get_line_count(large_content)
    let visible_lines = widget.editor_visible_lines(500)
    let current_line = scroll_container.selected_index + 1

    winplat.draw_text(window_handle, info_x, info_y, "Lines: " + string_utils.int_to_text(current_line) + "-" + string_utils.int_to_text(current_line + visible_lines - 1) + " of " + string_utils.int_to_text(total_lines), theme.text_secondary())
    winplat.draw_text(window_handle, info_x + 300, info_y, "Scroll: " + string_utils.int_to_text(content_scroll_y) + " / " + string_utils.int_to_text(max_scroll_y), theme.text_secondary())
    winplat.draw_text(window_handle, info_x + 500, info_y, "Use mouse wheel to scroll", theme.text_secondary())
end function