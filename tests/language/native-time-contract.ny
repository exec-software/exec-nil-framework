@intrinsic("time.now")
fn now_ms() -> i64
end fn

@intrinsic("time.sleep")
fn sleep_ms(duration: i64)
end fn

fn main() -> i32
    sleep_ms((-9223372036854775808.0) as i64)
    sleep_ms(0)
    sleep_ms(2)
    println(now_ms() > 0)
    println("awake")
    return 0
end fn
