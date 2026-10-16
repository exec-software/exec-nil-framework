module nonvoid_fallthrough

fn select_value(selected: bool) -> i32
    if selected then
        return 42
    end if
end fn

fn main() -> i32
    return select_value(false)
end fn
