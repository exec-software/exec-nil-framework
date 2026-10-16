@pragma once
# Experimental design only: monitor operations have no runtime provider yet
# and this module is excluded from the SDK.

module nil.std.threading

import nil.std.core
import nil.std.collections

# =============================================================================
# Monitor - Thread Synchronization (mirrors System.Threading.Monitor)
# =============================================================================

# Monitor provides a mechanism for threads to synchronize access to objects
pub struct monitor
end struct

impl monitor
    # Acquire an exclusive lock on the specified object
    @intrinsic("monitor.enter")
    pub fn enter(obj: ref<any>) -> void
end fn

    # Release an exclusive lock on the specified object
    @intrinsic("monitor.exit")
    pub fn exit(obj: ref<any>) -> void
end fn

    # Try to acquire an exclusive lock on the specified object
    @intrinsic("monitor.try_enter")
    pub fn try_enter(obj: ref<any>) -> bool
end fn

    # Try to acquire an exclusive lock on the specified object with timeout
    @intrinsic("monitor.try_enter_timeout")
    pub fn try_enter_timeout(obj: ref<any>, timeout_ms: i32) -> bool
end fn

    # Release the lock on an object and block the current thread until it reacquires the lock
    @intrinsic("monitor.wait")
    pub fn wait(obj: ref<any>) -> bool
end fn

    # Release the lock on an object and block the current thread until it reacquires the lock or timeout
    @intrinsic("monitor.wait_timeout")
    pub fn wait_timeout(obj: ref<any>, timeout_ms: i32) -> bool
end fn

    # Notify a thread in the waiting queue of a change in the locked object's state
    @intrinsic("monitor.pulse")
    pub fn pulse(obj: ref<any>) -> void
end fn

    # Notify all waiting threads of a change in the locked object's state
    @intrinsic("monitor.pulse_all")
    pub fn pulse_all(obj: ref<any>) -> void
end fn

    # Determine whether the current thread holds the lock on the specified object
    @intrinsic("monitor.is_entered")
    pub fn is_entered(obj: ref<any>) -> bool
end fn
end impl

# =============================================================================
# Lock Helper - Syntactic sugar for Monitor.Enter/Exit
# =============================================================================

# RAII-style lock helper
pub struct lock_guard<T>
    obj: ref<T>
end struct

impl<T> lock_guard<T>
    # Constructor - acquires lock
    fn new(obj: ref<T>) -> lock_guard<T>
        monitor.enter(obj as ref<any>)
        return lock_guard { obj: obj }
    end fn

    # Destructor - releases lock (called automatically)
    fn drop(mut self)
        monitor.exit(self.obj as ref<any>)
    end fn
end impl

# Helper function for lock syntax
pub fn lock<T>(obj: ref<T>) -> lock_guard<T>
    return lock_guard.new(obj)
end fn
