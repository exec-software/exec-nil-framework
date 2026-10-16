import ny.lang.core.*
import ny.lang.system.*

struct comparison_pair
    value: i32
end struct

enum comparison_state
    ready
end enum

fn main() -> i32
    let scenario = arg(0)
    if scenario == "struct-equality" then
        let struct_left = comparison_pair { value: 1 }
        let struct_right = comparison_pair { value: 1 }
        println(struct_left == struct_right)
    else if scenario == "enum-equality" then
        let enum_left: comparison_state = comparison_state.ready
        let enum_right: comparison_state = comparison_state.ready
        println(enum_left == enum_right)
    else if scenario == "array-equality" then
        let array_left: array<i32> = [1]
        let array_right: array<i32> = [1]
        println(array_left == array_right)
    else
        let ordered_left = comparison_pair { value: 1 }
        let ordered_right = comparison_pair { value: 2 }
        println(ordered_left < ordered_right)
    end if
    return 0
end fn
