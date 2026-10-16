struct left__Problem
    code: i32
end struct

struct CaseProblem
    code: i32
end struct

struct caseProblem
    code: i32
end struct

struct CaseHolder
    payload: CaseProblem
end struct

module left
pub struct Problem
    code: i32
end struct

pub fn fail() -> i32
    raise Problem { code: 11 }
    return -1
end fn

module right
pub struct Problem
    code: i32
end struct

pub fn fail() -> i32
    raise Problem { code: 22 }
    return -1
end fn

module generic
pub struct Problem<T>
    value: T
end struct

pub fn fail<T>(value: T) -> i32
    raise Problem<T> { value: value }
    return -1
end fn

module client
type LeftProblem = left::Problem

fn identity(value: CaseProblem) -> CaseProblem
    return value
end fn

fn fail_field(holder: CaseHolder) -> void
    raise identity(holder.payload)
end fn

fn main() -> i32
    attempt
        left::fail()
    fault left__Problem as sanitized_collision
        println("wrong-sanitized-identity")
    fault right::Problem as wrong
        println("wrong-right-type")
    fault LeftProblem as selected
        println(selected.code)
    end attempt
    attempt
        right::fail()
    fault left::Problem as wrong
        println("wrong-left-type")
    fault right::Problem as selected
        println(selected.code)
    end attempt
    attempt
        generic::fail<i32>(33)
    fault generic::Problem<text> as wrong
        println("wrong-generic-type")
    fault generic::Problem<i32> as selected
        println(selected.value)
    end attempt
    attempt
        fail_field(CaseHolder { payload: CaseProblem { code: 44 } })
    fault caseProblem as wrong
        println("wrong-case-type")
    fault CaseProblem as selected
        println(selected.code)
    end attempt
    attempt
        raise caseProblem { code: 55 }
    fault CaseProblem as wrong
        println("wrong-case-type")
    fault caseProblem as selected
        println(selected.code)
    end attempt
    return 0
end fn
