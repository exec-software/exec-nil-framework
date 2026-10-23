# Executable NIL format and compatibility

NIL is the shared stack-machine intermediate representation for Ny and Matrix. Text and binary
executables carry the same schemas, function declarations, constants and
instructions in the current compiler. They are inputs to `nilinterpreter` and
`nilassembler`; the latter emits an ordinary host object for `nillinker`.

## Version boundary

| Artifact | Version / flags | Contract |
| --- | --- | --- |
| Ordinary binary executable | 3 / 0 | Schemas, function annotations/flags/source line, signatures, instructions, constants, module/test registry |
| Class-bearing executable | 4 / 0 | V3 records plus explicit class ancestry and method declarations |
| Source-debug executable | 5 / 0 | V4 records plus exact per-instruction source and lexical variable maps; class section may be empty |
| Legacy binary executable | 1 / 0 | No schemas or function annotations; native assembly rejects operations requiring those omitted records |
| Binary library | 2 / 1 | Compiled bodies plus checked embedded Ny source interfaces; imports currently re-check/regenerate bodies |
| Class-bearing library | 4 / 1 | V3 records, embedded sources, and class declarations; the same source-bootstrap import contract |

Version numbers describe file layouts, not language or release versions.
The optional type-annotation flag (`2`) adds annotations to schema records;
it combines with the library flag (`1`). For example, generated tuple schemas
carry `@nil_tuple`, so an ordinary tuple executable uses version/flags `3 / 2`.
Readers reject unknown versions/flags. Current compilers emit executable v3
without classes and v4 when class metadata is present. `--debug-info` selects
v5 executable output independently of class use. Older SDKs cannot read
the new contract and must be rebuilt together with their tools. V1
executables remain readable subject to the current stack contract: regenerate
older programs containing redundant void-call `POP` instructions.

Libraries are not independently linkable native objects. The assembler rejects
libraries of either version; a native executable is currently compiled as one resolved module.
Retaining `@ai_context` in NIL is independent of opt-in native
[executable context](executable-context.md) embedding. Neither annotation
retention nor that section implements source-level debugger maps.

Tuples use ordinary typed record schemas with canonical `tuple<T1,T2,...>`
names and ordered numeric fields. See [NIL-0015](../design/NIL-0015-structural-tuples.md).
Formatted text lowers to `STR_CONCAT` and `TO_STRING <scalar-type>`; the
zero-operand legacy `TO_STRING` remains readable. The explicit type distinguishes
boolean formatting from integer formatting and preserves native/VM parity.
See [NIL-0014](../design/NIL-0014-formatted-text.md) for supported values and
conversion rules.

Text NIL preserves function declaration lines with a reserved
`# declaration_line N` comment immediately before the function header, after
its annotations. `N` is unsigned decimal in `0..2147483647`; zero or absent
metadata means unknown. Both readers reject duplicate, orphan, malformed and
in-function declaration records. Ordinary comments/blank lines may intervene,
but the next substantive record must be a function header. Old readers ignore
the comment; the unchanged header remains compatible and no binary version
change is required. This single line number does not identify a source file.

## Opt-in source debugging

Source file paths and lexical bindings are retained only with `--debug-info`.
They may reveal private project layout, are unauthenticated metadata, and are
not permissions to read a host file. Existing `@ai_context` retention and
native context embedding remain independent of this switch.

V5 executable NIL has sections 1–5 and 7 with the v4 encoding, plus section 8.
Section 7 may contain zero classes in v5. Section 8 starts with `u32 function_count`
exactly matching section 2. For each function in order: `u32 instruction_count`
exactly matching its body, then for each instruction a source-path string,
`u32 local_count`, and that many records of name, storage, type strings followed
by `u32 lexical_depth`. Empty source paths require zero locals. Strings are
bounded to 16 KiB each, valid UTF-8 and free of ASCII control bytes; at most
1024 locals and scope depth 1024 are accepted.
Visible names and storage identities must be unique per instruction. All
records must consume the section exactly. Libraries remain v2/v4, not v5.

Text NIL uses a reserved comment immediately before a mapped instruction:

```text
# source_debug 12 "/project/main.ny" 1 "value" "$ny.local.1.value" "i32" 2
```

The first number is the zero-based instruction index within the function;
the second is the local count. Each local contributes name, storage, type and
depth. Indices, record placement, string/count bounds and unique bindings are
validated by both text readers. Missing records mean no source location.
The instruction's existing debug line supplies the source line. Scope maps
describe the next instruction, not a second execution state; consumers must
also check whether a storage slot currently has a value.

`nildebugger --dap` uses these maps for source breakpoints, stepping and named
locals. Legacy artifacts remain usable in artifact-level debugger modes, but
cannot satisfy a source-debugging request. Native DWARF and attach debugging
are not implemented.

Ordinary function annotations retain their `@name(...)` text records. Annotations
with line controls or trailing whitespace use
`# function_annotation "<NIL-escaped exact annotation>"` instead, so text and
binary NIL preserve the same bytes. The record requires exactly one quoted
token decoding to a nonempty, NUL-free `@`-prefixed annotation, within the usual
string/count limits. It must appear at module scope before the function's
declaration-line record/header. Misplaced, malformed and orphan records reject;
older readers ignore this optional annotation comment rather than treating
its contents as instructions.

## Binary executable v3 layout

The source ownership profile uses `OWN_ENTER`/`OWN_LEAVE` lexical scopes,
`OWN_ALLOC element_type` (pop count, push scalar-buffer pointer), `OWN_BIND slot`
and `OWN_MOVE slot` (push transferred pointer, clear the source owner).
Allocation and move must immediately precede `STORE_LOCAL destination` and
`OWN_BIND destination`. Readers validate scope identities at control-flow
joins, definite-live moves, non-overwritten owned slots and fault-scope
snapshots. Function and fault unwinding restore the captured scope depth.
These operations preserve the raw scalar pointer representation internally;
arbitrary hand-written NIL is not thereby a borrow-checked source program.
Native owned release physically frees storage; ordinary raw `FREE` retains
its independent tombstone behavior.

`PARALLEL_MAP worker input_type output_type` pops one scalar array and pushes
the ordered output array. The resolved worker and every reachable helper must
pass the isolated scalar operation/signature contract before execution or
native generation. See [structured concurrency](structured-concurrency.md).
Both opcode families use existing named instruction records; old SDK readers
reject their unknown opcodes rather than ignoring them.

All unsigned integers are explicitly little-endian. A string is a `u32` byte
length followed by exactly that many bytes, without a terminator. Constant
strings may contain arbitrary bytes. Schema/annotation strings must not contain
NUL; required schema names/types and annotations must not be empty. Signatures
follow the existing NIL signature validation rules. A unit enum variant's
payload type is the empty string.

The header is four magic bytes `NIL\0`, then `u32 version`, `u32 flags`, and
`u32 section_count`. Exactly five section entries follow, each containing
`u32 type`, `u64 file_offset`, and `u64 byte_size`. Every section occurs once.
Sections collectively cover the entire payload without overlaps, gaps or
trailing unclaimed bytes; their order is not significant. Section-local records
must consume the section exactly.

1. Types: `u32 struct_count`; for each struct, its name, `u32 field_count`, and
   pairs of field name/type strings. Then `u32 enum_count`; for each enum, its
   name, `u32 variant_count`, and pairs of variant name/payload-type strings.
2. Functions: `u32 function_count`; each function contains name, `u32 parameter_count`
   and parameter strings, `u32 generic_count` and generic strings, return-type
   string, `u32 function_flags` (bit 0 inline, bit 1 test; other bits rejected),
   `u32 declaration_line`, `u32 annotation_count` and annotation strings, then
   `u32 instruction_count`. Each instruction has opcode string, `u32 operand_count`,
   operand strings, and `u32 debug_line`. Annotations start with `@` and retain
   their exact encoded text; flags are preserved independently.
3. Constants: `u32 count`, then constant name/value string pairs.
4. Symbols: reserved `u32 0`; nonzero records are rejected.
5. Module: module-name string, `u32 test_count`, then test function names.

Declared types still undergo backend ABI validation. Serialized metadata is not
permission to treat unknown types, mismatched handles, or incompatible enum
owners as integers. The VM validates instruction shapes and typed return counts;
it does not currently perform complete runtime value-type checking.

Current reader ceilings are 256 MiB per artifact, 16 MiB per string, 100,000
functions/structs/enums each, 1,000,000 instructions/constants, 65,536
parameters/generic parameters/operands/annotations/members per record, and
1,000,000 total schema members. Debug and declaration lines must fit signed
32-bit integers. These are validation ceilings, not practical program-size
recommendations.

## Class-bearing v4 layout and operations

V4 executables have six sections: the five v3 sections above and section 7,
class declarations. V4 libraries have seven sections, additionally including
section 6: `u32 source_count`, source path/content string pairs, then
`u32 dependency_count` and dependency-name strings. Function
flags, annotations and schemas use the v3 encoding in both v4 artifact kinds.
The class section is not a reinterpretation of ordinary struct schemas.

Section 7 contains `u32 class_count`. Each class records its name string,
`u32 abstract_flag` (0 or 1), `u32 base_count`, direct base-name strings, and
`u32 method_count`. Each declared method records name, implementation symbol,
receiver type and return type strings, then `u32 method_flags` (bit 0 virtual,
bit 1 abstract), `u32 parameter_count`, and nonreceiver parameter-type strings.
An abstract method has an empty implementation; concrete methods name an exact
function with an owner-typed receiver. Inherited methods are resolved through
ancestry, not copied into each class declaration.

Equivalent text records are:

```text
class Base 1
  method read - "Base" "i64" 1 1 0
end class
class Child 0
  base Base
  method read Child::read "Child" "i64" 1 0 0
end class
```

`CLASS_UPCAST target` changes a proven plain class handle's static view to an
ancestor, preserving its dynamic identity and shared fields. `CALL_METHOD
static_class method argc` checks the static signature; `argc` includes self.
Virtual slots choose the nearest dynamic implementation; nonvirtual calls bind
to the declaring owner. Exact-reference receivers are adapted only at this
checked method boundary, not through general reference covariance.

Readers validate class schemas, acyclic ancestry (at most 256 levels), inherited
field identity, method signatures and concrete abstract-method obligations.
Class provenance is checked through operand/local control flow, calls, returns
and fields. A cast, raw pointer or foreign return cannot manufacture a class
handle. Native objects carry a private dynamic tag; field accesses select the
actual concrete layout, including fields inherited from a non-first base.
Dispatch uses checked direct targets, not NIL-supplied host function pointers.
Both method dispatch and field access retain normal managed-root and fault
semantics. See the [source class contract](../language-reference/classes.md)
for deliberately unsupported class combinations.

## Scalar instruction contract

`ADD`, `SUB`, `MUL`, `DIV`, `MOD`, and `NEG` accept an optional exact scalar
type operand: `i32`, `i64`, `u8`, or `f64`. `BIT_AND`, `BIT_OR`, `BIT_XOR`,
`SHL`, and `SHR` accept only the three integer types. Source lowering emits
the resolved type. Legacy instructions without a type use `i64` integer
arithmetic or `f64` floating arithmetic, not an incidental LLVM value width.

Integer results wrap at the declared width. Division/remainder reject zero
and signed minimum divided by minus one. Shifts validate the full right
operand against the left operand's width before any narrowing; a negative
count or count at least the width fails. `SHR` is arithmetic for signed
types and logical for `u8`. See [source types](../language-reference/types.md)
for scalar casts and promotion. Recompile older development NIL to get
declared-width arithmetic instead of the legacy untyped instruction behavior.

## Typed globals

`LOAD_GLOBAL name type` and `STORE_GLOBAL name type` carry an optional trailing
storage type: `text`, `bool`, `u8`, `i32`, `i64`, or `f64`. Every access to one
name must agree; mixing typed and untyped accesses is invalid. Text/binary
readers check that metadata and both engines reject incompatible stored values.
Typed native globals track initialization and fail on a read before a store.

The historical one-operand form remains readable, with the legacy integer ABI
on native routes and general VM values on interpreted routes. Aggregate globals
remain interpreter-only. These optional operands use the existing instruction
encoding, not a new binary section or version; older tools that require exactly
one operand must be rebuilt with the SDK. Source globals use exact
`module::name` storage identities; unnamed-root globals retain bare names.
Imports and namespace aliases resolve to that identity before code generation,
while lexical locals continue to shadow imported/global spellings. Two modules
can declare the same variable name without sharing storage. Initializers run in
declaration order, not lexical name order; an explicitly typed forward reference
can type-check but still fails if it reads before initialization.

## Native arrays and aggregate references

Native arrays are runtime descriptors with exact element kinds for `i32`,
`i64`, `bool`, `u8`, `f64`, NUL-free `text`, and supported structural tuples.
`f64` values retain their bits; text and tuple elements hold traced pointers.
Tuple metadata preserves the exact ordered `@nil_tuple` schema, including
nominal type names. Function and field boundaries preserve array element
identity. Scalar out-of-range reads yield the element's zero value; tuple reads
fail on an invalid index or uninitialized element rather than fabricating a
default tuple. Out-of-range writes are no-ops. Arrays of records, enums and
nested arrays remain unsupported natively.

The public STL array primitives lower to
`INTEROP_CALL intrinsic operation arity result_type element_type`, where
`operation` is `array.new_with_value`, `array.get`, `array.set`, or
`array.length` (the fixed `u8` variants are also supported). The final operand
is exact element metadata, not an FFI calling convention. Constructors return
`array<T>`, getters return `T`, setters return `void`, and length returns `i32`.
Size and index arguments use the `i32` contract; constructors reject negative
sizes and raw NIL values outside that range instead of truncating or clamping.
The historical four-operand `array.length 1 i64` form remains readable.
Declared `f32` helpers and unsupported aggregate-element constructors reject
explicitly. Tuple constructors, getters and setters require matching schemas.

Nested struct fields hold nominally typed object handles, matching the VM's
reference semantics. `ref<Struct>` and aggregate-only `REF_LOCAL` preserve that
object identity across calls, returns, fields, and local aliases. They are not
raw pointer reinterpretations, scalar stack references, or a borrow/lifetime
checker. Recursive layouts and references to unsupported targets fail closed.
Native construction checks intersect initialized fields across control-flow
paths. An object must be fully initialized before a read, local store, call,
return, or other escape; one branch's stores cannot prove another branch safe.
Conditional expressions inside valid struct literals retain their construction
identity across expression joins.

## Native managed lifetime

Safe native code publishes initialized eight-byte root slots for active
function arguments, intermediate values and local storage, plus global slots
owned by the entry wrapper. Collection runs at generated function entries and
backward CFG edges once allocation pressure reaches the current threshold;
allocators themselves do not collect. Return values are spilled by the caller
before another safepoint can run. The same instrumentation is used by JIT,
native compilation and standalone assembly.

The runtime traces owned blocks and text/tuple-array elements, including reachable
cycles and nested aggregate/enum payloads. It follows only known owned address
ranges, not arbitrary words as host pointers. Numeric words that resemble
addresses may conservatively retain an object. Static root slots are reused
across loop iterations, but stale roots may retain their reachable graph until
the function returns; neither total live bytes nor pause times are bounded by
this contract. Raw pointer buffers remain explicitly released and have
invocation-scoped metadata. Execution with no registered root frames keeps the
older invocation lifetime; embedding code opting into root frames must publish
every live managed value. This adds runtime helpers, not a NIL wire-format change;
new native objects require the matching SDK runtime archive.

## Synchronous fault operations

The following appended opcode names use the existing operand encoding; no
binary section or format version changes. Older consumers reject the unknown
instructions. New native objects require the matching versioned fault runtime
helpers. Region and saved-slot names are per-invocation metadata, never host
addresses, raw pointers, or ordinary operand-stack tokens.

| Operation | Operands | Effect |
| --- | --- | --- |
| `FAULT_ENTER` | region, dispatch label | Register a same-function region and save the current stack prefix. |
| `FAULT_LEAVE` | region | Retire exactly the innermost region. |
| `FAULT_RAISE` | exact struct schema | Pop a typed payload and propagate, retaining its source line. |
| `FAULT_SAVE` | slot | Move the pending fault to an invocation-owned slot and clear pending propagation. |
| `FAULT_MATCH` | slot, exact struct schema | Push the boolean result of exact nominal matching. |
| `FAULT_BIND` | slot, exact struct schema | Push that same checked struct payload. |
| `FAULT_DROP` | slot | Retire a saved fault. |
| `FAULT_RESUME` | slot | Consume a saved fault and resume propagation. |
| `FAULT_RETHROW` | slot | Propagate a copy while retaining the source slot for nested handling. |

The selected region is popped before entering its dispatch label. Propagation
restores the saved stack prefix rather than merging incomplete call arguments
into the handler. The source compiler lowers cleanup to explicit regions and
slot operations; a handler fault cannot reenter sibling handlers. Pending and
saved payloads are collector roots. Functions discarded during propagation
retire their own records without dropping a surviving caller's saved fault.

Shared fault control-flow validation checks region stacks, dispatch targets,
saved-slot and pending-state agreement, stack shapes and typed payload schemas.
Text/binary consumers and loader-bypassing VM use the same contract; native
lowering uses the analysis for exceptional edges. Malformed region entry/leave,
ordinary jumps into dispatch, orphan/consumed slots and incompatible merge
states reject. This is a fault protocol verifier, not a claim of complete
runtime type verification for every historical NIL instruction.

Native fault dispatch does not require ENTER to appear textually before a
faulting edge. General native stack-to-SSA lowering still rejects a nonempty
stack block laid out before every incoming predecessor; that preexisting CFG
limitation is not relaxed by the fault protocol.

Native propagation is an explicit C-shaped pending-state protocol. Every Ny
call checks propagation before using its result; propagation returns use ABI
placeholders that cannot become ordinary source values. Root-frame cleanup is
balanced on those exits. No host C++ exception or `longjmp` crosses generated
native frames. Fatal runtime errors remain outside this language protocol.

## Time instructions

`TIME_NOW` pushes signed `i64` Unix-epoch milliseconds. `TIME_SLEEP` consumes
one integer millisecond duration and produces no value. Nonpositive durations
are immediate no-ops; positive waits use bounded host-clock conversions to
avoid overflow. Floating-point and text durations reject rather than silently
converting. Neither instruction takes encoded operands. These operations do
not establish a general threading or asynchronous execution model.
