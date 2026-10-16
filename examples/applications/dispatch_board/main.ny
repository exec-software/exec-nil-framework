enum JobState
    Queued(i32)
    Running(i32)
    Complete(i32)
    Failed(i32)
end enum

fn main() -> i32
    let queued: JobState = JobState::Queued(41)
    let running: JobState = JobState::Running(65)
    let complete: JobState = JobState::Complete(128)
    let failed: JobState = JobState::Failed(503)

    println("DISPATCH BOARD")
    match queued
        JobState.Queued(ticket) -> println(ticket)
        JobState.Running(percent) -> println(percent)
        JobState.Complete(records) -> println(records)
        JobState.Failed(code) -> println(code)
    end match
    match running
        JobState.Queued(ticket) -> println(ticket)
        JobState.Running(percent) -> println(percent)
        JobState.Complete(records) -> println(records)
        JobState.Failed(code) -> println(code)
    end match
    match complete
        JobState.Queued(ticket) -> println(ticket)
        JobState.Running(percent) -> println(percent)
        JobState.Complete(records) -> println(records)
        JobState.Failed(code) -> println(code)
    end match
    match failed
        JobState.Queued(ticket) -> println(ticket)
        JobState.Running(percent) -> println(percent)
        JobState.Complete(records) -> println(records)
        JobState.Failed(code) -> println(code)
    end match
    return 0
end fn
