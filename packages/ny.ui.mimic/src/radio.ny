@schema
@pragma once

module ny.ui.mimic

# Radio button widget - single selection from a group
# Radio buttons should be used in groups where only one can be selected

import mimic_window
import mimic_color

pub struct RadioButton
    x: i32
    y: i32
    width: i32
    height: i32
    label: text
    selected: bool
    group_id: i32      # Buttons with same group_id are mutually exclusive
    visible: bool
    # Colors
    circle_color: i32
    dot_color: i32
    text_color: i32
end struct

pub fn create(x: i32, y: i32, label_text: text, group: i32) -> RadioButton
    return RadioButton {
        x: x,
        y: y,
        width: 18,
        height: 18,
        label: label_text,
        selected: false,
        group_id: group,
        visible: true,
        circle_color: mimic_color.White(),
        dot_color: mimic_color.Blue(),
        text_color: mimic_color.Black()
    }
end fn

pub fn draw(rb: RadioButton, win: mimic_window.Window)
    if rb.visible then
        # Draw outer circle (as rectangle for simplicity)
        win.fill_rect(rb.x, rb.y, rb.width, rb.height, rb.circle_color)
        win.draw_rect(rb.x, rb.y, rb.width, rb.height, mimic_color.DarkGray())

        # Draw inner dot if selected
        if rb.selected then
            let dot_margin = 5
            win.fill_rect(rb.x + dot_margin, rb.y + dot_margin,
                         rb.width - (dot_margin * 2), rb.height - (dot_margin * 2),
                         rb.dot_color)
        end if

        # Draw label
        win.draw_text(rb.x + rb.width + 8, rb.y + 2, rb.label, rb.text_color)
    end if
end fn

# Check if click is on radio button
pub fn is_clicked(rb: RadioButton, click_x: i32, click_y: i32) -> bool
    if rb.visible == false then
        return false
    end if
    if click_x > rb.x then
        if click_x < (rb.x + rb.width) then
            if click_y > rb.y then
                if click_y < (rb.y + rb.height) then
                    return true
                end if
            end if
        end if
    end if
    return false
end fn

# Set selected state
pub fn set_selected(rb: RadioButton, is_selected: bool) -> RadioButton
    return RadioButton {
        x: rb.x,
        y: rb.y,
        width: rb.width,
        height: rb.height,
        label: rb.label,
        selected: is_selected,
        group_id: rb.group_id,
        visible: rb.visible,
        circle_color: rb.circle_color,
        dot_color: rb.dot_color,
        text_color: rb.text_color
    }
end fn

