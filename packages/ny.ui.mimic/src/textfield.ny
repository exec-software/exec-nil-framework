@schema
@pragma once

module ny.ui.mimic

# TextField widget - single line text input
# Note: Full text editing requires keyboard event handling
# This provides basic visual representation

import mimic_window
import mimic_color

pub struct TextField
    x: i32
    y: i32
    width: i32
    height: i32
    placeholder: text
    value: text
    focused: bool
    visible: bool
    # Colors
    bg_color: i32
    border_color: i32
    text_color: i32
    placeholder_color: i32
end struct

pub fn create(x: i32, y: i32, width: i32, placeholder_text: text) -> TextField
    return TextField {
        x: x,
        y: y,
        width: width,
        height: 24,
        placeholder: placeholder_text,
        value: "",
        focused: false,
        visible: true,
        bg_color: mimic_color.White(),
        border_color: mimic_color.Gray(),
        text_color: mimic_color.Black(),
        placeholder_color: mimic_color.Gray()
    }
end fn

pub fn draw(tf: TextField, win: mimic_window.Window)
    if tf.visible then
        # Draw background
        win.fill_rect(tf.x, tf.y, tf.width, tf.height, tf.bg_color)

        # Draw border (thicker if focused)
        if tf.focused then
            win.draw_rect(tf.x, tf.y, tf.width, tf.height, mimic_color.Blue())
        else
            win.draw_rect(tf.x, tf.y, tf.width, tf.height, tf.border_color)
        end if

        # Draw text or placeholder
        let text_y = tf.y + 5
        # Ny does not yet expose the string-length operation needed here.
        # So we'll just draw the value if it exists, otherwise placeholder
        win.draw_text(tf.x + 5, text_y, tf.value, tf.text_color)
    end if
end fn

# Check if a click is on the text field
pub fn is_clicked(tf: TextField, click_x: i32, click_y: i32) -> bool
    if tf.visible == false then
        return false
    end if
    if click_x > tf.x then
        if click_x < (tf.x + tf.width) then
            if click_y > tf.y then
                if click_y < (tf.y + tf.height) then
                    return true
                end if
            end if
        end if
    end if
    return false
end fn

# Set focus state
pub fn set_focused(tf: TextField, is_focused: bool) -> TextField
    return TextField {
        x: tf.x,
        y: tf.y,
        width: tf.width,
        height: tf.height,
        placeholder: tf.placeholder,
        value: tf.value,
        focused: is_focused,
        visible: tf.visible,
        bg_color: tf.bg_color,
        border_color: tf.border_color,
        text_color: tf.text_color,
        placeholder_color: tf.placeholder_color
    }
end fn

# Set value
pub fn set_value(tf: TextField, new_value: text) -> TextField
    return TextField {
        x: tf.x,
        y: tf.y,
        width: tf.width,
        height: tf.height,
        placeholder: tf.placeholder,
        value: new_value,
        focused: tf.focused,
        visible: tf.visible,
        bg_color: tf.bg_color,
        border_color: tf.border_color,
        text_color: tf.text_color,
        placeholder_color: tf.placeholder_color
    }
end fn
