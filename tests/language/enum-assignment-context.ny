module validation.enum_assignment
import nil.std.core.option

struct PayloadRecord
    amount: i64
end struct
struct Holder
    item: nil::std::core::Option<PayloadRecord>
end struct
let mut evaluations: i64 = 0

fn make_payload() -> PayloadRecord
    evaluations = evaluations + 1
    return PayloadRecord { amount: 40 + evaluations }
end fn

fn main() -> i32
    let mut selected: Option<PayloadRecord> = Option.None
    selected = Option.Some(make_payload())
    let mut saved: Option<PayloadRecord> = Option.None
    saved = Option.Some(make_payload())
    let mut container = Holder { item: Option.None }
    container.item = Option.Some(make_payload())
    match selected
        Option.Some(item) -> println(item.amount)
        Option.None -> return 9
    end match
    match saved
        Option.Some(item) -> println(item.amount)
        Option.None -> return 9
    end match
    match container.item
        Option.Some(item) -> println(item.amount)
        Option.None -> return 9
    end match
    println(evaluations)
    return 0
end fn
