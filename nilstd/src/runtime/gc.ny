@schema
@pragma once

module nil.std.runtime

###
 Control over the Exorcist Garbage Collector.
###

# Force a collection cycle. The current interpreter is a non-generational
# mark/sweep collector, so generation is validated but all levels request a
# full collection.
@intrinsic("gc.collect")
pub fn collect(generation: i32)
end fn

# Get the currently live logical payload bytes tracked by the interpreter.
# This excludes allocator, container, and host-object overhead.
@intrinsic("gc.allocated_bytes")
pub fn allocated_bytes() -> i64
end fn

# Get heap usage statistics. Counters are nonnegative i64 values, matching the
# interpreter's scalar ABI; u64 arithmetic is not a supported scalar contract.
pub struct GCStats
    pub total_collections: i64
    # Cumulative logical payload bytes allocated since VM startup.
    pub total_allocated: i64
    # Currently live logical payload bytes (the same unit as allocated_bytes).
    pub current_heap_usage: i64
    pub live_allocations: i64
    pub critical_section_depth: i64
end struct

@intrinsic("gc.stats")
pub fn stats() -> GCStats
end fn

# Hint that we are entering a memory-intensive phase
@intrinsic("gc.enter_critical")
pub fn enter_critical_section()
end fn

# Hint that we are leaving a memory-intensive phase
@intrinsic("gc.leave_critical")
pub fn leave_critical_section()
end fn
