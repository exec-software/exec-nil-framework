fn latency_limit() -> i64
    return 16
end fn

@ai_context(
    intent="Qualify a release from deterministic service-health checks",
    assumptions=["The input batch is fixed for conformance"],
    constraints=["Every route must make the same release decision"]
)
fn main() -> i32
    let mut accepted: i64 = 0
    let mut rejected: i64 = 0
    let mut request: i64 = 0

    while request < 8
        let latency = request * 3 + 4
        if latency <= latency_limit() then
            accepted = accepted + 1
        else
            rejected = rejected + 1
        end if
        request = request + 1
    end while

    let mut backlog: i64 = 6
    let mut drain_cycles: i64 = 0
    while backlog > 0
        backlog = backlog - 2
        drain_cycles = drain_cycles + 1
    end while

    println("RELEASE QUALIFIER")
    if accepted + rejected == 8 then
        println("COUNT OK")
    else
        println("COUNT FAILED")
    end if
    if accepted == 5 then
        println("LATENCY OK")
    else
        println("LATENCY FAILED")
    end if
    if rejected == 3 then
        println("REJECTION OK")
    else
        println("REJECTION FAILED")
    end if
    if backlog == 0 then
        if drain_cycles == 3 then
            println("DRAIN OK")
        else
            println("DRAIN FAILED")
        end if
    else
        println("DRAIN FAILED")
    end if
    return 0
end fn
