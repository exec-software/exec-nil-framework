# Classes, ancestor views and method dispatch

Nongeneric class values are shared handles. Assigning a derived value to an
explicit ancestor type changes its static view, not its dynamic class or field
storage. There is no slicing or implicit copy.

```ny
abstract class Reader
    pub abstract fn read(self) -> i32
    end fn
    pub fn relay(self) -> i32
        return self.read() + 1
    end fn
end class

class ConstantReader inherits Reader
    value: i32
    pub override fn read(self) -> i32
        return self.value
    end fn
end class

fn consume(reader: Reader) -> i32
    return reader.relay()
end fn

fn main() -> i32
    let concrete = ConstantReader { value: 41 }
    let reader: Reader = concrete
    println(consume(reader))
    concrete.value = 99
    println(reader.read())
    return 0
end fn
```

The output is `42`, then `99`: `Reader.relay` retains its declaring-owner scope,
but its virtual `self.read()` call selects `ConstantReader.read`.

## Static access, dynamic implementation

The static view controls available fields, method visibility and signatures.
It does not expose derived-only members. Public methods are callable by
consumers; private methods remain restricted to the declaring class. Field
access currently remains public. Class names preserve exact module and case
identity.

Virtual and abstract methods select the nearest effective implementation on
the dynamic class. An override remains virtual even if its optional `override`
marker is omitted. A nonvirtual method selects the statically resolved
declaring-owner body. Bodies keep their declaring module, private access and
owner-typed `self`; they may alias, pass or return self according to that type.
Explicit Base-valued parameters and returns never become Derived merely
because a derived object invokes an inherited method.

Abstract classes may be parameter, return, field or local types, but cannot be
constructed. A nearer abstract override removes an older concrete
implementation; a concrete descendant must satisfy all effective abstract
obligations. Override signatures and visibility remain checked.

Ancestor conversions are available at explicit typed local, assignment,
argument, return and field boundaries. Downcasts and unrelated conversions
reject. Mixed-class inferred conditional, match or array element joins do not
infer a common ancestor: establish an explicit ancestor-typed value first.
Generic containers, pointers and references remain invariant.

An explicitly reference-shaped receiver (`self: ref<Self>`) requires the
corresponding reference receiver. Calling an inherited method adapts self only
at that checked call boundary; this does not permit assigning `ref<Derived>`
to a `ref<Base>` variable. Plain-class ancestor views and references to those
views retain the object's dynamic identity.

Every construction initializes every inherited and local field exactly once.
Unambiguous multiple inheritance uses the actual concrete field layout, not
an assumed first-base prefix. Cycles, ambiguous members, incompatible fields,
reduced visibility and hiding a nonvirtual method reject.

## Lifetime, faults and execution boundary

Methods use the same managed roots and synchronous fault protocol as ordinary
calls. A virtual target's fault is checked before its result is used. Aliases
remain aliases through collection, inherited calls and ancestor conversions;
no destructor or ownership-transfer behavior is implied.

NIL carries explicit class declarations and checked `CLASS_UPCAST` and
`CALL_METHOD` operations. Native class objects have an internal dynamic tag;
dispatch uses a closed set of validated direct-call targets. This is not a
foreign C++ ABI or a publicly writable vtable. Match tools from the same SDK
when consuming class-bearing NIL.

Generic class execution, generic method execution, first-class bound methods,
trait objects, dynamic loading, ownership/borrowing, automatic destructors and
broader aggregate global/array storage are not supplied by this contract.
The `class-polymorphism` pipeline fixture checks ordinary and reference
receivers, aliases, typed boundaries, multiple inheritance, collection and
faults alongside targeted source and malformed-NIL tests.
