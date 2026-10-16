import ny.lang.collections

fn main() -> i32
    # The explicit generic receiver is sufficient to infer the variable type;
    # no duplicate `: vec<i32>` annotation should be required.
    let mut values = vec<i32>.new()
    values.push(21)
    values.push(42)
    println(values.length())
    println(values.get(1).unwrap())
    return 0
end fn
