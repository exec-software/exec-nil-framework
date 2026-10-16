import ny.lang.collections
import ny.lang.core.*

struct Message
    text: text
end struct

struct Packet
    message: Message
    words: array<text>
end struct

enum Delivery
    Ready(Packet)
    Empty
end enum

let retained: text = "global" + "-text"

fn churn(count: i32) -> i32
    let mut index: i32 = 0
    let mut sum: i32 = 0
    while index < count
        let values: array<i32> = new_with_value<i32>(1024, index)
        sum = sum + get<i32>(values, 1)
        index = index + 1
    end while
    return sum
end fn

fn make_packet() -> Packet
    let item = Message { text: "payload" + "-text" }
    let words: array<text> = new_with_value<text>(2, "array" + "-text")
    return Packet { message: item, words: words }
end fn

fn choose(value: Packet, count: i32) -> Packet
    if count > 0 then
        churn(1600)
        return choose(value, count - 1)
    end if
    return value
end fn

fn main() -> i32
    let packet = make_packet()
    let alias = packet.message
    let delivery: Delivery = Delivery.Ready(choose(packet, 3))
    println(churn(1600))
    match delivery
        Delivery.Ready(value) -> {
            println(value.message.text)
            println(get<text>(value.words, 1))
        }
        Delivery.Empty -> println("wrong")
    end match
    println(alias.text)
    println(retained)
    let selected = if true then make_packet() else packet end if
    let converted: Result<i32, text> = Convert.try_to_i32("4" + "2")
    let invalid: Result<i32, text> = Convert.try_to_i32("bad" + "-text")
    churn(1600)
    println(selected.message.text)
    match converted
        Result.Ok(value) -> println(value)
        Result.Error(_) -> println("wrong")
    end match
    match invalid
        Result.Ok(_) -> println("wrong")
        Result.Error(message) -> println(message != "")
    end match
    return 0
end fn
