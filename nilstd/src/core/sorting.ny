@schema
@pragma once

module nil.std.core

import nil.std.core.traits
import nil.std.core.option
import nil.std.core.result
import nil.std.core.sorting_indices

# =============================================================================
# Sorting - Sort algorithms and utilities (mirrors System.Array.Sort / LINQ OrderBy)
# =============================================================================
# Provides sorting algorithms for built-in arrays:
#   - Insertion sort (stable, good for small/nearly-sorted data)
#   - Stable numeric/text sorting (O(n log n) key comparisons, O(n) storage)
#   - Recursive integer merge sort (stable)
#   - Binary search on sorted data
#   - Utility: is_sorted, min, max, bounds, unique, and shuffle
#
# All sort functions operate on array<i32>, array<text>, or array<f64>.
# Text comparison cost additionally depends on the compared byte lengths.
#
# Usage:
#   import ny.lang.core
#   let mut nums: array<i32> = [5, 2, 8, 1, 9]
#   nums = sort_i32(nums)
#   let idx = binary_search_i32(nums, 8)
# =============================================================================

# =============================================================================
# Integer Sorting
# =============================================================================

# Insertion sort - stable, O(n^2), good for small arrays
pub fn insertion_sort_i32(mut arr: array<i32>) -> array<i32>
    let n = arr.length()
    if n <= 1 then
        return arr
    end if
    let mut i = 1
    while i < n do
        let key = arr[i]
        let mut j = i - 1
        while j >= 0 and arr[j] > key do
            arr[j + 1] = arr[j]
            j = j - 1
        end while
        arr[j + 1] = key
        i = i + 1
    end while
    return arr
end fn

# Sort values through the shared Matrix stable-index implementation.
# Copy ordered values into temporary storage before writing the original array;
# aliases and the returned handle still refer to that original array.
pub fn sort_i32(mut arr: array<i32>) -> array<i32>
    return sort_values<i32>(arr, false)
end fn

fn swap_i32(mut arr: array<i32>, a: i32, b: i32)
    let tmp = arr[a]
    arr[a] = arr[b]
    arr[b] = tmp
end fn

# Sort descending
pub fn sort_i32_desc(mut arr: array<i32>) -> array<i32>
    return sort_values<i32>(arr, true)
end fn

# Reverse an array
pub fn reverse_i32(mut arr: array<i32>) -> array<i32>
    let n = arr.length()
    let mut i = 0
    let mut j = n - 1
    while i < j do
        swap_i32(arr, i, j)
        i = i + 1
        j = j - 1
    end while
    return arr
end fn

# =============================================================================
# Integer Search
# =============================================================================

# Binary search - requires sorted array, returns index or -1
pub fn binary_search_i32(arr: array<i32>, target: i32) -> i32
    let mut low = 0
    let mut high = arr.length() - 1
    while low <= high do
        let mid = low + (high - low) / 2
        if arr[mid] == target then
            return mid
        elseif arr[mid] < target then
            low = mid + 1
        else
            high = mid - 1
        end if
    end while
    return -1
end fn

# Lower bound - first element >= target
pub fn lower_bound_i32(arr: array<i32>, target: i32) -> i32
    let mut low = 0
    let mut high = arr.length()
    while low < high do
        let mid = low + (high - low) / 2
        if arr[mid] < target then
            low = mid + 1
        else
            high = mid
        end if
    end while
    return low
end fn

# Upper bound - first element > target
pub fn upper_bound_i32(arr: array<i32>, target: i32) -> i32
    let mut low = 0
    let mut high = arr.length()
    while low < high do
        let mid = low + (high - low) / 2
        if arr[mid] <= target then
            low = mid + 1
        else
            high = mid
        end if
    end while
    return low
end fn

# =============================================================================
# Integer Queries
# =============================================================================

pub fn is_sorted_i32(arr: array<i32>) -> bool
    let n = arr.length()
    if n <= 1 then
        return true
    end if
    let mut i = 1
    while i < n do
        if arr[i] < arr[i - 1] then
            return false
        end if
        i = i + 1
    end while
    return true
end fn

pub fn min_element_i32(arr: array<i32>) -> Option<i32>
    let n = arr.length()
    if n == 0 then
        return Option.None
    end if
    let mut m = arr[0]
    let mut i = 1
    while i < n do
        if arr[i] < m then
            m = arr[i]
        end if
        i = i + 1
    end while
    return Option.Some(m)
end fn

pub fn max_element_i32(arr: array<i32>) -> Option<i32>
    let n = arr.length()
    if n == 0 then
        return Option.None
    end if
    let mut m = arr[0]
    let mut i = 1
    while i < n do
        if arr[i] > m then
            m = arr[i]
        end if
        i = i + 1
    end while
    return Option.Some(m)
end fn

pub fn sum_i32(arr: array<i32>) -> i64
    let mut total: i64 = 0
    let mut i = 0
    while i < arr.length() do
        total = total + arr[i]
        i = i + 1
    end while
    return total
end fn

# Unique - remove consecutive duplicates from sorted array
pub fn unique_i32(arr: array<i32>) -> array<i32>
    let n = arr.length()
    if n <= 1 then
        return arr
    end if
    let mut result: array<i32> = []
    result.push(arr[0])
    let mut i = 1
    while i < n do
        if arr[i] != arr[i - 1] then
            result.push(arr[i])
        end if
        i = i + 1
    end while
    return result
end fn

# =============================================================================
# Text Sorting
# =============================================================================

pub fn sort_text(mut arr: array<text>) -> array<text>
    return sort_values<text>(arr, false)
end fn

pub fn insertion_sort_text(mut arr: array<text>) -> array<text>
    let n = arr.length()
    if n <= 1 then
        return arr
    end if
    let mut i = 1
    while i < n do
        let key = arr[i]
        let mut j = i - 1
        while j >= 0 and arr[j] > key do
            arr[j + 1] = arr[j]
            j = j - 1
        end while
        arr[j + 1] = key
        i = i + 1
    end while
    return arr
end fn

fn swap_text(mut arr: array<text>, a: i32, b: i32)
    let tmp = arr[a]
    arr[a] = arr[b]
    arr[b] = tmp
end fn

pub fn sort_text_desc(mut arr: array<text>) -> array<text>
    return sort_values<text>(arr, true)
end fn

pub fn reverse_text(mut arr: array<text>) -> array<text>
    let n = arr.length()
    let mut i = 0
    let mut j = n - 1
    while i < j do
        swap_text(arr, i, j)
        i = i + 1
        j = j - 1
    end while
    return arr
end fn

# Binary search for text - requires sorted array
pub fn binary_search_text(arr: array<text>, target: text) -> i32
    let mut low = 0
    let mut high = arr.length() - 1
    while low <= high do
        let mid = low + (high - low) / 2
        if arr[mid] == target then
            return mid
        elseif arr[mid] < target then
            low = mid + 1
        else
            high = mid - 1
        end if
    end while
    return -1
end fn

pub fn is_sorted_text(arr: array<text>) -> bool
    let n = arr.length()
    if n <= 1 then
        return true
    end if
    let mut i = 1
    while i < n do
        if arr[i] < arr[i - 1] then
            return false
        end if
        i = i + 1
    end while
    return true
end fn

pub fn unique_text(arr: array<text>) -> array<text>
    let n = arr.length()
    if n <= 1 then
        return arr
    end if
    let mut result: array<text> = []
    result.push(arr[0])
    let mut i = 1
    while i < n do
        if arr[i] != arr[i - 1] then
            result.push(arr[i])
        end if
        i = i + 1
    end while
    return result
end fn

# =============================================================================
# Float Sorting
# =============================================================================

# Numeric order is ascending with NaNs last. Descending retains NaNs last
# too; equal keys, signed zeros, and NaNs retain their original relative order.
pub fn sort_f64(mut arr: array<f64>) -> array<f64>
    return sort_values<f64>(arr, false)
end fn

pub fn insertion_sort_f64(mut arr: array<f64>) -> array<f64>
    let n = arr.length()
    if n <= 1 then
        return arr
    end if
    let mut i = 1
    while i < n do
        let key = arr[i]
        let mut j = i - 1
        while j >= 0 and not sort_index_left_first<f64>(arr[j], key, false) do
            arr[j + 1] = arr[j]
            j = j - 1
        end while
        arr[j + 1] = key
        i = i + 1
    end while
    return arr
end fn

fn swap_f64(mut arr: array<f64>, a: i32, b: i32)
    let tmp = arr[a]
    arr[a] = arr[b]
    arr[b] = tmp
end fn

pub fn sort_f64_desc(mut arr: array<f64>) -> array<f64>
    return sort_values<f64>(arr, true)
end fn

pub fn reverse_f64(mut arr: array<f64>) -> array<f64>
    let n = arr.length()
    let mut i = 0
    let mut j = n - 1
    while i < j do
        swap_f64(arr, i, j)
        i = i + 1
        j = j - 1
    end while
    return arr
end fn

# Requires ascending NaN-last order. Matching uses the same ordering as
# compare_f64: two NaNs compare equal, as do positive and negative zero.
# As with the other binary searches, any matching index may be returned.
pub fn binary_search_f64(arr: array<f64>, target: f64) -> i32
    let mut low = 0
    let mut high = arr.length() - 1
    while low <= high do
        let mid = low + (high - low) / 2
        let candidate = arr[mid]
        if sort_index_left_first<f64>(candidate, target, false) then
            if sort_index_left_first<f64>(target, candidate, false) then
                return mid
            end if
            low = mid + 1
        else
            high = mid - 1
        end if
    end while
    return -1
end fn

pub fn is_sorted_f64(arr: array<f64>) -> bool
    let n = arr.length()
    if n <= 1 then
        return true
    end if
    let mut i = 1
    while i < n do
        if not sort_index_left_first<f64>(arr[i - 1], arr[i], false) then
            return false
        end if
        i = i + 1
    end while
    return true
end fn

# Extrema use the ascending NaN-last ordering. Minimum chooses a numeric
# value when one exists; maximum returns NaN when any input is NaN. Both keep
# the first value on ties, including signed zeros and all-NaN input.
pub fn min_f64(arr: array<f64>) -> Option<f64>
    let n = arr.length()
    if n == 0 then
        return Option.None
    end if
    let mut m = arr[0]
    let mut i = 1
    while i < n do
        if not sort_index_left_first<f64>(m, arr[i], false) then
            m = arr[i]
        end if
        i = i + 1
    end while
    return Option.Some(m)
end fn

pub fn max_f64(arr: array<f64>) -> Option<f64>
    let n = arr.length()
    if n == 0 then
        return Option.None
    end if
    let mut m = arr[0]
    let mut i = 1
    while i < n do
        if not sort_index_left_first<f64>(arr[i], m, false) then
            m = arr[i]
        end if
        i = i + 1
    end while
    return Option.Some(m)
end fn

pub fn sum_f64(arr: array<f64>) -> f64
    let mut total: f64 = 0.0
    let mut i = 0
    while i < arr.length() do
        total = total + arr[i]
        i = i + 1
    end while
    return total
end fn

# Recover the addition roundoff by subtracting the larger operand first.
# Callers keep the operands and their sum finite; reassociation is unsafe.
fn average_f64_roundoff(left: f64, right: f64, total: f64) -> f64
    let mut left_size = left
    let mut right_size = right
    if left_size < 0.0 then left_size = -left_size end if
    if right_size < 0.0 then right_size = -right_size end if
    if left_size >= right_size then
        return (left - total) + right
    end if
    return (right - total) + left
end fn

# Merge the two subtotals and their corrections without allocating an array.
fn average_f64_merge(a: f64, b: f64, c: f64, d: f64) -> f64
    let first = a + b
    let mut error = average_f64_roundoff(a, b, first)
    let second = first + c
    error = error + average_f64_roundoff(first, c, second)
    let third = second + d
    error = error + average_f64_roundoff(second, d, third)
    return third + error
end fn

# A finite mean must not overflow merely because its unscaled sum does.
# Compensate two sums: values of magnitude <= 1 stay unscaled, preserving
# subnormals, while larger values use an exact power-of-two divisor > count.
# This is an approximation, not an exact or correctly rounded reduction.
pub fn average_f64(arr: array<f64>) -> Option<f64>
    let n = arr.length()
    if n == 0 then
        return Option.None
    end if
    let count = n as f64
    let mut scale: f64 = 1.0
    while scale < count do
        scale = scale * 2.0
    end while
    scale = scale * 2.0

    let mut large: f64 = 0.0
    let mut large_error: f64 = 0.0
    let mut small: f64 = 0.0
    let mut small_error: f64 = 0.0
    let mut lower = arr[0]
    let mut upper = arr[0]
    let mut i = 0
    while i < n do
        let item = arr[i]
        if item != item or (item - item) != 0.0 then
            # Retain the original ordered IEEE arithmetic for nonfinite input.
            return Option.Some(sum_f64(arr) / count)
        end if
        if item < lower then lower = item end if
        if item > upper then upper = item end if
        if item >= -1.0 and item <= 1.0 then
            let next = small + item
            small_error = small_error + average_f64_roundoff(small, item, next)
            small = next
        else
            let scaled = item / scale
            let next = large + scaled
            large_error = large_error + average_f64_roundoff(large, scaled, next)
            large = next
        end if
        i = i + 1
    end while

    let large_total = large + large_error
    let large_tail = average_f64_roundoff(large, large_error, large_total)
    let mut mean: f64 = 0.0
    if large_total >= -1.0 and large_total <= 1.0 then
        # Cancellation made the large subtotal safe to restore. Merge before
        # dividing so tiny residuals and subnormal ties survive the reduction.
        let restored = large_total * scale
        let restored_tail = large_tail * scale
        mean = average_f64_merge(restored, small, restored_tail, small_error) / count
    else
        # The large subtotal dominates the small group, whose magnitude is at
        # most count / scale <= 1/2. Keep this merge scaled to avoid overflow.
        let combined = average_f64_merge(large_total, small / scale, large_tail, small_error / scale)
        mean = combined / (count / scale)
    end if
    # Rounding at the largest finite value must not escape the input range.
    if mean < lower then mean = lower end if
    if mean > upper then mean = upper end if
    return Option.Some(mean)
end fn

# =============================================================================
# Merge Sort (stable, for when stability is needed)
# =============================================================================

pub fn merge_sort_i32(arr: array<i32>) -> array<i32>
    let n = arr.length()
    if n <= 1 then
        return arr
    end if
    let mid = n / 2
    let mut left: array<i32> = []
    let mut right: array<i32> = []
    let mut i = 0
    while i < mid do
        left.push(arr[i])
        i = i + 1
    end while
    while i < n do
        right.push(arr[i])
        i = i + 1
    end while
    left = merge_sort_i32(left)
    right = merge_sort_i32(right)
    return merge_i32(left, right)
end fn

fn merge_i32(left: array<i32>, right: array<i32>) -> array<i32>
    let mut result: array<i32> = []
    let mut li = 0
    let mut ri = 0
    while li < left.length() and ri < right.length() do
        if left[li] <= right[ri] then
            result.push(left[li])
            li = li + 1
        else
            result.push(right[ri])
            ri = ri + 1
        end if
    end while
    while li < left.length() do
        result.push(left[li])
        li = li + 1
    end while
    while ri < right.length() do
        result.push(right[ri])
        ri = ri + 1
    end while
    return result
end fn

# =============================================================================
# Shuffle
# =============================================================================

# Fisher-Yates shuffle using a bounded deterministic generator. Keeping the
# generator state in i64 avoids the signed-overflow behavior of the former
# i32 LCG, including when the seed is the minimum i32 value.
pub fn shuffle_i32(mut arr: array<i32>, seed: i32) -> array<i32>
    let n = arr.length()
    if n <= 1 then
        return arr
    end if
    let mut rng: i64 = seed as i64
    if rng < 0 then
        rng = 0 - rng
    end if
    rng = rng mod 2147483647
    if rng == 0 then
        rng = 1
    end if
    let mut i = n - 1
    while i > 0 do
        rng = (rng * 48271) mod 2147483647
        let j = (rng mod ((i + 1) as i64)) as i32
        swap_i32(arr, i, j)
        i = i - 1
    end while
    return arr
end fn
