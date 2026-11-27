import nil.std.collections
import nil.std.core.*

struct bucket_key
    value: i64
end struct

impl Hash for bucket_key
    fn hash(self) -> i64
        if self.value < 0 then return -9223372036854775807 - 1 end if
        return self.value % 37
    end fn
end impl

impl Eq for bucket_key
    fn eq(self, other: bucket_key) -> bool
        return self.value == other.value
    end fn

    fn ne(self, other: bucket_key) -> bool
        return not self.eq(other)
    end fn
end impl

fn require_map(value: bool) -> void
    if not value then panic("HashMap bucket contract failed") end if
end fn

fn main() -> i32
    let mut values: HashMap<bucket_key, text> = HashMap<bucket_key, text>.new()
    let mut shared: HashMap<bucket_key, text> = values
    require_map(values.is_empty())
    require_map(values.get(bucket_key { value: 1 }).is_none())
    let mut i: i32 = 0
    while i < 257
        values.insert(bucket_key { value: i as i64 }, "value")
        i = i + 1
    end while
    require_map(shared.length() == 257)
    i = 0
    while i < 257
        require_map(shared.get(bucket_key { value: i as i64 }).unwrap() == "value")
        i = i + 1
    end while
    values.insert(bucket_key { value: 111 }, "replacement")
    require_map(shared.length() == 257)
    require_map(shared.get(bucket_key { value: 111 }).unwrap() == "replacement")
    values.insert(bucket_key { value: -1 }, "negative-one")
    values.insert(bucket_key { value: -2 }, "negative-two")
    require_map(shared.get(bucket_key { value: -1 }).unwrap() == "negative-one")
    require_map(shared.get(bucket_key { value: -2 }).unwrap() == "negative-two")
    require_map(shared.get(bucket_key { value: -3 }).is_none())

    # Remove from collision chains and the beginning/middle/end of dense storage.
    require_map(values.remove(bucket_key { value: 0 }).unwrap() == "value")
    require_map(shared.remove(bucket_key { value: 111 }).unwrap() == "replacement")
    require_map(values.remove(bucket_key { value: 256 }).unwrap() == "value")
    require_map(values.remove(bucket_key { value: -1 }).unwrap() == "negative-one")
    require_map(values.remove(bucket_key { value: -1 }).is_none())
    require_map(shared.length() == 255)
    require_map(values.keys.get(0).unwrap().value == 1)
    require_map(values.keys.get(110).unwrap().value == 112)
    require_map(values.keys.get(254).unwrap().value == -2)
    i = 1
    while i < 256
        if i != 111 then
            require_map(values.contains_key(bucket_key { value: i as i64 }))
        end if
        i = i + 1
    end while
    require_map(values.get(bucket_key { value: -2 }).unwrap() == "negative-two")
    println("hashmap-growth-collisions-replacement-remove-ok")

    shared.clear()
    require_map(values.is_empty())
    require_map(values.keys.is_empty() and values.vals.is_empty())
    require_map(values.get(bucket_key { value: 37 }).is_none())
    values.insert(bucket_key { value: 999 }, "reused")
    values.insert(bucket_key { value: -3 }, "negative-reused")
    require_map(shared.length() == 2)
    require_map(shared.get(bucket_key { value: 999 }).unwrap() == "reused")
    require_map(values.remove(bucket_key { value: -3 }).unwrap() == "negative-reused")
    require_map(values.remove(bucket_key { value: 999 }).unwrap() == "reused")
    require_map(shared.is_empty())
    println("hashmap-clear-alias-reuse-ok")

    # The queue accepts a record with no Default implementation.
    let mut records: queue<bucket_key> = queue<bucket_key>.with_capacity(4)
    i = 0
    while i < 17
        records.enqueue(bucket_key { value: i as i64 })
        i = i + 1
    end while
    i = 0
    while i < 9
        require_map(records.dequeue().unwrap().value == (i as i64))
        i = i + 1
    end while
    i = 17
    while i < 50
        records.enqueue(bucket_key { value: i as i64 })
        i = i + 1
    end while
    i = 9
    while i < 50
        require_map(records.dequeue().unwrap().value == (i as i64))
        i = i + 1
    end while
    require_map(records.is_empty())
    println("queue-record-no-default-ok")
    return 0
end fn
