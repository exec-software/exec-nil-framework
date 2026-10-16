@pragma once
module ny.godot.node

import ny.godot.math

# =============================================================================
# ny.godot.node — Node lifecycle and tree operations
# =============================================================================
# Nodes are referenced by i64 handles (pointer value cast to int).
# The handle is passed as the first argument to every lifecycle function
# by the ml_godot bridge (the NyScriptNode GDExtension class).
#
# Writing a Ny script:
#   import ny.godot.node
#
#   pub fn _ready(self: i64)
#       node_print(self, "Hello from Ny!")
#   end fn
#
#   pub fn _process(self: i64, delta: f64)
#       let pos = node3d_get_position(self)
#       node3d_set_position(self, pos.x + delta, pos.y, pos.z)
#   end fn
# =============================================================================

# ── Node (base) ───────────────────────────────────────────────────────────────

@interop("ml_godot")
pub fn ml_godot_node_get_name(node: i64) -> text
    return ""
end fn

@interop("ml_godot")
pub fn ml_godot_node_set_name(node: i64, name: text)
end fn

@interop("ml_godot")
pub fn ml_godot_node_get_child_count(node: i64) -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_node_get_child(node: i64, index: i64) -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_node_get_parent(node: i64) -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_node_find_child(node: i64, name: text) -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_node_add_child(node: i64, child: i64)
end fn

@interop("ml_godot")
pub fn ml_godot_node_queue_free(node: i64)
end fn

@interop("ml_godot")
pub fn ml_godot_node_is_inside_tree(node: i64) -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_node_print(node: i64, msg: text)
end fn

@interop("ml_godot")
pub fn ml_godot_node_emit_signal(node: i64, signal_name: text)
end fn

@interop("ml_godot")
pub fn ml_godot_node_emit_signal_i64(node: i64, signal_name: text, arg0: i64)
end fn

@interop("ml_godot")
pub fn ml_godot_node_emit_signal_f64(node: i64, signal_name: text, arg0: f64)
end fn

@interop("ml_godot")
pub fn ml_godot_node_emit_signal_text(node: i64, signal_name: text, arg0: text)
end fn

@interop("ml_godot")
pub fn ml_godot_node_get_property_i64(node: i64, prop: text) -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_node_set_property_i64(node: i64, prop: text, val: i64)
end fn

@interop("ml_godot")
pub fn ml_godot_node_get_property_f64(node: i64, prop: text) -> f64
    return 0.0
end fn

@interop("ml_godot")
pub fn ml_godot_node_set_property_f64(node: i64, prop: text, val: f64)
end fn

@interop("ml_godot")
pub fn ml_godot_node_get_property_text(node: i64, prop: text) -> text
    return ""
end fn

@interop("ml_godot")
pub fn ml_godot_node_set_property_text(node: i64, prop: text, val: text)
end fn

@interop("ml_godot")
pub fn ml_godot_node_call(node: i64, method: text) -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_node_call_arg_i64(node: i64, method: text, arg: i64) -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_node_call_arg_f64(node: i64, method: text, arg: f64) -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_node_call_arg_text(node: i64, method: text, arg: text) -> i64
    return 0
end fn

# ── Convenience wrappers ──────────────────────────────────────────────────────

pub fn node_get_name(node: i64) -> text
    return ml_godot_node_get_name(node)
end fn

pub fn node_print(node: i64, msg: text)
    ml_godot_node_print(node, msg)
end fn

pub fn node_find_child(node: i64, name: text) -> i64
    return ml_godot_node_find_child(node, name)
end fn

pub fn node_queue_free(node: i64)
    ml_godot_node_queue_free(node)
end fn

# ── Node3D ────────────────────────────────────────────────────────────────────

@interop("ml_godot")
pub fn ml_godot_node3d_get_position_x(node: i64) -> f64  return 0.0 end fn
@interop("ml_godot")
pub fn ml_godot_node3d_get_position_y(node: i64) -> f64  return 0.0 end fn
@interop("ml_godot")
pub fn ml_godot_node3d_get_position_z(node: i64) -> f64  return 0.0 end fn

@interop("ml_godot")
pub fn ml_godot_node3d_set_position(node: i64, x: f64, y: f64, z: f64)
end fn

@interop("ml_godot")
pub fn ml_godot_node3d_get_global_position_x(node: i64) -> f64  return 0.0 end fn
@interop("ml_godot")
pub fn ml_godot_node3d_get_global_position_y(node: i64) -> f64  return 0.0 end fn
@interop("ml_godot")
pub fn ml_godot_node3d_get_global_position_z(node: i64) -> f64  return 0.0 end fn

@interop("ml_godot")
pub fn ml_godot_node3d_set_global_position(node: i64, x: f64, y: f64, z: f64)
end fn

@interop("ml_godot")
pub fn ml_godot_node3d_get_rotation_x(node: i64) -> f64  return 0.0 end fn
@interop("ml_godot")
pub fn ml_godot_node3d_get_rotation_y(node: i64) -> f64  return 0.0 end fn
@interop("ml_godot")
pub fn ml_godot_node3d_get_rotation_z(node: i64) -> f64  return 0.0 end fn

@interop("ml_godot")
pub fn ml_godot_node3d_set_rotation(node: i64, x: f64, y: f64, z: f64)
end fn

@interop("ml_godot")
pub fn ml_godot_node3d_get_scale_x(node: i64) -> f64  return 1.0 end fn
@interop("ml_godot")
pub fn ml_godot_node3d_get_scale_y(node: i64) -> f64  return 1.0 end fn
@interop("ml_godot")
pub fn ml_godot_node3d_get_scale_z(node: i64) -> f64  return 1.0 end fn

@interop("ml_godot")
pub fn ml_godot_node3d_set_scale(node: i64, x: f64, y: f64, z: f64)
end fn

@interop("ml_godot")
pub fn ml_godot_node3d_rotate_y(node: i64, angle: f64)
end fn

@interop("ml_godot")
pub fn ml_godot_node3d_look_at(node: i64, tx: f64, ty: f64, tz: f64)
end fn

@interop("ml_godot")
pub fn ml_godot_node3d_is_visible(node: i64) -> i64
    return 1
end fn

@interop("ml_godot")
pub fn ml_godot_node3d_set_visible(node: i64, visible: i64)
end fn

# Convenience: get position as Vector3
pub fn node3d_get_position(node: i64) -> math.Vector3
    return math.Vector3 {
        x: ml_godot_node3d_get_position_x(node),
        y: ml_godot_node3d_get_position_y(node),
        z: ml_godot_node3d_get_position_z(node)
    }
end fn

pub fn node3d_set_position(node: i64, v: math.Vector3)
    ml_godot_node3d_set_position(node, v.x, v.y, v.z)
end fn

pub fn node3d_get_global_position(node: i64) -> math.Vector3
    return math.Vector3 {
        x: ml_godot_node3d_get_global_position_x(node),
        y: ml_godot_node3d_get_global_position_y(node),
        z: ml_godot_node3d_get_global_position_z(node)
    }
end fn

pub fn node3d_set_global_position(node: i64, v: math.Vector3)
    ml_godot_node3d_set_global_position(node, v.x, v.y, v.z)
end fn

pub fn node3d_look_at(node: i64, target: math.Vector3)
    ml_godot_node3d_look_at(node, target.x, target.y, target.z)
end fn

# ── CharacterBody3D ───────────────────────────────────────────────────────────

@interop("ml_godot")
pub fn ml_godot_charbody3d_get_velocity_x(node: i64) -> f64  return 0.0 end fn
@interop("ml_godot")
pub fn ml_godot_charbody3d_get_velocity_y(node: i64) -> f64  return 0.0 end fn
@interop("ml_godot")
pub fn ml_godot_charbody3d_get_velocity_z(node: i64) -> f64  return 0.0 end fn

@interop("ml_godot")
pub fn ml_godot_charbody3d_set_velocity(node: i64, x: f64, y: f64, z: f64)
end fn

@interop("ml_godot")
pub fn ml_godot_charbody3d_move_and_slide(node: i64)
end fn

@interop("ml_godot")
pub fn ml_godot_charbody3d_is_on_floor(node: i64) -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_charbody3d_is_on_wall(node: i64) -> i64
    return 0
end fn

pub fn charbody3d_get_velocity(node: i64) -> math.Vector3
    return math.Vector3 {
        x: ml_godot_charbody3d_get_velocity_x(node),
        y: ml_godot_charbody3d_get_velocity_y(node),
        z: ml_godot_charbody3d_get_velocity_z(node)
    }
end fn

pub fn charbody3d_set_velocity(node: i64, v: math.Vector3)
    ml_godot_charbody3d_set_velocity(node, v.x, v.y, v.z)
end fn

pub fn charbody3d_move_and_slide(node: i64)
    ml_godot_charbody3d_move_and_slide(node)
end fn

pub fn charbody3d_is_on_floor(node: i64) -> bool
    return ml_godot_charbody3d_is_on_floor(node) == 1
end fn
