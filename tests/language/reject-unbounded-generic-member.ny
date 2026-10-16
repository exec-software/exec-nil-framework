struct Envelope<T>
    item: T
end struct

impl<T> Envelope<T>
    fn item_fingerprint(self) -> i64
        return self.item.fingerprint()
    end fn
end impl

fn main() -> i32
    return 0
end fn
