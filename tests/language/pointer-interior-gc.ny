import ny.lang.runtime.*

fn make_interior_pointer() -> ptr<i32>
    let base: ptr<i32> = allocate<i32>(2)
    base[1] = 731
    return base + 1
end fn

fn make_one_past_pointer() -> ptr<i32>
    let base: ptr<i32> = allocate<i32>(2)
    base[1] = 842
    return base + 2
end fn

fn main() -> i32
    # The callee frame (and its base pointer) is gone before collection. The
    # aligned interior pointer is therefore the allocation's only live root.
    let interior = make_interior_pointer()
    collect(2)
    println(*interior)

    # A logical one-past pointer is also a valid live pointer value. It must
    # retain its allocation while it is the only root so subtraction can walk
    # back to a dereferenceable element after collection.
    let one_past = make_one_past_pointer()
    collect(2)
    println(*(one_past - 1))
    return 0
end fn
