@schema
@pragma once

import linq.{IEnumerable, IEnumerator, Enumerable}
import linq.{WhereEnumerable, SelectEnumerable, RangeEnumerable, EmptyEnumerable}
import test.{TestSuite, assert_true, assert_false, assert_equals, benchmark}

# =============================================================================
# LINQ Extension Tests
# =============================================================================

pub fn test_linq_where() -> TestSuite
    let suite = TestSuite.new("LINQ Where Tests")

    # Test 1: Basic where filtering
    let numbers = create_test_numbers()
    let filtered = Enumerable.where(numbers, fn(x: i32) -> bool { return x > 3 })
    let filtered_list = Enumerable.to_list(filtered)

    suite.add_test(assert_equals(6, filtered_list.length(), "Where filters correct number of elements"))
    suite.add_test(assert_equals(4, filtered_list.get(0).unwrap(), "First filtered element is correct"))
    suite.add_test(assert_equals(9, filtered_list.get(5).unwrap(), "Last filtered element is correct"))

    # Test 2: Where with no matches
    let no_matches = Enumerable.where(numbers, fn(x: i32) -> bool { return x > 20 })
    let no_matches_list = Enumerable.to_list(no_matches)

    suite.add_test(assert_equals(0, no_matches_list.length(), "Where with no matches returns empty"))

    # Test 3: Where with all matches
    let all_matches = Enumerable.where(numbers, fn(x: i32) -> bool { return x >= 0 })
    let all_matches_list = Enumerable.to_list(all_matches)

    suite.add_test(assert_equals(9, all_matches_list.length(), "Where with all matches returns all elements"))

    return suite
end fn

pub fn test_linq_select() -> TestSuite
    let suite = TestSuite.new("LINQ Select Tests")

    # Test 1: Basic select projection
    let numbers = create_test_numbers()
    let squared = Enumerable.select(numbers, fn(x: i32) -> i32 { return x * x })
    let squared_list = Enumerable.to_list(squared)

    suite.add_test(assert_equals(9, squared_list.length(), "Select preserves element count"))
    suite.add_test(assert_equals(1, squared_list.get(0).unwrap(), "First projection is correct (1*1)"))
    suite.add_test(assert_equals(81, squared_list.get(8).unwrap(), "Last projection is correct (9*9)"))

    # Test 2: Select to different type
    let strings = Enumerable.select(numbers, fn(x: i32) -> string { return string.from("Number_" + x.to_text()) })
    let strings_list = Enumerable.to_list(strings)

    suite.add_test(assert_equals("Number_1", strings_list.get(0).unwrap().fmt(), "Select to string works"))
    suite.add_test(assert_equals("Number_9", strings_list.get(8).unwrap().fmt(), "Select to string works for last element"))

    return suite
end fn

pub fn test_linq_ordering() -> TestSuite
    let suite = TestSuite.new("LINQ Ordering Tests")

    # Test 1: Order by
    let unsorted = create_unsorted_numbers()
    let sorted = Enumerable.order_by(unsorted, fn(x: i32) -> i32 { return x })
    let sorted_list = Enumerable.to_list(sorted)

    suite.add_test(assert_equals(7, sorted_list.length(), "Order by preserves element count"))
    suite.add_test(assert_equals(1, sorted_list.get(0).unwrap(), "First element is minimum"))
    suite.add_test(assert_equals(13, sorted_list.get(6).unwrap(), "Last element is maximum"))

    # Test 2: Order by descending
    let desc_sorted = Enumerable.order_by_descending(unsorted, fn(x: i32) -> i32 { return x })
    let desc_list = Enumerable.to_list(desc_sorted)

    suite.add_test(assert_equals(13, desc_list.get(0).unwrap(), "Descending order: first element is maximum"))
    suite.add_test(assert_equals(1, desc_list.get(6).unwrap(), "Descending order: last element is minimum"))

    return suite
end fn

pub fn test_linq_aggregation() -> TestSuite
    let suite = TestSuite.new("LINQ Aggregation Tests")

    # Test 1: Count
    let numbers = create_test_numbers()
    let count = Enumerable.count(numbers)
    suite.add_test(assert_equals(9, count, "Count returns correct number"))

    # Test 2: Count with predicate
    let count_greater = Enumerable.count_where(numbers, fn(x: i32) -> bool { return x > 5 })
    suite.add_test(assert_equals(4, count_greater, "Count with predicate works"))

    # Test 3: First
    let first = Enumerable.first(numbers)
    suite.add_test(assert_equals(1, first, "First returns first element"))

    # Test 4: First with predicate
    let first_gt_5 = Enumerable.first_where(numbers, fn(x: i32) -> bool { return x > 5 })
    suite.add_test(assert_equals(6, first_gt_5, "First with predicate works"))

    # Test 5: First or default
    let empty = create_empty_numbers()
    let first_or_empty = Enumerable.first_or_default(empty)
    suite.add_test(assert_equals(0, first_or_empty, "First or default on empty returns default"))

    # Test 6: Last
    let last = Enumerable.last(numbers)
    suite.add_test(assert_equals(9, last, "Last returns last element"))

    # Test 7: Min and Max
    let min_val = Enumerable.min(numbers)
    let max_val = Enumerable.max(numbers)
    suite.add_test(assert_equals(1, min_val, "Min returns correct minimum"))
    suite.add_test(assert_equals(9, max_val, "Max returns correct maximum"))

    # Test 8: Any
    let any_numbers = Enumerable.any(numbers)
    suite.add_test(assert_true(any_numbers, "Any on non-empty returns true"))

    let any_empty = Enumerable.any(empty)
    suite.add_test(assert_false(any_empty, "Any on empty returns false"))

    # Test 9: Any with predicate
    let any_gt_5 = Enumerable.any_where(numbers, fn(x: i32) -> bool { return x > 5 })
    suite.add_test(assert_true(any_gt_5, "Any with predicate returns true when match exists"))

    let any_gt_20 = Enumerable.any_where(numbers, fn(x: i32) -> bool { return x > 20 })
    suite.add_test(assert_false(any_gt_20, "Any with predicate returns false when no match"))

    # Test 10: All
    let all_positive = Enumerable.all(numbers, fn(x: i32) -> bool { return x > 0 })
    suite.add_test(assert_true(all_positive, "All returns true when all match"))

    let all_gt_5 = Enumerable.all(numbers, fn(x: i32) -> bool { return x > 5 })
    suite.add_test(assert_false(all_gt_5, "All returns false when some don't match"))

    return suite
end fn

pub fn test_linq_generation() -> TestSuite
    let suite = TestSuite.new("LINQ Generation Tests")

    # Test 1: Range
    let range = Enumerable.range(1, 5)
    let range_list = Enumerable.to_list(range)

    suite.add_test(assert_equals(5, range_list.length(), "Range generates correct count"))
    suite.add_test(assert_equals(1, range_list.get(0).unwrap(), "Range starts at correct value"))
    suite.add_test(assert_equals(5, range_list.get(4).unwrap(), "Range ends at correct value"))

    # Test 2: Repeat
    let repeated = Enumerable.repeat(7, 3)
    let repeated_list = Enumerable.to_list(repeated)

    suite.add_test(assert_equals(3, repeated_list.length(), "Repeat generates correct count"))
    suite.add_test(assert_equals(7, repeated_list.get(0).unwrap(), "Repeat repeats correct value"))
    suite.add_test(assert_equals(7, repeated_list.get(1).unwrap(), "Repeat repeats correct value"))
    suite.add_test(assert_equals(7, repeated_list.get(2).unwrap(), "Repeat repeats correct value"))

    # Test 3: Empty
    let empty = Enumerable.empty<i32>()
    let empty_list = Enumerable.to_list(empty)

    suite.add_test(assert_equals(0, empty_list.length(), "Empty generates no elements"))

    return suite
end fn

pub fn test_linq_partitioning() -> TestSuite
    let suite = TestSuite.new("LINQ Partitioning Tests")

    # Test 1: Take
    let numbers = create_test_numbers()
    let take_three = Enumerable.take(numbers, 3)
    let take_list = Enumerable.to_list(take_three)

    suite.add_test(assert_equals(3, take_list.length(), "Take takes correct number of elements"))
    suite.add_test(assert_equals(1, take_list.get(0).unwrap(), "Take preserves order"))
    suite.add_test(assert_equals(2, take_list.get(1).unwrap(), "Take preserves order"))
    suite.add_test(assert_equals(3, take_list.get(2).unwrap(), "Take preserves order"))

    # Test 2: Skip
    let skip_three = Enumerable.skip(numbers, 3)
    let skip_list = Enumerable.to_list(skip_three)

    suite.add_test(assert_equals(6, skip_list.length(), "Skip leaves correct number of elements"))
    suite.add_test(assert_equals(4, skip_list.get(0).unwrap(), "Skip skips first elements"))
    suite.add_test(assert_equals(9, skip_list.get(5).unwrap(), "Skip skips first elements"))

    # Test 3: Take more than available
    let take_more = Enumerable.take(numbers, 20)
    let take_more_list = Enumerable.to_list(take_more)

    suite.add_test(assert_equals(9, take_more_list.length(), "Take more than available takes all"))

    # Test 4: Skip more than available
    let skip_more = Enumerable.skip(numbers, 20)
    let skip_more_list = Enumerable.to_list(skip_more)

    suite.add_test(assert_equals(0, skip_more_list.length(), "Skip more than available returns empty"))

    return suite
end fn

pub fn test_linq_conversion() -> TestSuite
    let suite = TestSuite.new("LINQ Conversion Tests")

    # Test 1: To list
    let numbers = create_test_numbers()
    let converted = Enumerable.to_list(numbers)

    suite.add_test(assert_equals(9, converted.length(), "To list preserves count"))
    suite.add_test(assert_equals(1, converted.get(0).unwrap(), "To list preserves order"))

    # Test 2: Group by
    let grouped = Enumerable.group_by(numbers, fn(x: i32) -> i32 { return x % 3 })

    suite.add_test(assert_equals(3, grouped.length(), "Group by creates correct number of groups"))

    # Verify groups contain correct elements
    let mut found_group_0 = false
    let mut found_group_1 = false
    let mut found_group_2 = false

    let mut i = 0
    while i < grouped.length()
        let group = grouped.get(i).unwrap()
        let remainder = group.0

        match remainder
            0 -> found_group_0 = true
            1 -> found_group_1 = true
            2 -> found_group_2 = true
            else -> {}
        end match
        i = i + 1
    end while

    suite.add_test(assert_true(found_group_0, "Group by creates group for remainder 0"))
    suite.add_test(assert_true(found_group_1, "Group by creates group for remainder 1"))
    suite.add_test(assert_true(found_group_2, "Group by creates group for remainder 2"))

    return suite
end fn

pub fn test_linq_set_operations() -> TestSuite
    let suite = TestSuite.new("LINQ Set Operations Tests")

    # Test 1: Concat
    let first = create_test_numbers()
    let second = create_more_numbers()
    let concatenated = Enumerable.concat(first, second)
    let concat_list = Enumerable.to_list(concatenated)

    suite.add_test(assert_equals(14, concat_list.length(), "Concat combines sequences"))
    suite.add_test(assert_equals(1, concat_list.get(0).unwrap(), "Concat preserves first sequence order"))
    suite.add_test(assert_equals(10, concat_list.get(9).unwrap(), "Concat joins sequences correctly"))

    # Test 2: Contains
    let numbers = create_test_numbers()
    let contains_5 = Enumerable.contains(numbers, 5)
    let contains_20 = Enumerable.contains(numbers, 20)

    suite.add_test(assert_true(contains_5, "Contains finds existing element"))
    suite.add_test(assert_false(contains_20, "Contains returns false for non-existing element"))

    return suite
end fn

pub fn test_linq_chaining() -> TestSuite
    let suite = TestSuite.new("LINQ Chaining Tests")

    # Test complex LINQ chain
    let numbers = create_test_numbers()
    let result = numbers
        |> Enumerable.where(fn(x: i32) -> bool { return x % 2 == 0 })  # Even numbers
        |> Enumerable.select(fn(x: i32) -> i32 { return x * 2 })     # Double them
        |> Enumerable.order_by(fn(x: i32) -> i32 { return x })         # Sort
        |> Enumerable.take(3)                                      # Take first 3

    let result_list = Enumerable.to_list(result)

    suite.add_test(assert_equals(3, result_list.length(), "Chained operations produce correct count"))
    suite.add_test(assert_equals(2, result_list.get(0).unwrap(), "Chained operations produce correct first result"))
    suite.add_test(assert_equals(4, result_list.get(1).unwrap(), "Chained operations produce correct second result"))
    suite.add_test(assert_equals(6, result_list.get(2).unwrap(), "Chained operations produce correct third result"))

    return suite
end fn

# =============================================================================
# Helper Functions for Testing
# =============================================================================

# Mock IEnumerable implementation for testing
pub struct TestEnumerable<T>
    pub data: vec<T>
end struct

impl<T> IEnumerable<T> for TestEnumerable<T>
    pub fn get_enumerator(this: ref<Self>) -> IEnumerator<T>
        return TestEnumerator.new(this.data)
    end fn
end impl

pub struct TestEnumerator<T>
    pub data: vec<T>
    pub index: i32
end struct

impl<T> IEnumerator<T> for TestEnumerator<T>
    pub fn current(this: ref<Self>) -> T
        return this.data.get(this.index).unwrap()
    end fn

    pub fn move_next(this: ref<Self>) -> bool
        this.index = this.index + 1
        return this.index < this.data.length()
    end fn

    pub fn reset(this: ref<Self>) -> void
        this.index = -1
    end fn
end impl

impl<T> TestEnumerator<T>
    pub fn new(data: vec<T>) -> TestEnumerator<T>
        return TestEnumerator { data: data, index: -1 }
    end fn
end impl

pub fn create_test_numbers() -> TestEnumerable<i32>
    let data = vec<i32>.new()
    data.push(1)
    data.push(2)
    data.push(3)
    data.push(4)
    data.push(5)
    data.push(6)
    data.push(7)
    data.push(8)
    data.push(9)
    return TestEnumerable { data: data }
end fn

pub fn create_empty_numbers() -> TestEnumerable<i32>
    let data = vec<i32>.new()
    return TestEnumerable { data: data }
end fn

pub fn create_unsorted_numbers() -> TestEnumerable<i32>
    let data = vec<i32>.new()
    data.push(7)
    data.push(3)
    data.push(9)
    data.push(1)
    data.push(5)
    data.push(13)
    data.push(11)
    return TestEnumerable { data: data }
end fn

pub fn create_more_numbers() -> TestEnumerable<i32>
    let data = vec<i32>.new()
    data.push(10)
    data.push(11)
    data.push(12)
    data.push(13)
    data.push(14)
    return TestEnumerable { data: data }
end fn

# Pipeline operator for LINQ chaining (this would be language syntax sugar)
pub fn pipeline<T, TResult>(source: IEnumerable<T>, operation: fn(IEnumerable<T>) -> TResult) -> TResult
    return operation(source)
end fn

# Mock pipeline implementation
infix operator |> fn

pub fn test_infix_operator(a: i32, b: fn(i32) -> i32) -> i32
    return b(a)
end fn

# =============================================================================
# Integration Test - Run all LINQ module tests
# =============================================================================

pub fn run_all_linq_tests() -> bool
    print("=== Running LINQ Module Tests ===")

    let mut all_passed = true

    all_passed = all_passed and test.run_test_suite("Where", test_linq_where)
    all_passed = all_passed and test.run_test_suite("Select", test_linq_select)
    all_passed = all_passed and test.run_test_suite("Ordering", test_linq_ordering)
    all_passed = all_passed and test.run_test_suite("Aggregation", test_linq_aggregation)
    all_passed = all_passed and test.run_test_suite("Generation", test_linq_generation)
    all_passed = all_passed and test.run_test_suite("Partitioning", test_linq_partitioning)
    all_passed = all_passed and test.run_test_suite("Conversion", test_linq_conversion)
    all_passed = all_passed and test.run_test_suite("Set Operations", test_linq_set_operations)
    all_passed = all_passed and test.run_test_suite("Chaining", test_linq_chaining)

    print("=== LINQ Module Tests Complete ===")
    if all_passed then
        print("✅ All LINQ module tests PASSED")
    else
        print("❌ Some LINQ module tests FAILED")
    end if

    return all_passed
end fn

# =============================================================================
# Performance Benchmarks
# =============================================================================

pub fn run_linq_benchmarks() -> void
    print("=== Running LINQ Module Benchmarks ===")

    let numbers = create_test_numbers()

    # Benchmark where operation
    benchmark("LINQ Where Operation", 100000, fn()
        let filtered = Enumerable.where(numbers, fn(x: i32) -> bool { return x > 5 })
        let count = Enumerable.count(filtered)
    end fn)

    # Benchmark select operation
    benchmark("LINQ Select Operation", 100000, fn()
        let projected = Enumerable.select(numbers, fn(x: i32) -> i32 { return x * x })
        let count = Enumerable.count(projected)
    end fn)

    # Benchmark order by operation
    benchmark("LINQ Order By Operation", 10000, fn()
        let sorted = Enumerable.order_by(numbers, fn(x: i32) -> i32 { return x })
        let count = Enumerable.count(sorted)
    end fn)

    # Benchmark chaining operations
    benchmark("LINQ Chaining Operations", 10000, fn()
        let result = numbers
            |> Enumerable.where(fn(x: i32) -> bool { return x % 2 == 0 })
            |> Enumerable.select(fn(x: i32) -> i32 { return x * 2 })
            |> Enumerable.order_by(fn(x: i32) -> i32 { return x })
        let count = Enumerable.count(result)
    end fn)

    print("=== LINQ Module Benchmarks Complete ===")
end fn