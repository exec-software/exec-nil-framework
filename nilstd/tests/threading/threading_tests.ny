@pragma once

module nil.std.threading

import test.TestSuite
import nil.std.threading
import nil.std.collections

# =============================================================================
# Threading Module Tests
# =============================================================================

pub fn run_all_threading_tests() -> bool
    let mut suite = TestSuite.new("Threading Module Tests")

    # Monitor tests
    suite.add_test(assert_monitor_enter_exit, "Monitor enter/exit operations")

    # WaitHandle tests
    suite.add_test(assert_auto_reset_event, "AutoResetEvent operations")
    suite.add_test(assert_manual_reset_event, "ManualResetEvent operations")

    # Interlocked tests
    suite.add_test(assert_interlocked_operations, "Interlocked atomic operations")

    # Thread tests
    suite.add_test(assert_thread_creation, "Thread creation and joining")

    # Synchronization tests
    suite.add_test(assert_mutex_operations, "Mutex operations")

    return suite.run()
end fn

# =============================================================================
# Monitor Tests
# =============================================================================

fn assert_monitor_enter_exit() -> bool
    let obj = 42
    let mut obj_ref = obj

    # Test basic enter/exit
    monitor.enter(obj_ref)
    let entered = monitor.is_entered(obj_ref)
    monitor.exit(obj_ref)

    return entered
end fn

# =============================================================================
# WaitHandle Tests
# =============================================================================

fn assert_auto_reset_event() -> bool
    let mut are = auto_reset_event.new(false)

    # Test initial state
    if are.wait_one_timeout(1) then
        return false  # Should not be signaled initially
    end if

    # Test set/reset
    let set_result = are.set()
    let signaled = are.wait_one_timeout(1)
    let already_reset = are.wait_one_timeout(1)  # Should be reset after first wait

    return set_result and signaled and not already_reset
end fn

fn assert_manual_reset_event() -> bool
    let mut mre = manual_reset_event.new(false)

    # Test initial state
    if mre.wait_one_timeout(1) then
        return false  # Should not be signaled initially
    end if

    # Test set (stays signaled)
    let set_result = mre.set()
    let signaled1 = mre.wait_one_timeout(1)
    let signaled2 = mre.wait_one_timeout(1)  # Should still be signaled

    # Test reset
    let reset_result = mre.reset()
    let not_signaled = not mre.wait_one_timeout(1)

    return set_result and signaled1 and signaled2 and reset_result and not_signaled
end fn

# =============================================================================
# Interlocked Tests
# =============================================================================

fn assert_interlocked_operations() -> bool
    let mut value: i32 = 10

    # Test increment
    let inc_result = interlocked.increment(value)
    if inc_result != 11 or value != 11 then
        return false
    end if

    # Test decrement
    let dec_result = interlocked.decrement(value)
    if dec_result != 10 or value != 10 then
        return false
    end if

    # Test add
    let add_result = interlocked.add(value, 5)
    if add_result != 10 or value != 15 then
        return false
    end if

    # Test exchange
    let exchange_result = interlocked.exchange(value, 20)
    if exchange_result != 15 or value != 20 then
        return false
    end if

    # Test compare exchange
    let compare_result = interlocked.compare_exchange(value, 25, 20)
    if compare_result != 20 or value != 25 then
        return false
    end if

    # Test failed compare exchange
    let failed_compare = interlocked.compare_exchange(value, 30, 20)
    if failed_compare != 25 or value != 25 then
        return false
    end if

    return true
end fn

# =============================================================================
# Thread Tests
# =============================================================================

fn assert_thread_creation() -> bool
    let mut counter = 0

    # Create and start a thread
    let t = thread.spawn(|| counter = counter + 1)

    # Wait for thread to complete
    thread.join(t)

    # Check result
    return counter == 1
end fn

# =============================================================================
# Synchronization Tests
# =============================================================================

fn assert_mutex_operations() -> bool
    let mut data = 0
    let mut mtx = mutex.new(data)

    # Test basic lock/unlock
    let guard = mtx.lock()
    data = 42
    drop(guard)

    # Data should be modified
    return data == 42
end fn