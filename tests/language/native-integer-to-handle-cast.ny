fn main() -> i32
    let address: i64 = 0
    let wrong = address as ptr<i32>
    wrong[0] = 7
    return 0
end fn
