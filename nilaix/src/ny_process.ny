module ny_process
# =============================================================================
# ny_process.ny — Ny wrapper around the legacy ml_process.dll bridge
#
# Provides:
#   - Process spawning with captured stdin/stdout/stderr pipes
#   - Non-blocking poll reads (drain buffer each call)
#   - Native folder/file open dialogs
#   - Directory creation
# =============================================================================

@interop("ml_process")
function process_spawn_impl(cmd: text) -> i32
end function

@interop("ml_process")
function process_write_stdin_impl(handle: i32, text_data: text) -> void
end function

@interop("ml_process")
function process_read_stdout_impl(handle: i32) -> text
end function

@interop("ml_process")
function process_read_stderr_impl(handle: i32) -> text
end function

@interop("ml_process")
function process_is_alive_impl(handle: i32) -> i32
end function

@interop("ml_process")
function process_exit_code_impl(handle: i32) -> i32
end function

@interop("ml_process")
function process_kill_impl(handle: i32) -> void
end function

@interop("ml_process")
function process_close_impl(handle: i32) -> void
end function

@interop("ml_process")
function create_directory_impl(path: text) -> i32
end function

@interop("ml_process")
function open_folder_dialog_impl(title: text) -> text
end function

@interop("ml_process")
function open_file_dialog_impl(title: text, filter_desc: text, filter_ext: text) -> text
end function

# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

# Spawn a process with captured stdin/stdout/stderr.
# Returns a handle (>0) or -1 on failure.
# cmd is the full command line including arguments.
pub function spawn(cmd: text) -> i32
    return process_spawn_impl(cmd)
end function

# Write text to the process's stdin.
pub function write_stdin(handle: i32, text_data: text) -> void
    process_write_stdin_impl(handle, text_data)
end function

# Drain and return all stdout buffered since last call. Returns "" if none.
pub function read_stdout(handle: i32) -> text
    return process_read_stdout_impl(handle)
end function

# Drain and return all stderr buffered since last call. Returns "" if none.
pub function read_stderr(handle: i32) -> text
    return process_read_stderr_impl(handle)
end function

# Returns true if the process is still running.
pub function is_alive(handle: i32) -> bool
    return process_is_alive_impl(handle) != 0
end function

# Returns the process exit code (valid once is_alive returns false).
pub function exit_code(handle: i32) -> i32
    return process_exit_code_impl(handle)
end function

# Forcefully terminate the process.
pub function kill(handle: i32) -> void
    process_kill_impl(handle)
end function

# Release all resources for the process handle.
pub function close_process(handle: i32) -> void
    process_close_impl(handle)
end function

# Create a directory (including intermediate dirs if possible).
# Returns true on success or if it already exists.
pub function create_directory(path: text) -> bool
    return create_directory_impl(path) != 0
end function

# Show a native Windows folder picker dialog.
# Returns the selected path, or "" if cancelled.
pub function open_folder_dialog(title: text) -> text
    return open_folder_dialog_impl(title)
end function

# Show a native Windows file open dialog.
# filter_desc: e.g. "Ny Source Files"
# filter_ext:  e.g. "*.ny"
# Returns the selected path, or "" if cancelled.
pub function open_file_dialog(title: text, filter_desc: text, filter_ext: text) -> text
    return open_file_dialog_impl(title, filter_desc, filter_ext)
end function
