import ny.lang.system.*

fn main() -> i32
    println(arg_count())
    match try_arg(0)
        Option.Some(value) -> println(value)
        Option.None -> println("missing")
    end match
    match try_arg(1)
        Option.Some(value) -> println(value.len())
        Option.None -> println(-1)
    end match
    match try_arg(2)
        Option.Some(value) -> println(value)
        Option.None -> println("missing")
    end match
    println(try_arg(3).is_none())
    return 0
end fn
