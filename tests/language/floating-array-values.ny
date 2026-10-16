fn main() -> i32
    let values: array<f64> = [1.5, 2.5]
    println(values.len())
    if values[0] < values[1] then
        println("ordered")
    end if
    return 0
end fn
