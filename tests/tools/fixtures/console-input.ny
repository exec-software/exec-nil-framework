module nil.tests.console_input

import nil.std.io

fn receive() -> text
    return read_line()
end fn

fn main() -> i32
    write("input> ")
    flush()
    let line = receive()
    writeln(line)
    return 0
end fn
