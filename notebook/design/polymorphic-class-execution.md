# Polymorphic class execution contract

Status: implemented and integration-verified. Recorded 2026-09-26. Qualification
evidence and remaining boundaries are tracked with the implementation.

## Source contract

Plain nongeneric class values are shared object handles. A derived value may
be converted to a declared ancestor view in an explicitly typed local,
assignment, parameter, return or field. The view preserves the dynamic class
and shared storage; there is no slicing. Downcasts, unrelated conversions and
container/reference covariance remain rejected. Mixed inferred class joins
must not depend on operand order: use an explicit base-typed binding until a
separate common-ancestor inference rule exists.

The static receiver determines member visibility and call signatures. Virtual
and abstract slots dispatch to the nearest concrete override of the dynamic
class. Virtualness propagates through unmarked overrides. Nonvirtual calls
select the declaring owner's body; a virtual call on self inside that body
still dispatches dynamically. Abstract values may be passed and stored, but
abstract classes cannot be constructed. Nonreceiver parameter and return types
are invariant across overrides.

Emit concrete method bodies once for their declaring owner, with self typed
as that owner; do not clone a base body while relabeling explicit Base values
as Derived. This also permits inherited self aliases, arguments and returns.
Methods retain their declaring module and private-access scope.

Existing unambiguous inheritance layouts may retain multiple parents: field
access uses actual dynamic layouts, never a presumed first-base prefix. Existing
ambiguity/cycle checks remain required. Generic classes, dynamic trait objects,
first-class bound method values, class globals/aggregate arrays beyond their
existing storage contracts, destructors and ownership checking are separate.

## Serialized and execution contract

Keep ordinary struct layouts and add explicit class declarations recording
canonical name, abstractness, direct bases and declared methods. Each method
records its name, virtual/abstract flags, concrete implementation symbol (empty
when abstract), nonreceiver parameter types and exact result type. Resolve
inherited declarations with an unambiguous nearest-declaration rule.

Add two opcodes without renumbering existing operations:

- `CLASS_UPCAST target`: consume a proven class handle and produce its ancestor
  view. Identity views are permitted; unrelated, downcast and pointer casts are
  not. No allocation or payload copying occurs.
- `CALL_METHOD static_class method argc`: argc includes self. Validate the
  static signature. Virtual slots resolve from immutable dynamic identity;
  nonvirtual methods select the static declaring owner's implementation.
  Receiver adaptation at this call boundary preserves inherited reference
  methods without enabling general reference covariance. Never accept a
  NIL-supplied host function pointer.

Class-bearing executable/library containers use an explicitly new versioned
class metadata encoding. Preserve existing v1/v2/v3 readers for artifacts that
do not carry this contract; do not silently reinterpret v3 structs as classes.
Textual declarations carry the same information. All consumers must agree.

The VM retains dynamic identity separately from a checked static class view,
and keeps shared field storage. Native objects have an internal immutable
dynamic-class tag preceding the exact concrete field layout. Native base-field
access dispatches to that concrete layout. Virtual calls use a closed-module
switch of checked direct calls. Every branch participates in ordinary fault
propagation and managed-root instrumentation. Default/invalid tags fail closed.

Shared NIL validation checks unique schemas, known acyclic ancestry, flattened
fields, compatible overrides, abstract obligations, target signatures and
receiver provenance. Validate upcasts, virtual calls, class arguments/returns,
field writes and joins; reject fabrication through CAST, arithmetic, raw
pointers or untyped unknown values. Class values remain ineligible fault
payloads. Raw VM embedding without the required class metadata must reject new
class instructions rather than guess a registry from names.

## Evidence gate

Require positive interpreter, binary interpreter, bundled, JIT, AOT and split
assembler/linker tests. Cover multi-level overrides, abstract receivers,
nonvirtual helpers, self escape, alias mutation through base views, inherited
and non-first-base fields, qualified/case-distinct identities, base-valued
parameters/returns/nested fields, GC pressure and faults from overrides.
Negative source and malformed text/binary NIL cases must preserve prior output
artifacts. Existing concrete-class and fault matrices must remain green.

Record actual limits and integrated/installed SDK evidence in a dated follow-up
before advertising this contract as implemented. This is one RC prerequisite,
not a declaration that ownership, concurrency, source DAP or AI context is done.
