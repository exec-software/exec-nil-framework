@pragma once

module nil.std.threading
import nil.std.core

# =============================================================================
# Future - Represents a Value That Will Be Available Later
# =============================================================================

# A future represents a value that will be computed asynchronously
pub struct future<T>
    completed: bool
    value: option<T>
end struct

impl<T> future<T>
    # Create a pending future
    pub fn pending() -> future<T>
        return future { completed: false, value: none }
    end fn

    # Create an already-completed future
    pub fn ready(value: T) -> future<T>
        return future { completed: true, value: some(value) }
    end fn

    # Check if the future is complete
    pub fn is_ready(self) -> bool
        return self.completed
    end fn

    # Check if the future is still pending
    pub fn is_pending(self) -> bool
        return not self.completed
    end fn

    # Block and wait for the value
    @intrinsic("future.wait")
    pub fn wait(self) -> T
end fn

    # Poll without blocking (returns none if not ready)
    pub fn poll(self) -> option<T>
end fn
        if self.completed then
            return self.value
        end if
        return none
    end fn

    # Transform the future value when ready
    pub fn map<U>(self, f: fn(T) -> U) -> future<U>
        if self.completed then
            match self.value
                some(v) -> return future.ready(f(v))
                none -> return future.pending()
            end match
        end if
        return future.pending()
    end fn

    # Chain futures
    pub fn and_then<U>(self, f: fn(T) -> future<U>) -> future<U>
        if self.completed then
            match self.value
                some(v) -> return f(v)
                none -> return future.pending()
            end match
        end if
        return future.pending()
    end fn
end impl

# =============================================================================
# Promise - Completes a Future
# =============================================================================

# A promise that can complete a future
pub struct promise<T>
    future_ref: ref<future<T>>
end struct

impl<T> promise<T>
    # Create a promise/future pair
    pub fn create() -> (promise<T>, future<T>)
        let fut: future<T> = future.pending()
        let prom: promise<T> = promise { future_ref: fut }
        return (prom, fut)
    end fn

    # Complete the promise with a value
    pub fn complete(mut self, value: T)
        (*self.future_ref).completed = true
        (*self.future_ref).value = some(value)
    end fn

    # Fail the promise (no value)
    pub fn fail(mut self)
        (*self.future_ref).completed = true
        (*self.future_ref).value = none
    end fn
end impl

# =============================================================================
# Async Utilities
# =============================================================================

# Wait for all futures to complete
@intrinsic("async.wait_all")
pub fn wait_all<T>(futures: vec<future<T>>) -> vec<T>

# Wait for any future to complete (returns first completed)
@intrinsic("async.wait_any")
pub fn wait_any<T>(futures: vec<future<T>>) -> T

# Spawn an async task
@intrinsic("async.spawn")
pub fn spawn<T>(f: fn() -> T) -> future<T>
