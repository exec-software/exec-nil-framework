@pragma once
# Experimental design only: no scheduler, task runtime, or thread pool is
# shipped by the active standard library.
# =============================================================================
# ny.lang.concurrency — Concurrency / Tasks Package
# =============================================================================
# Async Task<T> and ValueTask<T> infrastructure.
# Mirrors C# System.Threading.Tasks.
#
# Provides:
#   - Task<T>: schedule, await, continuation (task.ny)
#   - Task combinators: when_all, when_any, delay (task.ny)
#
# Cross-refs:
#   ny.lang.threading  — Task is built on thread pool primitives
#   ny.lang.core       — Result<T,E> for task outcomes
# =============================================================================

package nil.std.concurrency

import nil.std.concurrency.task
