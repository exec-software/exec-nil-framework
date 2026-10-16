struct recursive_record
    next: recursive_record
end struct

fn make_recursive(value: i32) -> recursive_record
    return value
end fn

fn main() -> i32
    make_recursive(0)
    return 0
end fn
