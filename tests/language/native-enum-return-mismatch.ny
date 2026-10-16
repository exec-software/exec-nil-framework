enum source_mode
    ready
end enum

enum target_mode
    ready
end enum

fn wrong_mode() -> target_mode
    return source_mode.ready
end fn

fn main() -> i32
    let value = wrong_mode()
    return 0
end fn
