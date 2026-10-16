# User function declarations take precedence over legacy VM builtin spellings.
fn f64_to_str(value: i64) -> text
    return "user exact formatter"
end fn

fn tagged__f64_to_str(value: i64) -> text
    return "user namespaced formatter"
end fn

fn helper_vm__get_address(value: i64) -> i64
    return value + 5
end fn

fn String__len(left: i64, right: i64) -> i64
    return left * 10 + right
end fn

fn array__push(left: i64, right: i64) -> i64
    return left * 10 + right
end fn

fn main() -> i32
    println(f64_to_str(7))
    println(tagged__f64_to_str(8))
    println(helper_vm__get_address(9))
    println(String__len(1, 2))
    println(array__push(3, 4))
    return 0
end fn
