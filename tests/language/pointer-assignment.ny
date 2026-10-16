module test_ptr_assign

pub struct Foo
    x: i32
    y: i32
end struct

pub function test_direct(p: *Foo)
    (*p).x = 42
end function

pub function test_via_let()
    let p = allocate<Foo>(1)
    (*p).x = 10
    (*p).y = 20
end function

pub function test_chain()
    let p = allocate<Foo>(1)
    let q = p + 0
    (*q).x = 99
end function
