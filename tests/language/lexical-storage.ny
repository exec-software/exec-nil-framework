// Every lexical declaration owns a distinct NIL storage identity.
fn shadow_parameter(value: i64) -> i64
    let value: i64 = value + 1
    return value
end fn

fn recurse(value: i64) -> i64
    if value == 0 then
        return 0
    end if
    if true then
        let value: i64 = value - 1
        return recurse(value) + 1
    end if
    return 0
end fn

fn identity<T>(value: T) -> T
    if true then
        let value: T = value
        return value
    end if
    return value
end fn

fn main() -> i32
    let value: i64 = 40
    if true then
        let mut value: i64 = value + 2
        value = value + 1
        println(value)
        if true then
            let value: text = "nested"
            println(value)
        end if
        println(value)
    end if
    println(value)
    if true then
        let sibling: i64 = 6
        println(sibling)
    end if
    if true then
        let sibling: text = "sibling"
        println(sibling)
    end if
    println(shadow_parameter(9 as i64))
    println(recurse(4 as i64))
    println(identity<i64>(12 as i64))
    println(identity<text>("generic"))

    // The iterable resolves outside the scope of its own loop binding.
    let item: array<i32> = [3, 4]
    for item in item
        println(item)
    end for
    println(item.len())
    let limit: i32 = 2
    for limit in range(limit)
        println(limit)
    end for
    println(limit)
    for limit in range(limit, 4)
        println(limit)
    end for
    println(limit)

    // Compiler-generated loop and logical scratch slots cannot alias source.
    let idx_0: i64 = 77
    let logical_and_0: i64 = 78
    println(true && true)
    for i in range(1)
        println(i)
    end for
    println(idx_0)
    println(logical_and_0)
    return 0
end fn
