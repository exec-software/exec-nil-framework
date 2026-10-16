enum generic_mode<T>
    ready(T)
end enum

fn consume(value: generic_mode<i32>) -> i32
    return 1
end fn

fn main() -> i32
    println(consume(0))
    return 0
end fn
