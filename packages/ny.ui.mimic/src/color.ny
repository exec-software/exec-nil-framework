@schema
@pragma once

module ny.ui.mimic

pub struct Color
    value: i32
end struct

pub fn rgb(r: i32, g: i32, b: i32) -> i32
    let r_shifted = r * 65536
    let g_shifted = g * 256
    return r_shifted + g_shifted + b
end fn

# Common Colors as functions
pub fn White() -> i32 return 16777215 end fn
pub fn Black() -> i32 return 0 end fn
pub fn Red() -> i32 return 16711680 end fn
pub fn Green() -> i32 return 65280 end fn
pub fn Blue() -> i32 return 255 end fn
pub fn Cyan() -> i32 return 65535 end fn
pub fn Magenta() -> i32 return 16711935 end fn
pub fn Yellow() -> i32 return 16776960 end fn

pub fn Gray() -> i32 return 8421504 end fn
pub fn LightGray() -> i32 return 10066329 end fn
pub fn DarkGray() -> i32 return 2236962 end fn

# Demo Specific Colors
pub fn DarkRed() -> i32 return 10027161 end fn
pub fn DarkGreen() -> i32 return 26112 end fn
pub fn DarkBlue() -> i32 return 102 end fn
