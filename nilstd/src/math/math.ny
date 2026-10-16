@pragma once

module nil.std.math

import nil.std.core

# Ny Standard Library - Math Module
# Mathematical constants and functions for numeric computation.
# Primitive functions use runtime math intrinsics; composed functions are Ny.

# =============================================================================
# Mathematical Constants
# =============================================================================

pub const PI: f64 = 3.14159265358979323846
pub const E: f64 = 2.71828182845904523536
pub const TAU: f64 = 6.28318530717958647692  # 2 * PI
pub const SQRT_2: f64 = 1.41421356237309504880
pub const LN_2: f64 = 0.693147180559945309417
pub const LN_10: f64 = 2.30258509299404568402

# =============================================================================
# Basic Math Functions
# =============================================================================

# Absolute value for i32
pub fn abs_i32(x: i32) -> i32
    if x == -2147483648 then
        panic("abs_i32 is undefined for i32 minimum")
    end if
    if x < 0 then
        return -x
    else
        return x
    end if
end fn

# Absolute value for f64
@intrinsic("math.fabs")
pub fn abs(x: f64) -> f64
end fn

# Floor - round down to nearest integer
@intrinsic("math.floor")
pub fn floor(x: f64) -> f64
end fn

# Ceiling - round up to nearest integer
@intrinsic("math.ceil")
pub fn ceil(x: f64) -> f64
end fn

# Round to nearest integer
@intrinsic("math.round")
pub fn round(x: f64) -> f64
end fn

# Truncate - round towards zero
@intrinsic("math.trunc")
pub fn trunc(x: f64) -> f64
end fn

# =============================================================================
# Power and Logarithm Functions
# =============================================================================

# Power - x raised to the power y
@intrinsic("math.pow")
pub fn pow(x: f64, y: f64) -> f64
end fn

# Square root
@intrinsic("math.sqrt")
pub fn sqrt(x: f64) -> f64
end fn

# Cube root
@intrinsic("math.cbrt")
pub fn cbrt(x: f64) -> f64
end fn

# Exponential - e^x
@intrinsic("math.exp")
pub fn exp(x: f64) -> f64
end fn

# Natural logarithm - ln(x)
@intrinsic("math.log")
pub fn log(x: f64) -> f64
end fn

# Base-10 logarithm
@intrinsic("math.log10")
pub fn log10(x: f64) -> f64
end fn

# Base-2 logarithm
pub fn log2(x: f64) -> f64
    return log(x) / LN_2
end fn

# =============================================================================
# Min / Max / Clamp
# =============================================================================

# Minimum of two i32 values
pub fn min_i32(a: i32, b: i32) -> i32
    if a < b then return a else return b end if
end fn

# Maximum of two i32 values
pub fn max_i32(a: i32, b: i32) -> i32
    if a > b then return a else return b end if
end fn

# Minimum of two f64 values
@intrinsic("math.fmin")
pub fn min(a: f64, b: f64) -> f64
end fn

# Maximum of two f64 values
@intrinsic("math.fmax")
pub fn max(a: f64, b: f64) -> f64
end fn

# Clamp value to range [min_val, max_val]
pub fn clamp(x: f64, min_val: f64, max_val: f64) -> f64
    if min_val > max_val then
        panic("clamp minimum exceeds maximum")
    end if
    if x < min_val then
        return min_val
    elseif x > max_val then
        return max_val
    else
        return x
    end if
end fn

# Clamp i32 value to range
pub fn clamp_i32(x: i32, min_val: i32, max_val: i32) -> i32
    if min_val > max_val then
        panic("clamp_i32 minimum exceeds maximum")
    end if
    if x < min_val then
        return min_val
    elseif x > max_val then
        return max_val
    else
        return x
    end if
end fn

# =============================================================================
# Sign and Comparison
# =============================================================================

# Sign of a number: -1, 0, or 1
pub fn sign(x: f64) -> i32
    if x < 0.0 then
        return -1
    elseif x > 0.0 then
        return 1
    else
        return 0
    end if
end fn

# Check if value is approximately equal (within epsilon)
pub fn approx_eq(a: f64, b: f64, epsilon: f64) -> bool
    let mut diff = a - b
    if diff < 0.0 then
        diff = -diff
    end if
    return epsilon >= 0.0 and diff <= epsilon
end fn
