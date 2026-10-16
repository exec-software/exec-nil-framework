fn main() -> i32
    // --no-typecheck must not fabricate fault payload or catch-all proofs.
    raise 1
    attempt
        println("attempt")
    fault as problem
        println(problem)
    recover
        println("recover")
    end attempt
    return 0
end fn
