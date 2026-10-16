namespace fault_domain = fault_names
namespace nested_domain = fault_domain

struct Problem
    code: i32
end struct

struct Other
    code: i32
end struct

fn fail(code: i32) -> i32
    raise Problem { code: code }
    return -1
end fn

fn recursive(depth: i32) -> i32
    if depth > 0 then
        return recursive(depth - 1)
    end if
    return fail(7)
end fn

fn first_argument() -> i32
    println("first-argument")
    return 100
end fn

fn sum(left: i32, right: i32) -> i32
    return left + right
end fn

fn return_before_cleanup() -> i32
    let mut value: i32 = 10
    attempt
        return value
    recover
        value = 99
        println(value)
    end attempt
    return -1
end fn

fn replace_return() -> i32
    attempt
        return 11
    recover
        return 12
    end attempt
    return -1
end fn

fn raising_guard(code: i32) -> bool
    raise Problem { code: code }
    return false
end fn

fn void_raise() -> void
    raise Problem { code: 55 }
end fn

fn void_chain() -> void
    void_raise()
end fn

fn return_replaced_by_fault() -> i32
    attempt
        return 60
    recover
        raise Other { code: 61 }
    end attempt
    return -1
end fn

fn fault_replaced_by_return() -> i32
    attempt
        raise Problem { code: 62 }
    recover
        return 63
    end attempt
    return -1
end fn

fn fault_replaced_by_fault() -> i32
    attempt
        raise Problem { code: 70 }
    recover
        raise Other { code: 71 }
    end attempt
    return -1
end fn

fn handler_return() -> i32
    attempt
        fail(80)
    fault Problem as problem
        return problem.code
    recover
        println("handler-return-cleanup")
    end attempt
    return -1
end fn

fn lexical_cleanup() -> i32
    let value: i32 = 10
    while true
        attempt
            let value: i32 = 20
            while true
                return value
            end while
        recover
            println(value)
            break
        end attempt
    end while
    return 21
end fn

fn main() -> i32
    attempt
        println("ordinary-body")
    recover
        println("ordinary-cleanup")
    end attempt

    attempt
        println(sum(first_argument(), recursive(3)))
    fault Other as unrelated
        println("wrong-type")
    fault Problem as first when first.code == 99
        println("wrong-guard")
    fault Problem as selected when selected.code == 7
        println(selected.code)
    fault as fallback
        println("wrong-fallback")
    recover
        println("argument-cleanup")
    end attempt

    println(return_before_cleanup())
    println(replace_return())

    attempt
        attempt
            fail(20)
        fault Problem as original
            println(original.code)
            raise Other { code: 21 }
        fault Other as sibling
            println("wrong-sibling-recatch")
        recover
            println("handler-inner-cleanup")
        end attempt
    fault Other as replacement
        println(replacement.code)
    recover
        println("handler-outer-cleanup")
    end attempt

    attempt
        attempt
            fail(30)
        fault Problem as original when raising_guard(31)
            println("wrong-guard-body")
        fault Problem as sibling
            println("wrong-guard-recatch")
        recover
            println("guard-inner-cleanup")
        end attempt
    fault Problem as replacement
        println(replacement.code)
    end attempt

    attempt
        fail(40)
    fault as retained
        attempt
            raise retained
        fault Problem as first
            println(first.code)
        end attempt
        attempt
            raise retained
        fault Problem as second
            println(second.code)
        end attempt
    recover
        println("retained-cleanup")
    end attempt

    attempt
        attempt
            fail(50)
        fault Other as unrelated
            println("wrong-unmatched")
        recover
            println("unmatched-inner-cleanup")
        end attempt
    fault Problem as matched
        println(matched.code)
    recover
        println("unmatched-outer-cleanup")
    end attempt

    attempt
        void_chain()
        println("wrong-void-return")
    fault Problem as problem
        println(problem.code)
    end attempt

    let mut index: i32 = 0
    while index < 4
        attempt
            index = index + 1
            if index == 1 then
                continue
            end if
            if index == 3 then
                break
            end if
            println(index)
        recover
            println(100 + index)
        end attempt
    end while

    attempt
        let mut inner: i32 = 0
        while inner < 3
            inner = inner + 1
            if inner == 1 then
                continue
            end if
            break
        end while
        println("inner-loop-complete")
    recover
        println("inner-loop-cleanup-once")
    end attempt

    while true
        attempt
            attempt
                break
            recover
                println("break-inner-cleanup")
            end attempt
        recover
            println("break-outer-cleanup")
        end attempt
    end while

    let mut overridden_continue: i32 = 0
    while overridden_continue < 3
        attempt
            overridden_continue = overridden_continue + 1
            continue
        recover
            println(200 + overridden_continue)
            break
        end attempt
    end while

    let mut overridden_break: i32 = 0
    while overridden_break < 2
        attempt
            overridden_break = overridden_break + 1
            break
        recover
            println(300 + overridden_break)
            continue
        end attempt
    end while

    attempt
        println(return_replaced_by_fault())
    fault Other as problem
        println(problem.code)
    end attempt
    println(fault_replaced_by_return())
    attempt
        println(fault_replaced_by_fault())
    fault Other as problem
        println(problem.code)
    end attempt
    println(handler_return())
    println(lexical_cleanup())
    attempt
        raise fault_names::NamespacedFault { code: 91 }
    fault nested_domain::PublicAlias as selected
        println(selected.code)
    end attempt

    let raw = allocate<i32>(1)
    raw[0] = 92
    let alias = raw
    attempt
        attempt
            raise Problem { code: 93 }
        fault Problem as problem
            println(alias[0])
            alias[0] = 94
        end attempt
        println(raw[0])
    recover
        deallocate(raw)
        println("manual-cleanup-once")
    end attempt
    return 0
end fn

module fault_names
pub struct NamespacedFault
    code: i32
end struct
pub type PublicAlias = NamespacedFault
