@schema
@pragma once

module nil.std.testing

# =============================================================================
# Test Framework for Ny Standard Library
# =============================================================================

pub struct TestResult
    pub name: string
    pub passed: bool
    pub message: string
    pub duration: i64  # microseconds
end struct

pub struct TestSuite
    pub name: string
    pub tests: vec<TestResult>
    pub passed_count: i32
    pub failed_count: i32
end struct

impl TestSuite
    pub fn new(name: string) -> TestSuite
        return TestSuite {
            name: name,
            tests: vec<TestResult>.new(),
            passed_count: 0,
            failed_count: 0
        }
    end fn

    pub fn add_test(self: ref<Self>, result: TestResult)
        self.tests.push(result)
        if result.passed then
            self.passed_count = self.passed_count + 1
        else
            self.failed_count = self.failed_count + 1
        end if
    end fn

    pub fn print_results(self: ref<Self>)
        print("=== " + self.name.fmt() + " ===")
        print("Passed: " + self.passed_count.to_text() + ", Failed: " + self.failed_count.to_text())
        print("Total: " + self.tests.length().to_text() + " tests")

        if self.failed_count > 0 then
            print("\nFailed tests:")
            let mut i = 0
            while i < self.tests.length()
                let test = self.tests.get(i).unwrap()
                if not test.passed then
                    print("  ❌ " + test.name.fmt() + ": " + test.message.fmt())
                end if
                i = i + 1
            end while
        end if

        let success_rate = (self.passed_count as f32) / (self.tests.length() as f32) * 100.0
        print("Success rate: " + success_rate.to_text() + "%\n")
    end fn

    pub fn all_passed(self: ref<Self>) -> bool
        return self.failed_count == 0
    end fn
end impl

# =============================================================================
# Test assertion functions
# =============================================================================

pub fn assert_true(condition: bool, message: string) -> TestResult
    let start_time = get_microseconds()
    if condition then
        let end_time = get_microseconds()
        return TestResult {
            name: message,
            passed: true,
            message: string.new(),
            duration: end_time - start_time
        }
    else
        let end_time = get_microseconds()
        return TestResult {
            name: message,
            passed: false,
            message: string.from("Expected true but got false"),
            duration: end_time - start_time
        }
    end if
end fn

pub fn assert_false(condition: bool, message: string) -> TestResult
    return assert_true(not condition, message)
end fn

pub fn assert_equals<T>(expected: T, actual: T, message: string) -> TestResult
    let start_time = get_microseconds()
    # This is a simplified version - real implementation would need equality checking
    # For now, convert to text for comparison
    let expected_text = expected.to_text()
    let actual_text = actual.to_text()

    if expected_text == actual_text then
        let end_time = get_microseconds()
        return TestResult {
            name: message,
            passed: true,
            message: string.new(),
            duration: end_time - start_time
        }
    else
        let end_time = get_microseconds()
        let error_msg = string.from("Expected: ") + string.from(expected_text) +
                      string.from(", Actual: ") + string.from(actual_text)
        return TestResult {
            name: message,
            passed: false,
            message: error_msg,
            duration: end_time - start_time
        }
    end if
end fn

pub fn assert_not_null<T>(value: option<T>, message: string) -> TestResult
    let start_time = get_microseconds()
    match value
        option.some(_) ->
            let end_time = get_microseconds()
            return TestResult {
                name: message,
                passed: true,
                message: string.new(),
                duration: end_time - start_time
            }
        option.none ->
            let end_time = get_microseconds()
            return TestResult {
                name: message,
                passed: false,
                message: string.from("Expected some value but got none"),
                duration: end_time - start_time
            }
    end match
end fn

pub fn assert_null<T>(value: option<T>, message: string) -> TestResult
    let start_time = get_microseconds()
    match value
        option.some(_) ->
            let end_time = get_microseconds()
            return TestResult {
                name: message,
                passed: false,
                message: string.from("Expected none but got some value"),
                duration: end_time - start_time
            }
        option.none ->
            let end_time = get_microseconds()
            return TestResult {
                name: message,
                passed: true,
                message: string.new(),
                duration: end_time - start_time
            }
    end match
end fn

# =============================================================================
# Test helper functions
# =============================================================================

pub fn get_microseconds() -> i64
    # This would be implemented with platform-specific high-resolution timer
    # For now, return a mock value
    return 42 as i64
end fn

pub fn run_test_suite(name: string, test_fn: fn() -> TestSuite) -> bool
    print("Running test suite: " + name.fmt())
    let suite = test_fn()
    suite.print_results()
    return suite.all_passed()
end fn

# =============================================================================
# Test annotation support (for future use)
# =============================================================================

@test
pub fn test_annotation_example()
    # This function would be automatically discovered and run by test runner
    # Implementation pending
end fn

# =============================================================================
# Benchmark support
# =============================================================================

pub struct BenchmarkResult
    pub name: string
    pub iterations: i32
    pub total_time: i64
    pub average_time: f64
end struct

impl BenchmarkResult
    pub fn new(name: string, iterations: i32, total_time: i64) -> BenchmarkResult
        let avg_time = (total_time as f64) / (iterations as f64)
        return BenchmarkResult {
            name: name,
            iterations: iterations,
            total_time: total_time,
            average_time: avg_time
        }
    end fn

    pub fn print(self: ref<Self>)
        print("=== " + self.name.fmt() + " Benchmark ===")
        print("Iterations: " + self.iterations.to_text())
        print("Total time: " + self.total_time.to_text() + " μs")
        print("Average time: " + self.average_time.to_text() + " μs")
        print("Throughput: " + (1000000.0 / self.average_time).to_text() + " ops/sec")
    end fn
end impl

pub fn benchmark(name: string, iterations: i32, fn: fn()) -> BenchmarkResult
    print("Running benchmark: " + name.fmt())
    let start_time = get_microseconds()

    let mut i = 0
    while i < iterations
        fn()
        i = i + 1
    end while

    let end_time = get_microseconds()
    let result = BenchmarkResult.new(name, iterations, end_time - start_time)
    result.print()
    return result
end fn