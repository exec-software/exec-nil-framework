enum Signal
    Ready(i32)
end enum

fn signal_value(signal: Signal) -> i32
    match signal
        Signal.Ready(value) -> return value
    end match
end fn

fn main() -> i32
    let signal: Signal = Signal::Ready(9)
    match signal
        Signal.Ready(value) -> println(signal_value(signal) + value - value)
    end match
    return 0
end fn
