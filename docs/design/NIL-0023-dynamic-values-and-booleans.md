# NIL-0023: Dynamic values and Boolean vocabulary

Status: Implemented

## Purpose

NIL lets programs work with values before choosing a machine representation,
while retaining explicit control over concrete types where it matters. Dynamic
storage must preserve that contract: every allocated language value has a valid
initial value, and operations must explain why a value cannot be used as requested.
Absence remains an explicit `Option` value rather than a null reference.

## Dynamic storage

`anything` stores a concrete value and its canonical type. A mutable binding may
be reassigned a value of another type. Boxing creates an immutable container;
copying a box and subsequently reassigning one binding does not change the other
binding's stored type. Ordinary sharing rules still apply to the contained value.

Assignments, arguments, returns, fields, and explicitly typed `array<anything>`
literals box their values at a checked boundary. An `anything` binding needs an
initializer. An empty dynamic array is valid; a nonempty allocation needs valid
initial elements. Neither an uninitialized box nor a zero pointer is a language
default for `anything`.

`anything_is<T>(value)` tests the exact canonical type. `value as T` extracts that
type and faults with the expected and actual types on mismatch. The standard
library's `try_as<T>(value)` returns `Option<T>` for callers that expect a mismatch.
In Matrix these are `anything-is of T with value`, `value becomes T`, and
`try-as of T with value`. Extraction does not parse text, narrow a number, or
perform an implicit class downcast. Those operations follow extraction and use
the existing explicit conversion APIs.

Member access, calls, and arithmetic require a recovered concrete type. A dynamic
box does not grant unchecked access to a foreign value. Boxes cannot erase affine
ownership, borrowed slices, caller reference cells, or other internal values.
Raw pointers retain their existing lifetime and unsafe-access obligations; a box
does not extend a manually managed allocation's lifetime. A boxed class retains
its checked static view rather than manufacturing access to a derived class.

Native and interpreted execution share dedicated `BOX_ANY`, `UNBOX_ANY`, and
`IS_ANY_TYPE` instructions. Loaders validate descriptors and value provenance;
runtime checks validate payloads before use. Native boxes belong to the runtime
context and participate in managed-memory tracing. Existing backend limitations,
including native source function values, remain explicit diagnostics.

Static inference and aliases are separate. `let value = 1` and Matrix
`value count is 1` infer one fixed type. Ny `type Count = i64` and Matrix
`Count means override long` name one compile-time type. The internal `any`
spelling is not an alias for the checked dynamic box.

## Boolean values

`bool`, `boolean`, and `flag` name the same two-valued type. `true` and `yes`
represent true; `false` and `no` represent false. Matrix retains its usual
case-insensitive keywords. `flag` is contextual in type positions so existing
variables and parameters named `flag` remain valid.

Numeric Boolean conversions accept exactly zero and one, including floating
zero and one. Negative zero is zero. Other numbers, NaN, and infinity are errors.
Known invalid literal assignments fail during checking; dynamic conversions and
storage boundaries validate before mutation. Boolean operators remain Boolean
operations; this change does not introduce general numerical truthiness.
`Option<bool>` represents an absent Boolean without adding a third Boolean value.

The new literals also apply to annotations, patterns, and source preprocessor
conditions. NILData and JSON retain their independent serialization grammars.

## Validation

Source contracts cover Ny and Matrix through textual and binary NIL, interpreter
bundles, native JIT, and native executables. Invalid casts, malformed NIL, storage
boundaries, initialized dynamic arrays, and ownership restrictions require
negative tests. Editor tests cover contextual type names and literal inference.
