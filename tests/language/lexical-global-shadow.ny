let* counter: i64 = 40

fn local_value() -> i64
    let counter: i64 = 7
    return counter
end fn

fn main() -> i32
    if true then
        let counter: i64 = 8
        println(counter)
    end if
    println(counter)
    println(local_value())
    println(counter)
    if true then
        let value: i64 = 10
        println(value)
    end if
    if true then
        let value: i64 = 11
        println(value)
    end if
    return 0
end fn
