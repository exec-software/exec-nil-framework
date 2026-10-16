module pricing

pub fn subtotal(unit_price: i32, quantity: i32) -> i32
    return unit_price * quantity
end fn

pub fn volume_discount(amount: i32, quantity: i32) -> i32
    if quantity >= 10 then
        return amount / 10
    end if
    return 0
end fn
