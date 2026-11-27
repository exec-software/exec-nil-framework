import nil.std.collections

struct NestedArrayCounter
    value: i32
end struct

enum NestedArrayChoice
    Idle
    Value(i32)
end enum

# Empty nested arrays must materialize their leaves without a constructor.
struct EmptyNestedRecord<T>
    value: T
end struct
enum EmptyNestedEnum<T>
    Idle
    Value(T)
end enum

fn require_nested_array(condition: bool) -> void
    if not condition then panic("nested array contract failed") end if
end fn

fn nested_array_identity<T>(items: array<array<T>>) -> array<array<T>>
    return items
end fn

fn nested_array_argument(items: array<array<i32>>) -> i32
    return items[0].len() + items[1][0]
end fn

fn make_nested_arrays() -> array<array<text>>
    let rows: array<array<text>> = []
    let mut index: i32 = 0
    while index < 257
        rows.push([$"row-{index}", $"tail-{index}"])
        index = index + 1
    end while
    return rows
end fn

fn main() -> i32
    let no_records: array<array<EmptyNestedRecord<i32>>> = []
    let no_enums: array<array<EmptyNestedEnum<i32>>> = []
    let no_tuples: array<array<(i64, text)>> = []
    no_records.reserve(3)
    no_enums.reserve(5)
    no_tuples.reserve(7)
    require_nested_array(no_records.len() == 0 and no_records.capacity() >= 3)
    require_nested_array(no_enums.len() == 0 and no_enums.capacity() >= 5)
    require_nested_array(no_tuples.len() == 0 and no_tuples.capacity() >= 7)
    let inner = [1, 2]
    let rows: array<array<i32>> = [inner, inner, []]
    let alias = nested_array_identity<i32>(rows)
    let retained = rows[0]
    rows[0][1] = 7
    require_nested_array(inner[1] == 7 and rows[1][1] == 7)
    rows[0] = [11, 13, 17]
    require_nested_array(alias[0][2] == 17 and retained[1] == 7)
    retained.push(19)
    require_nested_array(rows[1].len() == 3 and rows[1][2] == 19)
    rows[2].push(23)
    require_nested_array(rows[2].len() == 1 and rows[2][0] == 23)
    alias.reserve(64)
    rows.push(retained)
    require_nested_array(alias.len() == 4 and alias.capacity() >= 64)
    rows[-1] = [91]
    rows[4] = [93]
    require_nested_array(rows.len() == 4 and rows[0][0] == 11)
    let cube: array<array<array<i32>>> = [rows, [[29], []]]
    cube[0][1][0] = 31
    require_nested_array(inner[0] == 31 and cube[0][3][0] == 31)
    cube[1][1].push(37)
    require_nested_array(cube[1][0][0] == 29 and cube[1][1][0] == 37)
    println("nested-array-jagged-alias-replacement-growth-ok")

    let filled = new_with_value<array<i32>>(3, inner)
    filled[0][0] = 41
    require_nested_array(filled[2][0] == 41 and inner[0] == 41)
    array_set<array<i32>>(filled, 1, [43, 47])
    let selected = get<array<i32>>(filled, 1)
    selected.push(53)
    require_nested_array(filled[1][2] == 53 and length<array<i32>>(filled) == 3)
    require_nested_array(try_copy_range<array<i32>>(filled, 0, filled, 1, 2))
    filled[0][0] = 59
    require_nested_array(filled[1][0] == 59 and filled[2][0] == 43)
    require_nested_array(try_copy_range<array<i32>>(filled, 1, filled, 0, 2))
    require_nested_array(filled[0][0] == 59 and filled[1][0] == 43)
    array_set<array<i32>>(filled, -1, [97])
    array_set<array<i32>>(filled, 3, [99])
    require_nested_array(not try_copy_range<array<i32>>(filled, 0, filled, 2, 2))
    let fresh: array<array<i32>> = []
    fresh.push([])
    fresh[0].push(101)
    array_set<array<i32>>(fresh, 0, [])
    require_nested_array(fresh[0].len() == 0)
    fresh.set(0, [])
    fresh[0].push(103)
    require_nested_array(fresh[0][0] == 103)
    let call_rows: array<array<i32>> = [[], [107]]
    require_nested_array(nested_array_argument(call_rows) == 107)
    require_nested_array(nested_array_argument([[], [107]]) == 107)
    let empty_first: array<array<array<i32>>> = [[], [[], [109]]]
    require_nested_array(empty_first[0].len() == 0 and empty_first[1][0].len() == 0)
    require_nested_array(empty_first[1][1][0] == 109)
    println("nested-array-intrinsics-overlap-ok")

    let counter = NestedArrayCounter { value: 61 }
    let records: array<array<NestedArrayCounter>> = [[counter], []]
    records[1].push(counter)
    records[0][0].value = 67
    require_nested_array(counter.value == 67 and records[1][0].value == 67)
    let tuples: array<array<(i64, text)>> = [[(71 as i64, "tuple")], []]
    tuples[1].push(tuples[0][0])
    require_nested_array(tuples[1][0].0 == 71 and tuples[1][0].1 == "tuple")
    let state: NestedArrayChoice = NestedArrayChoice.Value(73)
    let enums: array<array<NestedArrayChoice>> = [[state], []]
    enums[1].push(NestedArrayChoice.Idle)
    match enums[0][0]
        NestedArrayChoice.Value(amount) -> require_nested_array(amount == 73)
        NestedArrayChoice.Idle -> panic("lost nested enum")
    end match
    match enums[1][0]
        NestedArrayChoice.Idle -> require_nested_array(true)
        NestedArrayChoice.Value(_) -> panic("changed nested enum")
    end match
    let dynamic: array<anything> = [79, "boxed"]
    let dynamic_rows: array<array<anything>> = [dynamic, []]
    dynamic_rows[1].push(83)
    require_nested_array((dynamic_rows[0][0] as i32) == 79)
    require_nested_array((dynamic_rows[0][1] as text) == "boxed")
    require_nested_array((dynamic_rows[1][0] as i32) == 83)
    println("nested-array-record-tuple-enum-leaves-ok")

    let returned = make_nested_arrays()
    let retained_row = returned[128]
    returned[128] = ["replacement"]
    require_nested_array(returned.len() == 257)
    require_nested_array(returned[0][0] == "row-0" and returned[256][1] == "tail-256")
    require_nested_array(retained_row[0] == "row-128" and retained_row[1] == "tail-128")
    retained_row.push("held")
    require_nested_array(retained_row.len() == 3 and returned[128].len() == 1)
    println("nested-array-owned-return-retained-child-ok")
    return 0
end fn
