@schema
@pragma once

module ny.ui.mimic

# Button widget - clickable button with visual states

import mimic_window
import mimic_widget
import mimic_color

pub struct Button
    x: i32
    y: i32
    width: i32
    height: i32
    text: text
    visible: bool
    # Colors
    bg_color: i32
    text_color: i32
    border_color: i32
end struct

pub fn create(x: i32, y: i32, width: i32, height: i32, button_text: text) -> Button
    return Button {
        x: x,
        y: y,
        width: width,
        height: height,
        text: button_text,
        visible: true,
        bg_color: mimic_color.LightGray(),
        text_color: mimic_color.Black(),
        border_color: mimic_color.DarkGray()
    }
end fn

pub fn draw(btn: Button, win: mimic_window.Window)
    if btn.visible then
        # Draw button background
        win.fill_rect(btn.x, btn.y, btn.width, btn.height, btn.bg_color)
        # Draw button border
        win.draw_rect(btn.x, btn.y, btn.width, btn.height, btn.border_color)
        # Draw button text (centered approximately)
        let text_x = btn.x + 10
        let text_y = btn.y + (btn.height / 2) - 6
        win.draw_text(text_x, text_y, btn.text, btn.text_color)
    end if
end fn

# Check if a click event is inside the button
pub fn is_clicked(btn: Button, click_x: i32, click_y: i32) -> bool
    if btn.visible == false then
        return false
    end if
    if click_x > btn.x then
        if click_x > (btn.x + btn.width) then
            return false
        end if
        if click_y > btn.y then
            if click_y > (btn.y + btn.height) then
                return false
            end if
            return true
        end if
    end if
    return false
end fn

