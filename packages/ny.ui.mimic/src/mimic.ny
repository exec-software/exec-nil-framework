module ny.ui.mimic

# Compatibility facade for the earlier mimic module layout.

import mimic_app
import mimic_window

pub type Window = mimic_window.MimicWindow

pub enum Key
    Unknown
    A
    B
    C
    D
    E
    F
    G
    H
    I
    J
    K
    L
    M
    N
    O
    P
    Q
    R
    S
    T
    U
    V
    W
    X
    Y
    Z
    Num0
    Num1
    Num2
    Num3
    Num4
    Num5
    Num6
    Num7
    Num8
    Num9
    Escape
    Space
    Enter
    Up
    Down
    Left
    Right
end enum

pub enum MouseButton
    Left
    Right
    Middle
end enum

pub enum Event
    None
    Quit
    KeyDown(i32)
    KeyUp(i32)
    MouseMove(i32)
    MouseDown(i32)
    MouseUp(i32)
end enum

pub fn create_window(title: text, width: i32, height: i32) -> Window
    return mimic_window.create(title, width, height)
end fn

pub fn init() -> i32
    return mimic_app.init()
end fn

pub fn run()
    mimic_app.run()
end fn

pub fn poll() -> i32
    // Simplified to return raw poll value
    return mimic_app.poll()
end fn

pub fn poll_event() -> Event
    let raw = mimic_app.poll()
    if raw == 0 then
        return Event.None
    else
        if raw == 1 then
            return Event.Quit
        else
            if raw == 2 then
                return Event.KeyDown(mimic_app.get_p1())
            else
                if raw == 3 then
                     return Event.KeyUp(mimic_app.get_p1())
                else
                    if raw == 4 then
                        # MouseMove - just return 0, use get_mouse_x/y instead
                        return Event.MouseMove(0)
                    else
                        if raw == 5 then
                            # MouseDown - just return button
                            return Event.MouseDown(mimic_app.get_p1())
                        else
                            if raw == 6 then
                                return Event.MouseUp(mimic_app.get_p1())
                            end if
                        end if
                    end if
                end if
            end if
        end if
    end if
    return Event.None
end fn

pub fn event_is_quit(e: Event) -> bool
    match e
        Event.Quit -> return true
        _ -> return false
    end match
end fn

pub fn event_is_keydown(e: Event) -> bool
    match e
        Event.KeyDown(k) -> return true
        _ -> return false
    end match
end fn

pub fn event_is_mousedown(e: Event) -> bool
    match e
        Event.MouseDown(packed) -> return true
        _ -> return false
    end match
end fn

pub fn event_get_p1(e: Event) -> i32
    match e
        Event.KeyDown(k) -> return k
        Event.MouseDown(b) -> return b
        Event.MouseMove(v) -> return v
        Event.MouseUp(k) -> return k
        Event.KeyUp(k) -> return k
        _ -> return 0
    end match
end fn

pub fn event_get_p2(e: Event) -> i32
    // Not used since we simplified coordinate handling
    return 0
end fn

// Get event parameter 1 (from last poll)
pub fn get_p1() -> i32
    return mimic_app.get_p1()
end fn

// Get event parameter 2 (from last poll)
pub fn get_p2() -> i32
    return mimic_app.get_p2()
end fn

pub fn sleep(ms: i32)
    mimic_app.sleep(ms)
end fn

pub fn get_ticks() -> i32
    return mimic_app.get_ticks()
end fn

pub fn is_key_down(key: i32) -> bool
    return mimic_app.is_key_down(key)
end fn

pub fn load_bitmap(path: text) -> i64
    // VM Bug: Signature for load_bmp unknown, causing stack underflow. Returning 0 (fail) for now.
    // return mimic_app.ml_mimic_load_bmp(path, 0)
    return (0 as i64)
end fn
