module split_pane
# Split pane container for multi-view editing
# Supports horizontal and vertical splits with resizable panes

import widget
import theme
import winplat
import string_utils

// Split direction constants
pub function SPLIT_HORIZONTAL() -> i32
    return 0
end function

pub function SPLIT_VERTICAL() -> i32
    return 1
end function

// Split pane structure
pub struct split_pane_data
    x: i32
    y: i32
    width: i32
    height: i32
    direction: i32  // SPLIT_HORIZONTAL or SPLIT_VERTICAL
    split_ratio: f64  // 0.0 to 1.0, position of split
    first_widget: widget::widget_data
    second_widget: widget::widget_data
    split_width: i32  // Width of divider
    dragging: bool
    id: i32
end struct

// Helper functions for floating point operations
pub function f64_add(a: f64, b: f64) -> f64
    return a + b
end function

pub function f64_subtract(a: f64, b: f64) -> f64
    return a - b
end function

pub function f64_multiply(a: f64, b: f64) -> f64
    return a * b
end function

pub function f64_divide(a: f64, b: f64) -> f64
    if b != 0.0 then
        return a / b
    else
        return 0.0
    end if
end function

pub function create_split_pane(x: i32, y: i32, width: i32, height: i32, direction: i32,
                         first_widget: widget::widget_data, second_widget: widget::widget_data, id: i32) -> split_pane_data
    let pane = split_pane_data {
        x: x,
        y: y,
        width: width,
        height: height,
        direction: direction,
        split_ratio: 0.5,  // Default to 50/50 split
        first_widget: first_widget,
        second_widget: second_widget,
        split_width: 4,
        dragging: false,
        id: id
    }
    return pane
end function

pub function update_pane_layout(mut pane: split_pane_data)
    if pane.direction == SPLIT_HORIZONTAL() then
        // Horizontal split - top and bottom
        let available_height = pane.height - pane.split_width
        let first_height = i32(f64_multiply(f64(available_height), pane.split_ratio))
        let second_height = available_height - first_height

        // Update first widget (top)
        pane.first_widget.x = pane.x
        pane.first_widget.y = pane.y
        pane.first_widget.width = pane.width
        pane.first_widget.height = first_height

        // Update second widget (bottom)
        pane.second_widget.x = pane.x
        pane.second_widget.y = pane.y + first_height + pane.split_width
        pane.second_widget.width = pane.width
        pane.second_widget.height = second_height

    else  // SPLIT_VERTICAL()
        // Vertical split - left and right
        let available_width = pane.width - pane.split_width
        let first_width = i32(f64_multiply(f64(available_width), pane.split_ratio))
        let second_width = available_width - first_width

        // Update first widget (left)
        pane.first_widget.x = pane.x
        pane.first_widget.y = pane.y
        pane.first_widget.width = first_width
        pane.first_widget.height = pane.height

        // Update second widget (right)
        pane.second_widget.x = pane.x + first_width + pane.split_width
        pane.second_widget.y = pane.y
        pane.second_widget.width = second_width
        pane.second_widget.height = pane.height
    end if
end function

pub function get_split_position(pane: split_pane_data) -> i32
    if pane.direction == SPLIT_HORIZONTAL() then
        let available_height = pane.height - pane.split_width
        return pane.y + i32(f64_multiply(f64(available_height), pane.split_ratio))
    else
        let available_width = pane.width - pane.split_width
        return pane.x + i32(f64_multiply(f64(available_width), pane.split_ratio))
    end if
end function

pub function hit_test_splitter(pane: split_pane_data, mouse_x: i32, mouse_y: i32) -> bool
    let split_pos = get_split_position(pane)
    let hit_area = 8  // 8 pixels hit area on each side

    if pane.direction == SPLIT_HORIZONTAL() then
        // Check horizontal splitter (horizontal line)
        return mouse_x >= pane.x and mouse_x <= pane.x + pane.width and
               mouse_y >= split_pos - hit_area and mouse_y <= split_pos + hit_area + pane.split_width
    else
        // Check vertical splitter (vertical line)
        return mouse_y >= pane.y and mouse_y <= pane.y + pane.height and
               mouse_x >= split_pos - hit_area and mouse_x <= split_pos + hit_area + pane.split_width
    end if
end function

pub function update_split_ratio(mut pane: split_pane_data, mouse_x: i32, mouse_y: i32)
    let mut total_space = 0
    let mut offset_val = 0
    if pane.direction == SPLIT_HORIZONTAL() then
        total_space = pane.height - pane.split_width
        offset_val = mouse_y - pane.y
    else
        total_space = pane.width - pane.split_width
        offset_val = mouse_x - pane.x
    end if

    let mut new_ratio = 0.5
    if total_space > 0 then
        new_ratio = f64_divide(f64_multiply(1.0, offset_val), total_space)
    end if

    // Clamp ratio between 0.1 and 0.9
    if new_ratio < 0.1 then
        pane.split_ratio = 0.1
    elseif new_ratio > 0.9 then
        pane.split_ratio = 0.9
    else
        pane.split_ratio = new_ratio
    end if

    update_pane_layout(pane)
end function

pub function draw_split_pane(handle: i32, pane: split_pane_data)
    // Draw both widgets
    widget.draw_widget(handle, pane.first_widget)
    widget.draw_widget(handle, pane.second_widget)

    // Draw splitter
    let split_pos = get_split_position(pane)

    if pane.direction == SPLIT_HORIZONTAL() then
        // Draw horizontal splitter
        winplat.fill_rect(handle, pane.x, split_pos, pane.width, pane.split_width, theme.splitter_bg())
        winplat.fill_rect(handle, pane.x, split_pos, pane.width, 1, theme.border_primary())
        winplat.fill_rect(handle, pane.x, split_pos + pane.split_width - 1, pane.width, 1, theme.border_primary())
    else
        // Draw vertical splitter
        winplat.fill_rect(handle, split_pos, pane.y, pane.split_width, pane.height, theme.splitter_bg())
        winplat.fill_rect(handle, split_pos, pane.y, 1, pane.height, theme.border_primary())
        winplat.fill_rect(handle, split_pos + pane.split_width - 1, pane.y, 1, pane.height, theme.border_primary())
    end if
end function

pub function handle_split_pane_mouse_press(mut pane: split_pane_data, mouse_x: i32, mouse_y: i32) -> bool
    // First try the splitter
    if hit_test_splitter(pane, mouse_x, mouse_y) then
        pane.dragging = true
        return true
    end if

    // Then try the widgets (in reverse order for proper layering)
    if widget.is_point_in_widget(mouse_x, mouse_y, pane.second_widget) then
        return widget.check_widget_click(mouse_x, mouse_y, 0, pane.second_widget)
    elseif widget.is_point_in_widget(mouse_x, mouse_y, pane.first_widget) then
        return widget.check_widget_click(mouse_x, mouse_y, 0, pane.first_widget)
    end if

    return false
end function

pub function handle_split_pane_mouse_release(mut pane: split_pane_data) -> bool
    if pane.dragging then
        pane.dragging = false
        return true
    end if
    return false
end function

pub function handle_split_pane_mouse_motion(mut pane: split_pane_data, mouse_x: i32, mouse_y: i32) -> bool
    if pane.dragging then
        update_split_ratio(pane, mouse_x, mouse_y)
        return true
    end if

    // Update cursor based on hover position
    if hit_test_splitter(pane, mouse_x, mouse_y) then
        // Set resize cursor (would be platform-specific)
        return true
    end if

    return false
end function

pub function handle_split_pane_key_press(pane: split_pane_data, event: winplat::event) -> bool
    // Route to focused widget
    let focused_id = widget.get_focused_widget_id()

    if focused_id == pane.first_widget.id then
        return widget.process_key_event(event, pane.first_widget)
    elseif focused_id == pane.second_widget.id then
        return widget.process_key_event(event, pane.second_widget)
    end if

    return false
end function

pub function set_first_widget(mut pane: split_pane_data, w: widget::widget_data)
    pane.first_widget = w
    update_pane_layout(pane)
end function

pub function set_second_widget(mut pane: split_pane_data, w: widget::widget_data)
    pane.second_widget = w
    update_pane_layout(pane)
end function

pub function swap_widgets(mut pane: split_pane_data)
    let temp = pane.first_widget
    pane.first_widget = pane.second_widget
    pane.second_widget = temp
    update_pane_layout(pane)
end function

pub function set_split_ratio(mut pane: split_pane_data, r: f64)
    let mut ratio = r
    if ratio < 0.1 then ratio = 0.1 end if
    if ratio > 0.9 then ratio = 0.9 end if
    pane.split_ratio = ratio
    update_pane_layout(pane)
end function