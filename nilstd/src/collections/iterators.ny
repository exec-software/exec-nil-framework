@pragma once

module nil.std.collections
import nil.std.core

# =============================================================================
# Range Helper Function
# =============================================================================

# Create a range from start to stop (exclusive)
fn range(start: i32, stop: i32) -> range
    return range { start: start, stop: stop, step: 1 }
end fn

# Create a range with a custom step
fn range_step(start: i32, stop: i32, step: i32) -> range
    return range { start: start, stop: stop, step: step }
end fn

# =============================================================================
# Zip Helper (Disabled)
# =============================================================================
