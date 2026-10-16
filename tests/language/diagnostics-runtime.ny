module ny.tests.diagnostics_runtime

import ny.lang.diagnostics.*
import ny.lang.datetime.std_time

fn main() -> i32
    println(TimeSpan.ticks_per_millisecond())
    println(TimeSpan.ticks_per_second())

    let exact = TimeSpan.from_ticks(25000)
    println(exact.total_milliseconds())

    let mut stopwatch = Stopwatch.new()
    stopwatch.start()
    ny::lang::datetime::sleep(1)
    stopwatch.stop()

    let elapsed = stopwatch.elapsed().total_milliseconds()
    println(elapsed >= 1)
    println(stopwatch.is_running)
    return 0
end fn
