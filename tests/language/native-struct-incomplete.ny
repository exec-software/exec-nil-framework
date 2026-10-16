struct native_record
    count: i32
    label: text
end struct

fn main() -> i32
    let value = native_record { count: 1 }
    return value.count
end fn
