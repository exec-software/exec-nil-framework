# Clean end of input, paired with the Matrix fixture. read_line faults at EOF;
# read_line_option answers None, so a reader can stop without failing.
module nil.tests.console_input_option

import nil.std.core
import nil.std.io

fn main() -> i32
    let mut count = 0
    loop
        match read_line_option()
            Option.None -> break
            Option.Some(line) ->
                count = count + 1
                writeln("line:" + line)
        end match
    end loop
    writeln("eof:" + count.to_text())
    return 0
end fn
