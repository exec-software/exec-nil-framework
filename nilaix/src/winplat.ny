module winplat

@interop("winplat")
function create_window_impl(title: text, width: i32, height: i32) -> i32
end function

@interop("winplat")
function destroy_window_impl(handle: i32) -> void
end function

@interop("winplat")
function begin_draw_impl(handle: i32) -> i32
end function

@interop("winplat")
function clear_window_impl(dc: i32, color: i32) -> void
end function

@interop("winplat")
function end_draw_impl(dc: i32) -> void
end function

@interop("winplat")
function pop_event_impl() -> i32
end function

@interop("winplat")
function get_event_type_impl() -> i32
end function

@interop("winplat")
function get_event_key_impl() -> i32
end function

@interop("winplat")
function get_event_x_impl() -> i32
end function

@interop("winplat")
function get_event_y_impl() -> i32
end function

@interop("winplat")
function get_event_modifiers_impl() -> i32
end function

@interop("winplat")
function show_window_impl(handle: i32) -> i32
end function

@interop("winplat")
function poll_events_impl() -> void
end function

@interop("winplat")
function wait_events_timeout_impl(timeout_ms: i32) -> void
end function

@interop("winplat")
function post_empty_event_impl() -> void
end function

@interop("winplat")
function get_window_width_impl() -> i32
end function

@interop("winplat")
function get_window_height_impl() -> i32
end function

@interop("winplat")
function fill_rect_impl(handle: i32, x: i32, y: i32, width: i32, height: i32, color: i32) -> void
end function

@interop("winplat")
function draw_text_impl(handle: i32, x: i32, y: i32, text_str: text, color: i32) -> void
end function

@interop("winplat")
function should_close_impl() -> i32
end function

@interop("winplat")
function get_simulated_time_ms_impl() -> i32
end function

@interop("winplat")
function set_clipboard_text_impl(text: text) -> void
end function

@interop("winplat")
function get_clipboard_text_impl() -> text
end function

@interop("winplat")
function read_file_impl(path: text) -> text
end function

@interop("winplat")
function write_file_impl(path: text, content: text) -> i32
end function

@interop("winplat")
function list_directory_impl(path: text) -> text
end function

@interop("winplat")
function file_exists_impl(path: text) -> i32
end function

// run_command_impl not yet in winplat.dll

pub struct event
    event_type: i32
    key_code: i32
    mouse_x: i32
    mouse_y: i32
    width: i32
    height: i32
    modifiers: i32  // Bit flags: Shift=1, Ctrl=2, Alt=4
end struct

pub function create_window(title: text, width: i32, height: i32) -> i32
    return create_window_impl(title, width, height)
end function

pub function destroy_window(handle: i32) -> void
    destroy_window_impl(handle)
end function

pub function begin_draw(handle: i32) -> i32
    return begin_draw_impl(handle)
end function

pub function clear_window(dc: i32, color: i32) -> void
    clear_window_impl(dc, color)
end function

pub function end_draw(dc: i32) -> void
    end_draw_impl(dc)
end function

pub function get_next_event() -> event
    if pop_event_impl() == 0 then
        return event { event_type: 0, key_code: 0, mouse_x: 0, mouse_y: 0, width: 0, height: 0, modifiers: 0 }
    end if

    return event {
        event_type: get_event_type_impl(),
        key_code: get_event_key_impl(),
        mouse_x: get_event_x_impl(),
        mouse_y: get_event_y_impl(),
        width: 0,
        height: 0,
        modifiers: get_event_modifiers_impl()
    }
end function

pub function show_window(handle: i32) -> i32
    return show_window_impl(handle)
end function

pub function poll_events() -> void
    poll_events_impl()
end function

pub function wait_events_timeout(timeout_ms: i32) -> void
    wait_events_timeout_impl(timeout_ms)
end function

pub function post_empty_event() -> void
    post_empty_event_impl()
end function

pub function get_window_width() -> i32
    return get_window_width_impl()
end function

pub function get_window_height() -> i32
    return get_window_height_impl()
end function

pub function should_close() -> bool
    return should_close_impl() != 0
end function

pub function get_simulated_time_ms() -> i32
    return get_simulated_time_ms_impl()
end function

pub function set_clipboard_text(text: text) -> void
    set_clipboard_text_impl(text)
end function

pub function get_clipboard_text() -> text
    return get_clipboard_text_impl()
end function

pub function EVENT_KEY_PRESS() -> i32
    return 1
end function

pub function EVENT_MOUSE_MOVE() -> i32
    return 2
end function

pub function EVENT_WINDOW_CLOSE() -> i32
    return 3
end function

pub function EVENT_MOUSE_CLICK() -> i32
    return 4
end function

pub function EVENT_WINDOW_RESIZE() -> i32
    return 5
end function

pub function EVENT_MOUSE_PRESS() -> i32
    return 6
end function

pub function EVENT_MOUSE_RELEASE() -> i32
    return 7
end function

pub function EVENT_MOUSE_MOTION() -> i32
    return 8
end function

pub function EVENT_MOUSE_WHEEL() -> i32
    return 9
end function

pub function EVENT_CHAR() -> i32
    return 10
end function

pub function EVENT_SCROLL() -> i32
    return 11
end function

pub function keycode_to_char(key_code: i32) -> text
    return ""
end function

// Modifier key constants
pub function MODIFIER_SHIFT() -> i32
    return 1
end function

pub function MODIFIER_CTRL() -> i32
    return 2
end function

pub function MODIFIER_ALT() -> i32
    return 4
end function

pub function fill_rect(handle: i32, x: i32, y: i32, width: i32, height: i32, color: i32) -> void
    fill_rect_impl(handle, x, y, width, height, color)
end function

pub function draw_text(handle: i32, x: i32, y: i32, text_str: text, color: i32) -> void
    draw_text_impl(handle, x, y, text_str, color)
end function

// =========================================================================
// File I/O Wrappers
// =========================================================================

pub function read_file(path: text) -> text
    return read_file_impl(path)
end function

pub function write_file(path: text, content: text) -> bool
    return write_file_impl(path, content) != 0
end function

pub function file_exists(path: text) -> bool
    return file_exists_impl(path) != 0
end function

// Returns pipe-separated list of entries: "type:name|type:name|..."
// type: 0=file, 1=directory
pub function list_directory(path: text) -> text
    return list_directory_impl(path)
end function

// TODO: expose VM arg_count / get_arg intrinsics once interop is wired
pub function args_count() -> i32
    return 0
end function

pub function get_arg(index: i32) -> text
    return ""
end function

// Run an external command and capture its output (stub: run_command_impl not yet in winplat.dll)
pub function run_command(cmd: text) -> text
    return ""
end function

// =========================================================================
// CLI / Console helpers (print, args, exit)
// =========================================================================

@interop("winplat")
function _get_args() -> text
end function

@interop("winplat")
function _print_line(s: text)
end function

@interop("winplat")
function _print_err(s: text)
end function

@interop("winplat")
function ny_exit(code: i32)
end function

pub function get_args() -> text
    return _get_args()
end function

pub function print_line(s: text)
    _print_line(s)
end function

pub function print_err(s: text)
    _print_err(s)
end function

pub function exit_process(code: i32)
    ny_exit(code)
end function

@interop("winplat")
pub function ny_mimic_fill_rect_buffer(dc: i32, x: i32, y: i32, width: i32, height: i32, color: i32) -> void
end function

@interop("winplat")
pub function ny_mimic_draw_line_buffer(dc: i32, x1: i32, y1: i32, x2: i32, y2: i32, color: i32) -> void
end function

pub function add_simulated_time_ms(ms: i32) -> void
    // Stub for simulated time - no-op implementation
    let dummy = 0
end function

// =========================================================================
// IPC Hot Reload Client Functions
// =========================================================================

@interop("winplat")
function ipc_hot_reload_client_connect_impl(socket_path: text) -> i32
end function

@interop("winplat")
function ipc_hot_reload_client_disconnect_impl() -> void
end function

@interop("winplat")
function ipc_hot_reload_client_is_connected_impl() -> i32
end function

@interop("winplat")
function ipc_hot_reload_client_send_reload_impl(target: text) -> i32
end function

@interop("winplat")
function ipc_hot_reload_client_get_socket_path_for_pid_impl(pid: i32) -> text
end function

@interop("winplat")
function get_current_process_id_impl() -> i32
end function

// Wrapper functions for IPC hot reload
pub function ipc_hot_reload_client_connect(socket_path: text) -> bool
    return ipc_hot_reload_client_connect_impl(socket_path) != 0
end function

pub function ipc_hot_reload_client_disconnect() -> void
    ipc_hot_reload_client_disconnect_impl()
end function

pub function ipc_hot_reload_client_is_connected() -> bool
    return ipc_hot_reload_client_is_connected_impl() != 0
end function

pub function ipc_hot_reload_client_send_reload(target: text) -> bool
    return ipc_hot_reload_client_send_reload_impl(target) != 0
end function

pub function ipc_hot_reload_client_get_socket_path_for_pid(pid: i32) -> text
    return ipc_hot_reload_client_get_socket_path_for_pid_impl(pid)
end function

pub function get_current_process_id() -> i32
    return get_current_process_id_impl()
end function

// ========================================================================
// DEBUG IPC CLIENT FUNCTIONS
// ========================================================================

@interop("winplat")
function debug_ipc_client_connect_impl(socket_path: text) -> i32
end function

@interop("winplat")
function debug_ipc_client_disconnect_impl() -> void
end function

@interop("winplat")
function debug_ipc_client_is_connected_impl() -> i32
end function

@interop("winplat")
function debug_ipc_client_send_command_impl(command: text, params: text) -> i32
end function

@interop("winplat")
function debug_ipc_client_get_response_impl() -> text
end function

@interop("winplat")
function debug_ipc_server_start_impl(socket_path: text) -> i32
end function

@interop("winplat")
function debug_ipc_server_stop_impl() -> void
end function

# Wrapper functions for debug IPC
pub function debug_ipc_client_connect(socket_path: text) -> bool
    return debug_ipc_client_connect_impl(socket_path) != 0
end function

pub function debug_ipc_client_disconnect() -> void
    debug_ipc_client_disconnect_impl()
end function

pub function debug_ipc_client_is_connected() -> bool
    return debug_ipc_client_is_connected_impl() != 0
end function

pub function debug_ipc_client_send_command(command: text, params: text) -> bool
    return debug_ipc_client_send_command_impl(command, params) != 0
end function

pub function debug_ipc_client_get_response() -> text
    return debug_ipc_client_get_response_impl()
end function

pub function debug_ipc_server_start(socket_path: text) -> bool
    return debug_ipc_server_start_impl(socket_path) != 0
end function

pub function debug_ipc_server_stop() -> void
    debug_ipc_server_stop_impl()
end function

# High-level debug command helpers
pub function debug_start_debugging(file_path: text) -> bool
    if not debug_ipc_client_is_connected() then
        return false
    end if

    return debug_ipc_client_send_command("start", file_path)
end function

pub function debug_stop_debugging() -> bool
    if not debug_ipc_client_is_connected() then
        return false
    end if

    return debug_ipc_client_send_command("stop", "")
end function

pub function debug_step_into() -> bool
    if not debug_ipc_client_is_connected() then
        return false
    end if

    return debug_ipc_client_send_command("step_into", "")
end function

pub function debug_step_over() -> bool
    if not debug_ipc_client_is_connected() then
        return false
    end if

    return debug_ipc_client_send_command("step_over", "")
end function

pub function debug_step_out() -> bool
    if not debug_ipc_client_is_connected() then
        return false
    end if

    return debug_ipc_client_send_command("step_out", "")
end function

pub function debug_continue() -> bool
    if not debug_ipc_client_is_connected() then
        return false
    end if

    return debug_ipc_client_send_command("continue", "")
end function

pub function debug_set_breakpoint(file: text, line: i32, condition: text) -> bool
    if not debug_ipc_client_is_connected() then
        return false
    end if

    let params = file + ":" + string_utils::int_to_text(line) + ":" + condition
    return debug_ipc_client_send_command("set_breakpoint", params)
end function

pub function debug_remove_breakpoint(breakpoint_id: i32) -> bool
    if not debug_ipc_client_is_connected() then
        return false
    end if

    let params = string_utils::int_to_text(breakpoint_id)
    return debug_ipc_client_send_command("remove_breakpoint", params)
end function

pub function debug_get_call_stack() -> text
    if not debug_ipc_client_is_connected() then
        return "Not connected to debugger"
    end if

    debug_ipc_client_send_command("get_call_stack", "")
    return debug_ipc_client_get_response()
end function

pub function debug_get_variables() -> text
    if not debug_ipc_client_is_connected() then
        return "Not connected to debugger"
    end if

    debug_ipc_client_send_command("get_variables", "")
    return debug_ipc_client_get_response()
end function

pub function debug_get_breakpoints() -> text
    if not debug_ipc_client_is_connected() then
        return "Not connected to debugger"
    end if

    debug_ipc_client_send_command("get_breakpoints", "")
    return debug_ipc_client_get_response()
end function

pub function debug_evaluate_expression(expr: text) -> text
    if not debug_ipc_client_is_connected() then
        return "Not connected to debugger"
    end if

    debug_ipc_client_send_command("evaluate", expr)
    return debug_ipc_client_get_response()
end function

# Debug IPC socket path generation
pub function debug_get_socket_path() -> text
    let pid = get_current_process_id()
    return "/tmp/ny_debug_" + string_utils::int_to_text(pid)
end function

// =========================================================================
// LSP Client Functions
// =========================================================================

@interop("winplat")
function lsp_client_start_impl(server_path: text, file_path: text) -> i32
end function

@interop("winplat")
function lsp_client_stop_impl() -> void
end function

@interop("winplat")
function lsp_client_is_running_impl() -> i32
end function

@interop("winplat")
function lsp_client_send_request_impl(request: text) -> text
end function

@interop("winplat")
function lsp_client_did_open_impl(uri: text, content: text) -> i32
end function

@interop("winplat")
function lsp_client_did_change_impl(uri: text, content: text) -> i32
end function

@interop("winplat")
function lsp_client_completion_impl(uri: text, line: i32, column: i32) -> text
end function

@interop("winplat")
function lsp_client_definition_impl(uri: text, line: i32, column: i32) -> text
end function

@interop("winplat")
function lsp_client_hover_impl(uri: text, line: i32, column: i32) -> text
end function

@interop("winplat")
function lsp_client_diagnostics_impl() -> text
end function

// Wrapper functions for LSP client
pub function lsp_client_start(server_path: text, file_path: text) -> bool
    return lsp_client_start_impl(server_path, file_path) != 0
end function

pub function lsp_client_stop() -> void
    lsp_client_stop_impl()
end function

pub function lsp_client_is_running() -> bool
    return lsp_client_is_running_impl() != 0
end function

pub function lsp_client_send_request(request: text) -> text
    return lsp_client_send_request_impl(request)
end function

pub function lsp_client_did_open(uri: text, content: text) -> bool
    return lsp_client_did_open_impl(uri, content) != 0
end function

pub function lsp_client_did_change(uri: text, content: text) -> bool
    return lsp_client_did_change_impl(uri, content) != 0
end function

pub function lsp_client_completion(uri: text, line: i32, column: i32) -> text
    return lsp_client_completion_impl(uri, line, column)
end function

pub function lsp_client_definition(uri: text, line: i32, column: i32) -> text
    return lsp_client_definition_impl(uri, line, column)
end function

pub function lsp_client_hover(uri: text, line: i32, column: i32) -> text
    return lsp_client_hover_impl(uri, line, column)
end function

pub function lsp_client_diagnostics() -> text
    return lsp_client_diagnostics_impl()
end function
