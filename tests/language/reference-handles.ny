struct Counter
    value: i32
end struct

struct Holder<T>
    item: T
end struct

type CounterAlias = Counter

class Reader
    value: i32
    pub virtual fn read(self: ref<Self>) -> i32
        return self.value
    end fn
end class

class OffsetReader inherits Reader
    offset: i32
    pub override fn read(self: ref<Self>) -> i32
        return self.value + self.offset
    end fn
end class

fn make_counter(value: i32) -> ref<Counter>
    let counter = Counter { value: value }
    return ref counter
end fn

fn update(counter: ref<Counter>, value: i32) -> ref<Counter>
    counter.value = value
    return counter
end fn

fn local_type_shadow(counter: Counter) -> ref<Counter>
    let Counter: i32 = 0
    return ref counter
end fn

fn parameter_type_shadow(holder: Holder<Counter>, Holder: i32) -> ref<Holder<Counter>>
    return ref holder
end fn

fn main() -> i32
    let mut original: CounterAlias = Counter { value: 3 }
    let alias: ref<CounterAlias> = ref original
    alias.value = 9
    println(original.value)

    # A reference aliases the object handle, not the local binding.
    original = Counter { value: 11 }
    alias.value = 13
    println(original.value)
    println(alias.value)

    let escaped = make_counter(17)
    println(update(escaped, 19).value)
    println(escaped.value)

    let holder = Holder<Counter> { item: original }
    let holder_alias = ref holder
    holder_alias.item.value = 23
    println(original.value)

    let concrete = OffsetReader { value: 29, offset: 2 }
    let concrete_alias = ref concrete
    println(concrete_alias.read())
    let base: Reader = concrete
    let base_alias = ref base
    println(base_alias.read())
    base_alias.value = 37
    println(concrete_alias.read())

    # Raw-pointer reads and writes remain their separate supported operation.
    let pointer = allocate<i32>(1)
    *pointer = 41
    println(*pointer)
    deallocate(pointer)

    # Value bindings do not change an existing handle's nominal type.
    println(local_type_shadow(original).value)
    println(parameter_type_shadow(holder, 0).item.value)
    return 0
end fn
