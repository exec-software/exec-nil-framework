fn main() -> i32
    attempt
        let zero: i32 = 0
        println(1 / zero)
    fault as problem
        println("fatal-trap-must-not-be-caught")
    recover
        println("fatal-trap-must-not-run-recover")
    end attempt
    return 0
end fn
