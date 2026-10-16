@pragma once
module ny.godot.input

import ny.godot.math

# =============================================================================
# ny.godot.input — Input singleton bindings
# =============================================================================

@interop("ml_godot")
pub fn ml_godot_input_is_action_pressed(action: text) -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_input_is_action_just_pressed(action: text) -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_input_is_action_just_released(action: text) -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_input_get_axis(neg: text, pos: text) -> f64
    return 0.0
end fn

@interop("ml_godot")
pub fn ml_godot_input_get_vector_x(neg_x: text, pos_x: text, neg_y: text, pos_y: text) -> f64
    return 0.0
end fn

@interop("ml_godot")
pub fn ml_godot_input_get_vector_y(neg_x: text, pos_x: text, neg_y: text, pos_y: text) -> f64
    return 0.0
end fn

@interop("ml_godot")
pub fn ml_godot_input_get_mouse_position_x() -> f64
    return 0.0
end fn

@interop("ml_godot")
pub fn ml_godot_input_get_mouse_position_y() -> f64
    return 0.0
end fn

@interop("ml_godot")
pub fn ml_godot_input_get_mouse_velocity_x() -> f64
    return 0.0
end fn

@interop("ml_godot")
pub fn ml_godot_input_get_mouse_velocity_y() -> f64
    return 0.0
end fn

@interop("ml_godot")
pub fn ml_godot_input_set_mouse_mode(mode: i64)
end fn

@interop("ml_godot")
pub fn ml_godot_input_is_key_pressed(keycode: i64) -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_input_get_action_strength(action: text) -> f64
    return 0.0
end fn

# ── Convenience wrappers ──────────────────────────────────────────────────────

pub fn is_pressed(action: text) -> bool
    return ml_godot_input_is_action_pressed(action) == 1
end fn

pub fn is_just_pressed(action: text) -> bool
    return ml_godot_input_is_action_just_pressed(action) == 1
end fn

pub fn is_just_released(action: text) -> bool
    return ml_godot_input_is_action_just_released(action) == 1
end fn

pub fn get_axis(neg: text, pos: text) -> f64
    return ml_godot_input_get_axis(neg, pos)
end fn

pub fn get_vector(neg_x: text, pos_x: text, neg_y: text, pos_y: text) -> math.Vector2
    return math.Vector2 {
        x: ml_godot_input_get_vector_x(neg_x, pos_x, neg_y, pos_y),
        y: ml_godot_input_get_vector_y(neg_x, pos_x, neg_y, pos_y)
    }
end fn

pub fn get_mouse_position() -> math.Vector2
    return math.Vector2 {
        x: ml_godot_input_get_mouse_position_x(),
        y: ml_godot_input_get_mouse_position_y()
    }
end fn

pub fn get_mouse_velocity() -> math.Vector2
    return math.Vector2 {
        x: ml_godot_input_get_mouse_velocity_x(),
        y: ml_godot_input_get_mouse_velocity_y()
    }
end fn

# Mouse mode constants
pub const MOUSE_MODE_VISIBLE   = (0 as i64)
pub const MOUSE_MODE_HIDDEN    = (1 as i64)
pub const MOUSE_MODE_CAPTURED  = (2 as i64)
pub const MOUSE_MODE_CONFINED  = (3 as i64)

pub fn capture_mouse()
    ml_godot_input_set_mouse_mode(MOUSE_MODE_CAPTURED)
end fn

pub fn release_mouse()
    ml_godot_input_set_mouse_mode(MOUSE_MODE_VISIBLE)
end fn
