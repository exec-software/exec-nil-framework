@pragma once
module ny.nullptr.input

import ny.godot.node

# =============================================================================
# ny.nullptr.input — Input system for nullptr bridge
# =============================================================================
# Provides input action querying and keyboard/mouse state access.
# =============================================================================

# ── Input Actions ─────────────────────────────────────────────────────────────

@interop("ml_godot")
pub fn ml_godot_input_action_pressed(action: text) -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_input_action_toggled(action: text) -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_input_get_event_time(event: i64) -> f64
    return 0.0
end fn

@interop("ml_godot")
pub fn ml_godot_input_is_key_pressed(keycode: i32) -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_input_is_mouse_pressed(button: i32) -> i64
    return 0
end fn

# ── Mouse Position ────────────────────────────────────────────────────────────

@interop("ml_godot")
pub fn ml_godot_input_get_mouse_position_x() -> f64
    return 0.0
end fn

@interop("ml_godot")
pub fn ml_godot_input_get_mouse_position_y() -> f64
    return 0.0
end fn

@interop("ml_godot")
pub fn ml_godot_input_get_mouse_position() -> math.Vec2
    return math.Vec2 { x: ml_godot_input_get_mouse_position_x(), y: ml_godot_input_get_mouse_position_y() }
end fn

# ── Input Event Creation ──────────────────────────────────────────────────────

@interop("ml_godot")
pub fn ml_godot_input_event_mouse_move_new() -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_input_event_key_new() -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_input_event_mouse_button_new() -> i64
    return 0
end fn

# ── Convenience Wrappers ──────────────────────────────────────────────────────

pub fn input_action_pressed(action: text) -> bool
    return ml_godot_input_action_pressed(action) == 1
end fn

pub fn input_action_toggled(action: text) -> bool
    return ml_godot_input_action_toggled(action) == 1
end fn

pub fn is_key_pressed(keycode: i32) -> bool
    return ml_godot_input_is_key_pressed(keycode) == 1
end fn

pub fn is_mouse_pressed(button: i32) -> bool
    return ml_godot_input_is_mouse_pressed(button) == 1
end fn

pub fn get_mouse_position() -> math.Vec2
    return ml_godot_input_get_mouse_position()
end fn

pub fn get_mouse_position_x() -> f64
    return ml_godot_input_get_mouse_position_x()
end fn

pub fn get_mouse_position_y() -> f64
    return ml_godot_input_get_mouse_position_y()
end fn