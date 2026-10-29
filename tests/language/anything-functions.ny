// Function values retain their exact signature inside anything.
// Native compilation rejects this fixture until callable values have a native ABI.
fn add_one(value: i32) -> i32
    return value + 1
end fn

fn main() -> i32
    let original: anything = add_one
    if not anything_is<fn(i32) -> i32>(original) then
        return 1
    end if
    if anything_is<fn(i64) -> i32>(original) then
        return 2
    end if
    let recovered = original as fn(i32) -> i32
    println(recovered(41))
    return 0
end fn
