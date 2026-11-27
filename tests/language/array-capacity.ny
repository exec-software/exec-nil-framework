module tests.array_capacity
import nil.std.collections.*

fn main() -> i32
    let mut values: array<i32> = [7, 11]
    let alias: array<i32> = values
    values.reserve(64)
    let reserved: i32 = values.capacity()
    println(reserved >= 64)
    println(values.len() == 2)
    println(alias.capacity() == reserved)
    values.reserve(3)
    println(values.capacity() == reserved)
    let mut index: i32 = 2
    while index < 64
        values.push(index)
        index = index + 1
    end while
    println(values.capacity() == reserved)
    println(alias.len() == 64)
    println(alias[0] == 7 and alias[1] == 11 and alias[63] == 63)
    let mut words: array<text> = []
    words.reserve(32)
    println(words.len() == 0 and words.capacity() >= 32)
    words.push("retained")
    words.reserve(128)
    println(words[0] == "retained" and words.len() == 1)
    let mut packed: array<u8> = [0 as u8, 127 as u8, 255 as u8]
    packed.reserve(128)
    println(packed.capacity() >= 128 and packed[2] == (255 as u8))
    let mut fractions: array<f64> = [1.25, -0.5]
    fractions.reserve(64)
    println(fractions[0] == 1.25 and fractions[1] == -0.5)
    let mut vector: vec<i32> = vec<i32>.with_capacity(128)
    println(vector.capacity() >= 128 and vector.length() == 0)
    let vector_capacity: i32 = vector.capacity()
    vector.push(9)
    vector.clear()
    vector.push(12)
    println(vector.capacity() == vector_capacity and vector.length() == 1 and vector.get(0).unwrap() == 12)
    return 0
end fn
