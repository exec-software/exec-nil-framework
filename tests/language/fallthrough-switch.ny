module fallthrough_switch

enum Signal
    Idle
    Running
    Finished
end enum

fn choose(ref calls: i32) -> i32
    calls = calls + 1
    return 2
end fn

fn falling(input: i32) -> i32
    let mut total: i32 = 0
    switch input
        case 1:
            total = total + 1
        case 2:
            total = total + 2
        default:
            total = total + 4
        case 3:
            total = total + 8
            break
        case 4:
            total = total + 16
    end switch
    return total
end fn

fn stacked(input: i32) -> i32
    let mut total: i32 = 0
    switch (input) {
        case 1:
        case 2:
            total = 7
            break;
        default:
            total = 9
    }
    return total
end fn

fn loop_flow() -> i32
    let mut index: i32 = 0
    let mut total: i32 = 0
    while index < 4
        index = index + 1
        switch index
            case 1:
                continue
            case 2:
                total = total + 2
                break
            default:
                total = total + 4
            case 4:
                total = total + 8
                break
        end switch
        total = total + 100
    end while
    return total
end fn

fn selected(input: i32) -> i32
    let mut state: i32 = 0
    let result = switch input
        case 1:
            state = 10
        case 2:
            break state + 2
        default:
            break 90
    end switch
    return result
end fn

fn text_choice(input: text) -> text
    return switch input
        default:
            break "other"
        case "start":
        case "begin":
            break "ok"
        case "λ":
            break "unicode"
    end switch
end fn

fn early(input: i32) -> i32
    let result = switch input
        case 1:
            return 77
        default:
            break 88
    end switch
    return result
end fn

fn conditional(input: i32) -> i32
    return switch input
        default:
            if input > 0 then
                break 1
            else
                break 2
            end if
    end switch
end fn

fn cleaned() -> i32
    let mut state: i32 = 5
    let result = switch 1
        case 1:
            attempt
                break state
            recover
                state = 99
            end attempt
        default:
            break 0
    end switch
    return result + state
end fn

fn nested() -> i32
    let mut total: i32 = 0
    switch 1
        case 1:
            switch 2
                case 2:
                    total = 3
                    break
                default:
                    total = 99
            end switch
            total = total + 1
            break
        default:
            total = 100
    end switch
    return total
end fn

fn owners() -> i32
    let mut storage = owned<i32>(1)
    storage[0] = 12
    switch 1
        case 1:
            let view = owned_borrow(storage)
            println(view[0])
        default:
            let mut view = owned_borrow_mut(storage)
            view[0] = 13
            break
    end switch
    return storage[0]
end fn

fn suspended(input: i32) -> i32
    return 100 + switch input { case 1: return 77; default: break 2; }
end fn

fn combine(left: i32, right: i32) -> i32
    return left + right
end fn

fn suspended_call(input: i32) -> i32
    return combine(100, switch input { case 1: return 77; default: break 2; })
end fn

fn suspended_tuple(input: i32) -> i32
    let pair = (100, switch input { case 1: return 77; default: break 2; })
    return pair.0 + pair.1
end fn

fn suspended_text(input: i32) -> text
    return "prefix" + switch input { case 1: return "early"; default: break "late"; }
end fn

struct Problem
    code: i32
end struct

fn suspended_fault(input: i32) -> i32
    attempt
        return 100 + switch input { case 1: raise Problem { code: 9 }; default: break 2; }
    fault Problem as problem
        return problem.code
    end attempt
    return 0
end fn

fn replaced(input: i32) -> i32
    attempt
        return 100 + switch input { case 1: return 77; default: break 2; }
    recover
        return 99
    end attempt
    return 0
end fn

fn suspended_void() -> void
    println(100 + switch 1 { case 1: return; default: break 2; })
end fn

fn main() -> i32
    println(falling(1))
    println(falling(2))
    println(falling(3))
    println(falling(4))
    println(falling(9))
    println(stacked(1))
    println(stacked(2))
    println(stacked(9))
    let mut calls: i32 = 0
    switch choose(ref calls)
        default:
            println("wrong default")
        case 2:
            println(calls)
            break
    end switch
    println(loop_flow())
    println(selected(1))
    println(selected(2))
    println(selected(9))
    println(text_choice("start"))
    println(text_choice("begin"))
    println(text_choice("λ"))
    println(text_choice("missing"))
    println(early(1))
    println(early(2))
    println(conditional(1))
    println(conditional(-1))
    println(cleaned())
    println(nested())
    println(owners())
    println(switch true { case false: break 0; case true: break 1; default: break 2; })
    println(switch -9223372036854775808 { case -9223372036854775808: break 3; default: break 0; })
    let widened = switch 1 { case 1: break 1; default: break 2.5; }
    println(widened == 1.0)
    let nested_value = switch 1
        case 1:
            break (switch 2 { case 2: break 22; default: break 0; })
        default:
            break 0
    end switch
    println(nested_value)
    switch 9
        case 1:
            println("unmatched")
    end switch
    switch choose(ref calls)
    end switch
    println(calls)
    let phase: Signal = Signal::Running
    println(switch phase {
        case Signal.Idle: break 0;
        case Signal::Running:
        case Signal::Finished: break 42;
        default: break 99;
    })
    println(suspended(1))
    println(suspended(2))
    let pair = switch 1 { case 1: break (3, "pair"); default: break (4, "other"); }
    let (count, label) = pair
    println(count)
    println(label)
    let values = switch 1 { case 1: break [5, 6]; default: break [7, 8]; }
    println(values[1])
    println(suspended_call(1))
    println(suspended_call(2))
    println(suspended_tuple(1))
    println(suspended_tuple(2))
    println(suspended_text(1))
    println(suspended_text(2))
    println(suspended_fault(1))
    println(suspended_fault(2))
    println(replaced(1))
    println(replaced(2))
    suspended_void()
    return 0
end fn
