module telemetry_rollup

fn main() -> i32
    let mut samples: array<i32> = [12, 18, 7, 25, 18, 30]
    samples[2] = 10

    let mut total: i32 = 0
    let mut minimum: i32 = samples[0]
    let mut maximum: i32 = samples[0]
    let mut alerts: i32 = 0

    for index in range(0, 6)
        let reading = samples[index]
        total = total + reading

        if reading < minimum then
            minimum = reading
        end if
        if reading > maximum then
            maximum = reading
        end if
        if reading >= 25 then
            alerts = alerts + 1
        end if
    end for

    println("TELEMETRY ROLLUP")
    println(total)
    println(minimum)
    println(maximum)
    println(alerts)
    println(samples[5])
    return 0
end fn
