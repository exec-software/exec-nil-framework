fn main() -> i32
    let mut total: i32 = 0
    for value in range(0, 5)
        if value == 2 then
            continue
        end if
        total = total + value
    end for
    println(total)
    return 0
end fn
