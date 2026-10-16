import ny.lang.collections
import ny.lang.core.*

struct test_key
    value: i64
end struct

impl Hash for test_key
    fn hash(self) -> i64
        return self.value ^ 3
    end fn
end impl

impl Eq for test_key
    fn eq(self, other: test_key) -> bool
        return self.value == other.value
    end fn

    fn ne(self, other: test_key) -> bool
        return not self.eq(other)
    end fn
end impl

impl Debug for test_key
    fn debug(self) -> text
        return "key"
    end fn
end impl

impl Default for test_key
    fn default() -> test_key
        return test_key { value: 7 }
    end fn
end impl

fn main() -> i32
    let mut values: vec<i32> = vec<i32>.new()
    values.push(10)
    values.push(20)
    values.push(30)
    println(values.length())
    println(values.get(1).unwrap())
    println(values.remove_at(1).unwrap())
    println(values.length())
    println(values.get(1).unwrap())

    let mut numbers: list<i32> = list<i32>.new()
    numbers.add(4)
    numbers.add(8)
    numbers.insert(1, 6)
    println(numbers.count())
    println(numbers.get_item(1).unwrap())
    println(numbers.remove(4))
    println(numbers.get_item(0).unwrap())

    let mut lookup: map<i32, i32> = map<i32, i32>.new()
    lookup.insert(5, 50)
    lookup.insert(6, 60)
    println(lookup.get(6).unwrap())
    println(lookup.remove(5).unwrap())
    println(lookup.length())

    let first: test_key = test_key { value: 1 }
    let second: test_key = test_key { value: 2 }
    let mut hashed: HashMap<test_key, i32> = HashMap<test_key, i32>.new()
    hashed.insert(first, 100)
    hashed.insert(second, 200)
    println(hashed.get(second).unwrap())

    let pair: key_value_pair<test_key, test_key> =
        key_value_pair<test_key, test_key> { key: first, value: second }
    println(pair.hash())
    println(pair.eq(pair))
    println(pair.ne(pair))
    println(pair.debug())
    println(test_key.default().value)
    let absent: Option<test_key> = Option.None
    let present: Option<test_key> = Option.Some(first)
    println(absent.unwrap_or_default().value)
    println(present.unwrap_or_default().value)
    let failed: Result<test_key, text> = Result.Error("missing")
    let succeeded: Result<test_key, text> = Result.Ok(second)
    println(failed.unwrap_or_default().value)
    println(succeeded.unwrap_or_default().value)
    return 0
end fn
