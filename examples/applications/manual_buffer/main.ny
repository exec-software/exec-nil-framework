module manual_buffer

fn main() -> i32
    let count: i32 = 6
    let buffer: ptr<i32> = allocate<i32>(count)

    let mut index: i32 = 0
    while index < count
        buffer[index] = (index + 1) * 3
        index = index + 1
    end while

    let mut checksum: i32 = 0
    for offset in range(0, count)
        checksum = checksum + buffer[offset]
    end for

    println("MANUAL BUFFER")
    println(count)
    println(buffer[0])
    println(buffer[5])
    println(checksum)

    deallocate(buffer)
    return 0
end fn
