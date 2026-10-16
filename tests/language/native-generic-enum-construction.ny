enum Option<T>
    Some(T)
    None
end enum

impl<T> Option<T>
    fn unwrap(self) -> T
        match self
            Option.Some(payload) -> return payload
            Option.None -> panic("unexpected empty option")
        end match
    end fn
end impl

@intrinsic("math.atan2")
fn enum_atan2(y: f64, x: f64) -> f64
end fn

@intrinsic("math.sqrt")
fn enum_sqrt(value: f64) -> f64
end fn

fn construct_option() -> Option<i32>
    return Option.Some(7)
end fn

fn construct_floating(value: f64) -> Option<f64>
    return Option.Some(value)
end fn

fn pass_floating(value: Option<f64>) -> Option<f64>
    return value
end fn

fn unwrap_floating(value: Option<f64>) -> f64
    match value
        Option.Some(payload) -> return payload
        Option.None -> panic("unexpected empty floating option")
    end match
end fn

fn main() -> i32
    let value: Option<i32> = construct_option()
    match value
        Option.Some(payload) -> println(payload)
        Option.None -> return 10
    end match

    # Fractions and negative values must keep their floating-point bits when
    # stored in the enum's word-sized payload and returned through functions.
    let fraction = pass_floating(construct_floating(-2.375))
    println(unwrap_floating(fraction) == -2.375)
    println(fraction.unwrap() == -2.375)
    let positive = construct_floating(0.125)
    println(positive.unwrap() == 0.125)
    let large = construct_floating(-9000000000.25)
    println(unwrap_floating(large) == -9000000000.25)

    # atan2 distinguishes the sign of zero without dividing by zero or
    # depending on the text formatter's spelling of -0 or infinity.
    let negative_zero = construct_floating(-0.0)
    let positive_zero = construct_floating(0.0)
    println(enum_atan2(negative_zero.unwrap(), -1.0) < 0.0)
    println(enum_atan2(positive_zero.unwrap(), -1.0) > 0.0)

    let nan = construct_floating(enum_sqrt(-1.0))
    let nan_value = unwrap_floating(nan)
    println(nan_value != nan_value)
    return 0
end fn
