struct Leaf
    value: i64
end struct

struct Pair
    first: Leaf
    second: Leaf
end struct

struct Envelope<T>
    item: T
end struct

struct Borrowed
    item: ref<Leaf>
end struct

impl<T> Envelope<T>
    fn get(self: ref<Self>) -> T
        return self.item
    end fn
end impl

fn echo(value: ref<Leaf>) -> ref<Leaf>
    return value
end fn

fn update(value: ref<Leaf>, replacement: i64) -> void
    value.value = replacement
end fn

fn make_pair(first: Leaf, second: Leaf) -> Pair
    return Pair { first: first, second: second }
end fn

fn main() -> i32
    let left: Leaf = Leaf { value: 11 }
    let right: Leaf = Leaf { value: 19 }
    let pair: Pair = make_pair(left, right)
    let envelope: Envelope<Pair> = Envelope<Pair> { item: pair }
    println(envelope.get().first.value)
    println(envelope.get().second.value)

    let borrowed: ref<Leaf> = ref left
    let holder: Borrowed = Borrowed { item: borrowed }
    update(echo(borrowed), 23)
    println(holder.item.value)
    println(pair.first.value)
    pair.second.value = 29
    println(right.value)

    let replacement: Leaf = Leaf { value: 31 }
    pair.first = replacement
    println(envelope.item.first.value)
    println(left.value)

    let branched = Leaf { value: if true then 37 else 41 end if }
    println(branched.value)
    let matched = Leaf { value: match 2 { 1 -> 43; _ -> 47; } }
    println(matched.value)
    let nested = Envelope<Leaf> {
        item: if false then Leaf { value: 53 } else Leaf { value: 59 } end if
    }
    println(nested.item.value)
    let nested_match = Envelope<Leaf> {
        item: match 1 { 1 -> Leaf { value: 61 }; _ -> Leaf { value: 67 }; }
    }
    println(nested_match.item.value)
    return 0
end fn
