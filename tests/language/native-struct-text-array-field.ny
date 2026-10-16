# Text arrays are exact typed fields, not opaque integer words.
struct text_array_record
    items: array<text>
end struct

fn main() -> i32
    let items: array<text> = ["value"]
    let value: text_array_record = text_array_record { items: items }
    return 0
end fn
