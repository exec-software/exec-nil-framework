module webview
# =============================================================================
# webview.ny — Ny wrapper around ml_webview.dll (WebView2)
#
# Usage:
#   import webview
#   let wv = webview::create("My App", 1280, 800)
#   webview::navigate(wv, "http://localhost:3000")
#   webview::run(wv)        # blocks until window is closed
# =============================================================================

# ---------------------------------------------------------------------------
# Native interop stubs (ml_webview.dll exports)
# ---------------------------------------------------------------------------

@interop("ml_webview")
function create_webview_window_impl(title: text, width: i32, height: i32) -> i32
end function

@interop("ml_webview")
function webview_navigate_impl(handle: i32, url: text) -> void
end function

@interop("ml_webview")
function webview_run_loop_impl(handle: i32) -> void
end function

@interop("ml_webview")
function webview_is_closed_impl(handle: i32) -> i32
end function

@interop("ml_webview")
function webview_eval_js_impl(handle: i32, js: text) -> void
end function

@interop("ml_webview")
function webview_poll_message_impl(handle: i32) -> text
end function

@interop("ml_webview")
function webview_set_html_impl(handle: i32, html: text) -> void
end function

@interop("ml_webview")
function webview_load_html_base_impl(handle: i32, html: text, base_uri: text) -> void
end function

@interop("ml_webview")
function webview_close_impl(handle: i32) -> void
end function

@interop("ml_webview")
function webview_minimize_impl(handle: i32) -> void
end function

@interop("ml_webview")
function webview_maximize_impl(handle: i32) -> void
end function

@interop("ml_webview")
function webview_is_maximized_impl(handle: i32) -> i32
end function

@interop("ml_webview")
function webview_drag_impl(handle: i32) -> void
end function

@interop("ml_webview")
function webview_set_position_impl(handle: i32, x: i32, y: i32, w: i32, h: i32) -> void
end function

@interop("ml_webview")
function webview_get_x_impl(handle: i32) -> i32
end function

@interop("ml_webview")
function webview_get_y_impl(handle: i32) -> i32
end function

@interop("ml_webview")
function webview_get_w_impl(handle: i32) -> i32
end function

@interop("ml_webview")
function webview_get_h_impl(handle: i32) -> i32
end function

# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

# Create a WebView2 window. Returns a handle (>0) or -1 on failure.
# Blocks briefly while WebView2 initialises (~100-300ms first run).
pub function create(title: text, width: i32, height: i32) -> i32
    return create_webview_window_impl(title, width, height)
end function

# Navigate to a URL (http://, https://, about:blank, file://, etc.)
pub function navigate(handle: i32, url: text) -> void
    webview_navigate_impl(handle, url)
end function

# Run the window message loop. Blocks until the window is closed.
# Call this after create() + navigate().
pub function run(handle: i32) -> void
    webview_run_loop_impl(handle)
end function

# Returns true if the window has been closed.
pub function is_closed(handle: i32) -> bool
    return webview_is_closed_impl(handle) != 0
end function

# Poll for next JS→Ny message. Returns "" if none pending.
# Call this in your event loop; JS sends via window.chrome.webview.postMessage(str).
pub function poll_message(handle: i32) -> text
    return webview_poll_message_impl(handle)
end function

# Set the WebView content directly from an HTML string (no URL encoding needed).
pub function set_html(handle: i32, html: text) -> void
    webview_set_html_impl(handle, html)
end function

pub function load_html_base(handle: i32, html: text, base_uri: text) -> void
    webview_load_html_base_impl(handle, html, base_uri)
end function

# Execute a JavaScript string in the page context.
# Useful for pushing state updates or triggering hot-reload.
pub function eval_js(handle: i32, js: text) -> void
    webview_eval_js_impl(handle, js)
end function

# Close the window programmatically.
pub function close(handle: i32) -> void
    webview_close_impl(handle)
end function

# Minimise the window to the taskbar.
pub function minimize(handle: i32) -> void
    webview_minimize_impl(handle)
end function

# Maximise the window; if already maximised, restore it.
pub function maximize(handle: i32) -> void
    webview_maximize_impl(handle)
end function

# Returns true if the window is currently maximised.
pub function is_maximized(handle: i32) -> bool
    return webview_is_maximized_impl(handle) != 0
end function

# Initiate a window drag (call on mousedown on the drag region in JS).
pub function drag(handle: i32) -> void
    webview_drag_impl(handle)
end function

# Set window position and size. Pass x=-1, y=-1 to center on primary monitor.
pub function set_position(handle: i32, x: i32, y: i32, w: i32, h: i32) -> void
    webview_set_position_impl(handle, x, y, w, h)
end function

# Get current window bounds.
pub function get_x(handle: i32) -> i32
    return webview_get_x_impl(handle)
end function

pub function get_y(handle: i32) -> i32
    return webview_get_y_impl(handle)
end function

pub function get_w(handle: i32) -> i32
    return webview_get_w_impl(handle)
end function

pub function get_h(handle: i32) -> i32
    return webview_get_h_impl(handle)
end function

@interop("ml_webview")
function webview_pick_directory_impl(handle: i32) -> text
end function

pub function pick_directory(handle: i32) -> text
    return webview_pick_directory_impl(handle)
end function

@interop("ml_webview")
function webview_start_bridge_impl(handle: i32, watch_path: text, base_uri: text, interval_ms: i32) -> void
end function

pub function start_bridge(handle: i32, watch_path: text, base_uri: text, interval_ms: i32) -> void
    webview_start_bridge_impl(handle, watch_path, base_uri, interval_ms)
end function

@interop("ml_webview")
function webview_set_icon_impl(handle: i32, path: text) -> void
end function

pub function set_icon(handle: i32, path: text) -> void
    webview_set_icon_impl(handle, path)
end function
