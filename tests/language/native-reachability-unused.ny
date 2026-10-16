struct dormant_byte_buffer
    bytes: array<u8>
end struct

struct dormant_file_record
    payload: dormant_byte_buffer
end struct

fn dormant_read_regular_bytes(path: text, max_bytes: i64) -> array<u8>
    // Value context leaves the undefined call for native reachability checks;
    // an unresolved call statement has no known discard contract and is rejected.
    return missing_dormant_provider(path, max_bytes)
end fn

fn main() -> i32
    println("native reachability ok")
    return 0
end fn
