enum source_mode
    ready
end enum

enum target_mode
    ready
end enum

fn mode_code(value: target_mode) -> i32
    return 1
end fn

fn main() -> i32
    return mode_code(source_mode.ready)
end fn
