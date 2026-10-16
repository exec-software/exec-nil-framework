struct left_record
    value: i32
end struct

struct right_record
    value: i32
end struct

fn read_right(value: right_record) -> i32
    return value.value
end fn

fn main() -> i32
    let value = left_record { value: 7 }
    return read_right(value)
end fn
