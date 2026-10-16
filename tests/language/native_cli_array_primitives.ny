module ny.tests.native_cli_array_primitives

fn main() -> i32
    let mut filled: array<u8> = []
    filled.push(7)
    filled.push(7)
    println(filled.len())
    println(filled[0])

    filled[1] = 9
    filled.push(255)
    println(filled[1])
    println(filled[2])
    println(filled.len())

    let mut grown: array<u8> = []
    grown.push(0)
    grown.push(128)
    grown.push(255)
    grown[0] = 42
    println(grown.len())
    println(grown[0])
    println(grown[1])
    println(grown[2])
    return 0
end fn
