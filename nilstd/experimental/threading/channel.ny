@pragma once
# Experimental design only: channel handles have no runtime provider yet and
# this module is excluded from the SDK.

module nil.std.threading
import nil.std.core

# =============================================================================
# Channel - Message Passing Between Threads
# =============================================================================
# Simplified channel API using i64 handles (like file I/O)

# =============================================================================
# Raw Intrinsics (Private)
# =============================================================================

@intrinsic("channel_create")
fn channel_create_raw(capacity: i32) -> i64
end fn

@intrinsic("channel_send")
fn channel_send_raw(id: i64, value: any)
end fn

@intrinsic("channel_try_send")
fn channel_try_send_raw(id: i64, value: any) -> bool
end fn

@intrinsic("channel_recv")
fn channel_recv_raw(id: i64) -> any
end fn

@intrinsic("channel_try_recv")
fn channel_try_recv_raw(id: i64) -> any
end fn

@intrinsic("channel_recv_timeout")
fn channel_recv_timeout_raw(id: i64, timeout_ms: i64) -> any
end fn

@intrinsic("channel_close")
fn channel_close_raw(id: i64)
end fn

@intrinsic("channel_is_closed")
fn channel_is_closed_raw(id: i64) -> bool
end fn

# =============================================================================
# Public API
# =============================================================================

# Create an unbounded channel
pub fn create() -> i64
    return channel_create_raw(-1)
end fn

# Create a bounded channel with max capacity
pub fn create_bounded(capacity: i32) -> i64
    return channel_create_raw(capacity)
end fn

# Send a message (blocks if bounded channel is full)
pub fn send(channel_id: i64, value: any)
    channel_send_raw(channel_id, value)
end fn

# Try to send without blocking
pub fn try_send(channel_id: i64, value: any) -> bool
    return channel_try_send_raw(channel_id, value)
end fn

# Receive a message (blocks until available)
pub fn recv(channel_id: i64) -> any
    return channel_recv_raw(channel_id)
end fn

# Try to receive without blocking
pub fn try_recv(channel_id: i64) -> any
    return channel_try_recv_raw(channel_id)
end fn

# Receive with timeout (milliseconds)
pub fn recv_timeout(channel_id: i64, timeout_ms: i64) -> any
    return channel_recv_timeout_raw(channel_id, timeout_ms)
end fn

# Close the channel
pub fn close(channel_id: i64)
    channel_close_raw(channel_id)
end fn

# Check if channel is closed
pub fn is_closed(channel_id: i64) -> bool
    return channel_is_closed_raw(channel_id)
end fn
