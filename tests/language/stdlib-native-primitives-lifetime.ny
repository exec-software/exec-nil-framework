# More than 4 MiB of temporary native text forces a managed collection. Live
# source strings, substrings in arrays, and the last return value must survive.
fn main() -> i32
    let chunk: text = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
    let doubled: text = chunk + chunk
    let quarter: text = doubled + doubled
    let source: text = quarter + quarter
    let retained: array<text> = [source.substr(4, 20)]
    let mut total: i64 = 0
    let mut i: i32 = 0
    while i < 10000
        let sample: text = source.substr(0, 512)
        total = total + sample.len()
        i = i + 1
    end while
    println(total)
    println(retained.get(0))
    println(source.substr(508, 10))
    return 0
end fn
