import nil.std.collections

struct F32ArrayRecord
    samples: array<f32>
end struct

enum F32ArraySignal
    Samples(array<f32>)
    Sample(f32)
    Idle
end enum

fn repeat_sample<T>(value: T) -> array<T>
    return new_with_value<T>(2, value)
end fn

fn echo_sample(value: f32) -> f32
    return value
end fn

fn main() -> i32
    let values: array<f32> = [16777217.0, -0.0]
    let alias = values
    if (values[0] as f64) != 16777216.0 then return 1 end if
    alias[0] = 16777219.0
    if (values[0] as f64) != 16777220.0 then return 2 end if
    alias.push(16777217.0)
    if (values[2] as f64) != 16777216.0 then return 3 end if
    values.reserve(128)
    if values.len() != 3 then return 4 end if
    let empty: array<f32> = []
    if (empty[0] as f64) != 0.0 then return 5 end if
    empty.push(1.401298464324817e-45)
    if (empty[0] as f64) != 1.401298464324817e-45 then return 6 end if
    values[-1] = 1.0
    values[99] = 1.0
    if (values[0] as f64) != 16777220.0 then return 7 end if
    println(values[-1] as f64)
    println(values[99] as f64)
    println(values[1] as f64)

    let typed = new_f32(3, 16777217.0)
    if (get_f32(typed, 1) as f64) != 16777216.0 then return 8 end if
    set_f32(typed, 1, 16777219.0)
    if (echo_sample(get_f32(typed, 1)) as f64) != 16777220.0 then return 9 end if
    let generic = new_with_value<f32>(2, 16777217.0)
    array_set<f32>(generic, 1, -0.0)
    if (get<f32>(generic, 0) as f64) != 16777216.0 then return 10 end if
    if length<f32>(generic) != 2 then return 11 end if
    let repeated = repeat_sample<f32>(16777219.0)
    if (repeated[1] as f64) != 16777220.0 then return 12 end if
    println(get<f32>(generic, 1) as f64)
    println(get_f32(typed, -1) as f64)

    let rows: array<array<f32>> = [values, empty]
    rows[0][0] = 16777217.0
    if (alias[0] as f64) != 16777216.0 then return 13 end if
    let row_fill = new_with_value<array<f32>>(3, values)
    row_fill[2].push(16777219.0)
    if values.len() != 4 then return 14 end if
    let tuples: array<(f32, text)> = [(16777217.0, "sample")]
    if (tuples[0].0 as f64) != 16777216.0 then return 15 end if
    let records: array<F32ArrayRecord> = [F32ArrayRecord { samples: values }]
    if (records[0].samples[3] as f64) != 16777220.0 then return 16 end if
    let signals: array<F32ArraySignal> = [F32ArraySignal.Samples(values), F32ArraySignal.Sample(16777217.0)]
    match signals[1]
        F32ArraySignal.Sample(value) ->
            if (value as f64) != 16777216.0 then return 17 end if
        _ -> return 18
    end match
    let boxed: anything = values
    if not anything_is<array<f32>>(boxed) then return 19 end if
    if anything_is<array<f64>>(boxed) then return 20 end if
    let recovered = boxed as array<f32>
    if (recovered[3] as f64) != 16777220.0 then return 21 end if

    // Generic operations preserve the literal's scalar width through both
    // overlapping copy directions, including signed zero and subnormals.
    let copied: array<f32> = [16777217.0, -0.0, 1.401298464324817e-45, 3.0]
    array_set<f32>(copied, 0, 16777219.0)
    if (get<f32>(copied, 0) as f64) != 16777220.0 then return 22 end if
    if not try_copy_range<f32>(copied, 0, copied, 1, 3) then return 23 end if
    if (get<f32>(copied, 1) as f64) != 16777220.0 then return 24 end if
    if (get<f32>(copied, 3) as f64) != 1.401298464324817e-45 then return 25 end if
    println(get<f32>(copied, 2) as f64)
    if not try_copy_range<f32>(copied, 1, copied, 0, 3) then return 26 end if
    if (get<f32>(copied, 0) as f64) != 16777220.0 then return 27 end if
    if (get<f32>(copied, 2) as f64) != 1.401298464324817e-45 then return 28 end if
    println(get<f32>(copied, 1) as f64)
    if try_copy_range<f32>(copied, 0, copied, 3, 2) then return 29 end if
    if (get<f32>(copied, 3) as f64) != 1.401298464324817e-45 then return 30 end if
    println("f32 array contract")
    return 0
end fn
