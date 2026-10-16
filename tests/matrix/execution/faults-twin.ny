module tour.faults

struct ReadFault
    message: text
end struct

struct WriteFault
    code: i64
end struct

fn read_value(available: bool) -> i64
    if not available then
        raise ReadFault { message: "input is unavailable" }
    end if
    return 42
end fn

fn guarded(available: bool) -> i64
    attempt
        return read_value(available)
    fault ReadFault as problem when problem.message != ""
        println(problem.message)
        return 0
    fault WriteFault as failure
        println(failure.code)
        return 0
    recover
        println("cleanup")
    end attempt
    return 0
end fn

fn relay(available: bool) -> i64
    attempt
        return read_value(available)
    fault as token
        raise token
    end attempt
    return 0
end fn

fn replacing_exit() -> i64
    let mut attempts: i64 = 0
    attempt
        attempts = attempts + 1
        raise WriteFault { code: 7 }
    recover
        return attempts
    end attempt
    return 0
end fn

fn main() -> i32
    println(guarded(true))
    println(guarded(false))
    attempt
        println(relay(false))
    fault ReadFault as problem
        println(problem.message)
    end attempt
    println(replacing_exit())
    return 0
end fn
