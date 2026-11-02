# Step from consumer source into the sealed installed standard-library source.
import nil.std.math

fn main() -> i32
    let value: i32 = -9
    let result = abs_i32(value) # DBG_STDLIB_CALL
    println(result) # DBG_STDLIB_AFTER
    return 0
end fn
