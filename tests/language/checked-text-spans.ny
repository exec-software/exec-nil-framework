module validation.checked_text_spans

import nil.std.core
import nil.std.text
import ny.lang.text

fn require(ok: bool, label: text)
    assert_true(ok, label)
end fn

fn present(actual: Option<text>, expected: text, label: text)
    require(actual.is_some(), label)
    require(actual.unwrap() == expected, label)
end fn

fn absent(actual: Option<text>, label: text)
    require(actual.is_none(), label)
end fn

fn main() -> i32
    let mut source: text = "abcdef"
    let alias = source
    present(try_substring("", 0, 0), "", "empty source has a valid empty span")
    present(try_substring(source, 0, 0), "", "empty prefix")
    present(try_substring(source, 3, 0), "", "empty interior")
    present(try_substring(source, 6, 0), "", "empty endpoint")
    present(try_substring(source, 0, 6), "abcdef", "whole source")
    present(try_substring(source, 1, 3), "bcd", "interior span")
    present(try_substring(source, 5, 1), "f", "last byte")
    absent(try_substring("", 0, 1), "nonempty span in empty source")
    absent(try_substring("", 1, 0), "empty span beyond empty source")
    absent(try_substring(source, 6, 1), "nonempty endpoint")
    absent(try_substring(source, 5, 2), "span crosses endpoint")
    absent(try_substring(source, 1, 6), "length must fit remaining bytes")
    require(not try_substring(source, 6, 0).is_none(), "Some empty is not None")
    println("checked text distinguishes empty spans from missing spans")

    let invalid_starts: array<i64> = [-1, -2147483648, -9223372036854775808,
        7, 2147483647, 2147483648, 9223372036854775807]
    for start in invalid_starts
        absent(try_substring(source, start, 0), "invalid start with empty span")
        absent(try_substring(source, start, 1), "invalid start with one byte")
        absent(try_char_at(source, start), "invalid character index")
    end for
    let invalid_lengths: array<i64> = [-1, -2147483648, -9223372036854775808,
        7, 2147483647, 2147483648, 9223372036854775807]
    for count in invalid_lengths
        absent(try_substring(source, 0, count), "invalid span length")
        absent(try_substring(source, 1, count), "overflowing or invalid endpoint")
    end for
    absent(try_substring(source, 9223372036854775807, 9223372036854775807), "both bounds maximal")
    absent(try_substring(source, -9223372036854775808, -9223372036854775808), "both bounds minimal")
    absent(try_char_at("", 0), "empty source has no character")
    absent(try_char_at(source, 6), "character endpoint is exclusive")
    present(try_char_at(source, 0), "a", "first character")
    present(try_char_at(source, 5), "f", "last character")
    require(source == "abcdef" and alias == "abcdef", "rejected calls preserve source and alias")
    println("checked text rejects signed-width extremes without mutation")

    # UTF-8 bytes: A(1), é(2), 🙂(4), Z(1). Splitting a code point is allowed.
    # Length plus reconstruction checks exact bytes without printing invalid UTF-8.
    let utf8 = "Aé🙂Z"
    require(utf8.len() == 8, "byte length is distinct from scalar count")
    present(try_substring(utf8, 1, 2), "é", "two-byte scalar span")
    present(try_substring(utf8, 3, 4), "🙂", "four-byte scalar span")
    present(try_substring(utf8, 7, 1), "Z", "byte after Unicode scalars")
    present(try_substring(utf8, 8, 0), "", "UTF-8 empty endpoint")
    let first_half = try_char_at(utf8, 1).unwrap()
    let second_half = try_char_at(utf8, 2).unwrap()
    require(first_half.len() == 1 and second_half.len() == 1, "character access returns one byte")
    require(first_half + second_half == "é", "split scalar bytes reconstruct exactly")
    let crossing = try_substring(utf8, 2, 3).unwrap()
    require(crossing.len() == 3, "span may start and end inside UTF-8 scalars")
    require(try_substring(utf8, 0, 2).unwrap() + crossing + try_substring(utf8, 5, 3).unwrap() == utf8,
        "partial scalar spans preserve exact byte order")
    let mut rebuilt: text = ""
    let mut index: i64 = 0
    while index < 8
        let byte = try_char_at(utf8, index).unwrap()
        require(byte.len() == 1, "every valid byte index is present")
        rebuilt = rebuilt + byte
        index = index + 1
    end while
    require(rebuilt == "Aé🙂Z", "all byte accesses reconstruct the literal")
    absent(try_char_at(utf8, 8), "UTF-8 character endpoint")
    println("checked text spans preserve UTF-8 bytes including partial scalars")

    present(try_substring(try_substring(source, 1, 4).unwrap(), 1, 2), "cd", "nested checked spans")
    require(try_substring(source, 1, 3).unwrap().len() == 3, "nested Option and text methods")
    present(ny::lang::text::try_substring(source, 2, 2), "cd", "legacy qualified import")
    present(ny::lang::text::try_char_at(source, 3), "d", "legacy qualified character import")
    let retained = try_substring(source, 1, 3).unwrap()
    source = "changed"
    require(alias == "abcdef" and retained == "bcd", "results and aliases survive source reassignment")
    println("checked text composes through methods and standard imports")

    require(substring(alias, 1, 9223372036854775807) == "bcdef", "legacy length clamps before overflow")
    require(substring(alias, 2, 9223372036854775807) == "cdef", "legacy maximal length from interior")
    require(substring(alias, 1, 2147483647) == "bcdef", "legacy i32 maximum length")
    require(substring(alias, 1, 2147483648) == "bcdef", "legacy length above i32 maximum")
    require(substring(alias, -1, 2) == "", "legacy negative start remains empty")
    require(substring(alias, 0, -9223372036854775808) == "", "legacy negative length remains empty")
    require(substring(alias, 9223372036854775807, 1) == "", "legacy outside start remains empty")
    require(substring(alias, 6, 0) == "", "legacy endpoint remains empty")
    require(char_at(alias, 6) == "" and char_at(alias, 9223372036854775807) == "", "legacy missing character remains empty")
    absent(try_substring(alias, 1, 9223372036854775807), "checked span rejects legacy clamped request")
    println("legacy text clamping remains compatible at full-width bounds")
    return 0
end fn
