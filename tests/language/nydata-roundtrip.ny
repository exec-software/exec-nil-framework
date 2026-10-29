import ny.lang.nydata.*

fn main() -> i32
    let source = "object root\nfield greeting\nhello\nend greeting\nend root\n"
    let value = import_nydata(source).unwrap()
    println(to_json(value).unwrap())
    println(loads(source).is_error())
    println(to_json(loads(dumps(value).unwrap()).unwrap()).unwrap())
    return 0
end fn
