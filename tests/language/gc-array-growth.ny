import nil.std.runtime.*
import nil.std.system.*

fn require(passed: bool, label: text)
    if not passed then panic(label) end if
end fn

fn check_growth<T>(first: T, second: T)
    let mut values: array<T> = []
    let mut alias = values
    let before = stats()
    let mut index: i32 = 0
    while index < 65
        values.push(first)
        index = index + 1
    end while
    let grown = stats()
    # GC statistics retain their historical eight-byte array-slot unit for
    # every element type, independent of Aperture's typed payload accounting.
    require(grown.total_allocated - before.total_allocated == (520 as i64), "array growth charges cumulative bytes")
    require(grown.current_heap_usage - before.current_heap_usage == (520 as i64), "array growth charges live bytes")
    require(grown.live_allocations == before.live_allocations, "array growth preserves allocation identity")
    require(grown.total_allocated >= grown.current_heap_usage, "cumulative bytes cover live heap")

    index = 0
    while index < 17
        alias.push(second)
        index = index + 1
    end while
    let aliased = stats()
    require(values.len() == 82, "alias appends reach original array")
    require(aliased.total_allocated - grown.total_allocated == (136 as i64), "alias growth is charged exactly once")
    require(aliased.current_heap_usage - grown.current_heap_usage == (136 as i64), "alias growth live bytes")
    require(aliased.live_allocations == before.live_allocations, "alias growth shares one allocation")
    require(aliased.current_heap_usage == allocated_bytes(), "statistics agree with allocated_bytes")

    values[0] = second
    let written = stats()
    require(alias[0] == second, "aliases share element writes")
    require(written.total_allocated == aliased.total_allocated, "element writes do not allocate")
    require(written.current_heap_usage == aliased.current_heap_usage, "element writes preserve heap size")
end fn

fn check_free()
    let before = stats()
    let buffer = allocate<u8>(11)
    buffer[0] = 37 as u8
    let allocated = stats()
    require(allocated.total_allocated - before.total_allocated == (11 as i64), "raw allocation retains its typed byte unit")
    deallocate(buffer)
    let freed = stats()
    require(freed.total_allocated == allocated.total_allocated, "free preserves cumulative allocation bytes")
    require(freed.current_heap_usage == before.current_heap_usage, "free removes live bytes")
    require(freed.live_allocations == before.live_allocations, "free removes the allocation")
end fn

fn main() -> i32
    require(arg_count() == (1 as i64), "expected safe or unsafe fixture argument")
    let mode = arg(0 as i64)
    require(mode == "safe" or mode == "unsafe", "unknown fixture argument")
    let before = stats()
    enter_critical_section()
    check_growth<i32>(10, 20)
    check_growth<u8>(1 as u8, 255 as u8)
    check_growth<bool>(true, false)
    check_free()
    leave_critical_section()

    let grown = stats()
    require(grown.total_allocated - before.total_allocated == (1979 as i64), "all successful allocations counted once")
    require(grown.current_heap_usage - before.current_heap_usage == (1968 as i64), "three grown arrays remain live before collection")
    require(grown.total_allocated >= grown.current_heap_usage, "cumulative allocation invariant after free")
    if mode == "safe" then
        collect(2)
        let collected = stats()
        require(collected.total_allocated == grown.total_allocated, "collection preserves cumulative allocation bytes")
        require(collected.current_heap_usage == before.current_heap_usage, "collection reclaims departed array frames")
        require(collected.live_allocations == before.live_allocations, "collection reclaims array identities")
        require(collected.total_collections == grown.total_collections + (1 as i64), "explicit collection counted once")
    end if
    println("GC array growth accounting passed")
    return 0
end fn
