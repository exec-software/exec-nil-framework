@schema
@pragma once

module ny.ui.mimic

# Label widget - displays static text

import mimic_window
import mimic_widget
import mimic_color

pub struct Label
    x: i32
    y: i32
    width: i32
    height: i32
    text: text
    color: i32
    visible: bool
end struct

pub fn create(x: i32, y: i32, label_text: text) -> Label
    return Label {
        x: x,
        y: y,
        width: 200,
        height: 20,
        text: label_text,
        color: mimic_color.Black(),
        visible: true
    }
end fn

pub fn create_colored(x: i32, y: i32, label_text: text, color: i32) -> Label
    return Label {
        x: x,
        y: y,
        width: 200,
        height: 20,
        text: label_text,
        color: color,
        visible: true
    }
end fn

pub fn draw(label: Label, win: mimic_window.Window)
    if label.visible then
        win.draw_text(label.x, label.y, label.text, label.color)
    end if
end fn

