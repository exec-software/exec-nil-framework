@pragma once
# =============================================================================
# Task Module - Asynchronous Operations
# =============================================================================
# Provides async/await functionality for the Ny programming language:
#   - Task<T> - Asynchronous operation wrapper
#   - Task<void> - Asynchronous void operation
#   - CancellationToken - Cancellation support
#   - Task.WhenAll() - Parallel execution
#   - Task.WhenAny() - First completion
#   - Task.Delay() - Delay with cancellation
#   - Task.Run() - Execute on thread pool
#
# Usage:
#   import Ny.Language.Threading.Task
#   let task = Task.run(() -> 42)
#   let result = task.get()
# =============================================================================
module nil.std.concurrency

import nil.std.core
import nil.std.threading

# =============================================================================
# CancellationToken
# =============================================================================

pub struct CancellationToken
    cancelled: bool
end struct

pub impl CancellationToken
    pub fn is_cancelled(self) -> bool
        return self.cancelled
    end fn

    pub fn throw_if_cancelled(self)
        if self.cancelled then
            panic("Operation was cancelled")
        end if
    end fn
end impl

pub fn create_cancellation_token() -> CancellationToken
    return CancellationToken{cancelled: false}
end fn

pub fn create_cancellation_token_source() -> CancellationTokenSource
    return CancellationTokenSource{token: create_cancellation_token()}
end fn

pub struct CancellationTokenSource
    token: CancellationToken
end struct

pub impl CancellationTokenSource
    pub fn token(self) -> CancellationToken
        return self.token
    end fn

    pub fn cancel(self)
        self.token.cancelled = true
    end fn
end impl

# =============================================================================
# Task<T>
# =============================================================================

pub struct Task<T>
    promise: Promise<T>
    state: TaskState
    result: option<T>
end struct

pub enum TaskState
    pending
    running
    completed
    cancelled
    failed
end enum

pub impl<T> Task<T>
    pub fn is_completed(self) -> bool
        return self.state == TaskState.completed or self.state == TaskState.cancelled or self.state == TaskState.failed
    end fn

    pub fn is_running(self) -> bool
        return self.state == TaskState.running
    end fn

    pub fn is_pending(self) -> bool
        return self.state == TaskState.pending
    end fn

    pub fn get(self) -> T
        if self.state == TaskState.completed then
            return self.result.unwrap()
        else
            panic("Task is not completed")
        end if
    end fn

    pub fn wait(self) -> void
        while self.state == TaskState.running or self.state == TaskState.pending
            sleep_ms(10)
        end while
    end fn

    pub fn wait_for(self, milliseconds: i32) -> bool
        let mut start = current_time_millis()
        while self.state == TaskState.running or self.state == TaskState.pending
            if current_time_millis() - start >= milliseconds then
                return false
            end if
            sleep_ms(10)
        end while
        return true
    end fn

    pub fn get_or_default(self, default: T) -> T
        if self.state == TaskState.completed then
            return self.result.unwrap()
        else
            return default
        end if
    end fn
end impl

# =============================================================================
# Task<void>
# =============================================================================

pub struct Task
    promise: Promise<void>
    state: TaskState
end struct

pub impl Task
    pub fn is_completed(self) -> bool
        return self.state == TaskState.completed or self.state == TaskState.cancelled or self.state == TaskState.failed
    end fn

    pub fn wait(self) -> void
        while self.state == TaskState.running or self.state == TaskState.pending
            sleep_ms(10)
        end while
    end fn

    pub fn wait_for(self, milliseconds: i32) -> bool
        let mut start = current_time_millis()
        while self.state == TaskState.running or self.state == TaskState.pending
            if current_time_millis() - start >= milliseconds then
                return false
            end if
            sleep_ms(10)
        end while
        return true
    end fn
end impl

# =============================================================================
# Promise
# =============================================================================

pub struct Promise<T>
    value: option<T>
    state: PromiseState
end struct

pub enum PromiseState
    pending
    fulfilled
    rejected
end enum

pub impl<T> Promise<T>
    pub fn resolve(self, value: T)
        self.value = option.some(value)
        self.state = PromiseState.fulfilled
    end fn

    pub fn reject(self, error: text)
        self.value = option.none
        self.state = PromiseState.rejected
    end fn

    pub fn get(self) -> option<T>
        return self.value
    end fn

    pub fn is_fulfilled(self) -> bool
        return self.state == PromiseState.fulfilled
    end fn

    pub fn is_rejected(self) -> bool
        return self.state == PromiseState.rejected
    end fn
end impl

pub struct Promise
    value: option<any>
    state: PromiseState
end struct

pub impl Promise
    pub fn resolve(self, value: any)
        self.value = option.some(value)
        self.state = PromiseState.fulfilled
    end fn

    pub fn reject(self, error: text)
        self.value = option.none
        self.state = PromiseState.rejected
    end fn

    pub fn get(self) -> option<any>
        return self.value
    end fn

    pub fn is_fulfilled(self) -> bool
        return self.state == PromiseState.fulfilled
    end fn

    pub fn is_rejected(self) -> bool
        return self.state == PromiseState.rejected
    end fn
end impl

# =============================================================================
# Task Creation
# =============================================================================

pub fn run<T>(func: fn() -> T) -> Task<T>
    let promise = Promise<T>{value: option.none(), state: PromiseState.pending}
    let task = Task<T>{promise: promise, state: TaskState.pending, result: option.none()}

    # Execute on thread pool
    thread.spawn_named("Task.run", () -> {
        try
            let result = func()
            promise.resolve(result)
            task.state = TaskState.completed
            task.result = option.some(result)
        catch (error)
            promise.reject(error)
            task.state = TaskState.failed
        end try
    })

    return task
end fn

pub fn run(func: fn() -> void) -> Task
    let promise = Promise{value: option.none(), state: PromiseState.pending}
    let task = Task{promise: promise, state: TaskState.pending}

    # Execute on thread pool
    thread.spawn_named("Task.run", () -> {
        try
            func()
            promise.resolve(null)
            task.state = TaskState.completed
        catch (error)
            promise.reject(error)
            task.state = TaskState.failed
        end try
    })

    return task
end fn

pub fn delay(milliseconds: i32) -> Task<void>
    return run(() -> {
        sleep_ms(milliseconds)
    })
end fn

pub fn delay_with_cancellation(milliseconds: i32, token: CancellationToken) -> Task<void>
    return run(() -> {
        let mut elapsed = 0
        while elapsed < milliseconds
            token.throw_if_cancelled()
            sleep_ms(10)
            elapsed = elapsed + 10
        end while
    })
end fn

# =============================================================================
# Task Combinators
# =============================================================================

pub fn when_all(tasks: vec<Task<any>>) -> Task<void>
    let mut completed = 0
    let mut failed = 0

    for task in tasks
        if task.is_completed() then
            if task.state == TaskState.failed then
                failed = failed + 1
            else
                completed = completed + 1
            end if
        else
            task.wait()
            if task.state == TaskState.failed then
                failed = failed + 1
            else
                completed = completed + 1
            end if
        end if
    end for

    if failed > 0 then
        panic("Some tasks failed")
    end if

    return Task{promise: Promise{value: option.none(), state: PromiseState.pending}, state: TaskState.completed}
end fn

pub fn when_any(tasks: vec<Task<any>>) -> Task<any>
    let mut completed_task = null

    for task in tasks
        if task.is_completed() then
            completed_task = task
            break
        end if
    end for

    if completed_task == null then
        for task in tasks
            task.wait()
            if task.is_completed() then
                completed_task = task
                break
            end if
        end for
    end if

    if completed_task == null then
        panic("No tasks completed")
    end if

    return completed_task
end fn

pub fn of<T>(value: T) -> Task<T>
    return Task<T>{promise: Promise<T>{value: option.some(value), state: PromiseState.fulfilled}, state: TaskState.completed, result: option.some(value)}
end fn

pub fn of_void() -> Task
    return Task{promise: Promise{value: option.none(), state: PromiseState.fulfilled}, state: TaskState.completed}
end fn

pub fn of_error<T>(error: text) -> Task<T>
    return Task<T>{promise: Promise<T>{value: option.none(), state: PromiseState.rejected}, state: TaskState.failed, result: option.none()}
end fn

pub fn of_error_void(error: text) -> Task
    return Task{promise: Promise{value: option.none(), state: PromiseState.rejected}, state: TaskState.failed}
end fn

# =============================================================================
# Utility Functions
# =============================================================================

pub fn is_completed(task: Task<any>) -> bool
    return task.is_completed()
end fn

pub fn is_running(task: Task<any>) -> bool
    return task.is_running()
end fn

pub fn is_pending(task: Task<any>) -> bool
    return task.is_pending()
end fn

pub fn get_or_default<T>(task: Task<T>, default: T) -> T
    return task.get_or_default(default)
end fn

pub fn get_or_default_void(task: Task, default: void) -> void
    if task.is_completed() then
        task.wait()
    end if
end fn