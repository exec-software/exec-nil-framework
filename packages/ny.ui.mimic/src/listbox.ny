@schema
@pragma once

module ny.ui.mimic

# ListBox widget - scrollable list of selectable items
# Items are drawn as text lines, one can be selected

import mimic_window
import mimic_color

pub struct ListBox
    x: i32
    y: i32
    width: i32
    height: i32
    selected_index: i32    # -1 means none selected
    scroll_offset: i32     # For scrolling (items to skip)
    item_height: i32       # Height of each item
    visible: bool
    # Colors
    bg_color: i32
    border_color: i32
    text_color: i32
    selected_bg: i32
    selected_text: i32
end struct

pub fn create(x: i32, y: i32, width: i32, height: i32) -> ListBox
    return ListBox {
        x: x,
        y: y,
        width: width,
        height: height,
        selected_index: -1,
        scroll_offset: 0,
        item_height: 22,
        visible: true,
        bg_color: mimic_color.White(),
        border_color: mimic_color.Gray(),
        text_color: mimic_color.Black(),
        selected_bg: mimic_color.Blue(),
        selected_text: mimic_color.White()
    }
end fn

# Draw listbox background and border (items drawn separately)
pub fn draw_background(lb: ListBox, win: mimic_window.Window)
    if lb.visible then
        win.fill_rect(lb.x, lb.y, lb.width, lb.height, lb.bg_color)
        win.draw_rect(lb.x, lb.y, lb.width, lb.height, lb.border_color)
    end if
end fn

# Draw a single item at the given index position
pub fn draw_item(lb: ListBox, win: mimic_window.Window, index: i32, item_text: text)
    if lb.visible then
        let item_y = lb.y + ((index - lb.scroll_offset) * lb.item_height)

        # Only draw if visible in the listbox area
        if item_y > lb.y then
            if item_y < (lb.y + lb.height - lb.item_height) then
                # Draw selection highlight if selected
                if index == lb.selected_index then
                    win.fill_rect(lb.x + 1, item_y, lb.width - 2, lb.item_height, lb.selected_bg)
                    win.draw_text(lb.x + 5, item_y + 4, item_text, lb.selected_text)
                else
                    win.draw_text(lb.x + 5, item_y + 4, item_text, lb.text_color)
                end if
            end if
        end if
    end if
end fn

# Check if click is on the listbox and return item index, or -1 if outside
pub fn get_clicked_index(lb: ListBox, click_x: i32, click_y: i32) -> i32
    if lb.visible == false then
        return -1
    end if
    if click_x > lb.x then
        if click_x < (lb.x + lb.width) then
            if click_y > lb.y then
                if click_y < (lb.y + lb.height) then
                    let relative_y = click_y - lb.y
                    let index = lb.scroll_offset + (relative_y / lb.item_height)
                    return index
                end if
            end if
        end if
    end if
    return -1
end fn

# Set selection
pub fn set_selected(lb: ListBox, index: i32) -> ListBox
    return ListBox {
        x: lb.x,
        y: lb.y,
        width: lb.width,
        height: lb.height,
        selected_index: index,
        scroll_offset: lb.scroll_offset,
        item_height: lb.item_height,
        visible: lb.visible,
        bg_color: lb.bg_color,
        border_color: lb.border_color,
        text_color: lb.text_color,
        selected_bg: lb.selected_bg,
        selected_text: lb.selected_text
    }
end fn

