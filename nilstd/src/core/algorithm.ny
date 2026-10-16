@pragma once

module nil.std.core

# =============================================================================
# Comparison Utilities
# =============================================================================

# Return the minimum of two i32 values
pub fn min_i32(a: i32, b: i32) -> i32
    if a < b then
        return a
    else
        return b
    end if
end fn

# Return the maximum of two i32 values
pub fn max_i32(a: i32, b: i32) -> i32
    if a > b then
        return a
    else
        return b
    end if
end fn

# Clamp an i32 value to a range [low, high]
pub fn clamp_i32(value: i32, low: i32, high: i32) -> i32
    if low > high then
        panic("clamp_i32: low must not exceed high")
    end if
    if value < low then
        return low
    else if value > high then
        return high
    else
        return value
    end if
end fn

# Return the minimum of two i64 values
pub fn min_i64(a: i64, b: i64) -> i64
    if a < b then
        return a
    else
        return b
    end if
end fn

# Return the maximum of two i64 values
pub fn max_i64(a: i64, b: i64) -> i64
    if a > b then
        return a
    else
        return b
    end if
end fn

# =============================================================================
# Numeric Utilities
# =============================================================================

# Return the absolute value of an i32
pub fn abs_i32(x: i32) -> i32
    if x == -2147483648 then
        panic("abs_i32: result is outside the i32 range")
    end if
    if x < 0 then
        return -x
    else
        return x
    end if
end fn

# Return the absolute value of an i64
pub fn abs_i64(x: i64) -> i64
    if x == (-9223372036854775807 - 1) then
        panic("abs_i64: result is outside the i64 range")
    end if
    if x < 0 then
        return -x
    else
        return x
    end if
end fn

# Return the sign of an i32 (-1, 0, or 1)
pub fn sign_i32(x: i32) -> i32
    if x < 0 then
        return -1
    else if x > 0 then
        return 1
    else
        return 0
    end if
end fn

# Return true if value is in range [low, high] (inclusive)
pub fn in_range_i32(value: i32, low: i32, high: i32) -> bool
    return value >= low and value <= high
end fn

# Return true if value is positive
pub fn is_positive_i32(x: i32) -> bool
    return x > 0
end fn

# Return true if value is negative
pub fn is_negative_i32(x: i32) -> bool
    return x < 0
end fn

# Return true if value is zero
pub fn is_zero_i32(x: i32) -> bool
    return x == 0
end fn

# Return true if value is even
pub fn is_even_i32(x: i32) -> bool
    return x mod 2 == 0
end fn

# Return true if value is odd
pub fn is_odd_i32(x: i32) -> bool
    return x mod 2 != 0
end fn

# =============================================================================
# Integer Division Utilities
# =============================================================================

# Integer division that rounds toward negative infinity (floor division)
pub fn div_floor_i32(a: i32, b: i32) -> i32
    if b == 0 then
        panic("div_floor_i32: divisor must not be zero")
    end if
    if a == -2147483648 and b == -1 then
        panic("div_floor_i32: result is outside the i32 range")
    end if
    let q: i32 = a / b
    let r: i32 = a mod b
    # If signs differ and there's a remainder, adjust quotient down
    if (r != 0) and ((a < 0) != (b < 0)) then
        return q - 1
    else
        return q
    end if
end fn

# Modulo that always returns non-negative result (mathematical modulo)
pub fn mod_floor_i32(a: i32, b: i32) -> i32
    if b == 0 then
        panic("mod_floor_i32: divisor must not be zero")
    end if
    if a == -2147483648 and b == -1 then
        return 0
    end if
    let r: i32 = a mod b
    if r < 0 then
        if b > 0 then
            return r + b
        else
            return r - b
        end if
    else
        return r
    end if
end fn

# =============================================================================
# Power and Factorial
# =============================================================================

# Integer power (a^n) for non-negative n
pub fn pow_i32(base: i32, exp: i32) -> i32
    if exp < 0 then
        panic("pow_i32: exponent must not be negative")
    end if
    if exp == 0 then
        return 1
    end if
    let mut result: i64 = 1
    let mut e: i32 = exp
    let mut b: i64 = base as i64
    while e > 0
        if is_odd_i32(e) then
            result = result * b
            if result < -2147483648 or result > 2147483647 then
                panic("pow_i32: result is outside the i32 range")
            end if
        end if
        e = e / 2
        if e > 0 then
            b = b * b
            if b < -2147483648 or b > 2147483647 then
                panic("pow_i32: result is outside the i32 range")
            end if
        end if
    end while
    return result as i32
end fn

# Factorial for non-negative n
pub fn factorial_i32(n: i32) -> i32
    if n < 0 then
        panic("factorial_i32: input must not be negative")
    end if
    if n > 12 then
        panic("factorial_i32: result is outside the i32 range")
    end if
    if n <= 1 then
        return 1
    end if
    let mut result: i32 = 1
    let mut i: i32 = 2
    while i <= n
        result = result * i
        i = i + 1
    end while
    return result
end fn

# =============================================================================
# GCD and LCM
# =============================================================================

# Greatest common divisor using Euclidean algorithm
pub fn gcd_i32(a: i32, b: i32) -> i32
    # Widen before taking magnitudes so INT_MIN inputs remain usable whenever
    # the final greatest common divisor is representable as i32.
    let mut x: i64 = a as i64
    let mut y: i64 = b as i64
    if x < 0 then
        x = 0 - x
    end if
    if y < 0 then
        y = 0 - y
    end if
    while y != 0
        let temp: i64 = y
        y = x mod y
        x = temp
    end while
    if x > 2147483647 then
        panic("gcd_i32: result is outside the i32 range")
    end if
    return x as i32
end fn

# Least common multiple
pub fn lcm_i32(a: i32, b: i32) -> i32
    if a == 0 or b == 0 then
        return 0
    end if
    let g: i32 = gcd_i32(a, b)
    let mut result: i64 = (a / g) as i64 * (b as i64)
    if result < 0 then
        result = 0 - result
    end if
    if result > 2147483647 then
        panic("lcm_i32: result is outside the i32 range")
    end if
    return result as i32
end fn
