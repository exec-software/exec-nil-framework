# Published fixture for the installed-SDK JSONL/DAP consumer contract.
# Keep marker locations unique: the harness resolves source lines dynamically.
import ny.lang.core

fn scale(value: i64) -> i64
    return value * 2 # DBG_SCALE
end fn

fn accumulate(limit: i64) -> i64
    let buffer: ptr<i64> = allocate<i64>(limit)
    let mut total: i64 = 0
    for index in range(limit)
        let current: i64 = index + 1
        let transformed: i64 = scale(current) # DBG_CALL
        total = total + transformed # DBG_AFTER_CALL
        buffer[index] = transformed
    end for
    deallocate(buffer)
    return total
end fn

fn main() -> i32
    let count: i64 = 3
    let result = accumulate(count)
    println("debug-result=" + Convert.i64_to_text(result))
    let value: i64 = 3
    if true then
        let value: i64 = 99
        let inner = value # DBG_SHADOW_INNER
    end if
    let observed = value # DBG_SHADOW_OUTER
    println(observed)
    return 0
end fn
