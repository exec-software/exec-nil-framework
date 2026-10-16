module storage_first
pub let mut value: text = "first"
pub let mut count: i32 = 1
let z_seed: text = "seed"
let a_derived: text = z_seed + " first"

pub fn read_value<T>(unused: T) -> text
    return value
end fn

struct Holder<T>
    item: T
end struct

impl<T> Holder<T>
    fn read_global(self) -> text
        return value
    end fn
end impl

pub fn read_holder() -> text
    let holder = Holder<i32> { item: 7 }
    return holder.read_global()
end fn

pub fn derived_value() -> text
    return a_derived
end fn

module storage_second
pub let mut value: text = "second"
pub let mut count: i32 = 100

fn main() -> i32
    println(storage_first::value)
    println(value)
    storage_first::value = "first changed"
    value = "second changed"
    storage_first::count = storage_first::count + 2
    count = count + 3
    println(storage_first::value)
    println(value)
    println(storage_first::count)
    println(count)
    if true then
        let mut value: text = "local"
        value = value + " shadow"
        println(value)
    end if
    println(value)
    println(storage_first::read_value<i32>(9))
    println(storage_first::read_holder())
    println(storage_first::derived_value())
    return 0
end fn
