import nil.std.collections
fn main() -> i32
    let orders = [(101, false, 4.5), (102, true, 9.0), (103, false, 7.0)]
    let selected = collect query
        from order in orders
        where order.1 == false
        order by order.2 descending
        take 10
        select (order.0, order.2)
    end query
    println(query_count(selected))
    println(query_any(selected))
    println(query_first(selected).unwrap().0)
    println(query_last(selected).unwrap().0)
    println(query_single(selected).is_error())
    let single = collect query
        from row in selected
        take 1
        select row.0
    end query
    println(query_single(single).unwrap())
    let empty = collect query
        from row in selected
        where false
        select row.0
    end query
    println(query_first(empty).is_none())
    println(query_any(empty))
    return 0
end fn
