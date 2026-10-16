fn sum(..values: i32) -> i32
    let mut total: i32 = 0
    let mut index: i32 = 0
    while index < values.len()
        total = total + values[index]
        index = index + 1
    end while
    return total
end fn

fn main() -> i32
    println(sum())
    println(sum(7))
    println(sum(1, 2, 3, 4))
    return 0
end fn
