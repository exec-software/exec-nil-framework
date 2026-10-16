module call_return_contract

let* touches: i64 = 0

fn touch(amount: i64)
    touches = touches + amount
end fn

fn identity(value: i64) -> i64
    return value
end fn

fn truth() -> bool
    return true
end fn

fn mark(amount: i64) -> i64
    touch(amount)
    return amount
end fn

fn choose(value: i64) -> i64
    return match value {
        0 -> identity(40);
        _ -> identity(50);
    }
end fn

fn exercise() -> i64
    # These statements discard one result each, including non-call values.
    identity(300);
    truth();
    true;
    false;
    43;
    3.5;
    "discarded";
    (3 + 4);
    [1, 2, 3];
    # A standalone expression match discards only its selected arm's result.
    (match 1 {
        1 when false -> mark(100);
        _ -> mark(4);
    });

    # Assignments and void calls do not produce a placeholder value.
    let mut local: i64 = 7
    local = local + 1
    local
    touch(1)
    return 5 + identity(6)
end fn

fn main() -> i32
    touch(2)
    # exercise must preserve the pending left operand while it calls touch.
    println(100 + exercise())
    println(touches)
    println(identity(identity(9)))
    let selected = match 1 {
        1 -> identity(20);
        _ -> identity(30);
    }
    println(selected)
    println(choose(0))
    println(choose(2))
    println("contract passed")
    return 0
end fn
