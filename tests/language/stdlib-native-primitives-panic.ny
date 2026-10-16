import ny.lang.collections
import ny.lang.core.*

fn main() -> i32
    let mut bytes: vec<u8> = vec<u8>.new()
    bytes.push(255 as u8)
    bytes.set(1, 0 as u8)
    println("unreachable")
    return 0
end fn
