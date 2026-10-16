@intrinsic("console.writeln")
fn out(value: text)
end fn

fn main() -> i32
    out("intrinsic wrapper")
    return 0
end fn
