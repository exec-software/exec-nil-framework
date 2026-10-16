module stage.three

fn add_two(left: i64, right: i64) -> i64
    return left + right
end fn

fn classify(count: i64) -> text
    if count > 0 then
        return "positive"
    elseif count < 0 then
        return "negative"
    else
        return "zero"
    end if
end fn

fn factorial(count: i64) -> i64
    if count <= 1 then
        return 1
    end if
    return count * factorial(count - 1)
end fn

fn bit_work(word: i64) -> i64
    let masked: i64 = word & 0xFF
    let shifted_up: i64 = masked << 4
    return shifted_up ^ 0x0F
end fn

fn main() -> i32
    println(add_two(1, 2))
    println(add_two(40, 2))

    let total: i64 = 2 + 3 * 4
    println(total)
    println((2 + 3) * 4)
    println(17 % 5)
    println(17 / 5)
    println(-7)

    println(3 < 4)
    println(3 == 3)
    println(3 != 4)
    println(3 >= 3)
    println(3 <= 2)

    println(true and false)
    println(true or false)
    println(not true)

    println(classify(7))
    println(classify(0))
    let sign: i64 = if 5 > 0 then 1 else -1 end if
    println(sign)

    let mut index: i64 = 0
    while index < 3
        println(index)
        index = index + 1
    end while

    for step in range(0, 4)
        if step == 2 then
            continue
        end if
        println(step)
    end for

    let mut budget: i64 = 2
    loop
        if budget <= 0 then
            break
        end if
        budget = budget - 1
    end loop
    println(budget)

    println(factorial(5))
    println(bit_work(0x1234))
    let widened: i64 = 7 as i64
    println(widened)

    println("C:\\dir\\file.txt")
    println("he said " + "\"" + "hi" + "\"")
    println("one" + "\n" + "two")

    let mut a: i64 = 1
    let mut b: i64 = 2
    println(a + b)
    return 0
end fn
