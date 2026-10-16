@pragma once
# =============================================================================
# ny.lang.runtime — Runtime Package
# =============================================================================
# Low-level runtime control: garbage collector, memory stats.
#
# Provides:
#   - GC.collect, allocated_bytes, stats (gc.ny)
#   - GCStats struct
#   - enter/leave critical sections for GC pauses (gc.ny)
#
# Cross-refs:
#   ny.lang.core   — memory allocation primitives
# =============================================================================

package nil.std.runtime

import nil.std.runtime.gc
