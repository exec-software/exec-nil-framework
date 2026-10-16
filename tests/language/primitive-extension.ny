# Extension methods on builtin types.
#
# A method's NIL symbol is built from the impl block's target type, while the
# call site builds it from the receiver's inferred type. For a user-declared
# type both are the module-qualified declaration name and they agree. For a
# builtin the inferred type is the bare canonical name, so module-qualifying
# the declaration made the two disagree and every call failed at runtime:
#
#     function demo__prim__i64__doubled   <- declared
#     CALL     i64__doubled               <- called
#     -> Ny interpreter error: CALL: unknown function 'i64__doubled'
#
# Builtins are therefore never module-qualified. `text` and `String` name the
# same builtin and must resolve to one symbol.
#
# Note the consequence: an impl on a builtin lives in a global namespace, so
# two modules cannot both add the same method name to the same builtin. That
# follows from the call site mangling a bare type name.

module demo.prim

impl i64
    fn doubled(self) -> i64
        return self * 2
    end fn
end impl

impl u8
    fn widened(self) -> i64
        return self as i64
    end fn
end impl

impl bool
    fn flipped(self) -> bool
        return not self
    end fn
end impl

impl text
    fn shouted(self) -> text
        return self + "!"
    end fn
end impl

fn main() -> i32
    let n: i64 = 21
    println(n.doubled())

    let b: u8 = 255 as u8
    println(b.widened())

    let flag: bool = false
    println(flag.flipped())

    let greeting: text = "hi"
    println(greeting.shouted())
    return 0
end fn
