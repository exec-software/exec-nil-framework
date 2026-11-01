// NIL_STRICT_BOUNDS=1 turns the specified out-of-bounds silence into a
// diagnostic. The default contract is pinned by array-bounds-consistency.ny;
// this is its opt-in counterpart, so the two cannot drift apart unnoticed.
fn main() -> i32
    let values: array<i32> = [11, 22]
    println(values[0])
    println(values[99])
    println(999)
    return 0
end fn
