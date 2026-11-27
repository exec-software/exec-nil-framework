struct F32CallCounter
    count: i32
end struct

fn next_amount(counter: ref<F32CallCounter>) -> f64
    counter.count = counter.count + 1
    return 16777217.0 + (counter.count as f64) - 1.0
end fn

fn combine(first: f32, second: f32) -> f32
    return second - first
end fn

fn main() -> i32
    let calls = F32CallCounter { count: 0 }
    let callback = combine
    let result = callback(next_amount(ref calls), next_amount(ref calls))
    println(result as i64)
    println(calls.count)
    return 0
end fn
