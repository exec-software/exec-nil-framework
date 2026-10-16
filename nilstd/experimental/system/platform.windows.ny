@pragma once
# ny.lang.system - Windows platform interop (Win32 API wrappers)
# Minimal Win32 surface for window creation/destruction and GDI drawing

module nil.std.system

@interop("user32")
pub fn create_window_impl(title: text, width: i32, height: i32) -> void*
end fn

@interop("user32")
pub fn destroy_window_impl(hwnd: void*) -> void
end fn

@interop("user32")
pub fn show_window_impl(hwnd: void*) -> void
end fn

@interop("user32")
pub fn poll_events_impl() -> void
end fn

@interop("user32")
pub fn should_close_impl() -> bool
end fn

@interop("gdi32")
pub fn begin_draw_impl(hwnd: void*) -> void*
end fn

@interop("gdi32")
pub fn clear_window_impl(hdc: void*, color: u32) -> void
end fn

@interop("gdi32")
pub fn end_draw_impl(hdc: void*) -> void
end fn
