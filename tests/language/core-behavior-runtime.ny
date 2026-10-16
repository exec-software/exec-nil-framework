import ny.lang.core.*

fn double_value(value: i32) -> i32
    return value * 2
end fn

fn main() -> i32
    println(min_i32(4, -2))
    println(max_i64(4, 9))
    println(clamp_i32(12, 0, 10))
    println(div_floor_i32(-7, 3))
    println(mod_floor_i32(-7, 3))
    println(pow_i32(3, 5))
    println(factorial_i32(12))
    println(gcd_i32(-54, 24))
    println(gcd_i32(-2147483648, 3))
    println(lcm_i32(-54, 24))
    println(compare_text("alpha", "beta").is_less())
    println(compare_i32(7, 7).reverse().is_eq())
    assert_true(true, "true assertion failed")
    assert_false(false, "false assertion failed")
    assert_equal_i32(5, 5, "equality assertion failed")
    assert_equal_i64(5000000000, 5000000000, "i64 equality assertion failed")
    assert_equal_f64(1.25, 1.25, "f64 equality assertion failed")
    assert_equal_bool(true, true, "bool equality assertion failed")
    assert_equal_text("ny", "ny", "text equality assertion failed")
    println("assertions")

    let present: Option<i32> = Option.Some(7)
    let missing: Option<i32> = Option.None
    println(present.xor(missing).unwrap())
    println(present.map(double_value).unwrap())
    let nested: Option<Option<i32>> = Option.Some(Option.Some(31))
    match nested
        Option.Some(inner) -> println(inner.unwrap())
        Option.None -> println("unexpected")
    end match
    let zipped = present.zip(Option.Some("seven"))
    match zipped
        Option.Some(pair) ->
            println(pair.first)
            println(pair.second)
        Option.None -> println("unexpected")
    end match

    let mut option_iterator = present.iter()
    println(option_iterator.has_next())
    println(option_iterator.next().unwrap())
    println(option_iterator.has_next())
    println(option_iterator.next().is_none())

    let failure: Result<i32, text> = Result.Error("broken")
    match failure.err()
        Option.Some(message) -> println(message)
        Option.None -> println("unexpected")
    end match
    let mut result_iterator = failure.iter()
    println(result_iterator.has_next())
    println(result_iterator.next().is_none())
    println(result_iterator.has_next())

    let validation: error = error.validation("bad input")
    println(validation.to_string())
    println(validation.message)
    let assertion_fault: AssertionFault = AssertionFault.new("wrong value", "sample.ny", 27, "equal")
    println(assertion_fault.to_string())
    let ai_context = AIContext {
        operation: "decode",
        intent: "recover a frame",
        variables: ["offset=12"],
        likely_causes: ["truncated input"],
        suggested_fixes: ["request another chunk"]
    }
    println(ai_context.operation)
    println(ai_context.suggested_fixes[0])

    let mut numbers: array<i32> = [5, -1, 5, 2]
    numbers = sort_i32(numbers)
    println(numbers[0])
    println(numbers[1])
    println(numbers[2])
    println(numbers[3])
    println(binary_search_i32(numbers, 5) >= 2)
    println(is_sorted_i32(numbers))

    let mut many: array<i32> = [20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1]
    many = sort_i32(many)
    println(many[0])
    println(many[19])
    println(is_sorted_i32(many))

    let merged = merge_sort_i32([4, 1, 3, 1, 2])
    println(merged[0])
    println(merged[4])
    println(min_element_i32([]).is_none())
    println(max_element_i32(numbers).unwrap())

    let words = sort_text(["zeta", "alpha", "beta"])
    println(words[0])
    println(words[2])
    println(average_f64([]).is_none())
    println(average_f64([2.0, 4.0, 6.0]).unwrap())
    let shuffled = shuffle_i32([0, 1, 2, 3, 4], -2147483648)
    println(shuffled[0])
    println(shuffled[1])
    println(shuffled[2])
    println(shuffled[3])
    println(shuffled[4])
    return 0
end fn
