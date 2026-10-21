let mut tally: i64 = 0

fn next() -> i64
    tally = tally + 1
    return tally
end fn

fn render<T>(value: T) -> text
    return $"value={value}"
end fn

fn main() -> i32
    let value: f64 = -1.6
    let items = [7, 11]
    println($"position={value}; next={value + 0.5}")
    println($"bool={true}/{false}; byte={255 as u8}")
    println($"min={-9223372036854775808}; max={9223372036854775807}")
    println($"zero={0.0}/{-0.0}; small={1.6e-3}")
    println($"precise={1.2345678901234567}")
    println($"{{literal}} and {{{items[1]}}}")
    println($"{next()}:{next()}:{next()}")
    println(tally)
    println($"{"quoted"} and {$"nested {items[0]}"}")
    println($"" == "")
    println($"ordinary text")
    println($"{if true then 42 else 0 end if}")
    println(render(17))
    println(render(1.25))
    println(render("text"))
    println("legacy={value}")
    return 0
end fn
