@pragma once
# =============================================================================
# nil.std.math — Mathematics Package
# =============================================================================
# Mathematical constants, functions, and geometric types.
#
# Provides:
#   - Constants: PI, E, TAU, SQRT_2, LN_2, LN_10 (math.ny)
#   - Basic: abs, floor, ceil, round, trunc, pow, sqrt, exp, log (math.ny)
#   - Trigonometry: sin, cos, tan, asin, acos, atan, atan2 (trig.ny)
#   - Vectors: vec2, vec3 with dot, cross, normalize, length (vector.ny)
#   - Stable norms: hypot2, hypot3, is_finite (norms.matrix)
#   - Checked vectors: is_finite, try_normalize -> Option (vector.ny)
#   - Random: deterministic global and independent seeded generators
#
# Cross-refs:
#   ny.lang.core    — depends on primitive numeric types
#   ny.engine       — engine.math extends with vec4, quat, mat4
# =============================================================================

package nil.std.math

import nil.std.math.math
import nil.std.math.norms
import nil.std.math.trig
import nil.std.math.vector
import nil.std.math.random_generator
import nil.std.math.random
