enum choice
    Flag(bool)
    Text(text)
end enum

struct sample
    value: i64
end struct

fn show_choice(selected: choice) -> void
    let payload: i64 = 88
    match selected
        choice.Flag(payload) -> println(payload)
        choice.Text(payload) -> println(payload)
    end match
    println(payload)
end fn

fn main() -> i32
    let record: sample = sample { value: 5 }
    if true then
        let record: sample = sample { value: 9 }
        record.value = 10
        println(record.value)
    end if
    println(record.value)
    show_choice(choice.Flag(true))
    show_choice(choice.Text("payload"))
    return 0
end fn
