import ny.lang.runtime.*
import ny.lang.core.*

fn main() -> i32
    let values: array<i32> = [10, 20, 30]
    let wrapped: Option<Option<array<i32>>> = Option.Some(Option.Some([40, 41]))
    let before = stats()
    println(before.live_allocations >= (1 as i64))
    println(before.current_heap_usage >= (24 as i64))
    println(before.total_allocated >= before.current_heap_usage)

    enter_critical_section()
    println(stats().critical_section_depth)
    leave_critical_section()
    println(stats().critical_section_depth)

    collect(2)
    println(values[1])
    println(wrapped.unwrap().unwrap()[1])
    let after = stats()
    println(after.total_collections)
    println(after.current_heap_usage == allocated_bytes())
    return 0
end fn
