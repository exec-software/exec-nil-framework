import ny.lang.datetime.std_time

fn main() -> i32
    let before = ny::lang::datetime::now()
    ny::lang::datetime::sleep(1)
    let after = ny::lang::datetime::now()
    println(after >= before)
    println(after - before < 5000)
    return 0
end fn
