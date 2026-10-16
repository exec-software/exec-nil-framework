module inventory_model

struct InventoryItem
    sku: text
    quantity: i32
    reorder_at: i32
end struct

trait StockPolicy
    fn needs_reorder(self) -> bool
end trait

impl StockPolicy for InventoryItem
    pub fn needs_reorder(self) -> bool
        return self.quantity <= self.reorder_at
    end fn
end impl

fn main() -> i32
    let mut item = InventoryItem {
        sku: "NY-204",
        quantity: 14,
        reorder_at: 6
    }

    item.quantity = item.quantity - 9

    println("INVENTORY MODEL")
    println(item.sku)
    println(item.quantity)
    if item.needs_reorder() then
        println("REORDER")
    else
        println("STOCKED")
    end if
    return 0
end fn
