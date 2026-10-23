enum Option<T>
    Some(T)
    None
end enum

enum Result<T, E>
    Ok(T)
    Error(E)
end enum

enum EnumPair<T>
    Both(T, T)
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

fn construct_nested_none() -> Result<Option<i32>, text>
    return Result.Ok(Option.None)
end fn

fn construct_nested_some() -> Result<Option<f64>, text>
    return Result::Ok(Option::Some(-2.375))
end fn

fn enum_payload(value: i32) -> Option<i32>
    println(value)
    return Option.Some(value)
end fn

fn construct_nested_pair() -> EnumPair<Option<i32>>
    return EnumPair.Both(Option.None, Option.Some(29))
end fn

fn construct_ordered_pair() -> EnumPair<Option<i32>>
    return EnumPair.Both(enum_payload(31), enum_payload(37))
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

    # Nested nullary constructors need the outer payload's concrete generic
    # type before emission; value-taking constructors need it recursively too.
    match construct_nested_none()
        Result.Ok(optional) ->
            match optional
                Option.None -> println(1)
                Option.Some(_) -> return 11
            end match
        Result.Error(_) -> return 12
    end match
    match construct_nested_some()
        Result.Ok(optional) -> println(unwrap_floating(optional) == -2.375)
        Result.Error(_) -> return 13
    end match
    # Multi-field enum matching is not lowered yet. Construction still must
    # provide exact nested schemas and retain both managed payloads.
    let nested_pair = construct_nested_pair()
    println("nested pair constructed")
    # Context propagation must preserve left-to-right argument evaluation.
    let ordered_pair = construct_ordered_pair()
    println("ordered pair constructed")
    return 0
end fn
