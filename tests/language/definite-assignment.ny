struct AssignmentFault
    code: i32
end struct

enum AssignmentState
    Ready(i32)
    Idle
end enum

fn complete_boolean(value: bool) -> text
    let mut selected: text
    match value
        true -> selected = "boolean-yes"
        false -> selected = "boolean-no"
    end match
    return selected
end fn

fn complete_enum(value: AssignmentState) -> text
    let mut selected: text
    match value
        AssignmentState.Idle -> selected = "enum-idle"
        AssignmentState.Ready(payload) -> selected = (if payload == 7 then "enum-ready" else "wrong" end if)
        _ when false -> selected = "unreachable"
    end match
    return selected
end fn

fn complete_tuple() -> text
    let mut selected: text
    match (1, (2, 3))
        (first, (_, third)) -> selected = "complete-tuple"
    end match
    return selected
end fn

fn typed_recovery() -> text
    let mut selected: text
    attempt
        raise AssignmentFault { code: 1 }
    fault AssignmentFault as problem
        selected = "typed-recovery"
    recover
        println(selected)
    end attempt
    return selected
end fn

fn dormant_call()
end fn

fn dead_call_recovery() -> text
    let mut selected: text
    attempt
        if false then
            dormant_call()
        end if
        raise AssignmentFault { code: 1 }
    fault AssignmentFault as problem
        selected = "dead-call-recovery"
    recover
        println(selected)
    end attempt
    return selected
end fn

fn branch(flag: bool) -> text
    let mut selected: text
    if flag then
        selected = "yes"
    else
        selected = "no"
    end if
    return selected
end fn

fn early(flag: bool) -> text
    let mut selected: text
    if flag then
        return "early"
    else
        selected = "late"
    end if
    return selected
end fn

fn repeated(flag: bool) -> text
    let mut selected: text
    let mut index: i32 = 0
    loop
        index = index + 1
        if index == 1 then
            continue
        end if
        if flag then
            selected = "loop-yes"
            break
        end if
        selected = "loop-no"
        break
    end loop
    return selected
end fn

fn selected_case(input: i32) -> text
    let mut selected: text
    switch input
        case 1:
            selected = "case-one"
            break
        default:
            selected = "case-other"
    end switch
    return selected
end fn

fn selected_pattern(input: i32) -> text
    let mut selected: text
    match input
        1 -> selected = "match-one"
        _ -> selected = "match-other"
    end match
    return selected
end fn

fn fault_paths(flag: bool) -> text
    let mut selected: text
    attempt
        if flag then
            raise AssignmentFault { code: 1 }
        end if
        selected = "normal-path"
    fault as problem
        selected = "fault-path"
    end attempt
    return selected
end fn

fn handled() -> text
    let mut selected: text
    attempt
        selected = "before-fault"
        raise AssignmentFault { code: 7 }
    fault AssignmentFault as problem
        println(selected)
        selected = "handled"
    end attempt
    return selected
end fn

fn recovered() -> text
    let mut selected: text
    loop
        attempt
            break
        recover
            selected = "recovered"
        end attempt
    end loop
    return selected
end fn

fn main() -> i32
    let mut first: text
    first = "straight"
    println(first)
    println(branch(true))
    println(branch(false))
    println(early(true))
    println(early(false))
    println(repeated(true))
    println(repeated(false))
    println(selected_case(1))
    println(selected_case(2))
    println(selected_pattern(1))
    println(selected_pattern(2))
    println(fault_paths(false))
    println(fault_paths(true))
    println(complete_boolean(true))
    println(complete_boolean(false))
    println(complete_enum(AssignmentState.Ready(7)))
    println(complete_enum(AssignmentState.Idle))
    println(complete_tuple())
    println(typed_recovery())
    println(dead_call_recovery())
    println(handled())
    println(recovered())
    let mut shadow: text
    if true then
        let shadow: text = "inner"
        println(shadow)
    end if
    shadow = "outer"
    println(shadow)
    let mut from_constant: text
    if true then
        from_constant = "constant"
    end if
    println(from_constant)
    let mut from_false: text
    if false then
        return 1
    else
        from_false = "constant-false"
    end if
    println(from_false)
    let mut from_while: text
    while true
        from_while = "while"
        break
    end while
    println(from_while)
    let (tuple_name, tuple_value) = ("tuple", 3)
    println(tuple_name)
    println(tuple_value)
    return 0
end fn
