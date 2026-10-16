module effects
import ny.lang.collections
let private_seed: i32 = 17

pub struct FilterFault
    pixel: i32
end struct

pub abstract class Stage
    calls: i32
    pub abstract fn apply(self, pixel: i32) -> i32
    end fn
    pub abstract fn name(self) -> text
    end fn
    pub fn process(self, pixels: array<i32>) -> i64
        let mut index: i32 = 0
        let mut total: i64 = 0
        while index < length<i32>(pixels)
            let output = self.apply(get<i32>(pixels, index))
            array_set<i32>(pixels, index, output)
            self.calls = self.calls + 1
            total = total + (output as i64)
            index = index + 1
        end while
        return total
    end fn
    pub fn identity(self) -> Stage
        return self
    end fn
    pub fn passed_identity(self) -> Stage
        return forward(self)
    end fn
    pub fn local_self(self) -> i32
        let alias = self
        return alias.apply(4)
    end fn
    pub fn argument_self(self) -> i32
        return invoke(self, 5)
    end fn
    fn secret(self) -> i32
        return private_seed
    end fn
    pub fn read_secret(self) -> i32
        return self.secret()
    end fn
    pub virtual fn mark(self, value: i32) -> void
        self.calls = value
    end fn
    pub virtual fn weight(self, value: f64) -> f64
        return value + 1.0
    end fn
    pub virtual fn byte(self, value: u8) -> u8
        return value
    end fn
    pub virtual fn ready(self) -> bool
        return false
    end fn
end class

pub class Gain inherits Stage
    factor: i32
    label: text
    // An override stays virtual even when the optional marker is omitted.
    pub fn apply(self, pixel: i32) -> i32
        return pixel * self.factor
    end fn
    pub override fn name(self) -> text
        churn()
        return self.label
    end fn
    pub override fn mark(self, value: i32) -> void
        self.calls = value * 2
    end fn
    pub override fn weight(self, value: f64) -> f64
        return value + (self.factor as f64)
    end fn
    pub override fn byte(self, value: u8) -> u8
        return ((value as i32) + self.factor) as u8
    end fn
    pub override fn ready(self) -> bool
        return true
    end fn
end class

pub class FastGain inherits Gain
    bias: i32
    pub override fn apply(self, pixel: i32) -> i32
        return pixel * self.factor + self.bias
    end fn
end class

pub class Bias inherits Stage
    amount: i32
    pub override fn apply(self, pixel: i32) -> i32
        return pixel + self.amount
    end fn
    pub override fn name(self) -> text
        return "bias"
    end fn
end class

pub class Faulting inherits Stage
    pub override fn apply(self, pixel: i32) -> i32
        if pixel == 3 then
            raise FilterFault { pixel: pixel }
        end if
        return pixel + 10
    end fn
    pub override fn name(self) -> text
        return "faulting"
    end fn
end class

pub struct Pipeline
    first: Stage
    second: Stage
end struct

pub fn forward(value: Stage) -> Stage
    return value
end fn

pub fn invoke(value: Stage, pixel: i32) -> i32
    return value.apply(pixel)
end fn

pub fn choose(fast: bool) -> Stage
    if fast then
        return FastGain { calls: 0, factor: 3, label: "fast" + "-managed", bias: 1 }
    end if
    return Gain { calls: 0, factor: 2, label: "gain" + "-managed" }
end fn

pub fn churn() -> i32
    let mut index: i32 = 0
    while index < 1600
        let values: array<i32> = new_with_value<i32>(1024, index)
        index = index + 1
    end while
    return index
end fn

module shapes
pub class Left
    left: i32
    pub virtual fn left_value(self) -> i32
        return self.left
    end fn
end class

pub class Right
    right: i32
    pub virtual fn right_value(self) -> i32
        return self.right
    end fn
end class

pub class Both inherits Left, Right
    marker: i32
    pub override fn right_value(self) -> i32
        return self.right + self.marker
    end fn
end class

pub class RefReader
    code: i32
    pub virtual fn read(self: ref<Self>) -> i32
        return self.code
    end fn
    fn secret(self: ref<Self>) -> i32
        return 2
    end fn
    pub fn relay(self: ref<Self>) -> i32
        return self.read() + self.secret()
    end fn
end class

pub class RefChild inherits RefReader
    pub override fn read(self: ref<Self>) -> i32
        return self.code + 3
    end fn
end class

module names
pub class Value
    code: i32
    pub virtual fn read(self) -> i32
        return self.code
    end fn
end class

pub class value
    code: i32
    pub virtual fn read(self) -> i32
        return self.code + 1
    end fn
end class

module client
import ny.lang.collections
fn main() -> i32
    let pixels = new_with_value<i32>(4, 0)
    array_set<i32>(pixels, 0, 1)
    array_set<i32>(pixels, 1, 2)
    array_set<i32>(pixels, 2, 3)
    array_set<i32>(pixels, 3, 4)
    let stage: effects::Stage = effects::choose(false)
    let pipeline = effects::Pipeline {
        first: stage,
        second: effects::Bias { calls: 0, amount: 5 }
    }
    println(pipeline.first.process(pixels))
    println(stage.name())
    let alias = stage.identity()
    println(alias.calls)
    alias.calls = 40
    println(stage.calls)
    println(pipeline.second.process(pixels))
    println(pipeline.second.calls)
    println(stage.passed_identity().local_self())
    println(stage.argument_self())
    println(stage.read_secret())
    stage.mark(3)
    println(alias.calls)
    println(stage.weight(1.25))
    println(stage.byte(255 as u8))
    println(stage.ready())

    let mut selected: effects::Stage = stage
    selected = effects::choose(true)
    println(selected.process(pixels))
    println(selected.name())
    println(selected.calls)
    println(get<i32>(pixels, 3))
    // A field store uses the same checked ancestor conversion as a parameter.
    pipeline.first = effects::Bias { calls: 0, amount: 7 }
    println(pipeline.first.apply(5))

    let both = shapes::Both { left: 11, right: 22, marker: 3 }
    let left: shapes::Left = both
    let right: shapes::Right = both
    println(left.left_value())
    println(right.right)
    println(right.right_value())
    right.right = 30
    println(both.right)
    println(right.right_value())
    println(left.left)

    let upper = names::Value { code: 50 }
    let lower = names::value { code: 60 }
    println(upper.read())
    println(lower.read())

    let referenced = shapes::RefChild { code: 7 }
    let exact_reference = ref referenced
    println(exact_reference.read())
    println(exact_reference.relay())
    let parent: shapes::RefReader = referenced
    let parent_reference = ref parent
    println(parent_reference.read())
    println(parent_reference.relay())

    let broken: effects::Stage = effects::Faulting { calls: 0 }
    let input = new_with_value<i32>(3, 0)
    array_set<i32>(input, 0, 1)
    array_set<i32>(input, 1, 2)
    array_set<i32>(input, 2, 3)
    attempt
        println(broken.process(input))
        println("wrong-virtual-fault-return")
    fault effects::FilterFault as problem
        effects::churn()
        println(problem.pixel)
        println(broken.name())
    recover
        println(broken.calls)
        println(get<i32>(input, 1))
        println(get<i32>(input, 2))
        println("virtual-cleanup")
    end attempt
    return 0
end fn
