fn make_empty() -> array<i32>
    return []
end fn

fn count_values(values: array<i32>) -> i32
    return values.len()
end fn

fn main() -> i32
    let declared: array<i32> = []
    println(declared.len())

    let returned: array<i32> = make_empty()
    println(returned.len())

    println(count_values([]))
    return 0
end fn
