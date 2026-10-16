@schema
@pragma once

module ny.ui.mimic

# Checkbox widget - toggleable boolean control

import mimic_window
import mimic_color

pub struct Checkbox
    x: i32
    y: i32
    width: i32
    height: i32
    label: text
    checked: bool
    visible: bool
    # Colors
    box_color: i32
    check_color: i32
    text_color: i32
end struct

pub fn create(x: i32, y: i32, label_text: text) -> Checkbox
    return Checkbox {
        x: x,
        y: y,
        width: 20,
        height: 20,
        label: label_text,
        checked: false,
        visible: true,
        box_color: mimic_color.White(),
        check_color: mimic_color.Blue(),
        text_color: mimic_color.Black()
    }
end fn

pub fn create_checked(x: i32, y: i32, label_text: text, is_checked: bool) -> Checkbox
    return Checkbox {
        x: x,
        y: y,
        width: 20,
        height: 20,
        label: label_text,
        checked: is_checked,
        visible: true,
        box_color: mimic_color.White(),
        check_color: mimic_color.Blue(),
        text_color: mimic_color.Black()
    }
end fn

pub fn draw(cb: Checkbox, win: mimic_window.Window)
    if cb.visible then
        # Draw checkbox box
        win.fill_rect(cb.x, cb.y, cb.width, cb.height, cb.box_color)
        win.draw_rect(cb.x, cb.y, cb.width, cb.height, mimic_color.DarkGray())

        # Draw checkmark if checked
        if cb.checked then
            # Draw a simple X or filled inner square as checkmark
            let inner_margin = 4
            win.fill_rect(cb.x + inner_margin, cb.y + inner_margin,
                         cb.width - (inner_margin * 2), cb.height - (inner_margin * 2),
                         cb.check_color)
        end if

        # Draw label text to the right of the checkbox
        win.draw_text(cb.x + cb.width + 8, cb.y + 3, cb.label, cb.text_color)
    end if
end fn

# Check if a click is on the checkbox (including label area)
pub fn is_clicked(cb: Checkbox, click_x: i32, click_y: i32) -> bool
    if cb.visible == false then
        return false
    end if
    # Check if click is within checkbox bounds (box only, not label)
    if click_x > cb.x then
        if click_x < (cb.x + cb.width) then
            if click_y > cb.y then
                if click_y < (cb.y + cb.height) then
                    return true
                end if
            end if
        end if
    end if
    return false
end fn

# Toggle the checked state
pub fn toggle(cb: Checkbox) -> Checkbox
    return Checkbox {
        x: cb.x,
        y: cb.y,
        width: cb.width,
        height: cb.height,
        label: cb.label,
        checked: cb.checked == false,
        visible: cb.visible,
        box_color: cb.box_color,
        check_color: cb.check_color,
        text_color: cb.text_color
    }
end fn

