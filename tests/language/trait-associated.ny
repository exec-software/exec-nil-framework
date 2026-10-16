trait Factory
    fn create(value: i64) -> Self
end trait

struct Item
    value: i64
end struct

impl Factory for Item
    fn create(value: i64) -> Item
        println(value + 100)
        return Item { value: value }
    end fn
end impl

struct Box<T>
    value: i64
end struct

impl<T: Factory> Factory for Box<T>
    fn create(value: i64) -> Box<T>
        # Keep the generic factory invocation observable without requiring
        # nested aggregate fields, which are outside the native route profile.
        T.create(value)
        return Box<T> { value: value + 2 }
    end fn
end impl

fn make<T: Factory>(value: i64) -> T
    return T.create(value)
end fn

fn main() -> i32
    let item: Item = make<Item>(41)
    println(item.value)
    let boxed: Box<Item> = make<Box<Item>>(43)
    println(boxed.value)
    return 0
end fn
