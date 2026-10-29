class Base
    value: i32
    pub virtual fn score(self) -> i32
        return self.value
    end fn
end class

class Derived inherits Base
    extra: i32
    pub override fn score(self) -> i32
        return self.value + self.extra
    end fn
end class

struct Record
    label: text
end struct

type RecordAlias = Record

fn boxed<T>(input: T) -> anything
    return input
end fn

fn identity<T>(input: T) -> T
    return input
end fn

fn array_identity<T>(input: array<T>) -> array<T>
    return input
end fn

fn main() -> i32
    let view: Base = Derived { value: 10, extra: 2 }
    let dynamic_class: anything = view
    if not (anything_is<Base>(dynamic_class)) then
        return 9
    end if
    if not (not anything_is<Derived>(dynamic_class)) then
        return 9
    end if
    let recovered = dynamic_class as Base
    println(recovered.score())

    let record: anything = Record { label: "record" }
    println((record as RecordAlias).label)
    let pair: anything = (5, "tuple")
    let concrete_pair = pair as (i32, text)
    println(concrete_pair.0)
    println(concrete_pair.1)

    let pointer = allocate<i32>(1)
    pointer[0] = 17
    let dynamic_pointer: anything = pointer
    if not (anything_is<ptr<i32>>(dynamic_pointer)) then
        return 9
    end if
    if not (not anything_is<ptr<i64>>(dynamic_pointer)) then
        return 9
    end if
    let recovered_pointer = dynamic_pointer as ptr<i32>
    println(recovered_pointer[0])
    deallocate(pointer)

    let bytes: array<u8> = [255 as u8]
    let dynamic_array: anything = bytes
    let recovered_array = dynamic_array as array<u8>
    println(recovered_array[0])
    let generic = boxed<i64>(10000000000)
    println(generic as i64)
    let mixed = identity<array<anything>>([1, "mixed", yes])
    println(mixed[0] as i32)
    println(mixed[1] as text)
    println(mixed[2] as boolean)
    let specialized = array_identity<anything>(["specialized", 2])
    println(specialized[0] as text)
    println(specialized[1] as i32)
    return 0
end fn
