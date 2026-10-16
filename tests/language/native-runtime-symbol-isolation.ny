import ny.lang.io.console

fn fputs(value: i32) -> i32
    return value + 1
end fn

fn main() -> i32
    println(fputs(6))
    ny::lang::io::writeln("runtime-ok")
    return 0
end fn
