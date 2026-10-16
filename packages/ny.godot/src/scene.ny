@pragma once
module ny.godot.scene

import ny.godot.node
import ny.godot.math

# =============================================================================
# ny.godot.scene — Scene tree operations
# =============================================================================
# Provides scene tree access and node hierarchy operations.
# =============================================================================

# ── Tree Access ───────────────────────────────────────────────────────────────

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

# ── Viewport Access ───────────────────────────────────────────────────────────

@interop("ml_godot")
pub fn ml_godot_node_get_viewport(node: i64) -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_viewport_get_world_3d(vp: i64) -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_viewport_get_tree(vp: i64) -> i64
    return 0
end fn

# ── Node Hierarchy ────────────────────────────────────────────────────────────

@interop("ml_godot")
pub fn ml_godot_node_get_node(parent: i64, path: text) -> i64
    return 0
end fn

@interop("ml_godot")
pub fn ml_godot_node_add_child(parent: i64, child: i64, p_defer: i64, p_take_over_path: i64)
end fn

@interop("ml_godot")
pub fn ml_godot_node_get_child(parent: i64, index: i64) -> i64
    return 0
end fn

# ── Convenience Wrappers ──────────────────────────────────────────────────────

pub fn tree_get_root(vp: i64) -> i64
    return ml_godot_tree_get_root(vp)
end fn

pub fn tree_get_current_scene(tree: i64) -> i64
    return ml_godot_tree_get_current_scene(tree)
end fn

pub fn node_get_viewport(node: i64) -> i64
    return ml_godot_node_get_viewport(node)
end fn

pub fn viewport_get_world_3d(vp: i64) -> i64
    return ml_godot_viewport_get_world_3d(vp)
end fn

pub fn viewport_get_tree(vp: i64) -> i64
    return ml_godot_viewport_get_tree(vp)
end fn

pub fn node_get_node(parent: i64, path: text) -> i64
    return ml_godot_node_get_node(parent, path)
end fn

pub fn node_add_child(parent: i64, child: i64, p_defer: bool, p_take_over_path: bool)
    ml_godot_node_add_child(parent, child, p_defer as i64, p_take_over_path as i64)
end fn

pub fn node_get_child(parent: i64, index: i64) -> i64
    return ml_godot_node_get_child(parent, index)
end fn