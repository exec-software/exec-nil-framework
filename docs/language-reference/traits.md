# Traits

Traits declare method contracts. Implementations use static dispatch; this is
not a dynamic trait-object or virtual-table ABI.

## Declaring and implementing a contract

```ny
trait Reading<T>
    fn read(self) -> T
    fn replace(mut self, value: T) -> void
end trait

struct Meter
    value: i64
end struct

impl Reading<i64> for Meter
    fn read(self) -> i64
        return self.value
    end fn

    fn replace(mut self, value: i64) -> void
        self.value = value
    end fn
end impl
```

Every required method must be present. Parameter and return types must match
after substituting the trait arguments and `Self`, including receiver
mutability and method generic bounds. Numeric assignment conversions do not
make different signatures equivalent: `i32` is not an implementation of an
`i64` parameter or result. Generic parameter names may differ when their
structure and bounds agree.

Extra concrete helper methods are allowed in an implementation. They do not
become requirements of the trait. Duplicate method names are rejected; Ny
does not select overloaded trait methods by signature.

After an implementation passes these checks, required methods that omit `pub`
inherit the trait's visibility. Required methods of an exported trait therefore
remain callable by consumers. Extra helper methods retain their declared access;
implementing an exported trait does not make private helpers public.

## Generic bounds

```ny
trait Fingerprint
    fn fingerprint(self) -> i64
end trait

struct Envelope<T>
    item: T
end struct

impl<T: Fingerprint> Envelope<T>
    fn item_fingerprint(self) -> i64
        return self.item.fingerprint()
    end fn
end impl
```

Bounds constrain which concrete types can instantiate generic code. Matching
generic implementations must respect the actual type arguments and their
bounds, not just the number of arguments. Native layout restrictions still
apply independently: a valid trait implementation does not by itself make
recursive layouts, scalar references, or nested array elements available on
the native route. Concrete nested structs and `ref<Struct>` handles are
supported within the [verified type subset](types.md).

Associated functions without a `self` parameter can also be called through a
bounded type parameter, such as `T.default()` when `T: Default`. The checker
validates the arguments and result against the bound; monomorphization selects
the concrete implementation. Multiple different bounds exposing the same
associated function are ambiguous and reject rather than choosing one by
declaration order. Repeating an identical bound does not introduce ambiguity.
Associated functions with their own method-level generic parameters are not
supported through this route and diagnose explicitly.

## Standard library contracts

The core library declares `Clone`, `Display`, `Debug`, `Default`, `Eq`, and
`Hash`. `Self` denotes the implementing type; for example, `Clone.clone`
returns `Self`, while `Eq.eq` and `Eq.ne` take another `Self`.

Collection interfaces spell out their complete method lists. `IList<T>`,
`ISet<T>`, and `IDictionary<K, V>` are independent contracts, **not** checked
subtraits of `ICollection<T>`. In particular, set insertion returns `bool`,
whereas `ICollection.add` returns `void`. An implementation of one does not
implicitly satisfy a bound naming another.

## Not implemented

Default method bodies, supertrait inheritance (`: Parent` or `where Self:`),
associated type declarations, and unknown trait-body constructs are rejected
rather than silently ignored. Put shared behavior in explicit implementation
methods or helper functions for now.

Dynamic trait objects, automatic operator-trait dispatch, generic default
arguments, and extension blocks are not part of the evidenced trait contract.
The existence of a similarly named method does not establish any of these
features. See the [release profile](../release/v1-profile.md)
for the current release limitations and route evidence.
