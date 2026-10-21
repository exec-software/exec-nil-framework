module tuple_contract

struct Counter
    value: i64
end struct

struct Box<T>
    item: T
end struct

type Pair = (i64, text)

fn next(counter: Counter) -> i64
    counter.value = counter.value + 1
    return counter.value
end fn

fn wrap<T>(item: T) -> (T, text)
    return (item, "wrapped")
end fn

fn main() -> i32
    let counter = Counter { value: 0 }
    let calls = (next(counter), next(counter), next(counter))
    let (first, second, third) = calls
    println(first)
    println(second)
    println(third)
    let pair: (i64, text) = (42, "tuple")
    let boxed = Box<(i64, text)> { item: pair }
    let aliased: Pair = boxed.item
    let (count, label) = aliased
    println(count)
    println(label)
    let nested = ((-1.5, true), pair)
    println(nested.0.0)
    println(nested.1.1)
    let singleton: (i64,) = (9,)
    let (single,) = singleton
    println(single)
    let generic = wrap<i64>(11)
    println(generic.0)
    println(generic.1)
    let references = (counter, ref counter)
    let copy = references
    copy.0.value = 17
    println(references.1.value)
    let (_, discarded) = (next(counter), "kept")
    println(counter.value)
    println(discarded)
    match nested
        ((-1.5, flag), (number, _)) ->
            println(number)
            println(flag)
        _ -> println(0)
    end match
    let matched = match pair
        (42, name) -> name
        _ -> "miss"
    end match
    println(matched)
    match nested
        ((_, _), (_, _)) -> println(23)
    end match
    let (_, _) = pair
    return 0
end fn
