import nil.std.core
import nil.std.collections

fn read_decimal(source_text: text) -> Result<i64, text>
    return Convert.try_to_i64(source_text)
end fn

fn require_error(source_text: text, expected_message: text)
    match read_decimal(source_text)
        Result.Ok(_) -> panic("invalid decimal input was accepted")
        Result.Error(message) -> assert_true(message == expected_message, "exact i64 parse error")
    end match
end fn

fn churn() -> i64
    let mut total: i64 = 0
    # 5 MiB of array payload crosses the native safe-mode 4 MiB GC threshold.
    for index in range(640)
        let items = new_with_value<i64>(1024, index as i64)
        total = total + items[0]
    end for
    return total
end fn

fn main() -> i32
    println(read_decimal("0").unwrap())
    println(read_decimal("+0").unwrap())
    println(read_decimal("-0").unwrap())
    println(read_decimal("+0007").unwrap())
    println(read_decimal("00000000000000000000000000000000000000000000000001").unwrap())
    println(read_decimal("2147483648").unwrap())
    println(read_decimal("-2147483649").unwrap())
    println(read_decimal("4294967296").unwrap())
    println(read_decimal("9223372036854775807").unwrap())
    println(read_decimal("+9223372036854775807").unwrap())
    println(read_decimal("-9223372036854775808").unwrap())

    require_error("", "convert.try_to_i64: value is empty")
    let signs = ["+", "-"]
    for index in range(signs.len())
        require_error(signs[index], "convert.try_to_i64: value has no digits")
    end for
    let malformed = [" 1", "1 ", "1\t", "1\n", "1.0", "1e3", "0x10", "1_000", "++1", "+-1", "--1", "+ 1", "１２", "١", "12x"]
    for index in range(malformed.len())
        require_error(malformed[index], "convert.try_to_i64: value contains a digit outside the requested base")
    end for
    let overflow = ["9223372036854775808", "+9223372036854775808", "-9223372036854775809", "18446744073709551615", "999999999999999999999999999999999999999999999999999999999999999999", "9223372036854775808x"]
    for index in range(overflow.len())
        require_error(overflow[index], "convert.try_to_i64: value is outside the i64 range")
    end for
    println("checked-i64-errors-ok")

    let retained_success = read_decimal("9223372036" + "854775807")
    let retained_failure = read_decimal("bad" + "-decimal")
    println(churn())
    println(retained_success.unwrap())
    match retained_failure
        Result.Ok(_) -> panic("retained failure changed variant")
        Result.Error(message) -> println(message)
    end match
    println(Convert.try_to_i32("2147483647").unwrap())
    match Convert.try_to_i32("2147483648")
        Result.Ok(_) -> panic("i32 destination range was lost")
        Result.Error(message) -> println(message)
    end match
    return 0
end fn
