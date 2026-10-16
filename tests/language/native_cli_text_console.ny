module ny.tests.native_cli_text_console

import ny.lang.io.console

fn assembled_text() -> text
    let prefix = "native"
    let suffix = "-text"
    return prefix + suffix
end fn

fn main() -> i32
    let actual = assembled_text()
    let independently_built = "native-" + "text"

    ny::lang::io::write("stdout")
    ny::lang::io::writeln("-exact")

    if actual == independently_built then
        ny::lang::io::writeln("content-equal")
    else
        return 10
    end if

    if actual != "native-pointer" then
        ny::lang::io::writeln("content-different")
    else
        return 11
    end if

    ny::lang::io::write("concat:")
    ny::lang::io::writeln(actual + ":ok")
    ny::lang::io::write_error("stderr")
    ny::lang::io::write_error("-exact\n")
    return 0
end fn
