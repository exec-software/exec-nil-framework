struct Problem
    code: i32
end struct
fn fail() -> i32
    raise Problem { code: 9 }
    return -1
end fn
fn main() -> i32
    attempt
        fail()
    recover
        println("uncaught-cleanup")
    end attempt
    return 0
end fn
