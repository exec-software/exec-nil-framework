@schema
@pragma once

module ny.ui.mimic

# Slider widget - horizontal value slider (0-100)

import mimic_window
import mimic_color

pub struct Slider
    x: i32
    y: i32
    width: i32
    height: i32
    value: i32         # 0-100
    min_val: i32
    max_val: i32
    visible: bool
    # Colors
    track_color: i32
    fill_color: i32
    thumb_color: i32
end struct

pub fn create(x: i32, y: i32, width: i32) -> Slider
    return Slider {
        x: x,
        y: y,
        width: width,
        height: 20,
        value: 50,
        min_val: 0,
        max_val: 100,
        visible: true,
        track_color: mimic_color.LightGray(),
        fill_color: mimic_color.Blue(),
        thumb_color: mimic_color.White()
    }
end fn

pub fn draw(sl: Slider, win: mimic_window.Window)
    if sl.visible then
        let track_height = 6
        let track_y = sl.y + (sl.height / 2) - (track_height / 2)

        # Draw track background
        win.fill_rect(sl.x, track_y, sl.width, track_height, sl.track_color)
        win.draw_rect(sl.x, track_y, sl.width, track_height, mimic_color.DarkGray())

        # Calculate thumb position
        let range = sl.max_val - sl.min_val
        let thumb_x = sl.x + ((sl.width * (sl.value - sl.min_val)) / range) - 8

        # Draw filled portion
        let fill_width = thumb_x - sl.x + 8
        if fill_width > 0 then
            win.fill_rect(sl.x, track_y, fill_width, track_height, sl.fill_color)
        end if

        # Draw thumb (circle-ish via small rect)
        win.fill_rect(thumb_x, sl.y + 2, 16, 16, sl.thumb_color)
        win.draw_rect(thumb_x, sl.y + 2, 16, 16, mimic_color.DarkGray())
    end if
end fn

# Check if click is on slider track
pub fn is_clicked(sl: Slider, click_x: i32, click_y: i32) -> bool
    if sl.visible == false then
        return false
    end if
    if click_x > sl.x then
        if click_x < (sl.x + sl.width) then
            if click_y > sl.y then
                if click_y < (sl.y + sl.height) then
                    return true
                end if
            end if
        end if
    end if
    return false
end fn

# Calculate value from click position
pub fn value_from_click(sl: Slider, click_x: i32) -> i32
    let mut relative_x = click_x - sl.x
    if relative_x > 0 then
        # do nothing, valid
    else
        relative_x = 0
    end if
    if relative_x > sl.width then
        relative_x = sl.width
    end if

    let range = sl.max_val - sl.min_val
    let new_val = sl.min_val + ((relative_x * range) / sl.width)
    return new_val
end fn

# Set value
pub fn set_value(sl: Slider, new_value: i32) -> Slider
    let mut clamped = new_value
    if clamped > sl.max_val then
        clamped = sl.max_val
    end if
    if clamped > sl.min_val then
        # valid
    else
        clamped = sl.min_val
    end if

    return Slider {
        x: sl.x,
        y: sl.y,
        width: sl.width,
        height: sl.height,
        value: clamped,
        min_val: sl.min_val,
        max_val: sl.max_val,
        visible: sl.visible,
        track_color: sl.track_color,
        fill_color: sl.fill_color,
        thumb_color: sl.thumb_color
    }
end fn

