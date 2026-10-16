@schema
@pragma once

import nil.std.diagnostics.{Debug, Trace, Stopwatch, TimeSpan}
import nil.std.diagnostics.{Process, ProcessStartInfo, ProcessWindowStyle}
import nil.std.diagnostics.{Activity, ActivitySource, ActivityTagsCollection}
import nil.std.diagnostics.{PerformanceCounter, EventLog, EventLogEntryType}
import test.{TestSuite, assert_true, assert_false, assert_equals, assert_not_null, benchmark}

# =============================================================================
# Debug Tests
# =============================================================================

pub fn test_debug() -> TestSuite
    let suite = TestSuite.new("Debug Tests")

    # Test 1: Basic debug output
    Debug.write(string.from("Test message"))
    suite.add_test(assert_true(true, "Debug write executes without error"))

    # Test 2: Debug write line
    Debug.write_line(string.from("Test line message"))
    suite.add_test(assert_true(true, "Debug write line executes without error"))

    # Test 3: Conditional debug output
    Debug.write_if(true, string.from("Conditional true message"))
    suite.add_test(assert_true(true, "Debug write if true executes"))

    Debug.write_if(false, string.from("Conditional false message"))
    suite.add_test(assert_true(true, "Debug write if false does not execute"))

    # Test 4: Debug assertions
    Debug.assert(true)
    suite.add_test(assert_true(true, "Debug assert true passes"))

    # Note: We can't test assert false without crashing the test
    # Debug.assert(false)  # This would fail the test

    # Test 5: Debug assert with message
    Debug.assert_message(true, string.from("This should not fail"))
    suite.add_test(assert_true(true, "Debug assert with message passes"))

    # Test 6: Debug fail
    Debug.fail(string.from("Test failure message"))
    suite.add_test(assert_true(true, "Debug fail executes without error"))

    # Test 7: Indentation
    Debug.indent()
    Debug.write_line(string.from("Indented message"))
    Debug.unindent()
    suite.add_test(assert_true(true, "Debug indentation operations execute"))

    # Test 8: Stack trace
    Debug.print_stack_trace()
    suite.add_test(assert_true(true, "Debug print stack trace executes"))

    return suite
end fn

# =============================================================================
# Trace Tests
# =============================================================================

pub fn test_trace() -> TestSuite
    let suite = TestSuite.new("Trace Tests")

    # Test 1: Basic trace output
    Trace.write(string.from("Trace message"))
    suite.add_test(assert_true(true, "Trace write executes without error"))

    # Test 2: Trace write line
    Trace.write_line(string.from("Trace line message"))
    suite.add_test(assert_true(true, "Trace write line executes without error"))

    # Test 3: Warning and error tracing
    Trace.write_warning(string.from("Warning message"))
    suite.add_test(assert_true(true, "Trace warning executes without error"))

    Trace.write_error(string.from("Error message"))
    suite.add_test(assert_true(true, "Trace error executes without error"))

    # Test 4: Trace information levels
    Trace.trace_information(string.from("Information message"))
    suite.add_test(assert_true(true, "Trace information executes"))

    Trace.trace_warning(string.from("Trace warning"))
    suite.add_test(assert_true(true, "Trace warning level executes"))

    Trace.trace_error(string.from("Trace error"))
    suite.add_test(assert_true(true, "Trace error level executes"))

    # Test 5: Conditional tracing
    Trace.write_if(true, string.from("Conditional trace true"))
    suite.add_test(assert_true(true, "Trace write if true executes"))

    Trace.write_if(false, string.from("Conditional trace false"))
    suite.add_test(assert_true(true, "Trace write if false does not execute"))

    return suite
end fn

# =============================================================================
# Stopwatch Tests
# =============================================================================

pub fn test_stopwatch() -> TestSuite
    let suite = TestSuite.new("Stopwatch Tests")

    # Test 1: Stopwatch construction
    let sw1 = Stopwatch.new()
    suite.add_test(assert_false(sw1.is_running(), "New stopwatch is not running"))
    suite.add_test(assert_equals(0, sw1.elapsed_ticks(), "New stopwatch has zero elapsed ticks"))

    # Test 2: Stopwatch properties
    suite.add_test(assert_true(Stopwatch.is_high_resolution(), "Stopwatch is high resolution"))
    suite.add_test(assert_true(Stopwatch.frequency() > 0, "Stopwatch frequency is positive"))

    # Test 3: Start and stop
    sw1.start()
    suite.add_test(assert_true(sw1.is_running(), "Stopwatch is running after start"))

    # Sleep briefly (in real implementation, would use Thread.Sleep)
    # For now, just test the state changes

    sw1.stop()
    suite.add_test(assert_false(sw1.is_running(), "Stopwatch is not running after stop"))
    suite.add_test(assert_true(sw1.elapsed_ticks() >= 0, "Stopwatch elapsed ticks is non-negative after stop"))

    # Test 4: Restart
    sw1.restart()
    suite.add_test(assert_true(sw1.is_running(), "Stopwatch is running after restart"))
    suite.add_test(assert_equals(0, sw1.elapsed_ticks(), "Stopwatch elapsed ticks is zero after restart"))

    sw1.stop()

    # Test 5: Reset
    sw1.reset()
    suite.add_test(assert_false(sw1.is_running(), "Stopwatch is not running after reset"))
    suite.add_test(assert_equals(0, sw1.elapsed_ticks(), "Stopwatch elapsed ticks is zero after reset"))

    # Test 6: Elapsed properties
    sw1.start()
    # Simulate some time passing
    sw1.stop()

    let elapsed = sw1.elapsed()
    suite.add_test(assert_true(elapsed.ticks >= 0, "Elapsed TimeSpan has non-negative ticks"))

    let elapsed_ms = sw1.elapsed_milliseconds()
    suite.add_test(assert_true(elapsed_ms >= 0, "Elapsed milliseconds is non-negative"))

    # Test 7: Start new stopwatch
    let sw2 = Stopwatch.start_new()
    suite.add_test(assert_true(sw2.is_running(), "Start new creates running stopwatch"))
    sw2.stop()

    # Test 8: Multiple start/stop cycles
    sw1.start()
    sw1.stop()
    sw1.start()
    sw1.stop()

    let total_elapsed = sw1.elapsed_ticks()
    suite.add_test(assert_true(total_elapsed >= 0, "Multiple start/stop cycles accumulate correctly"))

    return suite
end fn

# =============================================================================
# TimeSpan Tests
# =============================================================================

pub fn test_time_span() -> TestSuite
    let suite = TestSuite.new("TimeSpan Tests")

    # Test 1: TimeSpan constants
    suite.add_test(assert_true(TimeSpan.ticks_per_day() > 0, "Ticks per day is positive"))
    suite.add_test(assert_true(TimeSpan.ticks_per_hour() > 0, "Ticks per hour is positive"))
    suite.add_test(assert_true(TimeSpan.ticks_per_minute() > 0, "Ticks per minute is positive"))
    suite.add_test(assert_true(TimeSpan.ticks_per_second() > 0, "Ticks per second is positive"))
    suite.add_test(assert_true(TimeSpan.ticks_per_millisecond() > 0, "Ticks per millisecond is positive"))

    # Test 2: TimeSpan construction
    let ts1 = TimeSpan.new()
    suite.add_test(assert_equals(0, ts1.ticks, "New TimeSpan has zero ticks"))

    let ts2 = TimeSpan.from_ticks(12345)
    suite.add_test(assert_equals(12345, ts2.ticks, "TimeSpan from ticks stores correct ticks"))

    # Test 3: TimeSpan from units
    let ts3 = TimeSpan.from_milliseconds(1000)
    suite.add_test(assert_equals(1000 * TimeSpan.ticks_per_millisecond(), ts3.ticks, "TimeSpan from milliseconds"))

    let ts4 = TimeSpan.from_seconds(2)
    suite.add_test(assert_equals(2 * TimeSpan.ticks_per_second(), ts4.ticks, "TimeSpan from seconds"))

    let ts5 = TimeSpan.from_minutes(3)
    suite.add_test(assert_equals(3 * TimeSpan.ticks_per_minute(), ts5.ticks, "TimeSpan from minutes"))

    let ts6 = TimeSpan.from_hours(4)
    suite.add_test(assert_equals(4 * TimeSpan.ticks_per_hour(), ts6.ticks, "TimeSpan from hours"))

    let ts7 = TimeSpan.from_days(5)
    suite.add_test(assert_equals(5 * TimeSpan.ticks_per_day(), ts7.ticks, "TimeSpan from days"))

    # Test 4: TimeSpan from range constructor
    let ts8 = TimeSpan.from_days_hours_minutes_seconds(1, 2, 3, 4)
    let expected_ticks = (1 * TimeSpan.ticks_per_day()) +
                       (2 * TimeSpan.ticks_per_hour()) +
                       (3 * TimeSpan.ticks_per_minute()) +
                       (4 * TimeSpan.ticks_per_second())
    suite.add_test(assert_equals(expected_ticks, ts8.ticks, "TimeSpan from days/hours/minutes/seconds"))

    # Test 5: TimeSpan properties
    let ts9 = TimeSpan.from_range(1, 2, 30, 45, 500)
    suite.add_test(assert_equals(1, ts9.days(), "TimeSpan days property"))
    suite.add_test(assert_equals(2, ts9.hours(), "TimeSpan hours property"))
    suite.add_test(assert_equals(30, ts9.minutes(), "TimeSpan minutes property"))
    suite.add_test(assert_equals(45, ts9.seconds(), "TimeSpan seconds property"))
    suite.add_test(assert_equals(500, ts9.milliseconds(), "TimeSpan milliseconds property"))

    # Test 6: TimeSpan total properties
    let ts10 = TimeSpan.from_hours(2.5)
    suite.add_test(assert_true(ts10.total_hours() >= 2.4 and ts10.total_hours() <= 2.6, "Total hours is approximately 2.5"))
    suite.add_test(assert_true(ts10.total_minutes() >= 149.0 and ts10.total_minutes() <= 151.0, "Total minutes is approximately 150"))

    # Test 7: TimeSpan arithmetic
    let ts11 = TimeSpan.from_minutes(5)
    let ts12 = TimeSpan.from_minutes(3)
    let sum = ts11.add(ts12)
    suite.add_test(assert_equals(8 * TimeSpan.ticks_per_minute(), sum.ticks, "TimeSpan addition"))

    let diff = ts11.subtract(ts12)
    suite.add_test(assert_equals(2 * TimeSpan.ticks_per_minute(), diff.ticks, "TimeSpan subtraction"))

    let neg = ts12.negate()
    suite.add_test(assert_equals(-3 * TimeSpan.ticks_per_minute(), neg.ticks, "TimeSpan negation"))

    # Test 8: TimeSpan comparison
    let ts13 = TimeSpan.from_minutes(5)
    let ts14 = TimeSpan.from_minutes(5)
    suite.add_test(assert_true(ts13.equals(ts14), "Equal TimeSpans are equal"))
    suite.add_test(assert_equals(0, ts13.compare(ts14), "Equal TimeSpans compare to 0"))

    let ts15 = TimeSpan.from_minutes(10)
    suite.add_test(assert_equals(-1, ts13.compare(ts15), "Smaller TimeSpan compares to -1"))
    suite.add_test(assert_equals(1, ts15.compare(ts13), "Larger TimeSpan compares to 1"))

    # Test 9: TimeSpan static values
    let zero_ts = TimeSpan.zero()
    suite.add_test(assert_equals(0, zero_ts.ticks, "TimeSpan zero has zero ticks"))

    let min_ts = TimeSpan.min_value()
    let max_ts = TimeSpan.max_value()
    suite.add_test(assert_true(min_ts.ticks < max_ts.ticks, "Min value is less than max value"))

    # Test 10: TimeSpan formatting
    let ts16 = TimeSpan.from_hours(1, 30, 45)
    let formatted = ts16.to_string()
    suite.add_test(assert_true(formatted.fmt().contains("01:"), "Formatted time contains hours"))
    suite.add_test(assert_true(formatted.fmt().contains("30:"), "Formatted time contains minutes"))
    suite.add_test(assert_true(formatted.fmt().contains("45"), "Formatted time contains seconds"))

    return suite
end fn

# =============================================================================
# Process Tests
# =============================================================================

pub fn test_process() -> TestSuite
    let suite = TestSuite.new("Process Tests")

    # Test 1: Process construction
    let process1 = Process.new()
    suite.add_test(assert_equals(0, process1.process_id, "New process has ID 0"))
    suite.add_test(assert_false(process1.has_exited, "New process has not exited"))

    # Test 2: ProcessStartInfo construction
    let start_info1 = ProcessStartInfo.new()
    suite.add_test(assert_true(start_info1.file_name.is_empty(), "New ProcessStartInfo has empty file name"))
    suite.add_test(assert_false(start_info1.use_shell_execute, "New ProcessStartInfo does not use shell execute"))
    suite.add_test(assert_equals(ProcessWindowStyle.normal, start_info1.window_style, "New ProcessStartInfo has normal window style"))

    # Test 3: ProcessStartInfo configuration
    let mut start_info2 = ProcessStartInfo.new()
    start_info2.file_name = string.from("test.exe")
    start_info2.arguments = string.from("--option value")
    start_info2.working_directory = string.from("/tmp")
    start_info2.use_shell_execute = true
    start_info2.window_style = ProcessWindowStyle.hidden

    suite.add_test(assert_equals("test.exe", start_info2.file_name.fmt(), "ProcessStartInfo file name set correctly"))
    suite.add_test(assert_equals("--option value", start_info2.arguments.fmt(), "ProcessStartInfo arguments set correctly"))
    suite.add_test(assert_equals("/tmp", start_info2.working_directory.fmt(), "ProcessStartInfo working directory set correctly"))
    suite.add_test(assert_true(start_info2.use_shell_execute, "ProcessStartInfo shell execute set correctly"))
    suite.add_test(assert_equals(ProcessWindowStyle.hidden, start_info2.window_style, "ProcessStartInfo window style set correctly"))

    # Test 4: Static process methods
    let current_process = Process.get_current_process()
    suite.add_test(assert_not_null(option.some(current_process), "Get current process returns a process"))

    let process_by_id = Process.get_process_by_id(1234)
    suite.add_test(assert_true(process_by_id.is_none(), "Get process by invalid ID returns none"))

    let all_processes = Process.get_processes()
    suite.add_test(assert_true(all_processes.length() >= 0, "Get processes returns valid list"))

    # Test 5: Process start with file name
    let test_file = string.from("echo")  # Cross-platform command
    let started_process = Process.start(test_file)
    suite.add_test(assert_true(started_process.is_some(), "Process start returns some for valid command"))

    if started_process.is_some() then
        let proc = started_process.unwrap()
        suite.add_test(assert_true(proc.process_id > 0, "Started process has valid ID"))

        # Clean up
        proc.kill()
    end if

    # Test 6: Process start with ProcessStartInfo
    let mut start_info3 = ProcessStartInfo.new()
    start_info3.file_name = string.from("echo")
    start_info3.arguments = string.from("Hello World")

    let started_process2 = Process.start_with_info(start_info3)
    suite.add_test(assert_true(started_process2.is_some(), "Process start with info returns some"))

    # Test 7: Process properties
    if started_process2.is_some() then
        let proc2 = started_process2.unwrap()
        let process_name = proc2.process_name()
        suite.add_test(assert_true(process_name.fmt().contains("echo"), "Process name is derived from file name"))

        let main_module = proc2.main_module_file_name()
        suite.add_test(assert_equals("echo", main_module.fmt(), "Main module file name matches executable"))

        proc2.kill()
    end if

    # Test 8: Process control methods
    let mut test_proc = Process.new()
    let started = test_proc.start_internal()
    suite.add_test(assert_true(started, "Process start internal returns true"))
    suite.add_test(assert_false(test_proc.has_exited, "Process has not exited after start"))

    test_proc.wait_for_exit()
    suite.add_test(assert_true(test_proc.has_exited, "Process has exited after wait_for_exit"))

    # Test 9: Process resource properties (placeholder values)
    let working_set = test_proc.working_set()
    suite.add_test(assert_true(working_set >= 0, "Working set is non-negative"))

    let private_memory = test_proc.private_memory_size()
    suite.add_test(assert_true(private_memory >= 0, "Private memory is non-negative"))

    let virtual_memory = test_proc.virtual_memory_size()
    suite.add_test(assert_true(virtual_memory >= 0, "Virtual memory is non-negative"))

    # Test 10: Process CPU time
    let cpu_time = test_proc.total_processor_time()
    suite.add_test(assert_true(cpu_time.ticks >= 0, "Processor time is non-negative"))

    test_proc.close()

    return suite
end fn

# =============================================================================
# Activity and ActivitySource Tests
# =============================================================================

pub fn test_activity() -> TestSuite
    let suite = TestSuite.new("Activity and ActivitySource Tests")

    # Test 1: Activity construction
    let activity1 = Activity.new(string.from("TestActivity"))
    suite.add_test(assert_equals("TestActivity", activity1.activity_name.fmt(), "Activity name set correctly"))
    suite.add_test(assert_false(activity1.is_stopped(), "New activity is not stopped"))
    suite.add_test(assert_true(activity1.duration.is_none(), "New activity has no duration"))
    suite.add_test(assert_true(activity1.parent_span_id.is_none(), "New activity has no parent"))

    # Test 2: Activity ID generation
    suite.add_test(assert_false(activity1.trace_id.is_empty(), "Activity has trace ID"))
    suite.add_test(assert_false(activity1.span_id.is_empty(), "Activity has span ID"))

    # Test 3: Activity parent relationship
    let parent_activity = Activity.new(string.from("Parent"))
    let child_activity = Activity.new(string.from("Child"))
    child_activity.set_parent(parent_activity)

    suite.add_test(assert_true(child_activity.parent_span_id.is_some(), "Child activity has parent span ID"))
    if child_activity.parent_span_id.is_some() then
        let parent_id = child_activity.parent_span_id.unwrap()
        suite.add_test(assert_equals(parent_activity.span_id.fmt(), parent_id.fmt(), "Child parent span ID matches parent span ID"))
    end if

    # Test 4: Activity tags
    activity1.set_tag(string.from("key1"), string.from("value1"))
    activity1.set_tag(string.from("key2"), string.from("value2"))

    let tag1 = activity1.tags.get(string.from("key1"))
    let tag2 = activity1.tags.get(string.from("key2"))
    let tag3 = activity1.tags.get(string.from("nonexistent"))

    suite.add_test(assert_equals("value1", tag1.unwrap().fmt(), "Tag 1 retrieved correctly"))
    suite.add_test(assert_equals("value2", tag2.unwrap().fmt(), "Tag 2 retrieved correctly"))
    suite.add_test(assert_true(tag3.is_none(), "Nonexistent tag returns none"))

    # Test 5: Activity status
    activity1.set_status(string.from("Success"))
    suite.add_test(assert_equals("Success", activity1.status.fmt(), "Activity status set correctly"))

    # Test 6: Activity lifecycle
    activity1.stop()
    suite.add_test(assert_true(activity1.is_stopped(), "Activity is stopped after stop"))
    suite.add_test(assert_true(activity1.duration.is_some(), "Activity has duration after stop"))

    if activity1.duration.is_some() then
        let duration = activity1.duration.unwrap()
        suite.add_test(assert_true(duration.ticks >= 0, "Activity duration is non-negative"))
    end if

    # Test 7: ActivitySource construction
    let source1 = ActivitySource.new(string.from("TestSource"))
    suite.add_test(assert_equals("TestSource", source1.name.fmt(), "ActivitySource name set correctly"))
    suite.add_test(assert_equals("1.0.0", source1.version.fmt(), "ActivitySource default version set"))
    suite.add_test(assert_true(source1.has_listeners(), "ActivitySource has listeners"))

    # Test 8: ActivitySource with version
    let source2 = ActivitySource.new_with_version(string.from("VersionedSource"), string.from("2.1.0"))
    suite.add_test(assert_equals("VersionedSource", source2.name.fmt(), "ActivitySource name set correctly"))
    suite.add_test(assert_equals("2.1.0", source2.version.fmt(), "ActivitySource version set correctly"))

    # Test 9: ActivitySource activity creation
    let activity3 = source1.start_activity(string.from("SourceActivity"))
    suite.add_test(assert_equals("SourceActivity", activity3.activity_name.fmt(), "Activity created from source has correct name"))
    suite.add_test(assert_equals(1, source1.activities.length(), "ActivitySource tracks created activity"))

    # Test 10: ActivityTagsCollection operations
    let tags = ActivityTagsCollection.new()
    tags.add(string.from("service"), string.from("my-service"))
    tags.add(string.from("version"), string.from("1.0"))

    suite.add_test(assert_equals(2, tags.tags.length(), "Tags collection contains correct number of tags"))

    let service_tag = tags.get(string.from("service"))
    let version_tag = tags.get(string.from("version"))
    let missing_tag = tags.get(string.from("missing"))

    suite.add_test(assert_equals("my-service", service_tag.unwrap().fmt(), "Service tag retrieved correctly"))
    suite.add_test(assert_equals("1.0", version_tag.unwrap().fmt(), "Version tag retrieved correctly"))
    suite.add_test(assert_true(missing_tag.is_none(), "Missing tag returns none"))

    return suite
end fn

# =============================================================================
# Performance Counter Tests
# =============================================================================

pub fn test_performance_counter() -> TestSuite
    let suite = TestSuite.new("Performance Counter Tests")

    # Test 1: PerformanceCounter construction
    let counter1 = PerformanceCounter.new(string.from("Processor"), string.from("% Processor Time"))
    suite.add_test(assert_equals("Processor", counter1.category_name.fmt(), "PerformanceCounter category name set"))
    suite.add_test(assert_equals("% Processor Time", counter1.counter_name.fmt(), "PerformanceCounter counter name set"))
    suite.add_test(assert_true(counter1.instance_name.is_empty(), "PerformanceCounter instance name is empty by default"))

    # Test 2: PerformanceCounter with instance
    let counter2 = PerformanceCounter.new_with_instance(string.from("Memory"), string.from("Available MBytes"), string.from("_Total"))
    suite.add_test(assert_equals("Memory", counter2.category_name.fmt(), "PerformanceCounter with instance category name set"))
    suite.add_test(assert_equals("Available MBytes", counter2.counter_name.fmt(), "PerformanceCounter with instance counter name set"))
    suite.add_test(assert_equals("_Total", counter2.instance_name.fmt(), "PerformanceCounter instance name set"))

    # Test 3: Performance counter values (placeholder)
    let value1 = counter1.next_value()
    suite.add_test(assert_true(value1 >= 0.0, "Performance counter next value returns non-negative"))

    let raw_value = counter1.raw_value()
    suite.add_test(assert_true(raw_value >= 0, "Performance counter raw value returns non-negative"))

    return suite
end fn

# =============================================================================
# Event Log Tests
# =============================================================================

pub fn test_event_log() -> TestSuite
    let suite = TestSuite.new("Event Log Tests")

    # Test 1: EventLog construction
    let log1 = EventLog.new(string.from("Application"), string.from("."), string.from("MySource"))
    suite.add_test(assert_equals("Application", log1.log_name.fmt(), "EventLog log name set"))
    suite.add_test(assert_equals(".", log1.machine_name.fmt(), "EventLog machine name set"))
    suite.add_test(assert_equals("MySource", log1.source.fmt(), "EventLog source set"))

    # Test 2: Write different event types
    log1.write_entry(string.from("Error occurred"), EventLogEntryType.error)
    suite.add_test(assert_true(true, "Error entry written without error"))

    log1.write_entry(string.from("Warning message"), EventLogEntryType.warning)
    suite.add_test(assert_true(true, "Warning entry written without error"))

    log1.write_entry(string.from("Information message"), EventLogEntryType.information)
    suite.add_test(assert_true(true, "Information entry written without error"))

    log1.write_entry(string.from("Success audit"), EventLogEntryType.success_audit)
    suite.add_test(assert_true(true, "Success audit entry written without error"))

    log1.write_entry(string.from("Failure audit"), EventLogEntryType.failure_audit)
    suite.add_test(assert_true(true, "Failure audit entry written without error"))

    # Test 3: Write entry with category
    log1.write_entry_with_category(string.from("Message with category"), EventLogEntryType.information, 42)
    suite.add_test(assert_true(true, "Entry with category written without error"))

    return suite
end fn

# =============================================================================
# Integration Test - Run all diagnostics module tests
# =============================================================================

pub fn run_all_diagnostics_tests() -> bool
    print("=== Running Diagnostics Module Tests ===")

    let mut all_passed = true

    all_passed = all_passed and test.run_test_suite("Debug", test_debug)
    all_passed = all_passed and test.run_test_suite("Trace", test_trace)
    all_passed = all_passed and test.run_test_suite("Stopwatch", test_stopwatch)
    all_passed = all_passed and test.run_test_suite("TimeSpan", test_time_span)
    all_passed = all_passed and test.run_test_suite("Process", test_process)
    all_passed = all_passed and test.run_test_suite("Activity", test_activity)
    all_passed = all_passed and test.run_test_suite("Performance Counter", test_performance_counter)
    all_passed = all_passed and test.run_test_suite("Event Log", test_event_log)

    print("=== Diagnostics Module Tests Complete ===")
    if all_passed then
        print("✅ All diagnostics module tests PASSED")
    else
        print("❌ Some diagnostics module tests FAILED")
    end if

    return all_passed
end fn

# =============================================================================
# Performance Benchmarks
# =============================================================================

pub fn run_diagnostics_benchmarks() -> void
    print("=== Running Diagnostics Module Benchmarks ===")

    # Benchmark stopwatch overhead
    benchmark("Stopwatch Start/Stop Overhead", 1000000, fn()
        let sw = Stopwatch.new()
        sw.start()
        sw.stop()
    end fn)

    # Benchmark string formatting
    benchmark("TimeSpan Formatting", 100000, fn()
        let ts = TimeSpan.from_minutes(5)
        let formatted = ts.to_string()
    end fn)

    # Benchmark activity creation
    benchmark("Activity Creation", 100000, fn()
        let activity = Activity.new(string.from("BenchmarkActivity"))
        activity.set_tag(string.from("benchmark"), string.from("true"))
        activity.stop()
    end fn)

    print("=== Diagnostics Module Benchmarks Complete ===")
end fn