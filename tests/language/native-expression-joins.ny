module native_expression_joins

let* order: i64 = 0

fn mark(value: i64) -> i64
    order = order * 10 + value
    return value
end fn

fn combine(first: i64, second: i64, third: i64) -> i64
    return first * 100 + second * 10 + third
end fn

fn boolean_mark(value: i64) -> bool
    mark(value)
    return value != 0
end fn

fn choose_word(value: i32, flag: bool) -> i32
    return if flag then value else 12 end if
end fn

fn main() -> i32
    println(100 + (match 1 { 1 -> 20; _ -> 30; }))
    println(10 + (if true then 2 else 9 end if))
    println(10 + (if false then 2 else 9 end if))
    println(100 + (match 2 {
        1 -> 20;
        _ -> (if true then 30 else 40 end if);
    }))
    println(100 + (if false then 1 else (match 2 {
        1 -> 7;
        _ -> 8;
    }) end if))

    order = 0
    println(combine(mark(1),
        (if mark(2) == 2 then mark(3) else mark(9) end if), mark(4)))
    println(order)

    order = 0
    println(combine(mark(1),
        (match mark(2) { 2 -> mark(3); _ -> mark(9); }), mark(4)))
    println(order)

    order = 0
    println(combine(mark(1),
        (if false && boolean_mark(9) then 8 else 2 end if), mark(3)))
    println(order)

    order = 0
    println(combine(mark(1),
        (if true || boolean_mark(9) then 2 else 8 end if), mark(3)))
    println(order)

    println(choose_word(7, true))
    println(choose_word(7, false))
    println(if true then (1 < 2) else false end if)
    println(combine(1, (if true then true else false end if) as i64, 1))
    println("prefix-" + (if false then "wrong" else "text" end if))

    let mut total: i64 = 0
    for index in range(4)
        total = total + 10 + (if index < 2 then 1 else 2 end if)
    end for
    println(total)
    println(if false then (1 as i32) else (4294967297 as i64) end if)
    println(if true then (4294967297 as i64) else (1 as i32) end if)
    println((if false then (1 as i32) else 2.5 end if) == 2.5)
    println((if true then (255 as u8) else 0.5 end if) == 255.0)
    let chosen = match 2 { 1 -> (1 as i32); _ -> (4294967297 as i64); }
    println(chosen)
    println(match 1 { 1 -> (4294967297 as i64); _ -> (1 as i32); })
    println((match 2 { 1 -> (1 as i32); _ -> 2.5; }) == 2.5)
    println((match 1 { 1 -> (255 as u8); _ -> 0.5; }) == 255.0)
    return 0
end fn
