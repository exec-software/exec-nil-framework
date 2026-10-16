struct CastRecord
    value: i32
end struct

type CastAlias = CastRecord

enum CastChoice<T>
    Some(T)
    None
end enum

fn same_value<T>(value: T) -> T
    return value as T
end fn

fn main() -> i32
    println("identity" as text)
    let record = CastRecord { value: 17 }
    let exact_record = record as CastAlias
    println(exact_record.value)
    println(same_value(record).value)

    let values: array<i32> = [23, 29]
    let exact_values = values as array<i32>
    println(exact_values[1])

    let pointer = allocate<i32>(1)
    pointer[0] = 31
    let exact_pointer = pointer as ptr<i32>
    println(exact_pointer[0])
    deallocate(exact_pointer)

    let choice: CastChoice<i32> = CastChoice.Some(37)
    let exact_choice = choice as CastChoice<i32>
    match exact_choice
        CastChoice.Some(value) -> println(value)
        CastChoice.None -> return 1
    end match
    println(same_value(41 as i64))
    println(not false)
    println(not true)
    println(not not true)
    let inverted: bool = not (0 as bool)
    println(inverted)
    return 0
end fn
