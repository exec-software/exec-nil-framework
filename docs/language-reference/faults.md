# Faults and cleanup

Ny uses `attempt`, `fault`, `recover`, and `raise` for synchronous, typed
language faults. This is distinct from fatal runtime safety errors.

```ny
struct ReadFault
    message: text
end struct

fn read_value() -> i32
    raise ReadFault { message: "input is unavailable" }
    return 0
end fn

fn main() -> i32
    attempt
        println(read_value())
    fault ReadFault as problem when problem.message != ""
        println(problem.message)
    recover
        println("cleanup")
    end attempt
    return 0
end fn
```

## Payloads and matching

Raise a fully initialized value of a concrete struct type. User-defined
structs and standard-library fault records follow the same rule; there is no
required `Fault` trait or inferred superclass. Scalars, class instances, enum
values, raw pointers, and values erased to `any` are not fault payloads.

The existing raw-memory layout restrictions still apply: dereferencing a raw
`ptr<Struct>` is not a cross-route way to obtain a payload. Native raw buffers
currently support scalar element layouts, and serialized fault validation
rejects payloads whose concrete type cannot be proven. Use ordinary struct
values, typed parameters/returns, or typed struct fields instead.

A typed handler matches the exact canonical struct identity, including module
and concrete generic arguments. Case and module punctuation are significant.
Aliases resolve to that identity. Equally
named records in different modules, or `Problem<i32>` and `Problem<text>`, do
not match each other. The library's `Fault` struct is not a universal catch.

Handlers are tried in source order. A guard must have type `bool`; a false
guard proceeds to the next handler. The binding is visible in its guard and
body, not in siblings or `recover`. Attempt-body locals are likewise not
visible to handlers or cleanup. Declare cleanup resources outside the attempt.

`fault as token` catches any language fault. This is an opaque token, not an
unchecked object: it may be ignored or used as the complete expression of
`raise token`. Field access, printing, copying, casting, returning, or passing
the token to another function is rejected. A retained token can be rethrown
more than once if an inner attempt handles each rethrow. An unguarded catch-all
must be the final handler.

Faults preserve the existing shared struct-object identity. Handler mutation
is visible through surviving aliases; raising does not deep-copy, transfer
ownership, or establish borrowing guarantees.

## Cleanup and replacement exits

`recover` is cleanup, analogous to `finally`, not an additional catch handler.
It runs once when leaving the attempt normally, after a selected handler, on
unmatched propagation, or through a return or loop exit. Nested cleanup runs
innermost first. Breaking or continuing a loop contained entirely inside the
attempt does not leave that attempt or prematurely run its cleanup.

The return expression is evaluated and saved before cleanup begins. Cleanup
uses its lexical scope and loop targets, not an inner body's shadowed names.
A return, break, continue, or new fault from `recover` replaces the suspended
exit. This can deliberately suppress an earlier fault or return value. The
current cleanup is not run recursively; outer cleanup still applies.

A new fault in a guard or handler is not caught by sibling handlers of the
same attempt. That attempt's cleanup runs, then outer handlers may catch the
new fault. Successful helper calls made during a guard or cleanup do not
accidentally re-propagate the suspended fault.

Unwinding preserves managed payloads and saved return values, including through
collection in helper calls. It discards unfinished expression operands from
the interrupted call. It does **not** close aliased files or release every raw
buffer automatically. Explicit cleanup releases explicit resources; managed
collection and manual-memory mode retain their separate lifetime contracts.

## Boundary and diagnostics

Uncaught faults terminate the program with exit status 1 and a diagnostic of
the form `Ny uncaught fault: Problem at line 5`. Rethrowing a catch-all token
preserves the original raise location and type. Initializer faults prevent
`main` from executing.

Division-by-zero and other runtime safety traps, invalid NIL, allocation
failure, panic, and explicit process exit remain fatal and are not intercepted
by `fault` or `recover`. Foreign-language exceptions, cross-thread propagation,
task cancellation, postfix `?`, and automatic destructors are not implemented
by this synchronous contract. Compile native programs with the matching SDK
runtime archive; older SDKs do not implement the new fault operations.

The execution and boundary checks live in the `fault-*` language fixtures,
the source fault contract tests, and the NIL/VM/native runtime fault tests.
