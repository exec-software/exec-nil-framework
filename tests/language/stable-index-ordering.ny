# Public Matrix-authored APIs consumed from Ny. The oracle checks permutation,
# pairwise ordering, and original-index stability independently of merge sort.
import ny.lang.core
import ny.lang.collections
import ny.lang.math

fn require(ok: bool, label: text)
    if not ok then panic(label) end if
end fn

fn same<T>(actual: array<T>, expected: array<T>, label: text)
    require(actual.len() == expected.len(), label)
    for index in range(actual.len())
        require(actual[index] == expected[index], label)
    end for
end fn

fn permutation(order: array<i32>, count: i32)
    require(order.len() == count, "permutation length")
    let mut seen: array<bool> = []
    for index in range(count)
        seen.push(false)
    end for
    for position in range(count)
        let index = order[position]
        require(index >= 0 and index < count, "permutation range")
        require(not seen[index], "permutation uniqueness")
        seen[index] = true
    end for
end fn

fn check_i32(keys: array<i32>)
    let mut original: array<i32> = []
    for index in range(keys.len())
        original.push(keys[index])
    end for
    for direction in range(2)
        let descending = direction == 1
        let order = stable_sort_indices_i32(keys, descending)
        permutation(order, keys.len())
        for position in range(1, order.len())
            let left = order[position - 1]
            let right = order[position]
            if descending then
                require(keys[left] >= keys[right], "i32 descending")
            else
                require(keys[left] <= keys[right], "i32 ascending")
            end if
            if keys[left] == keys[right] then
                require(left < right, "i32 stable equal keys")
            end if
        end for
        same<i32>(keys, original, "i32 keys unchanged")
        # Even empty and singleton outputs must be independent from the keys.
        order.push(99)
        if order.len() > 1 then order[0] = 99 end if
        same<i32>(keys, original, "i32 result independent")
    end for
end fn

fn check_f64(keys: array<f64>)
    let mut original: array<f64> = []
    for index in range(keys.len())
        original.push(keys[index])
    end for
    for direction in range(2)
        let descending = direction == 1
        let order = stable_sort_indices_f64(keys, descending)
        permutation(order, keys.len())
        for position in range(1, order.len())
            let left = order[position - 1]
            let right = order[position]
            let a = keys[left]
            let b = keys[right]
            let a_nan = a != a
            let b_nan = b != b
            if a_nan then
                require(b_nan, "NaNs last in both directions")
                require(left < right, "stable NaNs")
            elseif not b_nan then
                if descending then
                    require(a >= b, "f64 descending")
                else
                    require(a <= b, "f64 ascending")
                end if
                if a == b then require(left < right, "f64 stable equal keys and zeros") end if
            end if
        end for
        order.push(99)
        require(keys.len() == original.len(), "f64 key length unchanged")
        for index in range(keys.len())
            let actual = keys[index]
            let expected = original[index]
            if expected != expected then
                require(actual != actual, "NaN key unchanged")
            else
                require(actual == expected, "f64 key unchanged")
                if expected == 0.0 then
                    require((atan2(actual, -1.0) < 0.0) == (atan2(expected, -1.0) < 0.0), "zero sign unchanged")
                end if
            end if
        end for
    end for
end fn

fn check_gather<T>(first: T, second: T, third: T, label: text)
    let items: array<T> = [first, second, third, first]
    let original: array<T> = [first, second, third, first]
    let indices: array<i32> = [3, 1, 1, 0]
    let gathered: array<T> = try_gather<T>(items, indices).unwrap()
    same<T>(gathered, [first, second, second, first], "duplicates and reordered gather")
    same<T>(items, original, "gather leaves input")
    same<i32>(indices, [3, 1, 1, 0], "gather leaves indices")
    let subset: array<T> = try_gather<T>(items, [2, 0]).unwrap()
    same<T>(subset, [third, first], "subset gather")
    let identity: array<T> = try_gather<T>(items, [0, 1, 2, 3]).unwrap()
    identity[0] = third
    same<T>(items, original, "identity gather is fresh")
    gathered[0] = third
    same<T>(items, original, "gathered writes do not reach input")
    items[1] = first
    require(gathered[1] == second, "input writes do not reach result")
    items[1] = second
    let minimum: i32 = -2147483647 - 1
    require(try_gather<T>(items, [0, -1]).is_none(), "gather negative index")
    require(try_gather<T>(items, [0, 4]).is_none(), "gather endpoint index")
    require(try_gather<T>(items, [0, 2147483647]).is_none(), "gather maximum index")
    require(try_gather<T>(items, [0, minimum]).is_none(), "gather minimum index")
    same<T>(items, original, "rejected gather leaves input")
    let empty: array<T> = []
    let empty_result: array<T> = try_gather<T>(empty, []).unwrap()
    require(empty_result.len() == 0, "empty gather is Some empty")
    empty_result.push(first)
    require(empty.len() == 0, "empty gather is fresh")
    let no_selection: array<T> = try_gather<T>(items, []).unwrap()
    require(no_selection.len() == 0, "empty subset")
    no_selection.push(third)
    same<T>(items, original, "empty subset is independent")
    require(try_gather<T>(empty, [0]).is_none(), "nonempty selection from empty input")
    println(label)
end fn

fn main() -> i32
    let sizes: array<i32> = [0, 1, 2, 3, 5, 16, 17, 31, 32, 33, 257]
    for size_index in range(sizes.len())
        let n = sizes[size_index]
        let mut integers: array<i32> = []
        let mut floating: array<f64> = []
        for index in range(n)
            let key = (index * 37 + 11) % 19 - 9
            integers.push(key)
            floating.push((key as f64) / 4.0)
        end for
        check_i32(integers)
        check_f64(floating)
    end for
    let minimum: i32 = -2147483647 - 1
    check_i32([2147483647, minimum, 0, minimum, 2147483647, -1, 0])
    check_i32([0])
    check_i32([-3, -2, -1, 0, 1, 2, 3])
    check_i32([3, 2, 1, 0, -1, -2, -3])
    let mut equal_keys: array<i32> = []
    for index in range(257)
        equal_keys.push(7)
    end for
    check_i32(equal_keys)
    same<i32>(stable_sort_indices_i32([3, 1, 3, 1, 2], false), [1, 3, 4, 0, 2], "literal ascending permutation")
    same<i32>(stable_sort_indices_i32([3, 1, 3, 1, 2], true), [0, 2, 4, 1, 3], "literal descending permutation")
    println("stable i32 permutations")

    let nan = sqrt(-1.0)
    let infinity = exp(1000.0)
    check_f64([nan])
    check_f64([nan, nan, nan])
    check_f64([nan, 2.0, -0.0, 0.0, -infinity, nan, infinity, 2.0, -1.0])
    same<i32>(stable_sort_indices_f64([nan, 2.0, -0.0, 0.0, -infinity, nan, infinity, 2.0, -1.0], false), [4, 8, 2, 3, 1, 7, 6, 0, 5], "literal floating ascending")
    same<i32>(stable_sort_indices_f64([nan, 2.0, -0.0, 0.0, -infinity, nan, infinity, 2.0, -1.0], true), [6, 1, 7, 2, 3, 8, 4, 0, 5], "literal floating descending")
    println("stable f64 NaN infinity zero permutations")

    check_gather<i32>(-7, 0, 2147483647, "gather i32")
    check_gather<i64>(-9000000000 as i64, 0 as i64, 9000000000 as i64, "gather i64")
    check_gather<u8>(0 as u8, 128 as u8, 255 as u8, "gather u8")
    check_gather<bool>(false, true, true, "gather bool")
    check_gather<f64>(-0.5, 0.25, 1.5, "gather f64")
    check_gather<text>("", "material" + "-name", "last", "gather text")
    let self_indices: array<i32> = [2, 0, 1]
    let self_result = try_gather<i32>(self_indices, self_indices).unwrap()
    same<i32>(self_result, [1, 2, 0], "items and indices may alias")
    self_result[0] = 99
    same<i32>(self_indices, [2, 0, 1], "aliased gather result is fresh")
    let special = try_gather<f64>([nan, -0.0, infinity], [1, 0, 2, 0]).unwrap()
    require(atan2(special[0], -1.0) < 0.0, "gather preserves negative zero")
    require(special[1] != special[1] and special[3] != special[3], "gather preserves repeated NaNs")
    require(special[2] == infinity, "gather preserves infinity")
    println("gather aliases and IEEE values")

    # A single stable depth permutation keeps parallel draw metadata aligned.
    let depths: array<f64> = [4.5, 1.0, 4.5, nan, 2.0, 4.5]
    let draw_ids: array<i32> = [101, 102, 103, 104, 105, 106]
    let materials: array<i32> = [7, 2, 8, 9, 3, 7]
    let order = stable_sort_indices_f64(depths, true)
    same<i32>(order, [0, 2, 5, 4, 1, 3], "stable scene order")
    same<i32>(try_gather<i32>(draw_ids, order).unwrap(), [101, 103, 106, 105, 102, 104], "scene IDs")
    same<i32>(try_gather<i32>(materials, order).unwrap(), [7, 8, 7, 3, 2, 9], "scene materials")
    same<i32>(draw_ids, [101, 102, 103, 104, 105, 106], "scene IDs unchanged")
    same<i32>(materials, [7, 2, 8, 9, 3, 7], "scene materials unchanged")
    println("parallel scene arrays stay aligned")
    return 0
end fn
