module modular_billing

import pricing

fn main() -> i32
    let units: i32 = 12
    let gross = pricing::subtotal(17, units)
    let discount = pricing::volume_discount(gross, units)
    let net = gross - discount

    println("MODULAR BILLING")
    println(units)
    println(gross)
    println(discount)
    println(net)
    return 0
end fn
