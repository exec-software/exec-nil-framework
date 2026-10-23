import nil.std.text

pattern price
capture amount matching one or more ascii digits
require next to match literal " USD"
end pattern

fn main() -> i32
    let pattern = compile_pattern("(?<word>[A-Za-z]+)([0-9]+)?").unwrap()
    let found = first_match(pattern, "!alpha42 beta").unwrap().unwrap()
    println(capture_named(found, "word").unwrap())
    println(capture_index(found, 0).unwrap())
    println(capture_index(found, 2).unwrap())
    println(capture_start(found, 0).unwrap())
    println(capture_length(found, 0).unwrap())
    let second = next_match(found).unwrap().unwrap()
    println(capture_index(second, 0).unwrap())
    println(capture_index(second, 2).is_none())
    println(next_match(second).unwrap().is_none())
    println(capture_named(found, "missing").is_none())
    println(capture_index(found, -1).is_none())
    println(capture_index(found, 99).is_none())
    println(full_match(pattern, "a7").unwrap().is_some())
    println(full_match(pattern, "_a7").unwrap().is_none())
    println(search_pattern(pattern, "abc", -1).is_error())
    println(search_pattern(pattern, "abc", 4).is_error())
    println(compile_pattern("[").is_error())
    let empty = compile_pattern("").unwrap()
    let mut cursor = first_match(empty, "é").unwrap()
    let mut count = 0
    while cursor.is_some()
        count = count + 1
        cursor = next_match(cursor.unwrap()).unwrap()
    end while
    println(count)
    let mut index = 0
    while index < 30000
        let temporary = first_match(pattern, "gc17").unwrap().unwrap()
        if capture_index(temporary, 2).unwrap() != "17" then return 9 end if
        index = index + 1
    end while
    println(capture_index(found, 0).unwrap())
    let price_pattern = compile_pattern(price).unwrap()
    let price_match = first_match(price_pattern, "12 USD").unwrap().unwrap()
    println(capture_named(price_match, "amount").unwrap())
    return 0
end fn
