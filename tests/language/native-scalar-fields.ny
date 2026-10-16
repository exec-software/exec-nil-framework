struct Cell<T>
    value: T
end struct

struct Pair<A, B>
    first: A
    second: B
end struct

struct Mixed
    byte: u8
    fraction: f64
    count: i32
    flag: bool
end struct

impl<T> Cell<T>
    fn get(self: ref<Self>) -> T
        return self.value
    end fn
end impl

fn make_fraction(value: f64) -> Cell<f64>
    return Cell<f64> { value: value }
end fn

fn make_byte(value: u8) -> Cell<u8>
    return Cell<u8> { value: value }
end fn

fn pass_fraction(value: Cell<f64>) -> Cell<f64>
    return value
end fn

fn pass_pair(value: Pair<Cell<f64>, Cell<u8>>) -> Pair<Cell<f64>, Cell<u8>>
    return value
end fn

fn main() -> i32
    let fraction: Cell<f64> = make_fraction(-2.375)
    println(fraction.get() == -2.375)
    let byte: Cell<u8> = make_byte(-1 as u8)
    println(byte.get())
    let mixed = Mixed { byte: 511 as u8, fraction: 1.125, count: 17, flag: true }
    println(mixed.byte)
    println(mixed.fraction == 1.125)
    println(mixed.count)
    println(mixed.flag)
    mixed.byte = 256 as u8
    mixed.fraction = -7.625
    println(mixed.byte)
    println(mixed.fraction == -7.625)

    let pair = Pair<Cell<f64>, Cell<u8>> { first: fraction, second: byte }
    let alias = pass_pair(pair)
    alias.first.value = 3.875
    alias.second.value = 256 as u8
    println(fraction.value == 3.875)
    println(byte.value)
    alias.second.value = 511 as u8
    println(pair.second.get())

    let signed_zero = pass_fraction(make_fraction(-0.0))
    println(signed_zero.get())
    signed_zero.value = 0.0
    println(signed_zero.get())
    let branch = Cell<f64> { value: if false then 1.5 else -0.125 end if }
    println(branch.value == -0.125)
    let chosen = Cell<u8> { value: match 1 { 1 -> (511 as u8); _ -> (256 as u8); } }
    println(chosen.value)
    fraction.value = 9007199254740991.0
    println(pass_fraction(fraction).get() == 9007199254740991.0)
    return 0
end fn
