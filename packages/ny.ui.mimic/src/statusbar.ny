@schema
@pragma once

module ny.ui.mimic

# StatusBar widget - horizontal bar at bottom of window for status info

import mimic_window
import mimic_color

pub struct StatusBar
    x: i32
    y: i32
    width: i32
    height: i32
    visible: bool
    # Colors
    bg_color: i32
    border_color: i32
    text_color: i32
end struct

pub fn create(x: i32, y: i32, width: i32) -> StatusBar
    return StatusBar {
        x: x,
        y: y,
        width: width,
        height: 24,
        visible: true,
        bg_color: mimic_color.LightGray(),
        border_color: mimic_color.Gray(),
        text_color: mimic_color.Black()
    }
end fn

# Draw status bar background
pub fn draw(sb: StatusBar, win: mimic_window.Window)
    if sb.visible then
        win.fill_rect(sb.x, sb.y, sb.width, sb.height, sb.bg_color)
        win.draw_line(sb.x, sb.y, sb.x + sb.width, sb.y, sb.border_color)
    end if
end fn

# Draw text in status bar
pub fn draw_text_left(sb: StatusBar, win: mimic_window.Window, status_text: text)
    if sb.visible then
        win.draw_text(sb.x + 8, sb.y + 5, status_text, sb.text_color)
    end if
end fn

# Draw text on right side of status bar
pub fn draw_text_right(sb: StatusBar, win: mimic_window.Window, status_text: text, approx_width: i32)
    if sb.visible then
        let text_x = sb.x + sb.width - approx_width - 8
        win.draw_text(text_x, sb.y + 5, status_text, sb.text_color)
    end if
end fn

# Draw a vertical separator in status bar
pub fn draw_separator(sb: StatusBar, win: mimic_window.Window, x_offset: i32)
    if sb.visible then
        let sep_x = sb.x + x_offset
        win.draw_line(sep_x, sb.y + 4, sep_x, sb.y + sb.height - 4, sb.border_color)
    end if
end fn

