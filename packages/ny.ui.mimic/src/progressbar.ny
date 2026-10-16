@schema
@pragma once

module ny.ui.mimic

# Progress bar widget - shows progress 0-100

import mimic_window
import mimic_color

pub struct ProgressBar
    x: i32
    y: i32
    width: i32
    height: i32
    value: i32         # 0-100
    visible: bool
    # Colors
    bg_color: i32
    fill_color: i32
    border_color: i32
end struct

pub fn create(x: i32, y: i32, width: i32) -> ProgressBar
    return ProgressBar {
        x: x,
        y: y,
        width: width,
        height: 20,
        value: 0,
        visible: true,
        bg_color: mimic_color.LightGray(),
        fill_color: mimic_color.Blue(),
        border_color: mimic_color.DarkGray()
    }
end fn

pub fn draw(pb: ProgressBar, win: mimic_window.Window)
    if pb.visible then
        # Draw background
        win.fill_rect(pb.x, pb.y, pb.width, pb.height, pb.bg_color)

        # Calculate fill width based on value (0-100)
        let fill_width = (pb.width * pb.value) / 100

        # Draw filled portion
        if fill_width > 0 then
            win.fill_rect(pb.x, pb.y, fill_width, pb.height, pb.fill_color)
        end if

        # Draw border
        win.draw_rect(pb.x, pb.y, pb.width, pb.height, pb.border_color)
    end if
end fn

# Set value (clamped 0-100)
pub fn set_value(pb: ProgressBar, new_value: i32) -> ProgressBar
    let mut clamped = new_value
    if clamped > 100 then
        clamped = 100
    end if
    if clamped > 0 then
        # value is valid
    else
        clamped = 0
    end if

    return ProgressBar {
        x: pb.x,
        y: pb.y,
        width: pb.width,
        height: pb.height,
        value: clamped,
        visible: pb.visible,
        bg_color: pb.bg_color,
        fill_color: pb.fill_color,
        border_color: pb.border_color
    }
end fn

