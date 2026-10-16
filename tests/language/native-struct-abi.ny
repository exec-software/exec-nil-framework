enum packet_state
    ready
    done
end enum

struct packet
    count: i32
    data: ptr<i32>
    samples: array<i32>
    label: text
    state: packet_state
end struct

fn make_packet() -> packet
    let data = allocate<i32>(2)
    data[0] = 40
    data[1] = 2
    return packet {
        count: 3,
        data: data,
        samples: [5, 6, 7],
        label: "native structs",
        state: packet_state.ready
    }
end fn

fn mutate_alias(value: packet) -> packet
    let alias = value
    alias.count = 9
    alias.data[1] = 5
    return alias
end fn

fn state_code(value: packet_state) -> i32
    match value
        packet_state.ready -> return 7
        packet_state.done -> return 8
    end match
    return 0
end fn

fn main() -> i32
    let original = make_packet()
    let returned = mutate_alias(original)

    println(original.label)
    println(original.count)
    println(returned.count)
    println(original.data[0] + original.data[1])
    println(returned.samples.len())
    println(returned.samples[2])
    println(state_code(returned.state))

    deallocate(original.data)
    return 0
end fn
