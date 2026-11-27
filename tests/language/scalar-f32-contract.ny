let mut global_sample: f32 = 16777217.0

struct F32Cell<T>
    value: T
end struct

impl<T> F32Cell<T>
    fn replace(self: ref<Self>, first: T, second: T) -> T
        self.value = first
        return second
    end fn
end impl

fn counted_amount(counter: ref<F32Cell<i32>>) -> f64
    counter.value = counter.value + 1
    return counter.value as f64
end fn

enum F32Signal<T>
    Value(T)
    Pair(T, T)
    Idle
end enum

class F32Reader
    value: f32
    pub virtual fn sample(self) -> f32
        return self.value
    end fn
end class

@intrinsic("math.sqrt")
fn square_root(value: f64) -> f64
    return 0.0
end fn

@intrinsic("math.exp")
fn exponential(value: f64) -> f64
    return 0.0
end fn

fn identity<T>(value: T) -> T
    return value
end fn

fn rounded_return() -> f32
    return 16777217.0
end fn

fn main() -> i32
    let mut rounded: f32 = 16777217.0
    if (rounded as f64) != 16777216.0 then return 1 end if
    rounded = 16777219.0
    if (rounded as f64) != 16777220.0 then return 2 end if
    if (rounded_return() as f64) != 16777216.0 then return 3 end if
    if (identity<f32>(16777217.0) as f64) != 16777216.0 then return 4 end if

    # A direct integer cast must not round through binary64 first.
    let exact_integer: i64 = 4611686293305294849
    if ((exact_integer as f32) as f64) != 4611686568183201792.0 then return 5 end if
    if (((-exact_integer) as f32) as f64) != -4611686568183201792.0 then return 6 end if

    let unit: f32 = 1.0
    let half_ulp: f32 = 0.000000059604644775390625
    let adjacent: f32 = 1.00000011920928955078125
    if ((unit + half_ulp) as f64) != 1.0 then return 7 end if
    if ((adjacent + half_ulp) as f64) != 1.0000002384185791015625 then return 8 end if
    if ((adjacent * adjacent) as f64) != 1.0000002384185791015625 then return 9 end if
    if ((unit / (3.0 as f32)) as f64) != 0.3333333432674408 then return 10 end if
    if (((5.5 as f32) % (2.0 as f32)) as f64) != 1.5 then return 11 end if
    let wide: f32 = 16777216.0
    if ((wide + unit) - wide) != (0.0 as f32) then return 12 end if
    if ((wide + 1.0) - wide) != 1.0 then return 13 end if
    if ((wide + (1 as i32)) - wide) != (0.0 as f32) then return 14 end if

    let smallest: f32 = 1.401298464324817e-45
    if (smallest as f64) != 1.401298464324817e-45 then return 15 end if
    if ((smallest / (2.0 as f32)) as f64) != 0.0 then return 16 end if
    let minimum_normal: f32 = 1.1754943508222875e-38
    if (minimum_normal as f64) != 1.1754943508222875e-38 then return 17 end if
    let maximum: f32 = 3.4028234663852886e38
    let positive_infinity: f32 = exponential(1000.0)
    if (maximum * (2.0 as f32)) != positive_infinity then return 18 end if
    if (3.5e38 as f32) != positive_infinity then return 19 end if
    if (-3.5e38 as f32) != -positive_infinity then return 20 end if
    let not_a_number: f32 = square_root(-1.0)
    if not (not_a_number != not_a_number) then return 21 end if
    let computed_nan = positive_infinity - positive_infinity
    if not (computed_nan != computed_nan) then return 22 end if

    let object: F32Cell<f32> = F32Cell<f32> { value: 16777217.0 }
    if (object.value as f64) != 16777216.0 then return 23 end if
    object.value = 16777219.0
    if (object.value as f64) != 16777220.0 then return 24 end if
    let reader: F32Reader = F32Reader { value: 16777217.0 }
    if (reader.sample() as f64) != 16777216.0 then return 25 end if
    let tuple: (f32, f32) = (16777217.0, -0.0)
    if (tuple.0 as f64) != 16777216.0 then return 26 end if
    let signal: F32Signal<f32> = F32Signal.Pair(16777217.0, 16777219.0)
    match signal
        F32Signal.Pair(first, second) ->
            if (first as f64) != 16777216.0 then return 27 end if
            if (second as f64) != 16777220.0 then return 28 end if
        _ -> return 29
    end match

    let boxed: anything = unit
    if not anything_is<f32>(boxed) then return 30 end if
    if anything_is<f64>(boxed) then return 31 end if
    if ((boxed as f32) as f64) != 1.0 then return 32 end if
    let boxed_array: array<anything> = [rounded, not_a_number, tuple]
    if ((boxed_array[0] as f32) as f64) != 16777220.0 then return 33 end if
    let recovered_nan = boxed_array[1] as f32
    if not (recovered_nan != recovered_nan) then return 34 end if
    let recovered_tuple = boxed_array[2] as (f32, f32)
    if (global_sample as f64) != 16777216.0 then return 35 end if
    global_sample = 16777219.0
    if (global_sample as f64) != 16777220.0 then return 36 end if
    let joined = if unit == (1.0 as f32) then unit else (2 as i64) end if
    let joined_box: anything = joined
    if not anything_is<f32>(joined_box) then return 37 end if
    let widened = if unit == (1.0 as f32) then unit else 2.0 end if
    let widened_box: anything = widened
    if not anything_is<f64>(widened_box) then return 38 end if
    let calls = F32Cell<i32> { value: 0 }
    if (object.replace(counted_amount(ref calls), counted_amount(ref calls)) as f64) != 2.0 then return 40 end if
    if calls.value != 2 or (object.value as f64) != 1.0 then return 41 end if
    println(tuple.1 as f64)
    println(recovered_tuple.1 as f64)
    println((-1.0e-300 as f32) as f64)
    println(identity<f32>(-0.0) as f64)
    println("f32 scalar contract")
    return 0
end fn
