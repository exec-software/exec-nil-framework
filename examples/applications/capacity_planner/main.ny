module capacity_planner

let mut planner_runs: i32 = 0

fn fibonacci(value: i32) -> i32
    if value <= 1 then
        return value
    end if
    return fibonacci(value - 1) + fibonacci(value - 2)
end fn

fn forecast_score(day: i32) -> i32
    return fibonacci(day + 2)
end fn

fn main() -> i32
    planner_runs = planner_runs + 1

    let mut total: i32 = 0
    let mut urgent_days: i32 = 0
    let mut processed_days: i32 = 0

    for day in range(0, 8)
        if day == 3 then
            continue
        end if

        let score = forecast_score(day)
        total = total + score
        processed_days = processed_days + 1

        if score >= 8 then
            urgent_days = urgent_days + 1
        end if
    end for

    let mut normalization_cycles: i32 = 0
    while total > 50
        total = total - 10
        normalization_cycles = normalization_cycles + 1
    end while

    println("CAPACITY PLANNER")
    println(processed_days)
    println(urgent_days)
    println(total)
    println(normalization_cycles)
    println(planner_runs)
    return 0
end fn
