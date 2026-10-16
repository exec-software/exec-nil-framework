@schema
@pragma once

module ny.ui.mimic

# Toolbar widget - horizontal bar with tool buttons

import mimic_window
import mimic_color

pub struct Toolbar
    x: i32
    y: i32
    width: i32
    height: i32
    visible: bool
    # Colors
    bg_color: i32
    border_color: i32
end struct

pub fn create(x: i32, y: i32, width: i32) -> Toolbar
    return Toolbar {
        x: x,
        y: y,
        width: width,
        height: 32,
        visible: true,
        bg_color: mimic_color.LightGray(),
        border_color: mimic_color.Gray()
    }
end fn

# Draw toolbar background
pub fn draw(tb: Toolbar, win: mimic_window.Window)
    if tb.visible then
        win.fill_rect(tb.x, tb.y, tb.width, tb.height, tb.bg_color)
        win.draw_line(tb.x, tb.y + tb.height, tb.x + tb.width, tb.y + tb.height, tb.border_color)
    end if
end fn

# Draw a tool button at given index position
pub fn draw_button(tb: Toolbar, win: mimic_window.Window, index: i32, label: text, is_pressed: bool)
    if tb.visible then
        let btn_width = 60
        let btn_x = tb.x + 4 + (index * (btn_width + 4))
        let btn_y = tb.y + 4
        let btn_h = tb.height - 8

        if is_pressed then
            win.fill_rect(btn_x, btn_y, btn_width, btn_h, mimic_color.DarkGray())
            win.draw_text(btn_x + 8, btn_y + 4, label, mimic_color.White())
        else
            win.fill_rect(btn_x, btn_y, btn_width, btn_h, mimic_color.White())
            win.draw_rect(btn_x, btn_y, btn_width, btn_h, mimic_color.Gray())
            win.draw_text(btn_x + 8, btn_y + 4, label, mimic_color.Black())
        end if
    end if
end fn

# Check if a toolbar button was clicked, returns button index or -1
pub fn get_clicked_button(tb: Toolbar, click_x: i32, click_y: i32, button_count: i32) -> i32
    if tb.visible == false then
        return -1
    end if

    let btn_width = 60

    if click_y > (tb.y + 4) then
        if click_y < (tb.y + tb.height - 4) then
            let mut i = 0
            loop
                if i > button_count then
                    break
                end if
                if i == button_count then
                    break
                end if

                let btn_x = tb.x + 4 + (i * (btn_width + 4))
                if click_x > btn_x then
                    if click_x < (btn_x + btn_width) then
                        return i
                    end if
                end if

                i = i + 1
            end loop
        end if
    end if
    return -1
end fn

