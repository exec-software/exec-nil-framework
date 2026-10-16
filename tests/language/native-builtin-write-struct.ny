struct native_write_record
    value: i32
end struct

fn reject_write_struct(value: native_write_record)
    write_i32(value, 1)
end fn

fn main() -> i32
    let value: native_write_record = native_write_record { value: 0 }
    reject_write_struct(value)
    return 0
end fn
