@pragma once
# Experimental design only: wait handles have no runtime provider yet and this
# module is excluded from the SDK.

module nil.std.threading

import nil.std.core

# =============================================================================
# WaitHandle - Base class for synchronization objects
# =============================================================================

pub trait wait_handle
    # Wait for the handle to be signaled
    fn wait_one(self) -> bool

    # Wait for the handle to be signaled with timeout
    fn wait_one_timeout(self, timeout_ms: i32) -> bool

    # Close the wait handle
    fn close(mut self) -> void
end trait

# =============================================================================
# AutoResetEvent - Auto-resetting event (mirrors System.Threading.AutoResetEvent)
# =============================================================================

# An event that automatically resets to non-signaled after releasing a waiting thread
pub struct auto_reset_event
    signaled: bool
end struct

impl auto_reset_event
    # Create a new AutoResetEvent in the specified state
    pub fn new(initial_state: bool) -> auto_reset_event
        return auto_reset_event { signaled: initial_state }
    end fn

    # Set the event to signaled state, releasing one waiting thread
    @intrinsic("auto_reset_event.set")
    pub fn set(mut self) -> bool
end fn

    # Reset the event to non-signaled state
    @intrinsic("auto_reset_event.reset")
    pub fn reset(mut self) -> bool
end fn

    # Wait for the event to be signaled
    @intrinsic("auto_reset_event.wait_one")
    pub fn wait_one(self) -> bool
end fn

    # Wait for the event to be signaled with timeout
    @intrinsic("auto_reset_event.wait_one_timeout")
    pub fn wait_one_timeout(self, timeout_ms: i32) -> bool
end fn
end impl

impl wait_handle for auto_reset_event
    fn wait_one(self) -> bool
        return self.wait_one()
    end fn

    fn wait_one_timeout(self, timeout_ms: i32) -> bool
        return self.wait_one_timeout(timeout_ms)
    end fn

    fn close(mut self) -> void
        # AutoResetEvent doesn't need explicit cleanup in this implementation
    end fn
end impl

# =============================================================================
# ManualResetEvent - Manual-resetting event (mirrors System.Threading.ManualResetEvent)
# =============================================================================

# An event that remains signaled until manually reset
pub struct manual_reset_event
    signaled: bool
end struct

impl manual_reset_event
    # Create a new ManualResetEvent in the specified state
    pub fn new(initial_state: bool) -> manual_reset_event
        return manual_reset_event { signaled: initial_state }
    end fn

    # Set the event to signaled state, releasing all waiting threads
    @intrinsic("manual_reset_event.set")
    pub fn set(mut self) -> bool
end fn

    # Reset the event to non-signaled state
    @intrinsic("manual_reset_event.reset")
    pub fn reset(mut self) -> bool
end fn

    # Wait for the event to be signaled
    @intrinsic("manual_reset_event.wait_one")
    pub fn wait_one(self) -> bool
end fn

    # Wait for the event to be signaled with timeout
    @intrinsic("manual_reset_event.wait_one_timeout")
    pub fn wait_one_timeout(self, timeout_ms: i32) -> bool
end fn
end impl

impl wait_handle for manual_reset_event
    fn wait_one(self) -> bool
        return self.wait_one()
    end fn

    fn wait_one_timeout(self, timeout_ms: i32) -> bool
        return self.wait_one_timeout(timeout_ms)
    end fn

    fn close(mut self) -> void
        # ManualResetEvent doesn't need explicit cleanup in this implementation
    end fn
end impl

# =============================================================================
# WaitHandle static methods
# =============================================================================

@intrinsic("wait_handle.wait_any")
pub fn wait_any(handles: ref<array<wait_handle>>, timeout_ms: i32) -> i32
    # Wait for any of the handles to be signaled
    # Return the index of the signaled handle, or -1 on timeout
    return -1
end fn

@intrinsic("wait_handle.wait_all")
pub fn wait_all(handles: ref<array<wait_handle>>, timeout_ms: i32) -> bool
    # Wait for all handles to be signaled
    # Return true if all signaled, false on timeout
    return false
end fn
