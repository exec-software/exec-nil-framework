import nil.std.core
import ny.lang.core
namespace legacy = ny.lang.core

struct ordinary_fields
    lang: i32
    std: i32
end struct

fn main() -> i32
    let ny = ordinary_fields { lang: 17, std: 23 }
    let nil = ordinary_fields { lang: 31, std: 42 }
    println(ny.lang)
    println(nil.std)
    let numbers = [3, 1, 2]
    ny::lang::core::sort_i32(numbers)
    println(numbers[0])
    println(nil::std::core::is_sorted_i32(numbers))
    let selected: legacy::Option<i32> = Option.Some(9)
    match selected
        Option.Some(value) -> println(value)
        Option.None -> println(0)
    end match
    println("ny.lang.core and ny::lang::core stay text")
    return 0
end fn
