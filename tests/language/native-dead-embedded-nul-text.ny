module ny.tests.native_dead_embedded_nul_text

fn unreachable_text() -> text
    return "dead\0payload"
end fn

fn main() -> i32
    println("reachable")
    return 0
end fn
