fn classify(value: text) -> i32
    match value
        "12" -> return 1
        "true" -> return 2
        "" -> return 3
        "1.5" -> return 4
        "false" -> return 5
        _ -> return 6
    end match
    return 0
end fn

fn main() -> i32
    println(classify("12"))
    println(classify("true"))
    println(classify(""))
    println(classify("1.5"))
    println(classify("false"))
    println(classify("other"))
    println(match "12" { "12" -> 7; _ -> 0; })
    println(match "true" { "true" -> 8; _ -> 0; })
    println(match "" { "" -> 9; _ -> 0; })
    println(match "1.5" { "1.5" -> 10; _ -> 0; })
    match false
        true -> println(0)
        false -> println(11)
    end match
    match '1'
        '0' -> println(0)
        '1' -> println(12)
    end match
    println(match true { false -> 0; true -> 13; _ -> 0; })
    println(match '1' { '0' -> 0; '1' -> 14; _ -> 0; })
    return 0
end fn
