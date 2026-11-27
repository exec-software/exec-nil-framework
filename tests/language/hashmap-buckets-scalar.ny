import nil.std.collections
import nil.std.core.*

impl Hash for i64
    fn hash(self) -> i64
        if self < 0 then return -9223372036854775807 - 1 end if
        return self % 37
    end fn
end impl

impl Eq for i64
    fn eq(self, other: i64) -> bool
        return self == other
    end fn

    fn ne(self, other: i64) -> bool
        return not self.eq(other)
    end fn
end impl

fn require_map(value: bool) -> void
    if not value then panic("HashMap bucket contract failed") end if
end fn

fn main() -> i32
    let mut values: HashMap<i64, text> = HashMap<i64, text>.new()
    let mut shared: HashMap<i64, text> = values
    require_map(values.is_empty())
    require_map(values.get((1 as i64)).is_none())
    let mut i: i32 = 0
    while i < 257
        values.insert((i as i64 as i64), "value")
        i = i + 1
    end while
    require_map(shared.length() == 257)
    i = 0
    while i < 257
        require_map(shared.get((i as i64 as i64)).unwrap() == "value")
        i = i + 1
    end while
    values.insert((111 as i64), "replacement")
    require_map(shared.length() == 257)
    require_map(shared.get((111 as i64)).unwrap() == "replacement")
    values.insert((-1 as i64), "negative-one")
    values.insert((-2 as i64), "negative-two")
    require_map(shared.get((-1 as i64)).unwrap() == "negative-one")
    require_map(shared.get((-2 as i64)).unwrap() == "negative-two")
    require_map(shared.get((-3 as i64)).is_none())

    # Remove from collision chains and the beginning/middle/end of dense storage.
    require_map(values.remove((0 as i64)).unwrap() == "value")
    require_map(shared.remove((111 as i64)).unwrap() == "replacement")
    require_map(values.remove((256 as i64)).unwrap() == "value")
    require_map(values.remove((-1 as i64)).unwrap() == "negative-one")
    require_map(values.remove((-1 as i64)).is_none())
    require_map(shared.length() == 255)
    require_map(values.keys.get(0).unwrap() == 1)
    require_map(values.keys.get(110).unwrap() == 112)
    require_map(values.keys.get(254).unwrap() == -2)
    i = 1
    while i < 256
        if i != 111 then
            require_map(values.contains_key((i as i64 as i64)))
        end if
        i = i + 1
    end while
    require_map(values.get((-2 as i64)).unwrap() == "negative-two")
    println("hashmap-growth-collisions-replacement-remove-ok")

    shared.clear()
    require_map(values.is_empty())
    require_map(values.keys.is_empty() and values.vals.is_empty())
    require_map(values.get((37 as i64)).is_none())
    values.insert((999 as i64), "reused")
    values.insert((-3 as i64), "negative-reused")
    require_map(shared.length() == 2)
    require_map(shared.get((999 as i64)).unwrap() == "reused")
    require_map(values.remove((-3 as i64)).unwrap() == "negative-reused")
    require_map(values.remove((999 as i64)).unwrap() == "reused")
    require_map(shared.is_empty())
    println("hashmap-clear-alias-reuse-ok")
    return 0
end fn
