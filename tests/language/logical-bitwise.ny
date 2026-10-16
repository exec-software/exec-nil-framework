fn should_not_run() -> bool
    println("short-circuit failed")
    return true
end fn

fn main() -> i32
    let both: bool = false && should_not_run()
    let either: bool = true || should_not_run()
    println(both)
    println(either)
    println(not false)
    println(6 & 3)
    println(6 | 3)
    println(6 ^ 3)
    println(3 << 2)
    println(12 >> 2)
    return 0
end fn
