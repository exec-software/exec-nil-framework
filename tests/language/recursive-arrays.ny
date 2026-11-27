import nil.std.collections

struct RecursiveNode
    value: i32
    children: array<RecursiveNode>
end struct

struct RecursiveBox<T>
    value: T
    children: array<RecursiveBox<T>>
end struct

enum RecursiveList
    Empty
    Next(RecursiveList)
end enum

enum RecursiveLink
    Empty
    Next(RecursiveEnvelope)
end enum

struct RecursiveEnvelope
    value: i32
    link: RecursiveLink
end struct

fn make_recursive_ring() -> RecursiveNode
    let first = RecursiveNode { value: 17, children: [] }
    let second = RecursiveNode { value: 23, children: [] }
    first.children.push(second)
    second.children.push(first)
    return first
end fn

fn main() -> i32
    let root = make_recursive_ring()
    let retained = root.children[0]
    root.children[0].children[0].value = 31
    println(root.value)
    println(retained.value)
    let rows: array<array<RecursiveNode>> = [[root], []]
    rows[1].push(retained)
    rows[0][0].children[0].value = 37
    println(rows[1][0].value)

    let boxed = RecursiveBox<i64> { value: 4294967296, children: [] }
    boxed.children.push(boxed)
    println(boxed.children[0].children[0].value)

    let empty: RecursiveList = RecursiveList.Empty
    let first: RecursiveList = RecursiveList.Next(empty)
    let second: RecursiveList = RecursiveList.Next(first)
    let lists: array<RecursiveList> = [empty, first, second]
    match lists[2]
        RecursiveList.Empty -> panic("lost recursive variant")
        RecursiveList.Next(child) ->
            match child
                RecursiveList.Empty -> panic("lost recursive child")
                RecursiveList.Next(tail) ->
                    match tail
                        RecursiveList.Empty -> println("recursive-enum-base-ok")
                        RecursiveList.Next(_) -> panic("wrong recursive tail")
                    end match
            end match
    end match

    let envelope = RecursiveEnvelope { value: 41, link: RecursiveLink.Empty }
    envelope.link = RecursiveLink.Next(envelope)
    let links: array<RecursiveLink> = [envelope.link]
    match links[0]
        RecursiveLink.Empty -> panic("lost recursive object")
        RecursiveLink.Next(child) -> child.value = 43
    end match
    println(envelope.value)

    # More than 4 MiB of short-lived native arrays crosses a managed
    # safepoint while the only useful roots are the recursive object graphs.
    let mut index: i32 = 0
    let mut total: i64 = 0
    while index < 3000
        let garbage = new_with_value<i32>(512, index)
        total = total + (garbage[511] as i64)
        index = index + 1
    end while
    println(total)
    println(root.children[0].children[0].value)
    println(retained.value)
    println(boxed.children[0].value)
    match envelope.link
        RecursiveLink.Empty -> panic("collected recursive enum")
        RecursiveLink.Next(child) -> println(child.value)
    end match
    return 0
end fn
