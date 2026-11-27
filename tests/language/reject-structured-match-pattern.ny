enum Pair
    Both(i32, i32)
end enum

fn main() -> i32
    let pair: Pair = Pair.Both(1, 2)
    match pair
        Pair.Both(1, right) -> println(right)
    end match
    return 0
end fn
