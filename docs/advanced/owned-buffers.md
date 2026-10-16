# Owned buffers and lexical borrowing

Ny's explicit ownership feature is an opt-in affine buffer model. It is not a
new name for garbage collection, and does not change existing `ref<T>` shared
aggregate handles. Its initial contract covers local buffers of `i32`, `i64`,
and `u8`. It does not claim general Rust-style ownership of every Ny value.

```ny
let mut samples = owned<i32>(64)
samples[0] = 42

if true then
    let read_only = owned_borrow(samples)
    println(read_only[0])
end if

if true then
    let writable = owned_borrow_mut(samples)
    writable[0] = 43
end if

let final_samples = owned_move(samples)
println(final_samples[0])
```

`owned<T>(count)` creates zero-initialized storage. Like the existing raw
allocation operation, a count below one requests one element; oversized
allocations fail. Counts are `i32` or `i64` and element types are exact.
`owned<T>`, `borrow<T>`, and `borrow_mut<T>` can be written explicitly as local
type annotations, including through a type alias.

An owner is not copyable. `owned_move(owner)` transfers it into a new local
declaration and invalidates the source binding. Source and destination never
both own the allocation. Owners cannot be rebound by assignment. Mutating an
owned buffer requires `let mut`; a move destination chooses its own binding
mutability. Indexing and dereference read scalar values, which can be copied
normally.

`owned_borrow(owner)` creates an immutable view. Multiple immutable views may
coexist and the owner may still be read, but writes and moves are forbidden
until the views' lexical scopes end. `owned_borrow_mut(owner)` requires a
mutable owner and gives one exclusive view. Other reads, writes, moves, and
borrows of the owner are forbidden while that view exists. Mutability belongs
to the view type, so the view binding itself need not say `mut`.

Lifetimes are lexical, not last-use inferred: leaving a view's block ends its
borrow. A buffer is freed exactly once when its current owner's scope exits.
This includes ordinary block exit, return, break, continue, and structured
Ny fault unwinding. A move does not free storage. Native cleanup uses the same
buffer allocator as raw pointers but has separate thread-local ownership
bookkeeping; interpreter cleanup removes the exact corresponding heap block.

Control-flow checking is conservative. If either conditional branch moves an
owner, later use is rejected. A loop or fault region may allocate and move its
own local owners, but cannot consume an owner declared outside that region.
This prevents repeated moves and fault paths from reviving a moved owner.

The initial feature is deliberately local-only: ownership operations must be
direct local declaration initializers. Owners and views cannot be parameters,
return values, globals, aggregate fields, array elements, closure captures,
or values erased to `any`. Passing them to arbitrary functions, casting them,
taking raw addresses of their storage, using pointer arithmetic, or manually
deallocating them is rejected. Read a scalar to pass it elsewhere. Borrowed
helper parameters, owning aggregate fields, custom destructors, and returning
owners need an extended interprocedural lifetime contract and are not part of
this version.

The semantic proof is required by lowering even under `--no-typecheck`.
Disabling diagnostic enforcement does not authorize resource leaks or aliasing.
