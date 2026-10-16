@pragma once
# Experimental design only: synchronization operations have no runtime
# provider yet and this module is excluded from the SDK.

module nil.std.threading
import nil.std.core

# =============================================================================
# Mutex - Mutual Exclusion Lock
# =============================================================================

# A mutual exclusion lock for thread-safe access to shared data
pub struct mutex<T>
    data: T
    locked: bool
end struct

impl<T> mutex<T>
    # Create a new mutex wrapping data
    pub fn new(data: T) -> mutex<T>
        return mutex { data: data, locked: false }
    end fn

    # Acquire the lock and return a guard
    @intrinsic("mutex.lock")
    pub fn lock(mut self) -> mutex_guard<T>
end fn

    # Try to acquire the lock without blocking
    @intrinsic("mutex.try_lock")
    pub fn try_lock(mut self) -> option<mutex_guard<T>>
end fn

    # Check if mutex is currently locked
    pub fn is_locked(self) -> bool
        return self.locked
    end fn
end impl

# Guard that holds the lock and releases on drop
pub struct mutex_guard<T>
    data_ref: ref<T>
end struct

impl<T> mutex_guard<T>
    # Get reference to protected data
    pub fn get(self) -> ref<T>
        return self.data_ref
    end fn

    # Get mutable reference to protected data
    pub fn get_mut(mut self) -> ref<T>
        return self.data_ref
    end fn
end impl

# =============================================================================
# RwLock - Read-Write Lock
# =============================================================================

# A reader-writer lock allowing multiple readers or one writer
pub struct rw_lock<T>
    data: T
    readers: i32
    writer: bool
end struct

impl<T> rw_lock<T>
    # Create a new read-write lock
    pub fn new(data: T) -> rw_lock<T>
        return rw_lock { data: data, readers: 0, writer: false }
    end fn

    # Acquire a read lock
    @intrinsic("rwlock.read")
    pub fn read(self) -> read_guard<T>
end fn

    # Acquire a write lock
    @intrinsic("rwlock.write")
    pub fn write(mut self) -> write_guard<T>
end fn

    # Try to acquire a read lock
    @intrinsic("rwlock.try_read")
    pub fn try_read(self) -> option<read_guard<T>>
end fn

    # Try to acquire a write lock
    @intrinsic("rwlock.try_write")
    pub fn try_write(mut self) -> option<write_guard<T>>
end fn
end impl

pub struct read_guard<T>
    lock_ref: ref<rw_lock<T>>
end struct

pub struct write_guard<T>
    lock_ref: ref<rw_lock<T>>
end struct

# =============================================================================
# Semaphore
# =============================================================================

# A counting semaphore for limiting concurrent access
pub struct semaphore
    count: i32
    max_count: i32
end struct

impl semaphore
    # Create a semaphore with initial count
    pub fn new(count: i32) -> semaphore
        return semaphore { count: count, max_count: count }
    end fn

    # Wait/acquire (decrement count, block if zero)
    @intrinsic("semaphore.wait")
    pub fn wait(mut self)
end fn

    # Signal/release (increment count)
    @intrinsic("semaphore.signal")
    pub fn signal(mut self)
end fn

    # Try to wait without blocking
    @intrinsic("semaphore.try_wait")
    pub fn try_wait(mut self) -> bool
end fn

    # Get current count
    pub fn get_count(self) -> i32
        return self.count
    end fn
end impl

# =============================================================================
# Barrier
# =============================================================================

# Synchronization barrier for coordinating multiple threads
pub struct barrier
    count: i32
    waiting: i32
end struct

impl barrier
    # Create a barrier for n threads
    pub fn new(count: i32) -> barrier
        return barrier { count: count, waiting: 0 }
    end fn

    # Wait at the barrier until all threads arrive
    @intrinsic("barrier.wait")
    pub fn wait(mut self)
end fn
end impl
