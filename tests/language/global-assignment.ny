let mut counter: i32 = 2

fn increment()
    counter = counter + 3
end fn

fn update_shadow() -> i32
    let mut counter: i32 = 1
    counter = counter + 1
    return counter
end fn

fn main() -> i32
    increment()
    println(counter)
    println(update_shadow())
    return 0
end fn
