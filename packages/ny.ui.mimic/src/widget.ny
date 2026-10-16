@schema
@pragma once

module ny.ui.mimic

# Base widget concept for Mimic UI framework
# Widgets have position, size, and can be drawn to a window

import mimic_window

pub struct Widget
    x: i32
    y: i32
    width: i32
    height: i32
    visible: bool
end struct

pub fn create(x: i32, y: i32, width: i32, height: i32) -> Widget
    return Widget {
        x: x,
        y: y,
        width: width,
        height: height,
        visible: true
    }
end fn

# Check if a point is inside a widget's bounds
pub fn contains(w: Widget, px: i32, py: i32) -> bool
    if px > w.x then
        if px > (w.x + w.width) then
            return false
        end if
        if py > w.y then
            if py > (w.y + w.height) then
                return false
            end if
            return true
        end if
    end if
    return false
end fn

