@intrinsic("array.new_with_value")
fn new_values<T>(size: i32, value: T) -> array<T>
end fn

// More than 4 MiB of temporary arrays forces native collection while the
// replacement is reachable through the caller-reference cell.
fn churn() -> void
    let mut count = 0
    while count < 1600
        let discarded: array<i32> = new_values<i32>(1024, count)
        count = count + 1
    end while
end fn

// Caller references update the anything binding, while earlier copies keep their value.
fn replace(ref destination: anything) -> void
    destination = "re" + "placed"
    churn()
    println(destination as text)
end fn

fn forward(ref destination: anything) -> void
    replace(ref destination)
end fn

fn main() -> i32
    let mut value: anything = 42
    let original = value
    forward(ref value)
    // The caller cell is now the only current binding root for the new box.
    churn()
    println(original as i32)
    println(value as text)
    return 0
end fn
