struct OwnedProblem
    code: i32
end struct

type OwnedWords = owned<i32>

fn return_owned_scalar() -> i32
    let mut local = owned<i32>(1)
    *local = 37
    return *local
end fn

fn raise_with_owner() -> i32
    let mut local = owned<i64>(2)
    local[1] = 41
    raise OwnedProblem { code: local[1] as i32 }
    return 0
end fn

fn main() -> i32
    let mut words: OwnedWords = owned<i32>(3)
    words[0] = 11
    words[1] = 13
    if true then
        let first = owned_borrow(words)
        let second = owned_borrow(words)
        println(first[0] + second[1])
    end if
    if true then
        let writer = owned_borrow_mut(words)
        writer[2] = 17
        *writer = 19
        println(writer[0] + writer[2])
    end if
    let mut moved = owned_move(words)
    println(moved[1])
    *moved = 23
    println(*moved)
    let mut bytes = owned<u8>(2)
    bytes[0] = 257
    bytes[1] = 258
    println(bytes[0] + bytes[1])
    println(return_owned_scalar())
    attempt
        let pending = owned<i32>(4)
        println(raise_with_owner())
    fault OwnedProblem as problem
        let mut handled = owned<i32>(1)
        *handled = problem.code
        println(*handled)
    recover
        let mut cleanup = owned<i32>(1)
        *cleanup = 43
        println(*cleanup)
    end attempt
    let mut total = 0
    for index in range(6)
        let mut iteration = owned<i32>(1)
        *iteration = index
        if index == 1 then
            continue
        end if
        if index == 4 then
            break
        end if
        total = total + *iteration
    end for
    println(total)
    return 0
end fn
