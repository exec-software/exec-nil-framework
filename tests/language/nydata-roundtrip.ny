import ny.lang.nydata.*

fn main() -> i32
    let source = "object root\nfield greeting\nhello\nend greeting\nend root\n"
    let value = loads(source)
    println(dumps(value, "root", 0))
    return 0
end fn
