enum native_mode
    ready
end enum

fn wrong_mode() -> native_mode
    return 0
end fn

fn main() -> i32
    let value = wrong_mode()
    return 0
end fn
