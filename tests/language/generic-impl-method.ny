struct Box<T>
    value: T
end struct

impl<T> Box<T>
    fn get(self: ref<Self>) -> T
        return self.value
    end fn
end impl

fn main() -> i32
    let box: Box<i32> = Box<i32> { value: 42 }
    println(box.get())
    return 0
end fn
