
# assert.ny - Assertion Library

module nil.std.core

pub fn assert_true(condition: bool, message: text)
    if not condition then
        panic(message)
    end if
end fn

pub fn assert_false(condition: bool, message: text)
    if condition then
        panic(message)
    end if
end fn

pub fn assert_equal_i32(expected: i32, actual: i32, message: text)
    if expected != actual then
        panic(message)
    end if
end fn

pub fn assert_equal_i64(expected: i64, actual: i64, message: text)
    if expected != actual then
        panic(message)
    end if
end fn

pub fn assert_equal_f64(expected: f64, actual: f64, message: text)
    if expected != actual then
        panic(message)
    end if
end fn

pub fn assert_equal_bool(expected: bool, actual: bool, message: text)
    if expected != actual then
        panic(message)
    end if
end fn

pub fn assert_equal_text(expected: text, actual: text, message: text)
    if expected != actual then
        panic(message)
    end if
end fn
