@pragma once
# =============================================================================
# Task Module Tests
# =============================================================================
# Tests for asynchronous operations in Ny.Language.Threading.Task
# =============================================================================

module nil.std.concurrency

import concurrency.task

# =============================================================================
# Task Creation Tests
# =============================================================================

pub fn test_run() -> bool
    let task = run(() -> 42)
    task.wait()
    let result = task.get()
    if result != 42 then
        println("✗ run test failed: expected 42, got " + result)
        return false
    end if
    println("✓ run test passed")
    return true
end fn

pub fn test_run_void() -> bool
    let mut counter = 0
    let task = run(() -> {
        counter = counter + 1
    })
    task.wait()
    if counter != 1 then
        println("✗ run_void test failed: expected 1, got " + counter)
        return false
    end if
    println("✓ run_void test passed")
    return true
end fn

pub fn test_delay() -> bool
    let task = delay(100)
    if not task.wait_for(200) then
        println("✗ delay test failed: timeout")
        return false
    end if
    if not task.is_completed() then
        println("✗ delay test failed: task not completed")
        return false
    end if
    println("✓ delay test passed")
    return true
end fn

pub fn test_of() -> bool
    let task = of(123)
    if not task.is_completed() then
        println("✗ of test failed: task not completed")
        return false
    end if
    let result = task.get()
    if result != 123 then
        println("✗ of test failed: expected 123, got " + result)
        return false
    end if
    println("✓ of test passed")
    return true
end fn

pub fn test_of_void() -> bool
    let task = of_void()
    if not task.is_completed() then
        println("✗ of_void test failed: task not completed")
        return false
    end if
    println("✓ of_void test passed")
    return true
end fn

# =============================================================================
# Task State Tests
# =============================================================================

pub fn test_is_completed() -> bool
    let task = run(() -> 42)
    if task.is_completed() then
        println("✗ is_completed test failed: task should not be completed yet")
        return false
    end if
    task.wait()
    if not task.is_completed() then
        println("✗ is_completed test failed: task should be completed")
        return false
    end if
    println("✓ is_completed test passed")
    return true
end fn

pub fn test_is_running() -> bool
    let task = run(() -> 42)
    if not task.is_running() then
        println("✗ is_running test failed: task should be running")
        return false
    end if
    task.wait()
    if task.is_running() then
        println("✗ is_running test failed: task should not be running after completion")
        return false
    end if
    println("✓ is_running test passed")
    return true
end fn

pub fn test_is_pending() -> bool
    let task = run(() -> 42)
    if not task.is_pending() then
        println("✗ is_pending test failed: task should be pending")
        return false
    end if
    task.wait()
    if task.is_pending() then
        println("✗ is_pending test failed: task should not be pending after completion")
        return false
    end if
    println("✓ is_pending test passed")
    return true
end fn

# =============================================================================
# Task Wait Tests
# =============================================================================

pub fn test_wait() -> bool
    let task = run(() -> 42)
    task.wait()
    if not task.is_completed() then
        println("✗ wait test failed: task not completed")
        return false
    end if
    println("✓ wait test passed")
    return true
end fn

pub fn test_wait_for() -> bool
    let task = delay(100)
    if not task.wait_for(200) then
        println("✗ wait_for test failed: timeout")
        return false
    end if
    if not task.is_completed() then
        println("✗ wait_for test failed: task not completed")
        return false
    end if
    println("✓ wait_for test passed")
    return true
end fn

pub fn test_wait_for_timeout() -> bool
    let task = delay(1000)
    if task.wait_for(100) then
        println("✗ wait_for_timeout test failed: should have timed out")
        return false
    end if
    println("✓ wait_for_timeout test passed")
    return true
end fn

# =============================================================================
# Task Get Tests
# =============================================================================

pub fn test_get() -> bool
    let task = run(() -> 42)
    task.wait()
    let result = task.get()
    if result != 42 then
        println("✗ get test failed: expected 42, got " + result)
        return false
    end if
    println("✓ get test passed")
    return true
end fn

pub fn test_get_or_default() -> bool
    let task = run(() -> 42)
    task.wait()
    let result = task.get_or_default(0)
    if result != 42 then
        println("✗ get_or_default test failed: expected 42, got " + result)
        return false
    end if
    println("✓ get_or_default test passed")
    return true
end fn

pub fn test_get_or_default_with_default() -> bool
    let task = run(() -> 42)
    task.wait()
    let result = task.get_or_default(0)
    if result != 42 then
        println("✗ get_or_default_with_default test failed: expected 42, got " + result)
        return false
    end if
    println("✓ get_or_default_with_default test passed")
    return true
end fn

# =============================================================================
# CancellationToken Tests
# =============================================================================

pub fn test_cancellation_token() -> bool
    let source = create_cancellation_token_source()
    let token = source.token()
    if token.is_cancelled() then
        println("✗ cancellation_token test failed: token should not be cancelled")
        return false
    end if
    source.cancel()
    if not token.is_cancelled() then
        println("✗ cancellation_token test failed: token should be cancelled")
        return false
    end if
    println("✓ cancellation_token test passed")
    return true
end fn

pub fn test_cancellation_token_source() -> bool
    let source = create_cancellation_token_source()
    let token = source.token()
    if source.token() != token then
        println("✗ cancellation_token_source test failed: tokens should be equal")
        return false
    end if
    source.cancel()
    if not token.is_cancelled() then
        println("✗ cancellation_token_source test failed: token should be cancelled")
        return false
    end if
    println("✓ cancellation_token_source test passed")
    return true
end fn

# =============================================================================
# Task Combinators Tests
# =============================================================================

pub fn test_when_all() -> bool
    let task1 = run(() -> 1)
    let task2 = run(() -> 2)
    let task3 = run(() -> 3)

    let all_tasks = [task1, task2, task3]
    let combined = when_all(all_tasks)

    combined.wait()
    if not combined.is_completed() then
        println("✗ when_all test failed: combined task not completed")
        return false
    end if
    println("✓ when_all test passed")
    return true
end fn

pub fn test_when_any() -> bool
    let task1 = delay(100)
    let task2 = delay(200)
    let task3 = delay(300)

    let all_tasks = [task1, task2, task3]
    let any_task = when_any(all_tasks)

    any_task.wait()
    if not any_task.is_completed() then
        println("✗ when_any test failed: any task not completed")
        return false
    end if
    println("✓ when_any test passed")
    return true
end fn

# =============================================================================
# Run All Tests
# =============================================================================

pub fn main() -> i32
    println("Running Task Module Tests...")
    println("")

    let test1 = test_run()
    let test2 = test_run_void()
    let test3 = test_delay()
    let test4 = test_of()
    let test5 = test_of_void()
    let test6 = test_is_completed()
    let test7 = test_is_running()
    let test8 = test_is_pending()
    let test9 = test_wait()
    let test10 = test_wait_for()
    let test11 = test_wait_for_timeout()
    let test12 = test_get()
    let test13 = test_get_or_default()
    let test14 = test_get_or_default_with_default()
    let test15 = test_cancellation_token()
    let test16 = test_cancellation_token_source()
    let test17 = test_when_all()
    let test18 = test_when_any()

    println("")
    println("Test Results:")
    println("  Passed: " + (test1 + test2 + test3 + test4 + test5 + test6 + test7 + test8 + test9 + test10 +
                         test11 + test12 + test13 + test14 + test15 + test16 + test17 + test18) as text)
    println("  Failed: " + (18 - (test1 + test2 + test3 + test4 + test5 + test6 + test7 + test8 + test9 + test10 +
                         test11 + test12 + test13 + test14 + test15 + test16 + test17 + test18)) as text)

    if test1 + test2 + test3 + test4 + test5 + test6 + test7 + test8 + test9 + test10 +
       test11 + test12 + test13 + test14 + test15 + test16 + test17 + test18 == 18 then
        println("✓ All tests passed!")
        return 0
    else
        println("✗ Some tests failed")
        return 1
    end if
end fn