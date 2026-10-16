module stage.four

struct point
    x: i64
    y: i64
end struct

struct segment
    start: point
    finish: point
end struct

struct envelope<T>
    item: T
end struct

type coordinate = i64

enum job_state
    Queued(i64)
    Running(i64)
    Complete(i64)
    Failed(i64)
end enum

enum maybe<T>
    Present(T)
    Absent
end enum

fn describe(state: job_state) -> text
    match state
        job_state.Queued(ticket) -> return "queued"
        job_state.Running(percent) -> return "running"
        job_state.Complete(records) -> return "complete"
        job_state.Failed(code) -> return "client failure"
        _ -> return "unknown"
    end match
end fn

fn main() -> i32
    let origin = point { x: 0, y: 0 }
    let corner = point { x: 3, y: 4 }
    println(corner.x)
    println(corner.y)

    let span = segment { start: origin, finish: corner }
    println(span.finish.y)

    let boxed = envelope<i64> { item: 42 }
    println(boxed.item)

    let here: coordinate = 7
    println(here)

    println(describe(job_state.Queued(41)))
    println(describe(job_state.Failed(503)))
    println(describe(job_state.Failed(404)))

    let found: maybe<i64> = maybe.Present(42)
    match found
        maybe.Present(payload) -> println(payload)
        maybe.Absent -> println("no value")
    end match
    return 0
end fn
