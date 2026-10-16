module stage.five

abstract class reader
    level: i64
    pub abstract fn read_level(self) -> i64
    end fn
    pub fn relay(self) -> i64
        return self.read_level() + 1
    end fn
    pub fn label(self) -> text
        return "reader"
    end fn
end class

class constant_reader inherits reader
    pub override fn read_level(self) -> i64
        return self.level
    end fn
    fn scaled(self) -> i64
        return self.level * 2
    end fn
    pub fn doubled(self) -> i64
        return self.scaled()
    end fn
end class

class offset_reader inherits constant_reader
    offset: i64
    pub override fn read_level(self) -> i64
        return self.level + self.offset
    end fn
end class

fn consume(source: reader) -> i64
    return source.relay()
end fn

trait reading
    fn current(self) -> i64
    fn replace(mut self, amount: i64) -> void
end trait

struct meter
    value_held: i64
end struct

impl reading for meter
    fn current(self) -> i64
        return self.value_held
    end fn
    fn replace(mut self, amount: i64) -> void
        self.value_held = amount
    end fn
    fn doubled(self) -> i64
        return self.value_held * 2
    end fn
end impl

struct counter
    tally: i64
end struct

impl counter
    fn increment(self) -> i64
        return self.tally + 1
    end fn
end impl

fn main() -> i32
    let concrete = constant_reader { level: 41 }
    println(concrete.read_level())
    println(concrete.relay())
    println(concrete.doubled())

    let viewed: reader = concrete
    println(consume(viewed))
    println(viewed.label())

    let offset = offset_reader { level: 40, offset: 2 }
    let as_reader: reader = offset
    println(as_reader.read_level())

    let mut gauge = meter { value_held: 20 }
    println(gauge.current())
    gauge.replace(42)
    println(gauge.current())
    println(gauge.doubled())

    let tally = counter { tally: 41 }
    println(tally.increment())
    return 0
end fn
