module model
let offset: i32 = 3

pub class Base
    value: i32
    fraction: f64
    byte: u8
    pub virtual fn score(self) -> i32
        return self.value + offset
    end fn
    pub fn relay(self) -> i32
        return self.score() + 1
    end fn
    pub fn bump(self, amount: i32) -> i32
        self.value = self.value + amount
        return self.value
    end fn
    fn secret(self) -> i32
        return offset
    end fn
    pub fn read_secret(self) -> i32
        return self.secret()
    end fn
    pub fn fraction_matches(self, expected: f64) -> bool
        return self.fraction == expected
    end fn
    pub fn keep_base(self, other: Base) -> Base
        return other
    end fn
    pub fn make_base(self) -> Base
        return Base { value: 30, fraction: 0.5, byte: 7 as u8 }
    end fn
    pub fn pass_float(self, value: f64) -> f64
        return value
    end fn
    pub fn pass_byte(self, value: u8) -> u8
        return value
    end fn
    pub fn pass_word(self, value: i32) -> i32
        return value
    end fn
end class

pub class Middle inherits Base
    extra: i32
    pub override fn score(self) -> i32
        return self.value + self.extra
    end fn
end class

module other
pub class Base
    value: i64
    pub fn score(self) -> i64
        return self.value
    end fn
end class

module abstract_model
pub abstract class Reader
    pub abstract fn read(self) -> i32
    end fn
    pub fn relay(self) -> i32
        return self.read()
    end fn
end class

pub class Ready inherits Reader
    pub override fn read(self) -> i32
        return 41
    end fn
end class

pub abstract class Suspended inherits Ready
    pub abstract override fn read(self) -> i32
    end fn
end class

pub class Resumed inherits Suspended
    pub override fn read(self) -> i32
        return 84
    end fn
end class

module client
class Leaf inherits model::Middle
    label: text
end class

class ResumedLeaf inherits abstract_model::Resumed
end class

fn keep(value: Leaf) -> Leaf
    return value
end fn

fn main() -> i32
    let base = model::Base { value: 5, fraction: -2.5, byte: 511 as u8 }
    println(base.score())
    let object = Leaf { value: 5, fraction: 1.25, byte: 256 as u8, extra: 10, label: "leaf" }
    let alias = keep(object)
    println(object.score())
    println(object.relay())
    println(alias.bump(4))
    println(object.score())
    println(object.read_secret())
    println(object.fraction_matches(1.25))
    println(object.byte)
    object.byte = 511 as u8
    println(alias.byte)
    println(object.label)
    println(object.keep_base(base).score())
    println(object.make_base().score())
    println(base.pass_float(7 as f64) == 7.0)
    println(object.pass_float(7 as f64) == 7.0)
    println(base.pass_byte(511 as u8))
    println(object.pass_byte(-1 as u8))
    println(base.pass_word(4294967297))
    println(object.pass_word(4294967297))
    let separate = other::Base { value: 7000000000 }
    println(separate.score())
    let ready = abstract_model::Ready {}
    let resumed = abstract_model::Resumed {}
    let resumed_leaf = ResumedLeaf {}
    println(ready.relay())
    println(resumed.relay())
    println(resumed_leaf.relay())
    return 0
end fn
