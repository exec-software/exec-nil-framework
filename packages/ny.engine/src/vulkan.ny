module ny.engine
import ny.lang.core
import console
# import engine_window
import engine_native

# =============================================================================
# Gehenna Native Bindings
# =============================================================================

# Window Management (bypass mimic for direct Gehenna windowing)
@interop("gehenna_native")
pub fn native_create_window(title: text, width: i32, height: i32) -> i64
    return (0 as i64)
end fn

@interop("gehenna_native")
pub fn native_poll_events()
end fn

@interop("gehenna_native")
pub fn native_should_close() -> i32
    return 0
end fn

@interop("gehenna_native")
pub fn native_sleep(ms: i32)
end fn

# Renderer Lifecycle
@interop("gehenna_native")
pub fn native_init_renderer(hwnd_value: i64, width: i32, height: i32) -> i64
    return (0 as i64)
end fn

@interop("gehenna_native")
pub fn native_shutdown_renderer(handle: i64)
end fn

@interop("gehenna_native")
pub fn native_begin_frame(handle: i64) -> i32
    return -1
end fn

@interop("gehenna_native")
pub fn native_render_frame(
    handle: i64,
    cam_x: f64, cam_y: f64, cam_z: f64,
    cam_yaw: f64, cam_pitch: f64,
    cr: f64, cg: f64, cb: f64
) -> i32
    return -1
end fn

@interop("gehenna_native")
pub fn native_begin_render_pass(handle: i64, r: f64, g: f64, b: f64)
end fn

@interop("gehenna_native")
pub fn native_end_render_pass(handle: i64)
end fn

@interop("gehenna_native")
pub fn native_end_frame(handle: i64) -> i32
    return -1
end fn

@interop("gehenna_native")
pub fn native_create_vertex_buffer(handle: i64, size: i64) -> i64
    return (0 as i64)
end fn

@interop("gehenna_native")
pub fn native_bind_vertex_buffer(handle: i64, buffer: i64)
end fn

@interop("gehenna_native")
pub fn native_draw(handle: i64, vertex_count: i32)
end fn

@interop("gehenna_native")
pub fn native_init_simple_pipeline(handle: i64) -> i32
    return -1
end fn

@interop("gehenna_native")
pub fn native_bind_pipeline(handle: i64)
end fn

@interop("gehenna_native")
pub fn native_write_float(ptr: i64, offset: i64, value: f64)
end fn

@interop("gehenna_native")
pub fn native_write_i32(ptr: i64, offset: i64, value: i32)
end fn

@interop("gehenna_native")
@interop("gehenna_native")
pub fn native_update_uniforms(handle: i64, data_ptr: i64)
end fn

@interop("gehenna_native")
pub fn native_create_buffer(size: i64, usage: i32) -> i64
end fn

@interop("gehenna_native")
pub fn native_map_memory(handle: i64, buffer_handle: i64) -> i64
    return (0 as i64)
end fn

@interop("gehenna_native")
pub fn native_unmap_memory(handle: i64, buffer_handle: i64)
end fn

@interop("gehenna_native")
pub fn native_get_key(keycode: i32) -> i32
    return 0
end fn

@interop("gehenna_native")
pub fn native_get_mouse_delta() -> i64
    # Returns packed i64: high 32 bits = dx, low 32 bits = dy
    return (0 as i64)
end fn

@interop("gehenna_native")
pub fn native_update_camera(handle: i64, x: f64, y: f64, z: f64, yaw: f64, pitch: f64)
end fn


pub fn simple_test()
    core.print("[Vulkan] Simple Test Function Called!\n")
end fn

pub fn create_instance()
    # No-op in shim, native_init_renderer handles creation
end fn

# pub function init_from_window(window: engine_window.Window) -> i64
#     let r = native_init_renderer(window.handle, window.width, window.height)
#     if r > (0 as i64) then
#         native_init_simple_pipeline(r)
#     end if
#     return r
# end function

pub fn is_valid(renderer: i64) -> bool
    return renderer > (0 as i64)
end fn

pub fn create_vertex_buffer(renderer: i64, size_bytes: i64) -> i64
    return native_create_vertex_buffer(renderer, size_bytes)
end fn

pub fn shutdown(renderer: i64)
    native_shutdown_renderer(renderer)
end fn

pub fn begin_render_pass_packed(renderer: i64, clear_color: i32)
    # Extract RGB from 0xRRGGBB (assuming integer format from mimic_color)
    let r_val = (clear_color / 65536) % 256
    let g_val = (clear_color / 256) % 256
    let b_val = clear_color % 256

    let rf = (r_val as f64) / 255.0
    let gf = (g_val as f64) / 255.0
    let bf = (b_val as f64) / 255.0

    native_begin_render_pass(renderer, rf, gf, bf)
end fn

# pub function begin_render_pass(renderer: i64, r: f32, g: f32, b: f32)
#     native_begin_render_pass(renderer, (r as f64), (g as f64), (b as f64))
# end function

pub fn stop_render_pass(renderer: i64)
    native_end_render_pass(renderer)
end fn


