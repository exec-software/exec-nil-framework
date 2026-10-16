trait FirstView
    fn first(self) -> i32
end trait

trait SecondView
    fn second(self) -> i32
end trait

struct Metric<T>
    value: T
end struct

impl<T> FirstView for Metric<T>
    fn first(self) -> i32
        return 10
    end fn
end impl

impl<T> SecondView for Metric<T>
    fn second(self) -> i32
        return 20
    end fn
end impl

fn main() -> i32
    let metric: Metric<i32> = Metric<i32> { value: 5 }
    println(metric.first())
    println(metric.second())
    return 0
end fn
