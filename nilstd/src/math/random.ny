@pragma once

module nil.std.math

import nil.std.core

# Ny Standard Library - Random Number Generation
# Deterministic pseudo-random number generation utilities. This module is not
# suitable for keys, nonces, secrets, or security tokens. Ny does not currently
# ship an active cryptographically secure random provider.

# =============================================================================
# Random Number Generator State
# =============================================================================

# Simple LCG-based RNG state (Linear Congruential Generator)
# Uses standard MINSTD parameters for portability
struct rng_state
    seed: i64
end struct

# Global RNG state - initialized lazily
let mut global_rng: rng_state = rng_state { seed: 12345 }

# =============================================================================
# Seeding
# =============================================================================

# Seed the global random number generator
pub fn seed(value: i64)
    let m: i64 = 2147483647
    global_rng.seed = value % m
    if global_rng.seed < 0 then
        global_rng.seed = global_rng.seed + m
    end if
    if global_rng.seed == 0 then
        global_rng.seed = 1  # Avoid zero seed
    end if
end fn

# =============================================================================
# Random Generation Functions
# =============================================================================

# Generate next random i64 using LCG
# Uses MINSTD parameters: a = 48271, m = 2^31 - 1
fn next_raw() -> i64
    # LCG: seed = (a * seed) mod m
    let a: i64 = 48271
    let m: i64 = 2147483647  # 2^31 - 1
    global_rng.seed = (a * global_rng.seed) % m
    return global_rng.seed
end fn

# Generate the next positive MINSTD value in [1, 2147483646]
pub fn random_i32() -> i32
    let raw = next_raw()
    return (raw % 2147483647) as i32
end fn

# Generate random f64 in range [0.0, 1.0)
pub fn random_f64() -> f64
    let raw = next_raw()
    let max: f64 = 2147483647.0
    return (raw as f64) / max
end fn

# Generate random i32 in range [min, max] (inclusive)
pub fn random_range(min: i32, max: i32) -> i32
    if min > max then
        panic("random_range minimum exceeds maximum")
    end if
    if min == max then
        return min
    end if
    let range: i64 = (max as i64) - (min as i64) + 1
    # Two draws cover the entire i32 domain. Rejection sampling avoids the
    # modulo bias that occurs when the generator domain is not a multiple of
    # the requested range.
    let generator_width: i64 = 2147483646
    let generator_domain: i64 = 4611686009837453316
    let limit: i64 = generator_domain - (generator_domain % range)
    while true do
        let high: i64 = next_raw() - 1
        let low: i64 = next_raw() - 1
        let candidate: i64 = high * generator_width + low
        if candidate < limit then
            let offset: i64 = candidate % range
            return ((min as i64) + offset) as i32
        end if
    end while
    return min
end fn

# Generate random f64 in range [min, max)
pub fn random_range_f64(min: f64, max: f64) -> f64
    if min > max then
        panic("random_range_f64 minimum exceeds maximum")
    end if
    if min == max then
        return min
    end if
    let t = random_f64()
    return min + t * (max - min)
end fn

# Shuffle helper: generate random bool
pub fn random_bool() -> bool
    return random_i32() % 2 == 0
end fn
