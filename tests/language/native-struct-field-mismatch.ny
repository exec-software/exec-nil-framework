struct wide_buffer
    data: ptr<i64>
end struct

fn main() -> i32
    let data = allocate<i32>(1)
    let value = wide_buffer { data: data }
    deallocate(data)
    return value.data[0]
end fn
