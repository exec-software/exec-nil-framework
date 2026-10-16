module local_owner

import later_module

fn select_value() -> i32
    return 41
end fn

fn main() -> i32
    println(select_value() + 1)
    println(local_owner::select_value() + 2)
    println(later_module::select_value())
    return 0
end fn
