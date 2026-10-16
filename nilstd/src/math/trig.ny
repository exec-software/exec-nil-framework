@pragma once

module nil.std.math

import nil.std.core

const TRIG_PI: f64 = 3.14159265358979323846

# Ny Standard Library - Trigonometric Functions
# Trigonometric and hyperbolic functions for angle computations.

# =============================================================================
# Basic Trigonometric Functions (radians)
# =============================================================================

# Sine
@intrinsic("math.sin")
pub fn sin(x: f64) -> f64
end fn

# Cosine
@intrinsic("math.cos")
pub fn cos(x: f64) -> f64
end fn

# Tangent
@intrinsic("math.tan")
pub fn tan(x: f64) -> f64
end fn

# =============================================================================
# Inverse Trigonometric Functions
# =============================================================================

# Arc sine - returns radians
@intrinsic("math.asin")
pub fn asin(x: f64) -> f64
end fn

# Arc cosine - returns radians
@intrinsic("math.acos")
pub fn acos(x: f64) -> f64
end fn

# Arc tangent - returns radians
@intrinsic("math.atan")
pub fn atan(x: f64) -> f64
end fn

# Arc tangent of y/x, handling quadrant correctly
@intrinsic("math.atan2")
pub fn atan2(y: f64, x: f64) -> f64
end fn

# =============================================================================
# Hyperbolic Functions
# =============================================================================

# Hyperbolic sine
pub fn sinh(x: f64) -> f64
    if x > -0.00001 and x < 0.00001 then
        let x_squared = x * x
        return x + (x * x_squared) / 6.0
    end if
    return (exp(x) - exp(-x)) / 2.0
end fn

# Hyperbolic cosine
pub fn cosh(x: f64) -> f64
    if x > -0.00001 and x < 0.00001 then
        return 1.0 + (x * x) / 2.0
    end if
    return (exp(x) + exp(-x)) / 2.0
end fn

# Hyperbolic tangent
pub fn tanh(x: f64) -> f64
    # Avoid overflow in exp(2*x) while preserving saturation at large values.
    if x > 20.0 then return 1.0 end if
    if x < -20.0 then return -1.0 end if
    if x > -0.00001 and x < 0.00001 then
        return x - (x * x * x) / 3.0
    end if
    let doubled = exp(2.0 * x)
    return (doubled - 1.0) / (doubled + 1.0)
end fn

# =============================================================================
# Angle Conversion
# =============================================================================

# Convert degrees to radians
pub fn degrees_to_radians(degrees: f64) -> f64
    return degrees * (TRIG_PI / 180.0)
end fn

# Convert radians to degrees
pub fn radians_to_degrees(radians: f64) -> f64
    return radians * (180.0 / TRIG_PI)
end fn
