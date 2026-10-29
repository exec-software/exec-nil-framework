struct Box<T>
    value: T
end struct

struct Record
    value: i64
    values: array<i32>
end struct

struct Box_i64_
    value: text
end struct

struct Box_array_i32__
    value: text
end struct

fn echo_box(value: Box<i64>) -> Box<i64>
    return value
end fn

fn make_box<T>(value: T) -> Box<T>
    return Box<T> { value: value }
end fn

fn main() -> i32
    let record = Record { value: 7, values: [] }
    record.value = 19
    println(record.value)
    record.values = [3, 4]
    println(record.values[1])
    record.values = []
    println(record.values.len())

    let box: Box<i64> = Box<i64> { value: 10 }
    box.value = 23
    println(box.value)
    println(echo_box(box).value)
    println(make_box(29 as i64).value)
    let nominal: Box_i64_ = Box_i64_ { value: "nominal" }
    println(nominal.value)
    let arrays: Box<array<i32>> = Box<array<i32>> { value: [] }
    arrays.value = [5, 6]
    println(arrays.value[0])
    arrays.value = []
    println(arrays.value.len())
    let nominal_array: Box_array_i32__ = Box_array_i32__ { value: "nominal-array" }
    println(nominal_array.value)

    let mut values: array<i32> = [11]
    values = []
    println(values.len())
    values = [12]
    values[0] = 13
    println(values[0])

    let bytes: ptr<u8> = allocate<u8>(2)
    bytes[0] = 257
    *bytes = 258
    println(bytes[0])
    bytes[1] = -1
    println(bytes[1])
    deallocate(bytes)

    let flags: ptr<bool> = allocate<bool>(1)
    *flags = 1
    println(*flags)
    flags[0] = 0
    println(flags[0])
    deallocate(flags)
    return 0
end fn
