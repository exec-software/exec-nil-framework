import nil.std.collections
import nil.std.core.*

struct ArrayEnumCounter
    value: i32
end struct

enum ArraySignal<T>
    Idle
    Value(T)
    Named(text)
end enum

# This type has no constructor, parameter or return use elsewhere.
enum EmptyArrayEnum<T>
    Empty
    Present(T)
end enum

struct EmptyPayloadRecord<T>
    value: T
end struct
enum EmptyPayloadEnum<T>
    Value(T)
end enum
enum EmptyPayloadEnvelope
    Record(EmptyPayloadRecord<i32>)
    Variant(EmptyPayloadEnum<i32>)
    Tuple((i64, text))
end enum

enum ArrayEnvelope
    Text(text)
    Counter(ArrayEnumCounter)
    Numbers(array<i32>)
    Signal(ArraySignal<i64>)
end enum

enum ArrayTupleEnvelope
    Empty
    Pair((i64, text))
end enum

enum ArrayAggregateEnvelope
    Outcome(Result<i64, text>)
    Tagged((i64, text), i32)
end enum

class ArrayEnumBase
    value: i32
    pub virtual fn read(self) -> i32
        return self.value
    end fn
end class

class ArrayEnumDerived inherits ArrayEnumBase
    extra: i32
    pub override fn read(self) -> i32
        return self.value + self.extra
    end fn
end class

enum ArrayClassEnvelope
    Empty
    Object(ArrayEnumBase)
end enum

fn require_enum_array(condition: bool) -> void
    if not condition then panic("enum array contract failed") end if
end fn

fn signal_value(item: ArraySignal<i64>) -> i64
    match item
        ArraySignal.Idle -> return -1
        ArraySignal.Value(amount) -> return amount
        ArraySignal.Named(_) -> return -2
    end match
end fn

fn make_array_signals() -> array<ArraySignal<text>>
    let items: array<ArraySignal<text>> = []
    let mut index: i32 = 0
    while index < 257
        items.push(ArraySignal.Value($"enum-{index}"))
        index = index + 1
    end while
    return items
end fn

fn main() -> i32
    let empty: array<EmptyArrayEnum<i32>> = []
    empty.reserve(17)
    require_enum_array(empty.len() == 0 and empty.capacity() >= 17)
    let empty_payloads: array<EmptyPayloadEnvelope> = []
    let empty_payload_rows: array<array<EmptyPayloadEnvelope>> = []
    empty_payloads.reserve(3)
    empty_payload_rows.reserve(5)
    require_enum_array(empty_payloads.len() == 0 and empty_payloads.capacity() >= 3)
    require_enum_array(empty_payload_rows.len() == 0 and empty_payload_rows.capacity() >= 5)
    let first: ArraySignal<i64> = ArraySignal.Value(4294967296)
    let idle: ArraySignal<i64> = ArraySignal.Idle
    let named: ArraySignal<i64> = ArraySignal.Named("retained")
    let items: array<ArraySignal<i64>> = [first, idle, named]
    let alias = items
    let retained = items[0]
    items[0] = idle
    require_enum_array(signal_value(alias[0]) == -1)
    require_enum_array(signal_value(retained) == 4294967296)
    items.reserve(64)
    alias.push(retained)
    require_enum_array(items.len() == 4 and signal_value(items[3]) == 4294967296)
    items[-1] = first
    items[4] = first
    require_enum_array(items.len() == 4 and signal_value(items[0]) == -1)
    match items[2]
        ArraySignal.Named(word) -> require_enum_array(word == "retained")
        ArraySignal.Idle -> panic("lost named variant")
        ArraySignal.Value(_) -> panic("lost named variant")
    end match
    println("enum-array-variants-replacement-growth-ok")

    let filled = new_with_value<ArraySignal<i64>>(3, first)
    array_set<ArraySignal<i64>>(filled, 1, idle)
    require_enum_array(length<ArraySignal<i64>>(filled) == 3)
    require_enum_array(signal_value(get<ArraySignal<i64>>(filled, 1)) == -1)
    require_enum_array(try_copy_range<ArraySignal<i64>>(filled, 0, filled, 1, 2))
    require_enum_array(signal_value(filled[0]) == 4294967296)
    require_enum_array(signal_value(filled[1]) == 4294967296 and signal_value(filled[2]) == -1)
    require_enum_array(try_copy_range<ArraySignal<i64>>(filled, 1, filled, 0, 2))
    require_enum_array(signal_value(filled[0]) == 4294967296 and signal_value(filled[1]) == -1)
    array_set<ArraySignal<i64>>(filled, -1, named)
    array_set<ArraySignal<i64>>(filled, 3, named)
    require_enum_array(not try_copy_range<ArraySignal<i64>>(filled, 0, filled, 2, 2))
    let fresh_units = new_with_value<ArraySignal<i64>>(2, ArraySignal.Idle)
    let fresh_values = new_with_value<ArraySignal<i64>>(2, ArraySignal.Value(101))
    array_set<ArraySignal<i64>>(fresh_units, 1, ArraySignal.Value(103))
    require_enum_array(signal_value(fresh_units[0]) == -1 and signal_value(fresh_units[1]) == 103)
    require_enum_array(signal_value(fresh_values[1]) == 101)
    println("enum-array-intrinsics-overlap-ok")

    let object = ArrayEnumCounter { value: 7 }
    let numbers = [11, 13]
    let counter_case: ArrayEnvelope = ArrayEnvelope.Counter(object)
    let number_case: ArrayEnvelope = ArrayEnvelope.Numbers(numbers)
    let signal_case: ArrayEnvelope = ArrayEnvelope.Signal(first)
    let cases: array<ArrayEnvelope> = [counter_case, number_case, signal_case, ArrayEnvelope.Text("owned")]
    let saved_case = cases[0]
    cases[0] = ArrayEnvelope.Text("replacement")
    match saved_case
        ArrayEnvelope.Counter(saved) -> saved.value = 17
        ArrayEnvelope.Text(_) -> panic("changed retained variant")
        ArrayEnvelope.Numbers(_) -> panic("changed retained variant")
        ArrayEnvelope.Signal(_) -> panic("changed retained variant")
    end match
    require_enum_array(object.value == 17)
    match cases[1]
        ArrayEnvelope.Numbers(saved) -> saved[0] = 19
        ArrayEnvelope.Text(_) -> panic("changed array payload")
        ArrayEnvelope.Counter(_) -> panic("changed array payload")
        ArrayEnvelope.Signal(_) -> panic("changed array payload")
    end match
    require_enum_array(numbers[0] == 19)
    match cases[2]
        ArrayEnvelope.Signal(saved) -> require_enum_array(signal_value(saved) == 4294967296)
        ArrayEnvelope.Text(_) -> panic("changed enum payload")
        ArrayEnvelope.Counter(_) -> panic("changed enum payload")
        ArrayEnvelope.Numbers(_) -> panic("changed enum payload")
    end match
    let present: Option<i64> = Option.Some(4294967296)
    let absent: Option<i64> = Option.None
    let options = [present, absent]
    require_enum_array(options[0].unwrap() == 4294967296 and options[1].is_none())
    let ok: Result<f64, text> = Result.Ok(1.25)
    let error: Result<f64, text> = Result.Error("failure")
    let results = [ok, error]
    match results[0]
        Result.Ok(amount) -> require_enum_array(amount == 1.25)
        Result.Error(_) -> panic("changed result payload")
    end match
    match results[1]
        Result.Error(message) -> require_enum_array(message == "failure")
        Result.Ok(_) -> panic("changed result variant")
    end match
    let dynamic_number: ArraySignal<anything> = ArraySignal.Value(79)
    let dynamic_text: ArraySignal<anything> = ArraySignal.Value("boxed")
    let dynamic_items = [dynamic_number, dynamic_text]
    match dynamic_items[0]
        ArraySignal.Value(value) -> require_enum_array((value as i32) == 79)
        ArraySignal.Idle -> panic("lost boxed number")
        ArraySignal.Named(_) -> panic("lost boxed number")
    end match
    let dynamic_rows: array<array<ArraySignal<anything>>> = [dynamic_items]
    match dynamic_rows[0][1]
        ArraySignal.Value(value) -> require_enum_array((value as text) == "boxed")
        ArraySignal.Idle -> panic("lost boxed text")
        ArraySignal.Named(_) -> panic("lost boxed text")
    end match
    let concrete = ArrayEnumDerived { value: 83, extra: 6 }
    let class_case: ArrayClassEnvelope = ArrayClassEnvelope.Object(concrete)
    let class_items: array<ArrayClassEnvelope> = [class_case]
    let class_rows: array<array<ArrayClassEnvelope>> = [class_items]
    match class_rows[0][0]
        ArrayClassEnvelope.Object(value) -> require_enum_array(value.read() == 89)
        ArrayClassEnvelope.Empty -> panic("lost class payload")
    end match
    let pair: (i64, text) = (113, "tuple payload")
    let tuple_items: array<ArrayTupleEnvelope> = [ArrayTupleEnvelope.Pair(pair)]
    let tuple_rows: array<array<ArrayTupleEnvelope>> = [tuple_items]
    let saved_tuple = tuple_rows[0][0]
    tuple_items[0] = ArrayTupleEnvelope.Empty
    match saved_tuple
        ArrayTupleEnvelope.Pair(value) -> require_enum_array(value.0 == 113 and value.1 == "tuple payload")
        ArrayTupleEnvelope.Empty -> panic("lost tuple payload")
    end match
    match tuple_rows[0][0]
        ArrayTupleEnvelope.Empty -> require_enum_array(true)
        ArrayTupleEnvelope.Pair(_) -> panic("enum replacement changed retained tuple")
    end match
    let result_payload: Result<i64, text> = Result.Ok(127)
    let aggregate_items: array<ArrayAggregateEnvelope> = [
        ArrayAggregateEnvelope.Outcome(result_payload),
        ArrayAggregateEnvelope.Tagged(pair, 131)
    ]
    match aggregate_items[0]
        ArrayAggregateEnvelope.Outcome(result) -> require_enum_array(result.unwrap() == 127)
        ArrayAggregateEnvelope.Tagged(_, _) -> panic("lost generic enum payload")
    end match
    match aggregate_items[1]
        ArrayAggregateEnvelope.Tagged(value, tag) -> require_enum_array(value.0 == 113 and value.1 == "tuple payload" and tag == 131)
        ArrayAggregateEnvelope.Outcome(_) -> panic("lost multiple variant payloads")
    end match
    match aggregate_items[1]
        ArrayAggregateEnvelope.Tagged(_, tag) -> require_enum_array(tag == 131)
        ArrayAggregateEnvelope.Outcome(_) -> panic("lost trailing payload")
    end match
    match aggregate_items[1]
        ArrayAggregateEnvelope.Tagged(value, _) -> require_enum_array(value.0 == 113)
        ArrayAggregateEnvelope.Outcome(_) -> panic("lost leading payload")
    end match
    let selected_tag = match aggregate_items[1] {
        ArrayAggregateEnvelope.Tagged(_, tag) -> tag;
        _ -> 0;
    }
    let selected_value = match aggregate_items[1] {
        ArrayAggregateEnvelope.Tagged(value, _) -> value.0;
        _ -> (0 as i64);
    }
    require_enum_array(selected_tag == 131 and selected_value == 113)
    println("enum-array-payload-identity-option-result-ok")

    let returned = make_array_signals()
    let saved_text = returned[128]
    returned[128] = ArraySignal.Idle
    require_enum_array(returned.len() == 257)
    match saved_text
        ArraySignal.Value(word) -> require_enum_array(word == "enum-128")
        ArraySignal.Idle -> panic("lost owned text")
        ArraySignal.Named(_) -> panic("lost owned text")
    end match
    match returned[256]
        ArraySignal.Value(word) -> require_enum_array(word == "enum-256")
        ArraySignal.Idle -> panic("lost tail text")
        ArraySignal.Named(_) -> panic("lost tail text")
    end match
    let mut queue: queue<ArraySignal<i64>> = queue<ArraySignal<i64>>.with_capacity(2)
    queue.enqueue(first)
    queue.enqueue(idle)
    queue.enqueue(named)
    require_enum_array(signal_value(queue.dequeue().unwrap()) == 4294967296)
    require_enum_array(signal_value(queue.dequeue().unwrap()) == -1)
    require_enum_array(signal_value(queue.dequeue().unwrap()) == -2 and queue.is_empty())
    println("enum-array-owned-return-queue-ok")
    return 0
end fn
