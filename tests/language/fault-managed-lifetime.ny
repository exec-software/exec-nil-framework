import ny.lang.collections
import ny.lang.core.*

struct Message
    text: text
end struct

struct PacketFault
    message: Message
    words: array<text>
end struct

fn churn() -> i32
    let mut index: i32 = 0
    while index < 1600
        let values: array<i32> = new_with_value<i32>(1024, index)
        index = index + 1
    end while
    return index
end fn

fn make_packet() -> PacketFault
    return PacketFault {
        message: Message { text: "managed" + "-fault" },
        words: new_with_value<text>(2, "array" + "-payload")
    }
end fn

fn raise_packet() -> i32
    raise make_packet()
    return -1
end fn

fn guarded(value: PacketFault) -> bool
    churn()
    return value.message.text == "managed-fault"
end fn

fn saved_return() -> PacketFault
    attempt
        return make_packet()
    recover
        churn()
        println("saved-return-cleanup")
    end attempt
    return make_packet()
end fn

fn main() -> i32
    attempt
        attempt
            raise_packet()
        fault as retained
            churn()
            attempt
                raise IoFault { message: "nested" + "-io", path: "unused", operation: "test" }
            fault IoFault as nested
                churn()
                println(nested.message)
            end attempt
            raise retained
        recover
            churn()
            println("suspended-cleanup")
        end attempt
    fault PacketFault as packet when guarded(packet)
        churn()
        println(packet.message.text)
        println(get<text>(packet.words, 1))
        let alias = packet.message
        packet.message.text = "mutated" + "-alias"
        println(alias.text)
    recover
        churn()
        println("outer-cleanup")
    end attempt
    let returned = saved_return()
    println(returned.message.text)
    println(get<text>(returned.words, 0))
    return 0
end fn
