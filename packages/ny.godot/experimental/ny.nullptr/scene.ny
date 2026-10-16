@pragma once
module ny.nullptr.scene

import ny.godot.node
import ny.godot.math

# =============================================================================
# ny.nullptr.scene — Scene tree and node management for nullptr bridge
# =============================================================================
# This module provides scene traversal and node creation utilities
# that work with the Godot GDExtension bridge.
# =============================================================================

# ── Scene Tree ────────────────────────────────────────────────────────────────

@interop("ml_godot")
pub fn ml_godot_tree_get_root(vp: i64) -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_tree_get_current_scene(tree: i64) -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_tree_add_node(tree: i64, node: i64, p_below: i64, p_process_mode: i32)
end fn

@interop("ml_godot")
pub fn ml_godot_tree_get_node(tree: i64, path: text) -> i64
    return 0
end fn

# ── Node Creation ─────────────────────────────────────────────────────────────

@interop("ml_godot")
pub fn ml_godot_node3d_new() -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_camera3d_new() -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_canvas_layer_new() -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_mesh_instance3d_new() -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_box_mesh_new() -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_standard_material3d_new() -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_collision_shape3d_new() -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_box_shape3d_new() -> i64
    return 0
end fn

# ── Node Operations ───────────────────────────────────────────────────────────

@interop("ml_godot")
pub fn ml_godot_node_add_child(parent: i64, child: i64, p_defer: i64, p_take_over_path: i64)
end fn

@interop("ml_godot")
pub fn ml_godot_node_get_child(parent: i64, index: i64) -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_node_get_node(parent: i64, path: text) -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_node_set_visible(node: i64, visible: i64)
end fn

@interop("ml_godot")
pub fn ml_godot_node_get_visible(node: i64) -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_node_get_viewport(node: i64) -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_node_get_name(node: i64) -> text
    return ""
end fn

@interop("ml_godot")
pub fn ml_godot_node_set_name(node: i64, name: text)
end fn

# ── Node3D Operations ─────────────────────────────────────────────────────────

@interop("ml_godot")
pub fn ml_godot_node3d_get_global_position_x(node: i64) -> f64
    return 0.0
end fn

@interop("ml_godot")
pub fn ml_godot_node3d_get_global_position_y(node: i64) -> f64
    return 0.0
end fn

@interop("ml_godot")
pub fn ml_godot_node3d_get_global_position_z(node: i64) -> f64
    return 0.0
end fn

@interop("ml_godot")
pub fn ml_godot_node3d_set_global_position(node: i64, pos: math.Vec3)
end fn

@interop("ml_godot")
pub fn ml_godot_node3d_get_global_rotation_x(node: i64) -> f64
    return 0.0
end fn

@interop("ml_godot")
pub fn ml_godot_node3d_get_global_rotation_y(node: i64) -> f64
    return 0.0
end fn

@interop("ml_godot")
pub fn ml_godot_node3d_get_global_rotation_z(node: i64) -> f64
    return 0.0
end fn

@interop("ml_godot")
pub fn ml_godot_node3d_get_rotation(node: i64, x: ptr[f64], y: ptr[f64], z: ptr[f64])
end fn

@interop("ml_godot")
pub fn ml_godot_node3d_set_rotation(node: i64, x: f64, y: f64, z: f64)
end fn

# ── Camera3D Operations ───────────────────────────────────────────────────────

@interop("ml_godot")
pub fn ml_godot_camera3d_make_current(cam: i64)
end fn

@interop("ml_godot")
pub fn ml_godot_camera3d_get_current() -> i64
    return 0
end fn

# ── Viewport/WORLD Operations ─────────────────────────────────────────────────

@interop("ml_godot")
pub fn ml_godot_viewport_get_world_3d(vp: i64) -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_viewport_get_tree(vp: i64) -> i64
    return 0
end fn

# ── Godot Call Variant Helper ─────────────────────────────────────────────────

@interop("ml_godot")
pub fn ml_godot_call_method(node: i64, method: text, args: ... i64) -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_variant_new_i64(val: i64) -> i64
    return val
end fn

@interop("ml_godot")
pub fn ml_godot_variant_new_i32(val: i32) -> i64
    return val as i64
end fn

@interop("ml_godot")
pub fn ml_godot_variant_new_f64(val: f64) -> i64
    return val as i64
end fn

@interop("ml_godot")
pub fn ml_godot_variant_new_text(val: text) -> i64
    return 0
end fn

# ── Script Node Print ─────────────────────────────────────────────────────────

@interop("ml_godot")
pub fn ml_godot_script_node_print(node: i64, msg: text)
end fn

# ── Convenience Wrappers ──────────────────────────────────────────────────────

pub fn godot_tree_get_root(vp: i64) -> i64
    return ml_godot_tree_get_root(vp)
end fn

pub fn godot_node3d_new() -> i64
    return ml_godot_node3d_new()
end fn

pub fn godot_camera3d_new() -> i64
    return ml_godot_camera3d_new()
end fn

pub fn godot_canvas_layer_new() -> i64
    return ml_godot_canvas_layer_new()
end fn

pub fn godot_mesh_instance3d_new() -> i64
    return ml_godot_mesh_instance3d_new()
end fn

pub fn godot_box_mesh_new() -> i64
    return ml_godot_box_mesh_new()
end fn

pub fn godot_standard_material3d_new() -> i64
    return ml_godot_standard_material3d_new()
end fn

pub fn godot_collision_shape3d_new() -> i64
    return ml_godot_collision_shape3d_new()
end fn

pub fn godot_box_shape3d_new() -> i64
    return ml_godot_box_shape3d_new()
end fn

pub fn godot_node_add_child(parent: i64, child: i64, p_defer: bool, p_take_over_path: bool)
    ml_godot_node_add_child(parent, child, p_defer as i64, p_take_over_path as i64)
end fn

pub fn godot_node_get_node(parent: i64, path: text) -> i64
    return ml_godot_node_get_node(parent, path)
end fn

pub fn godot_node_set_visible(node: i64, visible: bool)
    ml_godot_node_set_visible(node, visible as i64)
end fn

pub fn godot_node_get_visible(node: i64) -> bool
    return ml_godot_node_get_visible(node) == 1
end fn

pub fn godot_node_get_viewport(node: i64) -> i64
    return ml_godot_node_get_viewport(node)
end fn

pub fn godot_node3d_set_global_position(node: i64, pos: math.Vec3)
    ml_godot_node3d_set_global_position(node, pos)
end fn

pub fn godot_node3d_get_rotation(node: i64, x: ptr[f64], y: ptr[f64], z: ptr[f64])
    ml_godot_node3d_get_rotation(node, x, y, z)
end fn

pub fn godot_camera3d_make_current(cam: i64)
    ml_godot_camera3d_make_current(cam)
end fn

pub fn godot_viewport_get_world_3d(vp: i64) -> i64
    return ml_godot_viewport_get_world_3d(vp)
end fn

pub fn godot_script_node_print(node: i64, msg: text)
    ml_godot_script_node_print(node, msg)
end fn

pub fn godot_call_method(node: i64, method: text, args: ... i64) -> i64
    return ml_godot_call_method(node, method, args)
end fn