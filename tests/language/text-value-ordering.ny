# Literal lexical snapshots plus independent multiset/permutation checks.
# Strings deliberately exclude embedded NUL, whose cross-route contract is open.
import ny.lang.core
import ny.lang.collections
import ny.lang.text.string_utils

fn require(ok: bool, label: text)
    if not ok then panic(label) end if
end fn

fn copy<T>(items: array<T>) -> array<T>
    let mut result: array<T> = []
    for index in range(items.len())
        result.push(items[index])
    end for
    return result
end fn

fn same<T>(actual: array<T>, expected: array<T>)
    require(actual.len() == expected.len(), "snapshot length")
    for index in range(actual.len())
        require(actual[index] == expected[index], "snapshot values")
    end for
end fn

fn occurrences(items: array<text>, value: text) -> i32
    let mut count = 0
    for index in range(items.len())
        if items[index] == value then count = count + 1 end if
    end for
    return count
end fn

fn same_bag(actual: array<text>, original: array<text>)
    require(actual.len() == original.len(), "sort length")
    let mut checked: array<text> = []
    for index in range(original.len())
        let value = original[index]
        if occurrences(checked, value) == 0 then
            require(occurrences(actual, value) == occurrences(original, value), "sort preserves multiset")
            checked.push(value)
        end if
    end for
end fn

fn check_text(keys: array<text>)
    let original = copy<text>(keys)
    for direction in range(2)
        let items = copy<text>(keys)
        let alias = items
        let ordered = if direction == 0 then sort_text(items) else sort_text_desc(items) end if
        same_bag(ordered, keys)
        same<text>(alias, ordered)
        for index in range(1, ordered.len())
            if direction == 0 then
                require(ordered[index - 1] <= ordered[index], "text ascending")
            else
                require(ordered[index - 1] >= ordered[index], "text descending")
            end if
        end for
        ordered.push("return growth")
        require(alias.len() == keys.len() + 1, "sort returned handle aliases input")
        alias[keys.len()] = "alias write"
        require(ordered[keys.len()] == "alias write", "input writes reach return")
        alias.push("input growth")
        require(ordered.len() == keys.len() + 2, "input growth reaches return")

        let order = stable_sort_indices_text(keys, direction == 1)
        require(order.len() == keys.len(), "permutation length")
        let seen = new_with_value<bool>(keys.len(), false)
        for position in range(order.len())
            let index = order[position]
            require(index >= 0 and index < keys.len(), "permutation range")
            require(not seen[index], "permutation uniqueness")
            seen[index] = true
        end for
        for position in range(1, order.len())
            let left = order[position - 1]
            let right = order[position]
            if direction == 0 then
                require(keys[left] <= keys[right], "index ascending")
            else
                require(keys[left] >= keys[right], "index descending")
            end if
            if keys[left] == keys[right] then
                require(left < right, "equal text keys keep original indices")
            end if
        end for
        order.push(99)
        order[0] = 99
        same<text>(keys, original)
    end for
end fn

fn make_owned() -> array<text>
    let prefix = ny::lang::text::repeat("common-", 64)
    return [prefix + "b", prefix + "a", prefix,
        ny::lang::text::substring("~alpha!", 1, 5),
        ny::lang::text::trim("  alpha  "),
        ny::lang::text::repeat("alpha", 1), prefix + "a"]
end fn

fn make_sorted_owned(descending: bool) -> array<text>
    let items = make_owned()
    return if descending then sort_text_desc(items) else sort_text(items) end if
end fn

fn churn() -> i32
    let mut total = 0
    # Cross the native managed heap's initial collection threshold, as in the
    # existing native-managed-lifetime fixture, after the helper frames return.
    for index in range(1600)
        let values = new_with_value<i32>(1024, index)
        total = total + values[1]
    end for
    return total
end fn

fn main() -> i32
    let keys: array<text> = ["é", "a", "", "😀", "A", "a", "aa", "\n", "Ω", "\t", "中", " ", "Å", "a\n", "a\t", "ab", "b", "z", "\"", "AA", "Aa", "B"]
    # Fixed expected byte-lexical order; this oracle does not invoke a sorter.
    let ascending: array<text> = ["", "\t", "\n", " ", "\"", "A", "AA", "Aa", "B", "a", "a", "a\t", "a\n", "aa", "ab", "b", "z", "Å", "é", "Ω", "中", "😀"]
    let descending: array<text> = ["😀", "中", "Ω", "é", "Å", "z", "b", "ab", "aa", "a\n", "a\t", "a", "a", "B", "Aa", "AA", "A", "\"", " ", "\n", "\t", ""]
    same<text>(sort_text(copy<text>(keys)), ascending)
    same<text>(sort_text_desc(copy<text>(keys)), descending)
    same<i32>(stable_sort_indices_text(keys, false), [2, 9, 7, 11, 18, 4, 19, 20, 21, 1, 5, 14, 13, 6, 15, 16, 17, 12, 0, 8, 10, 3])
    same<i32>(stable_sort_indices_text(keys, true), [3, 10, 8, 0, 12, 17, 16, 15, 6, 13, 14, 1, 5, 21, 20, 19, 4, 18, 11, 7, 9, 2])
    for left in range(ascending.len())
        for right in range(ascending.len())
            let a = ascending[left]
            let b = ascending[right]
            let equal = a == b
            require((a < b) == (left < right and not equal), "raw text less")
            require((a > b) == (left > right and not equal), "raw text greater")
            require((a <= b) == (left <= right or equal), "raw text at most")
            require((a >= b) == (left >= right or equal), "raw text at least")
            let relation = compare_text(a, b)
            require(relation.is_less() == (left < right and not equal), "text comparator less")
            require(relation.is_eq() == equal, "text comparator equal")
            require(relation.is_gt() == (left > right and not equal), "text comparator greater")
        end for
    end for
    println("text lexical snapshots and comparisons")

    let sizes: array<i32> = [0, 1, 2, 15, 16, 17, 31, 32, 33, 257]
    for slot in range(sizes.len())
        let mut items: array<text> = []
        for index in range(sizes[slot])
            items.push(keys[(index * 37 + 11) % keys.len()])
        end for
        check_text(items)
    end for
    check_text(ascending)
    check_text(descending)
    let mut equal_keys: array<text> = []
    for index in range(2048)
        equal_keys.push(ny::lang::text::repeat("equal", 1))
    end for
    check_text(equal_keys)
    println("text sorts preserve multisets aliases and stable indices")

    let insertion_input = copy<text>(keys)
    let inserted = insertion_sort_text(insertion_input)
    same<text>(inserted, ascending)
    inserted.push("tail")
    require(insertion_input.len() == keys.len() + 1, "insertion keeps handle")
    let reverse_input = copy<text>(ascending)
    let reversed = reverse_text(reverse_input)
    same<text>(reversed, descending)
    reversed.push("tail")
    require(reverse_input.len() == ascending.len() + 1, "reverse keeps handle")
    same<text>(insertion_sort_text([]), [])
    same<text>(insertion_sort_text(["only"]), ["only"])
    same<text>(reverse_text([]), [])
    same<text>(reverse_text(["only"]), ["only"])
    same<text>(unique_text([]), [])
    same<text>(unique_text(["only"]), ["only"])
    let runs: array<text> = ["b", "b", "a", "a", "b", "", "", "é", "é"]
    same<text>(unique_text(runs), ["b", "a", "b", "", "é"])
    same<text>(runs, ["b", "b", "a", "a", "b", "", "", "é", "é"])
    same<text>(unique_text(ascending), ["", "\t", "\n", " ", "\"", "A", "AA", "Aa", "B", "a", "a\t", "a\n", "aa", "ab", "b", "z", "Å", "é", "Ω", "中", "😀"])
    println("text insertion reverse and consecutive uniqueness")

    let snapshot = copy<text>(ascending)
    require(is_sorted_text(ascending), "ascending text sorted")
    require(is_sorted_text([]) and is_sorted_text(["only"]), "empty and singleton sorted")
    require(is_sorted_text(["same", "same"]), "equal text sorted")
    require(not is_sorted_text(["a", "A"]), "case inversion")
    require(not is_sorted_text(["aa", "a"]), "prefix inversion")
    require(not is_sorted_text(["😀", "é"]), "UTF8 inversion")
    for index in range(ascending.len())
        let found = binary_search_text(ascending, ascending[index])
        require(found >= 0 and found < ascending.len(), "search finds text class")
        require(ascending[found] == ascending[index], "search returns equal text")
    end for
    require(binary_search_text(ascending, "missing") == -1, "absent text")
    require(binary_search_text(ascending, "a ") == -1, "absent prefix extension")
    require(binary_search_text([], "") == -1, "empty search")
    require(binary_search_text(["same", "same"], "same") >= 0, "duplicate search")
    same<text>(ascending, snapshot)
    println("text sorted checks and search leave input unchanged")

    let owned_items = make_owned()
    let owned_ascending = make_sorted_owned(false)
    let owned_descending = make_sorted_owned(true)
    require(churn() == 1279200, "allocation churn completed")
    let prefix = ny::lang::text::repeat("common-", 64)
    let expected: array<text> = ["alpha", "alpha", "alpha", prefix, prefix + "a", prefix + "a", prefix + "b"]
    same<text>(owned_ascending, expected)
    same<text>(owned_descending, [prefix + "b", prefix + "a", prefix + "a", prefix, "alpha", "alpha", "alpha"])
    same<i32>(stable_sort_indices_text(owned_items, false), [3, 4, 5, 2, 1, 6, 0])
    same<i32>(stable_sort_indices_text(owned_items, true), [0, 1, 6, 2, 3, 4, 5])
    check_text(owned_items)
    same<text>(unique_text(owned_ascending), ["alpha", prefix, prefix + "a", prefix + "b"])
    let owned_alias = owned_ascending
    for index in range(80)
        owned_ascending.push(ny::lang::text::repeat("grown", 2))
    end for
    require(churn() == 1279200, "second allocation churn completed")
    require(owned_alias.len() == 87, "grown owned return keeps alias")
    for index in range(expected.len())
        require(owned_alias[index] == expected[index], "owned text retained through growth and collection")
    end for
    require(owned_alias[86] == "growngrown", "grown text retained")
    println("dynamic equal strings and sorted returns survive collection")
    return 0
end fn
