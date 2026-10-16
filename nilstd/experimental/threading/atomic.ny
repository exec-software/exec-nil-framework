@pragma once
# Experimental design only: the declared atomic intrinsics have no runtime
# provider yet and this module is excluded from the SDK.

module nil.std.threading
import nil.std.core

# =============================================================================
# Atomic Types - Lock-Free Thread-Safe Operations
# =============================================================================

# Atomic 32-bit integer
pub struct atomic_i32
    value: i32
end struct

impl atomic_i32
    # Create a new atomic i32
    pub fn new(value: i32) -> atomic_i32
        return atomic_i32 { value: value }
    end fn

    # Load value atomically
    @intrinsic("atomic.load_i32")
    pub fn load(self) -> i32
end fn

    # Store value atomically
    @intrinsic("atomic.store_i32")
    pub fn store(mut self, value: i32)
end fn

    # Atomically add and return previous value
    @intrinsic("atomic.fetch_add_i32")
    pub fn fetch_add(mut self, delta: i32) -> i32
end fn

    # Atomically subtract and return previous value
    @intrinsic("atomic.fetch_sub_i32")
    pub fn fetch_sub(mut self, delta: i32) -> i32
end fn

    # Atomically increment by 1
    pub fn increment(mut self) -> i32
        return self.fetch_add(1)
    end fn

    # Atomically decrement by 1
    pub fn decrement(mut self) -> i32
        return self.fetch_sub(1)
    end fn

    # Compare and swap: if current == expected, set to desired
    @intrinsic("atomic.cas_i32")
    pub fn compare_and_swap(mut self, expected: i32, desired: i32) -> bool
end fn

    # Exchange: set to new value and return old value
    @intrinsic("atomic.exchange_i32")
    pub fn exchange(mut self, value: i32) -> i32
end fn
end impl

# Atomic 64-bit integer
pub struct atomic_i64
    value: i64
end struct

impl atomic_i64
    pub fn new(value: i64) -> atomic_i64
        return atomic_i64 { value: value }
    end fn

    @intrinsic("atomic.load_i64")
    pub fn load(self) -> i64
end fn

    @intrinsic("atomic.store_i64")
    pub fn store(mut self, value: i64)
end fn

    @intrinsic("atomic.fetch_add_i64")
    pub fn fetch_add(mut self, delta: i64) -> i64
end fn

    @intrinsic("atomic.fetch_sub_i64")
    pub fn fetch_sub(mut self, delta: i64) -> i64
end fn

    pub fn increment(mut self) -> i64
        return self.fetch_add(1)
    end fn

    pub fn decrement(mut self) -> i64
        return self.fetch_sub(1)
    end fn

    @intrinsic("atomic.cas_i64")
    pub fn compare_and_swap(mut self, expected: i64, desired: i64) -> bool
end fn

    @intrinsic("atomic.exchange_i64")
    pub fn exchange(mut self, value: i64) -> i64
end fn
end impl

# Atomic boolean
pub struct atomic_bool
    value: bool
end struct

impl atomic_bool
    pub fn new(value: bool) -> atomic_bool
        return atomic_bool { value: value }
    end fn

    @intrinsic("atomic.load_bool")
    pub fn load(self) -> bool
end fn

    @intrinsic("atomic.store_bool")
    pub fn store(mut self, value: bool)
end fn

    @intrinsic("atomic.cas_bool")
    pub fn compare_and_swap(mut self, expected: bool, desired: bool) -> bool
end fn

    @intrinsic("atomic.exchange_bool")
    pub fn exchange(mut self, value: bool) -> bool
end fn
end impl

# =============================================================================
# Memory Ordering
# =============================================================================

pub enum memory_order
    relaxed    # No synchronization
    acquire    # Acquire semantics (loads)
    release    # Release semantics (stores)
    acq_rel    # Both acquire and release
    seq_cst    # Sequentially consistent (strongest)
end enum

# Memory fence/barrier
@intrinsic("atomic.fence")
pub fn fence(order: memory_order)
end fn
