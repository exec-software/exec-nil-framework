@schema
@pragma once

module ny.ui.mimic

# Dropdown/ComboBox widget - click to show list of options

import mimic_window
import mimic_color

pub struct Dropdown
    x: i32
    y: i32
    width: i32
    height: i32
    selected_index: i32
    is_open: bool          # Whether dropdown list is visible
    max_visible: i32       # Max items visible when open
    visible: bool
    # Colors
    bg_color: i32
    border_color: i32
    text_color: i32
    arrow_color: i32
    dropdown_bg: i32
    hover_bg: i32
end struct

pub fn create(x: i32, y: i32, width: i32) -> Dropdown
    return Dropdown {
        x: x,
        y: y,
        width: width,
        height: 26,
        selected_index: 0,
        is_open: false,
        max_visible: 5,
        visible: true,
        bg_color: mimic_color.White(),
        border_color: mimic_color.Gray(),
        text_color: mimic_color.Black(),
        arrow_color: mimic_color.DarkGray(),
        dropdown_bg: mimic_color.White(),
        hover_bg: mimic_color.LightGray()
    }
end fn

# Draw the closed dropdown button
pub fn draw_button(dd: Dropdown, win: mimic_window.Window, selected_text: text)
    if dd.visible then
        # Draw main button background
        win.fill_rect(dd.x, dd.y, dd.width, dd.height, dd.bg_color)
        win.draw_rect(dd.x, dd.y, dd.width, dd.height, dd.border_color)

        # Draw selected text
        win.draw_text(dd.x + 5, dd.y + 6, selected_text, dd.text_color)

        # Draw dropdown arrow (simple triangle representation as lines)
        let arrow_x = dd.x + dd.width - 20
        let arrow_y = dd.y + 10
        win.draw_line(arrow_x, arrow_y, arrow_x + 10, arrow_y, dd.arrow_color)
        win.draw_line(arrow_x, arrow_y, arrow_x + 5, arrow_y + 5, dd.arrow_color)
        win.draw_line(arrow_x + 10, arrow_y, arrow_x + 5, arrow_y + 5, dd.arrow_color)
    end if
end fn

# Draw dropdown list item
pub fn draw_item(dd: Dropdown, win: mimic_window.Window, index: i32, item_text: text, is_hovered: bool)
    if dd.visible then
        if dd.is_open then
            let item_y = dd.y + dd.height + (index * 22)

            # Draw item background
            if is_hovered then
                win.fill_rect(dd.x, item_y, dd.width, 22, dd.hover_bg)
            else
                win.fill_rect(dd.x, item_y, dd.width, 22, dd.dropdown_bg)
            end if

            # Draw item text
            win.draw_text(dd.x + 5, item_y + 4, item_text, dd.text_color)
        end if
    end if
end fn

# Draw dropdown list border (call after all items)
pub fn draw_list_border(dd: Dropdown, win: mimic_window.Window, item_count: i32)
    if dd.visible then
        if dd.is_open then
            let list_height = item_count * 22
            win.draw_rect(dd.x, dd.y + dd.height, dd.width, list_height, dd.border_color)
        end if
    end if
end fn

# Check if click is on the button
pub fn is_button_clicked(dd: Dropdown, click_x: i32, click_y: i32) -> bool
    if dd.visible == false then
        return false
    end if
    if click_x > dd.x then
        if click_x < (dd.x + dd.width) then
            if click_y > dd.y then
                if click_y < (dd.y + dd.height) then
                    return true
                end if
            end if
        end if
    end if
    return false
end fn

# Get clicked item index in open dropdown, or -1 if not clicking on list
pub fn get_clicked_item(dd: Dropdown, click_x: i32, click_y: i32, item_count: i32) -> i32
    if dd.visible == false then
        return -1
    end if
    if dd.is_open == false then
        return -1
    end if

    let list_top = dd.y + dd.height
    let list_bottom = list_top + (item_count * 22)

    if click_x > dd.x then
        if click_x < (dd.x + dd.width) then
            if click_y > list_top then
                if click_y < list_bottom then
                    let relative_y = click_y - list_top
                    let index = relative_y / 22
                    return index
                end if
            end if
        end if
    end if
    return -1
end fn

# Toggle open state
pub fn toggle_open(dd: Dropdown) -> Dropdown
    return Dropdown {
        x: dd.x,
        y: dd.y,
        width: dd.width,
        height: dd.height,
        selected_index: dd.selected_index,
        is_open: dd.is_open == false,
        max_visible: dd.max_visible,
        visible: dd.visible,
        bg_color: dd.bg_color,
        border_color: dd.border_color,
        text_color: dd.text_color,
        arrow_color: dd.arrow_color,
        dropdown_bg: dd.dropdown_bg,
        hover_bg: dd.hover_bg
    }
end fn

# Close dropdown
pub fn close(dd: Dropdown) -> Dropdown
    return Dropdown {
        x: dd.x,
        y: dd.y,
        width: dd.width,
        height: dd.height,
        selected_index: dd.selected_index,
        is_open: false,
        max_visible: dd.max_visible,
        visible: dd.visible,
        bg_color: dd.bg_color,
        border_color: dd.border_color,
        text_color: dd.text_color,
        arrow_color: dd.arrow_color,
        dropdown_bg: dd.dropdown_bg,
        hover_bg: dd.hover_bg
    }
end fn

# Set selected index
pub fn set_selected(dd: Dropdown, index: i32) -> Dropdown
    return Dropdown {
        x: dd.x,
        y: dd.y,
        width: dd.width,
        height: dd.height,
        selected_index: index,
        is_open: false,
        max_visible: dd.max_visible,
        visible: dd.visible,
        bg_color: dd.bg_color,
        border_color: dd.border_color,
        text_color: dd.text_color,
        arrow_color: dd.arrow_color,
        dropdown_bg: dd.dropdown_bg,
        hover_bg: dd.hover_bg
    }
end fn

