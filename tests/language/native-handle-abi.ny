fn echo_text(value: text) -> text
    return value
end fn

fn echo_array(values: array<i32>) -> array<i32>
    return values
end fn

fn echo_pointer(value: ptr<i32>) -> ptr<i32>
    return value
end fn

fn echo_bool_array(values: array<bool>) -> array<bool>
    return values
end fn

fn echo_bool_pointer(value: ptr<bool>) -> ptr<bool>
    return value
end fn

fn echo_i64_array(values: array<i64>) -> array<i64>
    return values
end fn

fn echo_i64_pointer(value: ptr<i64>) -> ptr<i64>
    return value
end fn

fn make_pointer() -> ptr<i32>
    let values = allocate<i32>(2)
    values[0] = 40
    values[1] = 2
    return values
end fn

fn main() -> i32
    println(echo_text("native handles"))

    let source: array<i32> = [10, 20, 30]
    let returned: array<i32> = echo_array(source)
    println(returned.len())
    println(returned[1])

    let allocated: ptr<i32> = echo_pointer(make_pointer())
    let exact_pointer = allocated as ptr<i32>
    println(exact_pointer[0] + exact_pointer[1])
    exact_pointer[1] = 5
    println(exact_pointer[1])
    deallocate(exact_pointer)

    let flags: array<bool> = echo_bool_array([true, false])
    if flags[0] then
        println(1)
    end if

    let flag_pointer: ptr<bool> = allocate<bool>(1)
    flag_pointer[0] = true
    let returned_flag_pointer = echo_bool_pointer(flag_pointer)
    if returned_flag_pointer[0] then
        println(1)
    end if
    deallocate(flag_pointer)

    let wide_values: array<i64> = [40 as i64, 2 as i64]
    let returned_wide_values = echo_i64_array(wide_values)
    println(returned_wide_values[0] + returned_wide_values[1])

    let wide_pointer: ptr<i64> = allocate<i64>(1)
    wide_pointer[0] = 43
    let returned_wide_pointer = echo_i64_pointer(wide_pointer)
    println(returned_wide_pointer[0])
    deallocate(returned_wide_pointer)
    return 0
end fn
