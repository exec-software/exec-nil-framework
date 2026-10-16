module split_pane_test
# Test application for split pane editing views
# Demonstrates horizontal and vertical splits with resizable panes

import widget
import theme
import winplat
import string_utils
import split_pane

let mut window_handle: i32 = -1
let mut running: bool = true
let mut status_text: text = "Split Pane Test - Drag splitter to resize"

// Split panes for different configurations
let mut horizontal_pane: split_pane::split_pane_data
let mut vertical_pane: split_pane::split_pane_data
let mut quad_pane1: split_pane::split_pane_data  // Left split
let mut quad_pane2: split_pane::split_pane_data  // Right split

// Editors for each pane
let mut editor1: widget::widget_data
let mut editor2: widget::widget_data
let mut editor3: widget::widget_data
let mut editor4: widget::widget_data

pub function main()
    window_handle = winplat.create_window("Split Pane Test", 1200, 800)
    if window_handle == -1 then
        print("Failed to create window")
        return
    end if

    theme.set_theme(theme.THEME_DARK())

    create_split_panes()

    while running do
        process_events()
        render_frame()
        winplat.wait_events()
    end while

    winplat.destroy_window(window_handle)
end function

pub function create_split_panes()
    // Create text editors
    editor1 = widget.create_text_editor(0, 0, 400, 300, "# Editor 1 - Top Left\nmodule top_left\n\nfunction demo1()\n    print(\"Top left pane\")\n    return true\nend function", 1001)
    editor2 = widget.create_text_editor(0, 0, 400, 300, "# Editor 2 - Bottom Left\nmodule bottom_left\n\nfunction demo2()\n    print(\"Bottom left pane\")\n    return true\nend function", 1002)
    editor3 = widget.create_text_editor(0, 0, 300, 300, "# Editor 3 - Top Right\nmodule top_right\n\nfunction demo3()\n    print(\"Top right pane\")\n    return true\nend function", 1003)
    editor4 = widget.create_text_editor(0, 0, 300, 300, "# Editor 4 - Bottom Right\nmodule bottom_right\n\nfunction demo4()\n    print(\"Bottom right pane\")\n    return true\nend function", 1004)

    // Create horizontal split (top half)
    horizontal_pane = split_pane.create_split_pane(10, 60, 580, 360, split_pane.SPLIT_HORIZONTAL(), editor1, editor2, 2001)

    // Create vertical split (bottom half)
    vertical_pane = split_pane.create_split_pane(10, 440, 580, 340, split_pane.SPLIT_VERTICAL(), editor3, editor4, 2002)

    // Create quad view using nested splits
    // First, create a vertical split for the entire right area
    let temp_editor3 = widget.create_text_editor(0, 0, 300, 360, "# Editor 3 - Right Top", 3003)
    let temp_editor4 = widget.create_text_editor(0, 0, 300, 360, "# Editor 4 - Right Bottom", 3004)
    let right_pane = split_pane.create_split_pane(610, 60, 580, 360, split_pane.SPLIT_VERTICAL(), temp_editor3, temp_editor4, 3005)

    // Replace temp editors with real ones
    split_pane.set_first_widget(right_pane, editor3)
    split_pane.set_second_widget(right_pane, editor4)

    quad_pane1 = right_pane  // Right side vertical split
end function

pub function process_events()
    let mut event = winplat::event { event_type: 0, key_code: 0, mouse_x: 0, mouse_y: 0 }

    while winplat.poll_event(event) do
        if event.event_type == winplat::EVENT_QUIT() then
            running = false
        elseif event.event_type == winplat::EVENT_MOUSE_PRESS() then
            handle_mouse_press(event.mouse_x, event.mouse_y)
        elseif event.event_type == winplat::EVENT_MOUSE_RELEASE() then
            handle_mouse_release()
        elseif event.event_type == winplat::EVENT_MOUSE_MOTION() then
            handle_mouse_motion(event.mouse_x, event.mouse_y)
        elseif event.event_type == winplat::EVENT_KEY_PRESS() then
            handle_key_press(event)
        end if
    end while
end function

pub function handle_mouse_press(mouse_x: i32, mouse_y: i32)
    // Try horizontal split first
    if split_pane.handle_split_pane_mouse_press(horizontal_pane, mouse_x, mouse_y) then
        status_text = "Dragging horizontal splitter"
    // Try vertical split
    elseif split_pane.handle_split_pane_mouse_press(vertical_pane, mouse_x, mouse_y) then
        status_text = "Dragging vertical splitter"
    // Try quad pane (right side)
    elseif split_pane.handle_split_pane_mouse_press(quad_pane1, mouse_x, mouse_y) then
        status_text = "Dragging quad pane splitter"
    else
        // Update cursor position for focused editor
        let focused_id = widget.get_focused_widget_id()
        if focused_id >= 1001 and focused_id <= 1004 then
            status_text = "Editor " + string_utils.int_to_text(focused_id - 1000) + " focused"
        end if
    end if
end function

pub function handle_mouse_release()
    split_pane.handle_split_pane_mouse_release(horizontal_pane)
    split_pane.handle_split_pane_mouse_release(vertical_pane)
    split_pane.handle_split_pane_mouse_release(quad_pane1)
    status_text = "Split Pane Test - Drag splitter to resize"
end function

pub function handle_mouse_motion(mouse_x: i32, mouse_y: i32)
    let handled = false

    if split_pane.handle_split_pane_mouse_motion(horizontal_pane, mouse_x, mouse_y) then
        handled = true
    elseif split_pane.handle_split_pane_mouse_motion(vertical_pane, mouse_x, mouse_y) then
        handled = true
    elseif split_pane.handle_split_pane_mouse_motion(quad_pane1, mouse_x, mouse_y) then
        handled = true
    end if

    if not handled then
        // Update status with position
        status_text = "Mouse: " + string_utils.int_to_text(mouse_x) + ", " + string_utils.int_to_text(mouse_y)
    end if
end function

pub function handle_key_press(event: winplat::event)
    let handled = false

    // Route to appropriate split pane
    if split_pane.handle_split_pane_key_press(horizontal_pane, event) then
        handled = true
    elseif split_pane.handle_split_pane_key_press(vertical_pane, event) then
        handled = true
    elseif split_pane.handle_split_pane_key_press(quad_pane1, event) then
        handled = true
    end if

    // Handle global shortcuts
    if not handled then
        if event.key_code == 84 or event.key_code == 116 then  // T or t
            // Toggle theme
            if theme.get_theme() == theme.THEME_DARK() then
                theme.set_theme(theme.THEME_LIGHT())
                status_text = "Switched to Light theme"
            else
                theme.set_theme(theme.THEME_DARK())
                status_text = "Switched to Dark theme"
            end if
        elseif event.key_code == 49 then  // 1
            split_pane.set_split_ratio(horizontal_pane, 0.3)
            status_text = "Horizontal split: 30%/70%"
        elseif event.key_code == 50 then  // 2
            split_pane.set_split_ratio(horizontal_pane, 0.5)
            status_text = "Horizontal split: 50%/50%"
        elseif event.key_code == 51 then  // 3
            split_pane.set_split_ratio(horizontal_pane, 0.7)
            status_text = "Horizontal split: 70%/30%"
        elseif event.key_code == 52 then  // 4
            split_pane.set_split_ratio(vertical_pane, 0.3)
            status_text = "Vertical split: 30%/70%"
        elseif event.key_code == 53 then  // 5
            split_pane.set_split_ratio(vertical_pane, 0.5)
            status_text = "Vertical split: 50%/50%"
        elseif event.key_code == 54 then  // 6
            split_pane.set_split_ratio(vertical_pane, 0.7)
            status_text = "Vertical split: 70%/30%"
        elseif event.key_code == 83 then  // S
            split_pane.swap_widgets(horizontal_pane)
            status_text = "Swapped horizontal pane widgets"
        end if
    end if
end function

pub function render_frame()
    // Clear background
    winplat.fill_rect(window_handle, 0, 0, 1200, 800, theme.bg_primary())

    // Draw title
    winplat.draw_text(window_handle, 10, 10, "Split Pane Editor Test", theme.text_primary())
    winplat.draw_text(window_handle, 10, 30, "Drag splitters to resize | 1-3: Horizontal ratios | 4-6: Vertical ratios | S: Swap | T: Toggle theme", theme.text_secondary())

    // Draw pane labels
    winplat.draw_text(window_handle, 610, 30, "Left Area (Horizontal + Vertical)", theme.text_secondary())
    winplat.draw_text(window_handle, 850, 30, "Right Area (Vertical)", theme.text_secondary())

    // Draw split panes
    split_pane.draw_split_pane(window_handle, horizontal_pane)
    split_pane.draw_split_pane(window_handle, vertical_pane)
    split_pane.draw_split_pane(window_handle, quad_pane1)

    // Draw splitter info
    draw_splitter_info()

    // Draw status bar
    winplat.fill_rect(window_handle, 0, 770, 1200, 30, theme.bg_status_bar())
    winplat.fill_rect(window_handle, 0, 770, 1200, 1, theme.border_primary())
    winplat.draw_text(window_handle, 10, 775, status_text, theme.text_primary())

    winplat.present(window_handle)
end function

pub function draw_splitter_info()
    let info_x = 850
    let info_y = 440
    let info_w = 330
    let info_h = 320

    // Draw info panel background
    winplat.fill_rect(window_handle, info_x, info_y, info_w, info_h, theme.panel_bg())
    winplat.fill_rect(window_handle, info_x, info_y, info_w, 1, theme.border_primary())
    winplat.fill_rect(window_handle, info_x, info_y + info_h - 1, info_w, 1, theme.border_primary())
    winplat.fill_rect(window_handle, info_x, info_y, 1, info_h, theme.border_primary())
    winplat.fill_rect(window_handle, info_x + info_w - 1, info_y, 1, info_h, theme.border_primary())

    // Panel header
    winplat.fill_rect(window_handle, info_x + 1, info_y + 1, info_w - 2, 22, theme.panel_header_bg())
    winplat.draw_text(window_handle, info_x + 6, info_y + 4, "Splitter Configuration", theme.text_primary())

    // Draw splitter information
    let text_y = info_y + 30
    let line_height = 20

    winplat.draw_text(window_handle, info_x + 10, text_y, "Horizontal Split Ratio:", theme.text_primary())
    winplat.draw_text(window_handle, info_x + 10, text_y + line_height, string_utils.f64_to_text(horizontal_pane.split_ratio * 100) + "% / " + string_utils.f64_to_text((1.0 - horizontal_pane.split_ratio) * 100) + "%", theme.text_secondary())

    winplat.draw_text(window_handle, info_x + 10, text_y + line_height * 2, "Vertical Split Ratio:", theme.text_primary())
    winplat.draw_text(window_handle, info_x + 10, text_y + line_height * 3, string_utils.f64_to_text(vertical_pane.split_ratio * 100) + "% / " + string_utils.f64_to_text((1.0 - vertical_pane.split_ratio) * 100) + "%", theme.text_secondary())

    winplat.draw_text(window_handle, info_x + 10, text_y + line_height * 5, "Controls:", theme.text_primary())
    winplat.draw_text(window_handle, info_x + 10, text_y + line_height * 6, "• Drag splitters to resize", theme.text_secondary())
    winplat.draw_text(window_handle, info_x + 10, text_y + line_height * 7, "• 1-3: Set horizontal ratio", theme.text_secondary())
    winplat.draw_text(window_handle, info_x + 10, text_y + line_height * 8, "• 4-6: Set vertical ratio", theme.text_secondary())
    winplat.draw_text(window_handle, info_x + 10, text_y + line_height * 9, "• S: Swap horizontal widgets", theme.text_secondary())
    winplat.draw_text(window_handle, info_x + 10, text_y + line_height * 10, "• T: Toggle dark/light theme", theme.text_secondary())
    winplat.draw_text(window_handle, info_x + 10, text_y + line_height * 11, "• Click editors to focus", theme.text_secondary())
end function