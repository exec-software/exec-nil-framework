enum Result<T, E>
    Ok(T)
    Error(E)
end enum

fn words() -> array<text>
    return ["one", "two", "é"]
end fn

fn wrapped_words() -> Result<array<text>, text>
    return Result.Ok(words())
end fn

fn index(position: i32) -> i32
    println(position)
    return position
end fn

fn main() -> i32
    let direct = ["one", "two", "é"]
    println(direct[0] + direct[1] == "onetwo")
    println(direct[0].len() == 3)
    println(direct[2].length() == 2)
    println(direct[0].substr(1, 2) == "ne")
    println(direct[0] < direct[1])
    println(direct[0] + direct[1] + direct[2] == "onetwoé")

    let returned = words()
    println(returned[0] + returned[1] == "onetwo")
    println(words()[2].len() == 2)
    println(words()[0] + words()[1] == "onetwo")

    let unwrapped = wrapped_words().unwrap()
    println(unwrapped.len() == 3)
    println(unwrapped[0] + unwrapped[1] == "onetwo")
    println(unwrapped[2].len() == 2)
    println(wrapped_words().unwrap()[0] + wrapped_words().unwrap()[1] == "onetwo")

    # Typed operator selection must keep each index evaluation once, in order.
    println(direct[index(0)] + direct[index(1)])
    return 0
end fn
