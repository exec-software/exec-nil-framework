fn main() -> i32
    println((1 + 2) * 3)
    let grouped = (1 + 2
    ) * 3
    println(grouped)
    let nested = ((1 + 2
    )
    ) * 4
    println(nested)
    let negated = -(1 + 2
    ) * 3
    println(negated)
    let casted = (1 + 2
    ) as i64 * 5
    println(casted)
    let chained = (2
    ) * (3
    ) * 4
    println(chained)

    # A new-line star still begins the following dereference statement.
    let slot: ptr<i32> = allocate<i32>(1)
    let unchanged = (10 + 1
    )
    *slot = 42
    println(unchanged)
    println(*slot)
    let again = 7
    *slot = 43
    println(again)
    println(*slot)
    deallocate(slot)
    return 0
end fn
