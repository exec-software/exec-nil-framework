import nil.std.core

struct ContextBox<T>
    value: T
end struct

struct ContextPair<T>
    first: T
    second: T
end struct

fn mark(value: i32) -> f64
    println(value)
    return (value as f64) + 0.25
end fn

fn main() -> i32
    let positional: ContextBox<Option<i32>> = ContextBox<Option<i32>>(Option.None)
    println(positional.value.is_none())
    let named: ContextBox<Option<i32>> = ContextBox<Option<i32>> { value: Option.None }
    println(named.value.is_none())

    let nested_none: ContextBox<Result<Option<f64>, text>> = ContextBox<Result<Option<f64>, text>>(Result.Ok(Option.None))
    println(nested_none.value.unwrap().is_none())
    let nested_some: ContextBox<Result<Option<f64>, text>> = ContextBox<Result<Option<f64>, text>>(Result.Ok(Option.Some(1.25)))
    println(nested_some.value.unwrap().unwrap() == 1.25)
    let named_nested: ContextBox<Result<Option<f64>, text>> = ContextBox<Result<Option<f64>, text>> { value: Result.Ok(Option.None) }
    println(named_nested.value.unwrap().is_none())

    let pair: ContextPair<f64> = ContextPair<f64>(mark(11), mark(17))
    println(pair.first == 11.25)
    println(pair.second == 17.25)
    return 0
end fn
