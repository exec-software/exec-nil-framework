@define ENABLED yes
@if ENABLED == yes
@tag(enabled=yes)
fn enabled() -> boolean
    return yes
end fn
@else
@error Boolean alias preprocessor condition failed
@endif

type Switch = flag
let global_value: anything = 7

struct Parcel
    item: anything
end struct

enum Choice
    Value(anything)
    Empty
end enum

fn echo(value: anything) -> anything
    return value
end fn

fn make() -> anything
    return "returned"
end fn

fn main() -> i32
    let flag: Switch = no
    let inferred = yes
    if not (enabled() and inferred and not flag) then
        return 9
    end if
    match yes
        yes -> println("yes")
        no -> return 1
    end match
    println((1 as boolean) and not (0 as flag))
    let mut value: anything = 42
    let original = value
    value = "ready"
    if not (anything_is<text>(value)) then
        return 9
    end if
    if not (not anything_is<i32>(value)) then
        return 9
    end if
    println(value as text)
    println(original as i32)
    println(global_value as i32)
    println(echo(yes) as boolean)
    println(make() as text)
    let parcel = Parcel { item: 2.5 }
    println(parcel.item as f64)
    parcel.item = "field"
    println(parcel.item as text)
    let variant: Choice = Choice.Value(9)
    match variant
        Choice.Value(item) -> println(item as i32)
        Choice.Empty -> return 2
    end match
    let mut values: array<anything> = [1, "two", no]
    println(values[0] as i32)
    println(values[1] as text)
    println(values[2] as flag)
    values[0] = "changed"
    values.push(3.5)
    println(values[0] as text)
    println(values[3] as f64)
    let boxed_array: anything = values
    let recovered = boxed_array as array<anything>
    println(recovered[1] as text)
    return 0
end fn
