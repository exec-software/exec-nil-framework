import nil.std.collections
import nil.std.core.*

fn require_queue(value: bool) -> void
    if not value then panic("Queue ring contract failed") end if
end fn

fn main() -> i32
    let mut pending: queue<i32> = queue<i32>.with_capacity(8)
    let mut shared: queue<i32> = pending
    require_queue(pending.is_empty())
    require_queue(pending.dequeue().is_none())
    require_queue(pending.peek().is_none())
    require_queue(not pending.is_read_only())
    let empty_target: array<i32> = []
    pending.copy_to(empty_target, 0)

    let mut i: i32 = 0
    while i < 32
        pending.enqueue(i)
        i = i + 1
    end while
    i = 0
    while i < 19
        require_queue(shared.dequeue().unwrap() == i)
        i = i + 1
    end while
    require_queue(pending.count() == 13)
    i = 32
    while i < 80
        shared.add(i)
        i = i + 1
    end while
    require_queue(pending.peek().unwrap() == 19)
    require_queue(not pending.contains(0))
    require_queue(pending.contains(79))
    require_queue(shared.remove(37))
    require_queue(not pending.remove(999))
    require_queue(pending.count() == 60)
    let mut copied: array<i32> = new_with_value<i32>(62, -5)
    pending.copy_to(copied, 1)
    require_queue(copied[0] == -5 and copied[61] == -5)
    i = 19
    let mut offset: i32 = 1
    while i < 80
        if i != 37 then
            require_queue(copied[offset] == i)
            require_queue(pending.dequeue().unwrap() == i)
            offset = offset + 1
        end if
        i = i + 1
    end while
    require_queue(shared.is_empty())
    require_queue(shared.count() == 0)

    # A long steady-state FIFO must reuse the ring across many wraps.
    i = 0
    while i < 256
        pending.enqueue(i)
        i = i + 1
    end while
    i = 0
    while i < 2048
        require_queue(shared.dequeue().unwrap() == i)
        pending.enqueue(i + 256)
        i = i + 1
    end while
    require_queue(pending.count() == 256)
    i = 2048
    while i < 2304
        require_queue(pending.dequeue().unwrap() == i)
        i = i + 1
    end while
    pending.enqueue(42)
    shared.clear()
    require_queue(pending.is_empty() and not pending.contains(42))
    pending.enqueue(99)
    require_queue(shared.dequeue().unwrap() == 99)
    println("queue-growth-wrap-alias-ok")

    let mut words: queue<text> = queue<text>.new()
    words.enqueue("first")
    words.enqueue("é/😀")
    words.enqueue("first")
    require_queue(words.remove("first"))
    require_queue(words.peek().unwrap() == "é/😀")
    words.enqueue("tail")
    let mut output: array<text> = ["guard", "", "", "", "guard"]
    words.copy_to(output, 1)
    require_queue(output[0] == "guard" and output[4] == "guard")
    require_queue(output[1] == "é/😀" and output[2] == "first" and output[3] == "tail")
    require_queue(words.dequeue().unwrap() == "é/😀")
    require_queue(words.dequeue().unwrap() == "first")
    require_queue(words.remove("tail"))
    require_queue(words.is_empty())
    println("queue-text-remove-copy-ok")
    return 0
end fn
