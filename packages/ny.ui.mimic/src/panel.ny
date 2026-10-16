@schema
@pragma once

module ny.ui.mimic

# Panel widget - a container with background color
# Used for grouping widgets and creating visual sections

import mimic_window
import mimic_color

pub struct Panel
    x: i32
    y: i32
    width: i32
    height: i32
    bg_color: i32
    border_color: i32
    has_border: bool
    visible: bool
end struct

pub fn create(x: i32, y: i32, width: i32, height: i32) -> Panel
    return Panel {
        x: x,
        y: y,
        width: width,
        height: height,
        bg_color: mimic_color.White(),
        border_color: mimic_color.Gray(),
        has_border: false,
        visible: true
    }
end fn

pub fn create_colored(x: i32, y: i32, width: i32, height: i32, bg: i32) -> Panel
    return Panel {
        x: x,
        y: y,
        width: width,
        height: height,
        bg_color: bg,
        border_color: mimic_color.Gray(),
        has_border: false,
        visible: true
    }
end fn

pub fn draw(panel: Panel, win: mimic_window.Window)
    if panel.visible then
        win.fill_rect(panel.x, panel.y, panel.width, panel.height, panel.bg_color)
        if panel.has_border then
            win.draw_rect(panel.x, panel.y, panel.width, panel.height, panel.border_color)
        end if
    end if
end fn

