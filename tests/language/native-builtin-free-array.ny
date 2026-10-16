fn reject_free_array(value: array<i32>)
    free(value)
end fn

fn main() -> i32
    let values: array<i32> = [1]
    reject_free_array(values)
    return 0
end fn
