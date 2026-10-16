import ny.lang.core.*
import ny.lang.system.*

fn main() -> i32
    let scenario = arg(0)
    if scenario == "clamp-range" then
        println(clamp_i32(1, 2, 1))
    else if scenario == "division-zero" then
        println(div_floor_i32(1, 0))
    else if scenario == "division-overflow" then
        println(div_floor_i32(-2147483648, -1))
    else if scenario == "negative-power" then
        println(pow_i32(2, -1))
    else if scenario == "power-overflow" then
        println(pow_i32(2, 31))
    else if scenario == "factorial-overflow" then
        println(factorial_i32(13))
    else if scenario == "absolute-overflow" then
        println(abs_i32(-2147483648))
    else if scenario == "gcd-overflow" then
        println(gcd_i32(-2147483648, 0))
    else
        println(factorial_i32(-1))
    end if
    return 0
end fn
