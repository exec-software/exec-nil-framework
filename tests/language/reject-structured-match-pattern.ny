enum Pair
    Both(i32, i32)
end enum

fn main() -> i32
    let pair: Pair = Pair.Both(1, 2)
    match pair
        Pair.Both(left, right) -> println(left)
    end match
    return 0
end fn
