@pragma once

module nil.std.math

import nil.std.math.random_generator

# Deterministic MINSTD helpers. The global stream starts at seed 12345.
# Use random_generator for independent streams; neither API is cryptographic.
# Scalar storage is supported by both the interpreter and native backend.
let mut global_rng: i64 = 12345

pub fn seed(value: i64)
    global_rng = rng_normalize(value)
end fn

fn next_raw() -> i64
    global_rng = rng_step(global_rng)
    return global_rng
end fn

# One draw in [1, 2147483646].
pub fn random_i32() -> i32
    return next_raw() as i32
end fn

# One draw strictly between 0.0 and 1.0.
pub fn random_f64() -> f64
    return (next_raw() as f64) / 2147483647.0
end fn

# Inclusive bounds, retaining the historical two-draw mapping. This is not
# a guarantee of uniform sampling or coverage of every i32 value.
pub fn random_range(min: i32, max: i32) -> i32
    # A temporary record shares the bounded algorithm; keep an independent
    # random_generator when reusing a stream in allocation-sensitive code.
    let stream = random_generator.new(global_rng)
    let result = stream.range_i32(min, max)
    global_rng = stream.state
    return result
end fn

# Finite bounds in [min, max); equal bounds return min without drawing.
pub fn random_range_f64(min: f64, max: f64) -> f64
    rng_validate_f64_range(min, max)
    if min == max then return min end if
    return rng_scale_f64(min, max, random_f64())
end fn

pub fn random_bool() -> bool
    return random_i32() % 2 == 0
end fn
