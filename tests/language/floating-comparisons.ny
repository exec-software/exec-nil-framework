fn main() -> i32
    let base: f64 = 1.5
    let shifted: f64 = base + 0.25
    println(base)
    println(shifted)
    println(5.5 % 2.0)
    println(1.1 != 1.9)
    if 1.5 < 2.5 then
        println("lt")
    end if
    if 1.5 <= 1.5 then
        println("le")
    end if
    if 2.5 > 1.5 then
        println("gt")
    end if
    if 2.5 >= 2.5 then
        println("ge")
    end if
    if 2.5 == 2.5 then
        println("eq")
    end if
    if 1.5 != 2.5 then
        println("ne")
    end if
    return 0
end fn
