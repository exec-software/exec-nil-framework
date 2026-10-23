import nil.std.text

pattern dollar_amount
    capture amount matching one or more ascii digits
    require next to match literal " USD"
end pattern

fn main() -> i32
    let pattern = compile_pattern(dollar_amount).unwrap()
    let found = first_match(pattern, "price: 12 USD").unwrap().unwrap()
    println(capture_named(found, "amount").unwrap())
    println(capture_start(found, 0).unwrap())
    println(full_match(pattern, "12 USD").unwrap().is_none())
    let alternate = compile_pattern("(?<word>cat|dog)(?!s)").unwrap()
    println(capture_named(first_match(alternate, "cats dog").unwrap().unwrap(), "word").unwrap())
    println(compile_pattern("[").is_error())
    println(first_match(pattern, "none").unwrap().is_none())
    return 0
end fn
