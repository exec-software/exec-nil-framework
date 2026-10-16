@schema
@pragma once

module ny.ui.mimic

# GroupBox widget - titled container for grouping related controls

import mimic_window
import mimic_color

pub struct GroupBox
    x: i32
    y: i32
    width: i32
    height: i32
    title: text
    visible: bool
    # Colors
    border_color: i32
    title_color: i32
    bg_color: i32
end struct

pub fn create(x: i32, y: i32, width: i32, height: i32, group_title: text) -> GroupBox
    return GroupBox {
        x: x,
        y: y,
        width: width,
        height: height,
        title: group_title,
        visible: true,
        border_color: mimic_color.Gray(),
        title_color: mimic_color.Black(),
        bg_color: mimic_color.LightGray()
    }
end fn

pub fn draw(gb: GroupBox, win: mimic_window.Window)
    if gb.visible then
        # Draw background
        win.fill_rect(gb.x, gb.y, gb.width, gb.height, gb.bg_color)

        # Draw border
        win.draw_rect(gb.x, gb.y, gb.width, gb.height, gb.border_color)

        # Draw title background (small rect to "break" the top border)
        win.fill_rect(gb.x + 10, gb.y - 2, 80, 12, gb.bg_color)

        # Draw title text
        win.draw_text(gb.x + 12, gb.y - 6, gb.title, gb.title_color)
    end if
end fn

