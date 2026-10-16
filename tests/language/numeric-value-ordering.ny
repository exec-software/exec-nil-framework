# Legacy value sorts retain aliases; the oracle checks order and independent
# occurrence counts, without duplicating the index-sort implementation.
import ny.lang.core
import ny.lang.math

fn require(ok: bool, label: text)
    if not ok then panic(label) end if
end fn

fn equivalent<T>(a: T, b: T) -> bool
    return a == b or (a != a and b != b)
end fn

fn copy<T>(items: array<T>) -> array<T>
    let mut result: array<T> = []
    for index in range(items.len())
        result.push(items[index])
    end for
    return result
end fn

fn occurrences<T>(items: array<T>, value: T) -> i32
    let mut count = 0
    for index in range(items.len())
        if equivalent<T>(items[index], value) then count = count + 1 end if
    end for
    return count
end fn

fn same_bag<T>(actual: array<T>, original: array<T>)
    require(actual.len() == original.len(), "sort length")
    let mut checked: array<T> = []
    for index in range(original.len())
        let value = original[index]
        if occurrences<T>(checked, value) == 0 then
            require(occurrences<T>(actual, value) == occurrences<T>(original, value), "sort preserves multiset")
            checked.push(value)
        end if
    end for
end fn

fn same<T>(actual: array<T>, expected: array<T>)
    require(actual.len() == expected.len(), "snapshot length")
    for index in range(actual.len())
        require(equivalent<T>(actual[index], expected[index]), "snapshot values")
    end for
end fn

fn same_f64(actual: array<f64>, expected: array<f64>)
    same<f64>(actual, expected)
    for index in range(actual.len())
        if expected[index] == 0.0 then
            require((atan2(actual[index], -1.0) < 0.0) == (atan2(expected[index], -1.0) < 0.0), "signed-zero order")
        end if
    end for
end fn

fn check_i32(keys: array<i32>)
    for direction in range(2)
        let items = copy<i32>(keys)
        let alias = items
        let ordered = if direction == 0 then sort_i32(items) else sort_i32_desc(items) end if
        same_bag<i32>(ordered, keys)
        same<i32>(alias, ordered)
        for index in range(1, ordered.len())
            if direction == 0 then
                require(ordered[index - 1] <= ordered[index], "integer ascending")
            else
                require(ordered[index - 1] >= ordered[index], "integer descending")
            end if
        end for
        # Return identity is observable even for zero/one-element inputs.
        ordered.push(41)
        require(alias.len() == keys.len() + 1, "integer returned handle aliases input")
        alias[keys.len()] = 42
        require(ordered[keys.len()] == 42, "integer alias writes reach return")
        alias.push(43)
        require(ordered.len() == keys.len() + 2, "integer input growth reaches return")
    end for
end fn

fn check_f64(keys: array<f64>)
    for direction in range(2)
        let items = copy<f64>(keys)
        let alias = items
        let ordered = if direction == 0 then sort_f64(items) else sort_f64_desc(items) end if
        same_bag<f64>(ordered, keys)
        same_f64(alias, ordered)
        for index in range(1, ordered.len())
            let previous = ordered[index - 1]
            let current = ordered[index]
            if previous != previous then
                require(current != current, "NaNs remain last in both directions")
            elseif current == current then
                if direction == 0 then
                    require(previous <= current, "floating ascending")
                else
                    require(previous >= current, "floating descending")
                end if
            end if
        end for
        ordered.push(41.25)
        require(alias.len() == keys.len() + 1, "floating returned handle aliases input")
        alias[keys.len()] = 42.25
        require(ordered[keys.len()] == 42.25, "floating alias writes reach return")
        alias.push(43.25)
        require(ordered.len() == keys.len() + 2, "floating input growth reaches return")
    end for
end fn

fn main() -> i32
    let nan = sqrt(-1.0)
    let infinity = exp(1000.0)
    let classes: array<f64> = [-infinity, -2.0, -0.0, 0.0, 1.0, infinity, nan, nan]
    let ranks: array<i32> = [0, 1, 2, 2, 3, 4, 5, 5]
    for left in range(classes.len())
        for right in range(classes.len())
            let relation = compare_f64(classes[left], classes[right])
            require(relation.is_less() == (ranks[left] < ranks[right]), "comparison less")
            require(relation.is_eq() == (ranks[left] == ranks[right]), "comparison equal")
            require(relation.is_gt() == (ranks[left] > ranks[right]), "comparison greater")
        end for
    end for
    require(nan != nan and not (nan == nan), "raw IEEE NaN equality unchanged")
    require(not (nan < 1.0) and not (nan > 1.0), "raw IEEE NaN order unchanged")
    println("numeric comparator policy")

    let sizes: array<i32> = [0, 1, 2, 15, 16, 17, 31, 32, 33, 257]
    for slot in range(sizes.len())
        let mut integers: array<i32> = []
        let mut fractions: array<f64> = []
        for index in range(sizes[slot])
            let value = (index * 37 + 11) % 19 - 9
            integers.push(value)
            fractions.push((value as f64) / 4.0)
        end for
        check_i32(integers)
        check_f64(fractions)
    end for
    let minimum: i32 = -2147483647 - 1
    check_i32([2147483647, minimum, 0, minimum, 2147483647])
    check_i32([-3, -2, -1, 0, 1, 2, 3])
    check_i32([3, 2, 1, 0, -1, -2, -3])
    let mut equal_i32: array<i32> = []
    let mut equal_f64: array<f64> = []
    for index in range(2048)
        equal_i32.push(7)
        equal_f64.push(7.25)
    end for
    check_i32(equal_i32)
    check_f64(equal_f64)
    check_f64([nan])
    check_f64([nan, nan, nan])
    check_f64([nan, 2.0, -0.0, 0.0, -infinity, nan, infinity, 2.0, -1.0])
    println("numeric sorts preserve values and aliases")

    let zero_keys: array<f64> = [nan, -0.0, 2.0, 0.0, -0.0, -1.0, 0.0, nan]
    let ascending = sort_f64(copy<f64>(zero_keys))
    let descending = sort_f64_desc(copy<f64>(zero_keys))
    same_f64(ascending, [-1.0, -0.0, 0.0, -0.0, 0.0, 2.0, nan, nan])
    same_f64(descending, [2.0, -0.0, 0.0, -0.0, 0.0, -1.0, nan, nan])
    let mut long_zeros: array<f64> = []
    for repetition in range(3)
        for index in range(zero_keys.len())
            long_zeros.push(zero_keys[index])
        end for
    end for
    same_f64(sort_f64(copy<f64>(long_zeros)), [-1.0, -1.0, -1.0, -0.0, 0.0, -0.0, 0.0, -0.0, 0.0, -0.0, 0.0, -0.0, 0.0, -0.0, 0.0, 2.0, 2.0, 2.0, nan, nan, nan, nan, nan, nan])
    same_f64(sort_f64_desc(copy<f64>(long_zeros)), [2.0, 2.0, 2.0, -0.0, 0.0, -0.0, 0.0, -0.0, 0.0, -0.0, 0.0, -0.0, 0.0, -0.0, 0.0, -1.0, -1.0, -1.0, nan, nan, nan, nan, nan, nan])
    let insertion_input = copy<f64>(zero_keys)
    let inserted = insertion_sort_f64(insertion_input)
    same_f64(inserted, ascending)
    inserted.push(9.0)
    require(insertion_input.len() == zero_keys.len() + 1, "insertion sort preserves handle")
    println("signed zeros stable and insertion policy aligned")

    let sorted_snapshot = copy<f64>(classes)
    require(is_sorted_f64(classes), "ascending classes sorted")
    require(is_sorted_f64([]) and is_sorted_f64([nan]), "empty and singleton sorted")
    require(is_sorted_f64([nan, nan]), "NaNs sorted together")
    require(not is_sorted_f64([2.0, nan, 1.0]), "NaN barrier does not conceal inversion")
    require(not is_sorted_f64([nan, 1.0]), "leading NaN unsorted")
    require(not is_sorted_f64([2.0, 1.0, nan]), "finite inversion before NaN")
    for target_index in range(classes.len())
        let target = classes[target_index]
        let found = binary_search_f64(classes, target)
        require(found >= 0 and found < classes.len(), "search finds each equivalence class")
        require(equivalent<f64>(classes[found], target), "search found equivalent value")
    end for
    require(binary_search_f64(classes, 0.5) == -1, "missing finite target")
    require(binary_search_f64([], nan) == -1, "empty NaN search")
    require(binary_search_f64([1.0, 2.0], nan) == -1, "missing NaN target")
    require(binary_search_f64([nan, nan], 1.0) == -1, "finite target absent from NaNs")
    require(binary_search_f64([nan, nan], nan) >= 0, "NaN-only search")
    same_f64(classes, sorted_snapshot)
    println("sorted checks and search agree without mutation")

    require(min_f64([]).is_none() and max_f64([]).is_none(), "empty extrema")
    let extrema: array<f64> = [nan, 2.0, -1.0, nan]
    let extrema_snapshot = copy<f64>(extrema)
    require(min_f64(extrema).unwrap() == -1.0, "minimum skips NaN when numeric present")
    let maximum = max_f64(extrema).unwrap()
    require(maximum != maximum, "maximum includes NaN")
    let middle_nan_maximum = max_f64([1.0, nan, 2.0]).unwrap()
    require(middle_nan_maximum != middle_nan_maximum, "maximum finds a later NaN")
    let nan_minimum = min_f64([nan, nan]).unwrap()
    let nan_maximum = max_f64([nan, nan]).unwrap()
    require(nan_minimum != nan_minimum and nan_maximum != nan_maximum, "all-NaN extrema")
    require(min_f64([-infinity, infinity]).unwrap() == -infinity, "negative infinity minimum")
    require(max_f64([-infinity, infinity]).unwrap() == infinity, "positive infinity maximum")
    require(atan2(min_f64([-0.0, 0.0]).unwrap(), -1.0) < 0.0, "minimum retains first zero")
    require(atan2(max_f64([-0.0, 0.0]).unwrap(), -1.0) < 0.0, "maximum retains first zero")
    require(atan2(min_f64([0.0, -0.0]).unwrap(), -1.0) > 0.0, "minimum retains positive first zero")
    require(atan2(max_f64([0.0, -0.0]).unwrap(), -1.0) > 0.0, "maximum retains positive first zero")
    same_f64(extrema, extrema_snapshot)
    let total = sum_f64(extrema)
    let average = average_f64(extrema).unwrap()
    require(total != total and average != average, "sum and average keep IEEE NaN propagation")
    println("extrema share ordering and preserve IEEE reductions")
    return 0
end fn
