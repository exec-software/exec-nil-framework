trait Fingerprint
    fn fingerprint(self) -> i64
end trait

struct Key
    value: i64
end struct

impl Fingerprint for Key
    fn fingerprint(self) -> i64
        return self.value
    end fn
end impl

struct Envelope<T>
    item: T
end struct

impl<T: Fingerprint> Envelope<T>
    fn item_fingerprint(self) -> i64
        return self.item.fingerprint()
    end fn
end impl

fn main() -> i32
    let envelope: Envelope<Key> = Envelope<Key> { item: Key { value: 42 } }
    println(envelope.item_fingerprint())
    return 0
end fn
