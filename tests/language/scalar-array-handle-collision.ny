module ny.tests.scalar_array_handle_collision

fn require(value: bool, label: text)
    if not value then panic(label) end if
end fn

fn identity(value: i64) -> i64
    return value
end fn

fn check_comparisons(value: i64)
    let same = identity(value)
    let lower = value - (1 as i64)
    let higher = value + (1 as i64)
    require(value == same, "scalar equality at live handle")
    require(not (value != same), "scalar inequality at live handle")
    require(not (value < same), "scalar less equal operands")
    require(not (value > same), "scalar greater equal operands")
    require(value <= same, "scalar less-or-equal at live handle")
    require(value >= same, "scalar greater-or-equal at live handle")
    require(value != lower and value != higher, "scalar unequal neighbors")
    require(not (value == lower) and not (value == higher), "scalar unequal equality checks")
    require(lower < value and value < higher, "scalar strict ordering")
    require(value > lower and higher > value, "scalar reverse strict ordering")
    require(lower <= value and value <= higher, "scalar inclusive ordering")
    require(value >= lower and higher >= value, "scalar reverse inclusive ordering")
    require(not (higher < value) and not (lower > value), "scalar false strict ordering")
    require(not (higher <= value) and not (lower >= value), "scalar false inclusive ordering")
end fn

fn classify(value: i64) -> i32
    match value
        17179869184 -> return 1
        17179869192 -> return 2
        17179869208 -> return 3
        _ -> return 4
    end match
    return 0
end fn

fn main() -> i32
    # These are the first allocations. In the interpreter they occupy handles
    # 0x400000000, +8, and +24. An equal i64 bit pattern is still an integer;
    # none of these checks compare array objects or authorize array equality.
    let first: array<i64> = []
    let second: array<i64> = [11 as i64, 22 as i64]
    let third: array<u8> = [7 as u8, 8 as u8, 9 as u8]
    let base: i64 = 17179869184
    check_comparisons(base)
    check_comparisons(base + (8 as i64))
    check_comparisons(base + (24 as i64))
    check_comparisons(base - (1 as i64))
    check_comparisons(base + (1 as i64))
    println("scalar comparisons ignore live array handle bits")

    require(classify(base) == 1, "statement match first handle bits")
    require(classify(base + (8 as i64)) == 2, "statement match second handle bits")
    require(classify(base + (24 as i64)) == 3, "statement match third handle bits")
    require(classify(base + (1 as i64)) == 4, "statement match fallback")
    let expression = match base { 17179869183 -> 0; 17179869184 -> 5; _ -> 0; }
    require(expression == 5, "expression match live handle bits")
    println("scalar literal matches ignore live array handle bits")

    let mut cursor: i64 = base - (2 as i64)
    let stop: i64 = base + (26 as i64)
    let mut visits: i32 = 0
    let mut boundary_visits: i32 = 0
    while cursor <= stop do
        if cursor == base or cursor == base + (8 as i64) or cursor == base + (24 as i64) then
            boundary_visits = boundary_visits + 1
        end if
        check_comparisons(cursor)
        cursor = cursor + 1
        visits = visits + 1
    end while
    require(visits == 29 and boundary_visits == 3, "loop visits all colliding integers")
    while cursor > base do
        cursor = cursor - 1
    end while
    require(cursor == base, "reverse loop stops at colliding integer")
    println("scalar loops cross live array handle bits")

    # These integers are exactly representable as f64. Ny requires explicit
    # conversion before comparing different scalar kinds.
    let floating: f64 = base as f64
    require(floating == (base as f64) and (base as f64) == floating, "converted numeric equality")
    require(not (floating != (base as f64)) and not ((base as f64) != floating), "converted numeric inequality")
    require(((base - (1 as i64)) as f64) < floating and floating < ((base + (1 as i64)) as f64), "converted numeric less")
    require(((base + (1 as i64)) as f64) > floating and floating > ((base - (1 as i64)) as f64), "converted numeric greater")
    require((base as f64) <= floating and floating <= (base as f64), "converted numeric less-or-equal")
    require((base as f64) >= floating and floating >= (base as f64), "converted numeric greater-or-equal")
    println("converted numeric comparisons stay numeric")

    first.push(base)
    first.push(base + (8 as i64))
    first.push(base + (24 as i64))
    # Cross the interpreter's allocation-count collection threshold while all
    # three original arrays remain live. Handle tagging must preserve GC roots
    # as well as scalar comparisons, including arrays returned from allocations.
    let mut allocations: i32 = 0
    while allocations < 1100 do
        let temporary: array<i64> = [base]
        require(temporary[0] == base, "temporary array scalar stays numeric")
        allocations = allocations + 1
    end while
    require(first.len() == 3, "first array length preserved")
    check_comparisons(first[0])
    check_comparisons(first[1])
    check_comparisons(first[2])
    require(second.len() == 2 and second[0] == (11 as i64) and second[1] == (22 as i64), "second array contents preserved")
    require(third.len() == 3 and third[0] == (7 as u8) and third[1] == (8 as u8) and third[2] == (9 as u8), "third array contents preserved")
    println("arrays retain contents and scalar elements")
    return 0
end fn
