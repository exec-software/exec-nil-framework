# Typed global storage must survive every execution route.
let greeting: text = "typed global"

fn main() -> i32
    println(greeting)
    return 0
end fn
