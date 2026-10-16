@pragma once
# Experimental design only: interlocked operations have no runtime provider
# yet and this module is excluded from the SDK.

module nil.std.threading

import nil.std.core

# =============================================================================
# Interlocked - Atomic Operations (mirrors System.Threading.Interlocked)
# =============================================================================

pub struct interlocked
end struct

impl interlocked
    # Atomically increment a 32-bit signed integer and return the incremented value
    @intrinsic("interlocked.increment_i32")
    pub fn increment(location: ref<i32>) -> i32
end fn

    # Atomically increment a 64-bit signed integer and return the incremented value
    @intrinsic("interlocked.increment_i64")
    pub fn increment(location: ref<i64>) -> i64
end fn

    # Atomically decrement a 32-bit signed integer and return the decremented value
    @intrinsic("interlocked.decrement_i32")
    pub fn decrement(location: ref<i32>) -> i32
end fn

    # Atomically decrement a 64-bit signed integer and return the decremented value
    @intrinsic("interlocked.decrement_i64")
    pub fn decrement(location: ref<i64>) -> i64
end fn

    # Atomically add two 32-bit signed integers and return the original value
    @intrinsic("interlocked.add_i32")
    pub fn add(location: ref<i32>, value: i32) -> i32
end fn

    # Atomically add two 64-bit signed integers and return the original value
    @intrinsic("interlocked.add_i64")
    pub fn add(location: ref<i64>, value: i64) -> i64
end fn

    # Atomically exchange values of two 32-bit signed integers
    @intrinsic("interlocked.exchange_i32")
    pub fn exchange(location: ref<i32>, value: i32) -> i32
end fn

    # Atomically exchange values of two 64-bit signed integers
    @intrinsic("interlocked.exchange_i64")
    pub fn exchange(location: ref<i64>, value: i64) -> i64
end fn

    # Atomically compare and exchange values
    @intrinsic("interlocked.compare_exchange_i32")
    pub fn compare_exchange(location: ref<i32>, value: i32, comparand: i32) -> i32
end fn

    # Atomically compare and exchange values
    @intrinsic("interlocked.compare_exchange_i64")
    pub fn compare_exchange(location: ref<i64>, value: i64, comparand: i64) -> i64
end fn

    # Atomically read a 64-bit signed integer
    @intrinsic("interlocked.read_i64")
    pub fn read(location: ref<i64>) -> i64
end fn

    # Atomically set a 32-bit signed integer and return the original value
    @intrinsic("interlocked.exchange_ref_i32")
    pub fn exchange(location: ref<ref<i32>>, value: ref<i32>) -> ref<i32>
end fn

    # Atomically set a 64-bit signed integer and return the original value
    @intrinsic("interlocked.exchange_ref_i64")
    pub fn exchange(location: ref<ref<i64>>, value: ref<i64>) -> ref<i64>
end fn

    # Memory barrier - ensures all memory operations before this are completed
    @intrinsic("interlocked.memory_barrier")
    pub fn memory_barrier() -> void
end fn

    # Full memory barrier - ensures all memory operations are completed
    @intrinsic("interlocked.memory_barrier_full")
    pub fn memory_barrier_process_wide() -> void
end fn
end impl
