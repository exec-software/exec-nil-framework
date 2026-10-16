trait Reading<T>
    fn read(self) -> T
    fn replace(mut self, value: T) -> void
end trait

trait CopyValue
    fn copy_value(self) -> Self
end trait

struct Meter<T>
    value: T
end struct

impl<T> Reading<T> for Meter<T>
    fn read(self) -> T
        return self.value
    end fn

    fn replace(mut self, value: T) -> void
        self.value = value
    end fn

    fn helper(self) -> i32
        return 7
    end fn
end impl

impl<T> CopyValue for Meter<T>
    fn copy_value(self) -> Meter<T>
        return Meter<T> { value: self.value }
    end fn
end impl

fn main() -> i32
    let mut meter: Meter<i64> = Meter<i64> { value: 42 }
    println(meter.read())
    meter.replace(91)
    println(meter.read())
    let copy: Meter<i64> = meter.copy_value()
    println(copy.read())
    println(copy.helper())
    return 0
end fn
