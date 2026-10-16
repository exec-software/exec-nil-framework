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

pub fn average_f64(arr: array<f64>) -> Option<f64>
    let n = arr.length()
    if n == 0 then
        return Option.None
    end if
    return Option.Some(sum_f64(arr) / (n as f64))
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
