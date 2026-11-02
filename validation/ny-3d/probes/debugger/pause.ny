# An actual running interpreter loop, stopped through the public DAP protocol.
fn main() -> i32
    let mut value: i64 = 0
    while true do
        value = value + 1 # DBG_PAUSE_LOOP
    end while
    return 0
end fn
