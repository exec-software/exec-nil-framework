import ny.test.generic_helpers.*

fn main() -> i32
    # Imported generic free functions must specialize from argument types;
    # emitting an unspecialized FQN leaves an impossible runtime call.
    println(first_value(17, 99))
    println(first_value("ny", "other"))
    return 0
end fn
