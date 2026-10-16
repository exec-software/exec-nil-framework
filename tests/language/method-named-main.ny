struct Tool
    value: i32
end struct

impl Tool
    fn main(self: ref<Self>) -> i32
        return self.value
    end fn
end impl

fn main() -> i32
    let tool: Tool = Tool { value: 42 }
    println(tool.main())
    return 0
end fn
