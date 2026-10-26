import nil.std.core

type Maybe<T> = Option<T>

fn alias_value(value: Maybe<f64>) -> f64
    return value.unwrap()
end fn

fn generic_alias_value<T>(value: Maybe<T>) -> T
    return value.unwrap()
end fn

fn empty(value: Option<i32>) -> bool
    return value.is_none()
end fn

fn generic_empty<T>(value: Option<T>) -> bool
    return value.is_none()
end fn

fn nested_empty<T>(value: Result<Option<T>, text>) -> bool
    return value.unwrap().is_none()
end fn

fn nested_value(value: Result<Option<f64>, text>) -> f64
    return value.unwrap().unwrap()
end fn

fn generic_forward<T>(anchor: T) -> bool
    return generic_empty<T>(Option.None)
end fn

fn inferred_empty<T>(anchor: T, value: Option<T>) -> bool
    return value.is_none()
end fn

fn mark(value: i32) -> f64
    println(value)
    return (value as f64) + 0.5
end fn

fn pair(first: Result<Option<f64>, text>, second: Result<Option<f64>, text>) -> bool
    return first.unwrap().unwrap() == 23.5 and second.unwrap().unwrap() == 29.5
end fn

fn main() -> i32
    println(empty(Option.None))
    println(generic_empty<f64>(Option.None))
    println(nested_empty<f64>(Result.Ok(Option.None)))
    println(nested_value(Result.Ok(Option.Some(3.25))) == 3.25)
    println(generic_forward(1.25))
    println(inferred_empty(1.25, Option.None))
    println(alias_value(Option.Some(1.25)) == 1.25)
    println(generic_alias_value<f64>(Option.Some(3.5)) == 3.5)
    println(pair(Result.Ok(Option.Some(mark(23))), Result.Ok(Option.Some(mark(29)))))
    return 0
end fn
