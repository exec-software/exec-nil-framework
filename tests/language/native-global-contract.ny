let z_first: text = "first"
let a_dependent: text = z_first + " second"
let mut greeting: text = "initial"
let inferred = "inferred"
let mut total: f64 = 1.5

fn read_greeting() -> text
    return greeting
end fn

fn replace_greeting(value: text)
    greeting = value + "!"
end fn

fn shadow_greeting() -> text
    let mut greeting: text = "local"
    greeting = greeting + " shadow"
    return greeting
end fn

fn main() -> i32
    println(a_dependent)
    println(read_greeting())
    replace_greeting("changed")
    println(read_greeting())
    println(shadow_greeting())
    println(read_greeting())
    if true then
        let greeting: text = "block"
        println(greeting)
    end if
    println(read_greeting())
    greeting = ""
    println(read_greeting().len())
    println(inferred)
    total = total + 2.25
    println(total == 3.75)
    return 0
end fn
