@pragma once
module ny.nullptr.node

import ny.godot.node
import ny.godot.math

# =============================================================================
# ny.nullptr.node — Node lifecycle utilities for nullptr bridge
# =============================================================================
# Provides node lifecycle callbacks and common operations used by Ny scripts.
# =============================================================================

# ── Node Lifecycle Callback Exports ───────────────────────────────────────────

@interop("ml_godot")
pub fn ml_godot_script_node_ready(node: i64)
end fn

@interop("ml_godot")
pub fn ml_godot_script_node_process(node: i64, delta: f64)
end fn

@interop("ml_godot")
pub fn ml_godot_script_node_physics_process(node: i64, delta: f64)
end fn

@interop("ml_godot")
pub fn ml_godot_script_node_exit_tree(node: i64)
end fn

@interop("ml_godot")
pub fn ml_godot_script_node_notification(node: i64, what: i32)
end fn

# ── Node Printing ─────────────────────────────────────────────────────────────

@interop("ml_godot")
pub fn ml_godot_script_node_print(node: i64, msg: text)
end fn

# ── Node Tree Traversal ───────────────────────────────────────────────────────

@interop("ml_godot")
pub fn ml_godot_script_node_get_parent(node: i64) -> i64
    return ml_godot_node_get_parent(node)
end fn

@interop("ml_godot")
pub fn ml_godot_script_node_get_child_count(node: i64) -> i64
    return ml_godot_node_get_child_count(node)
end fn

@interop("ml_godot")
pub fn ml_godot_script_node_get_child(node: i64, index: i64) -> i64
    return ml_godot_node_get_child(node, index)
end fn

@interop("ml_godot")
pub fn ml_godot_script_node_get_node(node: i64, path: text) -> i64
    return ml_godot_node_find_child(node, path)
end fn

@interop("ml_godot")
pub fn ml_godot_script_node_is_inside_tree(node: i64) -> i64
    return ml_godot_node_is_inside_tree(node)
end fn

# ── Node Properties ───────────────────────────────────────────────────────────

@interop("ml_godot")
pub fn ml_godot_script_node_get_name(node: i64) -> text
    return ml_godot_node_get_name(node)
end fn

@interop("ml_godot")
pub fn ml_godot_script_node_set_name(node: i64, name: text)
    ml_godot_node_set_name(node, name)
end fn

@interop("ml_godot")
pub fn ml_godot_script_node_get_instance_id(node: i64) -> i64
    return node
end fn

@interop("ml_godot")
pub fn ml_godot_script_node_get_class(node: i64) -> text
    return ""
end fn

# ── Node Visibility ───────────────────────────────────────────────────────────

@interop("ml_godot")
pub fn ml_godot_script_node_set_visible(node: i64, visible: i64)
    ml_godot_node_set_visible(node, visible)
end fn

@interop("ml_godot")
pub fn ml_godot_script_node_get_visible(node: i64) -> i64
    return ml_godot_node_get_property_i64(node, "visible")
end fn

# ── Signal Emission ───────────────────────────────────────────────────────────

@interop("ml_godot")
pub fn ml_godot_script_node_emit_signal(node: i64, signal_name: text)
    ml_godot_node_emit_signal(node, signal_name)
end fn

@interop("ml_godot")
pub fn ml_godot_script_node_emit_signal_i64(node: i64, signal_name: text, arg0: i64)
    ml_godot_node_emit_signal_i64(node, signal_name, arg0)
end fn

@interop("ml_godot")
pub fn ml_godot_script_node_emit_signal_f64(node: i64, signal_name: text, arg0: f64)
    ml_godot_node_emit_signal_f64(node, signal_name, arg0)
end fn

@interop("ml_godot")
pub fn ml_godot_script_node_emit_signal_text(node: i64, signal_name: text, arg0: text)
    ml_godot_node_emit_signal_text(node, signal_name, arg0)
end fn

# ── Method Calling ────────────────────────────────────────────────────────────

@interop("ml_godot")
pub fn ml_godot_script_node_call(node: i64, method: text) -> i64
    return ml_godot_node_call(node, method)
end fn

@interop("ml_godot")
pub fn ml_godot_script_node_call_arg_i64(node: i64, method: text, arg: i64) -> i64
    return ml_godot_node_call_arg_i64(node, method, arg)
end fn

@interop("ml_godot")
pub fn ml_godot_script_node_call_arg_f64(node: i64, method: text, arg: f64) -> i64
    return ml_godot_node_call_arg_f64(node, method, arg)
end fn

@interop("ml_godot")
pub fn ml_godot_script_node_call_arg_text(node: i64, method: text, arg: text) -> i64
    return ml_godot_node_call_arg_text(node, method, arg)
end fn

# ── Convenience Wrappers ──────────────────────────────────────────────────────

pub fn script_node_ready(node: i64)
    ml_godot_script_node_ready(node)
end fn

pub fn script_node_process(node: i64, delta: f64)
    ml_godot_script_node_process(node, delta)
end fn

pub fn script_node_physics_process(node: i64, delta: f64)
    ml_godot_script_node_physics_process(node, delta)
end fn

pub fn script_node_exit_tree(node: i64)
    ml_godot_script_node_exit_tree(node)
end fn

pub fn script_node_notification(node: i64, what: i32)
    ml_godot_script_node_notification(node, what)
end fn

pub fn script_node_print(node: i64, msg: text)
    ml_godot_script_node_print(node, msg)
end fn

pub fn script_node_get_parent(node: i64) -> i64
    return ml_godot_script_node_get_parent(node)
end fn

pub fn script_node_get_child_count(node: i64) -> i64
    return ml_godot_script_node_get_child_count(node)
end fn

pub fn script_node_get_child(node: i64, index: i64) -> i64
    return ml_godot_script_node_get_child(node, index)
end fn

pub fn script_node_get_node(node: i64, path: text) -> i64
    return ml_godot_script_node_get_node(node, path)
end fn

pub fn script_node_is_inside_tree(node: i64) -> bool
    return ml_godot_script_node_is_inside_tree(node) == 1
end fn

pub fn script_node_get_name(node: i64) -> text
    return ml_godot_script_node_get_name(node)
end fn

pub fn script_node_set_name(node: i64, name: text)
    ml_godot_script_node_set_name(node, name)
end fn

pub fn script_node_get_instance_id(node: i64) -> i64
    return ml_godot_script_node_get_instance_id(node)
end fn

pub fn script_node_set_visible(node: i64, visible: bool)
    ml_godot_script_node_set_visible(node, visible as i64)
end fn

pub fn script_node_get_visible(node: i64) -> bool
    return ml_godot_script_node_get_visible(node) == 1
end fn

pub fn script_node_emit_signal(node: i64, signal_name: text)
    ml_godot_script_node_emit_signal(node, signal_name)
end fn

pub fn script_node_call(node: i64, method: text) -> i64
    return ml_godot_script_node_call(node, method)
end fn

pub fn script_node_call_arg_i64(node: i64, method: text, arg: i64) -> i64
    return ml_godot_script_node_call_arg_i64(node, method, arg)
end fn

pub fn script_node_call_arg_f64(node: i64, method: text, arg: f64) -> i64
    return ml_godot_script_node_call_arg_f64(node, method, arg)
end fn

pub fn script_node_call_arg_text(node: i64, method: text, arg: text) -> i64
    return ml_godot_script_node_call_arg_text(node, method, arg)
end fn