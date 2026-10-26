module tuple_arrays
import nil.std.collections
import nil.std.core
import nil.std.core

struct Counter
    value: i64
end struct

enum ReadingState
    Ready(i32)
    Idle
end enum

fn repeat_pair<T>(item: T) -> array<T>
    let values: array<T> = new_with_value<T>(2, item)
    values.push(item)
    return values
end fn

fn make_owned() -> array<(i64, text)>
    let values: array<(i64, text)> = []
    let mut index: i32 = 0
    while index < 257
        let entry: (i64, text) = (index, $"owned-{index}")
        values.push(entry)
        index = index + 1
    end while
    return values
end fn

fn fresh_pairs() -> array<(i64, text)>
    return [(1, "returned"), (2 as i64, "wide")]
end fn

fn main() -> i32
    let first: (i64, text, f64, bool) = (41, "first", -1.5, true)
    let second: (i64, text, f64, bool) = (42, "second", 2.25, false)
    let items = [first, second]
    let alias = items
    let retained = items[0]
    println(items.len())
    println(items[0].0)
    println(items[1].1)
    println(items[0].2)
    println(items[1].3)
    items[0] = second
    println(alias[0].0)
    println(retained.1)
    alias.push(first)
    println(items.len())
    println(items[2].1)
    items[-1] = first
    items[3] = second
    println(items.len())
    println(items[0].0)

    let nested = [((1, "nested"), (false, -0.5)), ((2, "tail"), (true, 0.25))]
    println(nested[0].0.1)
    println(nested[1].1.0)
    println(nested[0].1.1)
    let singles: array<(i64,)> = [(9 as i64,), (10 as i64,)]
    println(singles[1].0)

    let object = Counter { value: 5 }
    let references = [(object, "shared")]
    let saved = references[0]
    saved.0.value = 17
    println(references[0].0.value)
    references[0] = (Counter { value: 23 }, "replacement")
    println(saved.0.value)
    println(references[0].0.value)

    let repeated = repeat_pair<(i64, text, f64, bool)>(first)
    println(length<(i64, text, f64, bool)>(repeated))
    array_set<(i64, text, f64, bool)>(repeated, 1, second)
    let selected = get<(i64, text, f64, bool)>(repeated, 1)
    println(selected.1)
    array_set<(i64, text, f64, bool)>(repeated, -1, second)
    array_set<(i64, text, f64, bool)>(repeated, 3, second)
    println(repeated[0].1)
    println(try_copy_range<(i64, text, f64, bool)>(repeated, 0, repeated, 1, 2))
    println(repeated[1].1)
    println(repeated[2].1)
    println(try_copy_range<(i64, text, f64, bool)>(repeated, 1, repeated, 0, 2))
    println(repeated[0].1)
    println(repeated[1].1)
    println(try_copy_range<(i64, text, f64, bool)>(repeated, 0, repeated, 2, 2))
    println(repeated[2].1)

    let empty: array<(i64, text, f64, bool)> = []
    println(empty.len())
    empty.push(second)
    println(empty[0].0)
    let filled = new_with_value<(Counter, text)>(2, (object, "filled"))
    filled[0].0.value = 31
    println(filled[1].0.value)
    println(object.value)
    let returned_values = make_owned()
    let owned_alias = returned_values
    let first_owned = returned_values[0]
    owned_alias.push((257 as i64, "added"))
    println(returned_values.len())
    println(first_owned.1)
    println(returned_values[128].1)
    println(returned_values[256].0)
    println(returned_values[257].1)
    let converted: array<(i64, text)> = [(1, "declared"), (2 as i64, "wide")]
    converted[0] = (3, "replaced")
    converted.push((4, "pushed"))
    println(converted[0].0)
    println(converted[2].0)
    let returned_pairs = fresh_pairs()
    println(returned_pairs[0].1)
    println(returned_pairs[1].0)

    let active: ReadingState = ReadingState.Ready(73)
    let inactive: ReadingState = ReadingState.Idle
    let states = [(active, "active"), (inactive, "idle")]
    match states[0].0
        ReadingState.Ready(amount) -> println(amount)
        ReadingState.Idle -> println(0)
    end match
    states[0] = (inactive, "replaced")
    match states[0].0
        ReadingState.Ready(amount) -> println(amount)
        ReadingState.Idle -> println(states[0].1)
    end match
    let present: Option<i64> = Option.Some(91 as i64)
    let absent: Option<i64> = Option.None
    let options = [(present,), (absent,)]
    match options[0].0
        Option.Some(amount) -> println(amount)
        Option.None -> println(0)
    end match
    match options[1].0
        Option.Some(amount) -> println(amount)
        Option.None -> println("empty")
    end match

    let parsed = [(Convert.try_to_i32("94"),), (Convert.try_to_i32("invalid"),)]
    match parsed[0].0
        Result.Ok(amount) -> println(amount)
        Result.Error(_) -> println("unexpected")
    end match
    match parsed[1].0
        Result.Ok(_) -> println("unexpected")
        Result.Error(_) -> println("parse rejected")
    end match

    let raw = allocate<i32>(3)
    raw[0] = 81
    raw[1] = 82
    raw[2] = 83
    let pointers = [(raw, "base"), (raw + 1, "interior"), (raw + 3, "one-past")]
    let base_pointer = pointers[0].0
    let interior_pointer = pointers[1].0
    let end_pointer = pointers[2].0
    println(base_pointer[0])
    println(interior_pointer[0])
    let last_pointer = end_pointer - 1
    println(last_pointer[0])
    println(end_pointer == raw + 3)
    deallocate(raw)
    return 0
end fn
