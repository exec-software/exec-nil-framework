@pragma once
# Experimental design package: imports below require concurrency runtime
# providers that are not built or shipped by the SDK.
# =============================================================================
# ny.lang.threading — Threading Package
# =============================================================================
# Concurrency primitives mirroring C# System.Threading.
#
# Provides:
#   - Thread: spawn, join, sleep, yield (thread.ny)
#   - Async/await: async fn, await, async blocks (async.ny)
#   - Atomic<T>: lock-free atomic operations (atomic.ny)
#   - Channel<T>: CSP-style message passing (channel.ny)
#   - Interlocked: atomic increment/decrement/compare-exchange (interlocked.ny)
#   - Monitor: lock/unlock, wait/pulse (monitor.ny)
#   - Sync: Mutex, RwLock, Semaphore, Barrier (sync.ny)
#   - ThreadPool: work queue with configurable thread count (thread_pool.ny)
#   - Timer: one-shot and repeating timers (timer.ny)
#   - WaitHandle: event signaling primitives (wait_handle.ny)
#
# Native shims: native/thread.windows.c, native/thread.linux.c
#
# Cross-refs:
#   ny.lang.core  — Send, Sync marker traits
#   ny.lang.concurrency — Task<T>/async built on top of these primitives
# =============================================================================

package nil.std.threading

import nil.std.threading.thread
import nil.std.threading.atomic
import nil.std.threading.channel
import nil.std.threading.interlocked
import nil.std.threading.monitor
import nil.std.threading.sync
import nil.std.threading.wait_handle
import nil.std.threading.async
import nil.std.threading.thread_pool
import nil.std.threading.timer
