# Fault execution and recovery contract

**Status:** Implemented synchronous contract; cross-route evidence and bounded
limitations are tracked with the implementation.
**Recorded:** September 26, 2026.

This entry preserves the design written before implementation. Statements
below about existing rejection describe that starting point, not current
behavior. The implementation follow-up and language reference are the current
capability record; this contract alone is not a full-language RC declaration.

## Existing evidence and design boundary

`nilcompiler/src/lexer.hpp` explicitly describes `attempt`, `fault`,
`recover`, and `raise` as replacements for try, catch, finally, and throw.
`ast_attempt_stmt` describes `recover` as cleanup. The checker already gives
the attempt body, each handler, and the recover block separate lexical scopes.

There is an important historical inconsistency: the checker has TODOs about a
`Fault` trait, but `nilstd/src/core/faults.ny` declares `Fault`,
`AssertionFault`, and `IoFault` as ordinary structs. There is no implemented
fault trait or common exception superclass to dispatch through.

The initial contract is synchronous, explicit source-level fault propagation.
It does not implicitly add task cancellation, faults between threads,
foreign-language exception interoperability, postfix `?`, assertion lowering,
or recoverable memory-safety traps.

## Values and exact handler matching

1. `raise value` accepts a value of a concrete struct type. User-defined structs
   and the existing standard-library fault structs follow the same rule. Do not
   require the nonexistent `Fault` trait or infer eligibility from a name ending
   in `Fault`.
2. A typed handler names a concrete struct type. Resolve module qualifications,
   namespace aliases, imports, and concrete generic arguments normally, then
   retain the exact canonical nominal identity in the checked AST and NIL.
   Distinct modules' equally spelled short names must not match each other.
3. Matching is exact identity, not shape, suffix, pointer equality between type
   name strings, or class subtyping. In particular, `fault Fault as e` does not
   match `AssertionFault` merely because both are library fault records.
4. The payload uses the language's existing shared struct-object semantics.
   Raising retains that object's identity; it does not silently deep-copy it or
   establish new move/borrow semantics. A typed handler sees the same object.
5. Reject scalars, enum values, unresolved types, class values, and opaque `any`
   as raised payloads in this first contract. Supporting those categories later
   requires an explicit contract extension, not native pointer/word guessing.
6. `fault as e` is a catch-all. Its binding is an opaque fault token preserving
   the original type and payload, not an unchecked native `any`. Initially the
   token can be ignored or used as the whole expression of `raise e` to rethrow;
   field access, arithmetic, casts, passing it through an unrelated ABI, and
   fabricated tokens must reject. A stable context-inspection API can follow
   separately. Do not silently bind it as the library's unrelated `Fault` struct.

The parser currently recognizes only one unqualified identifier immediately
before `as`. Replace that special case with the normal type parser so a typed
handler can use the same qualified and generic type spellings as declarations.

## Handler order, guards, and scopes

Handlers are considered in source order. A type match with a true guard selects
that handler; a false guard continues to the next handler. Guards must have
type `bool`, not arbitrary truthiness or `any`. The bound payload is visible in
the guard and handler body only. A final unguarded catch-all is exhaustive;
handlers following it are unreachable and should be diagnosed.

Once dispatch begins, the current attempt's handler region is disabled. A new
fault raised by a guard or handler is not caught again by sibling handlers of
the same attempt. It runs that attempt's recover block, then propagates to an
outer attempt. A failed guard must not consume the original fault before later
handlers have had a chance to match it.

Attempt-body locals are not in scope in handlers or recover. Handler bindings
are not in scope in recover. Resources needed by cleanup must therefore be
declared in an enclosing scope. An attempt containing only recover is valid
try/finally-style control flow.

## Recover executes on every ordinary exit

Run recover exactly once whenever execution leaves its attempt through:

- ordinary completion of the body;
- ordinary completion of a selected handler;
- an unmatched fault;
- a new fault from a guard or handler;
- `return`, `break`, or `continue` leaving the protected region.

Nested cleanup executes innermost first. A loop transfer whose destination is
still inside the attempt does not exit that attempt and must not run its recover
early. Conversely, leaving several nested attempts runs each exited cleanup.

Evaluate and save a return expression before starting recover, including its
exact type and managed roots. Normal cleanup then resumes the saved return,
loop transfer, normal continuation, or fault propagation. A new abrupt exit
from recover replaces the suspended exit: a new fault replaces the old fault,
and an otherwise valid return or loop transfer replaces the prior transfer.
That recover block is already being exited and must never recursively run
itself; outer cleanup still applies. Document this replacement rule explicitly
because it can intentionally suppress an earlier fault.

Fatal runtime termination is distinct. Invalid NIL, invalid memory accesses,
allocation failure, process exit, and existing panic/runtime safety errors do
not become recoverable simply because they occur inside attempt. Do not catch
all host `std::exception` values and reinterpret them as Ny faults. Preserve
existing fail-closed diagnostics and process behavior for those paths.

## NIL execution contract

Add an explicit, shared NIL fault protocol rather than embedding a
native-backend-only exception mechanism. The exact opcode spellings need to be
selected during implementation; the minimum operations and metadata are:

| Operation | Required meaning |
| --- | --- |
| Enter protected region | Record a region identity, same-function dispatch label, and the current operand-stack prefix. |
| Leave protected region | Remove the expected innermost region; reject an unmatched or wrong removal. |
| Raise typed payload | Consume one payload of the encoded exact struct type and begin propagation. |
| Match / bind fault | Test canonical type identity and bind a checked typed payload, or retain an opaque catch-all token. |
| Suspend / clear active propagation | Preserve the selected fault while permitting ordinary calls inside guards and cleanup. |
| Resume / rethrow | Restore the original exact type and payload without reconstructing or erasing their identity. |

The compiler should lower cleanup with an explicit cleanup-context stack,
separate from the existing loop-label stack. A selected handler's catch region
must be removed before executing its guard or body. Use a separate cleanup-only
landing path around guard/handler execution so a new fault still runs recover.
Disable that landing path before executing recover itself. The no-match path
also executes recover before resuming the saved original fault.

All normal and exceptional destinations have a defined operand-stack shape.
An exceptional edge retains exactly the stack prefix saved at attempt entry,
discarding partially evaluated arguments and intermediate results. For example,
a fault while evaluating a later argument cannot leave an earlier argument on
the handler's operand stack. Store fault state outside that discarded suffix.

Append new opcode enum values before `COUNT` without renumbering old values;
extend the shared name table, operand validation, text/binary serialization,
and every consumer together. Record the instruction/metadata compatibility
contract in `nil_contract.hpp` and the NIL format documentation. Decide the
format version from the actual encoding change; do not silently reinterpret
an existing opcode or treat an unknown operation as a no-op.

### Interpreter unwinding

A VM handler record must include its owning call-frame depth, exact saved
operand-stack height, region identity, and dispatch PC. Propagation removes
younger call frames and their handler records, trims the operand stack to the
saved prefix, and transfers control to the selected dispatch point. Frame
locals must continue to belong to the correct surviving invocation.

Nested records are required even when several attempts belong to one function.
Function return, debugger restart, and runtime reset must retire or clear
records correctly. Implement control transfer consistently for both normal
execution and `stepInstruction`; the debugger must observe the same real
unwound frames, not a second approximated stack.

### Native propagation ABI

Use an explicit C-shaped protocol in the native runtime, not `setjmp`/`longjmp`
or host C++ exceptions crossing generated frames. A runtime-context record
holds a pending exact type identity and payload. Suspended faults have separate
rooted storage so nested fault handling does not overwrite an outer saved fault.

After every generated Ny call, check pending propagation **before** consuming
or exposing the returned value. A pending edge dispatches to the nearest active
local handler or, if none exists, takes an internal propagation-return path.
That path returns an ABI-correct placeholder solely to satisfy the function's
ordinary calling convention. The placeholder is not a Ny return value and must
never reach user code because the caller checks pending state first. This
applies to all supported return types, synthetic initializer calls, and every
generated wrapper that can call Ny code.

The platform entry wrapper checks faults from both initialization and Ny main
and emits a deterministic nonzero uncaught-fault diagnostic. JIT and separately
linked AOT code resolve the same versioned runtime symbols. No fault crosses an
unsupported foreign callback boundary by accident.

Handler, guard, and recover execution suspend the old fault and clear active
pending state while making ordinary calls. Otherwise a successful helper call
would incorrectly re-propagate the fault currently being inspected. Fresh
faults take the cleanup-only or outer path described above.

LLVM exceptional edges must use the saved stack prefix when constructing PHIs.
Do not merge a partial call's current operand stack into a handler or invent
zero values to repair incompatible stack shapes.

## Managed roots and resource lifetime

The VM collector must trace active and suspended fault payloads in addition to
ordinary stack, frame-local, and global roots. Native runtime pending/suspended
fault records likewise participate in managed tracing. Saving a fault must
precede popping the frame that held its final ordinary reference. Clearing or
replacing it releases only that retained root, not the underlying object while
other aliases remain live.

Saved return values and cleanup temporaries remain rooted throughout calls and
collection inside recover. Native propagation-return edges must balance the
same shadow-root frame registration used by ordinary returns. A handler that
resumes execution must not retain root addresses from unwound function frames.

Unwinding does not automatically close every file or free every raw buffer
mentioned by a discarded frame. Aliases and globals may still own those
resources. Explicit recover performs explicit resource release; safe managed
collection reclaims unreachable language objects; manual pointers retain their
manual contract. Deterministic destructors and ownership-driven cleanup are a
separate language feature, not an implied side effect of fault handling.

## Validate malformed NIL and CFGs before execution

Source codegen alone cannot establish the public serialized-NIL boundary.
Validate text and binary inputs consistently, including:

- exact opcode operand counts, nonempty canonical types, valid payload schemas,
  and well-formed region identities;
- existing, unique dispatch labels inside the owning function;
- consistent region-stack state at every normal CFG merge, including loops and
  irreducible handwritten CFGs;
- no ordinary jump into a handler, no jump into a protected region without its
  entry operation, and no escape that bypasses required region removal/cleanup;
- no orphan leave, duplicate live region identity, cross-function target, or
  use of an absent/consumed pending or suspended fault;
- stack-height and type agreement on both normal and exceptional edges,
  including enough operands for raise and exact typed handler binding;
- all function exits retiring their own region and root records, while keeping
  outer invocations' records intact.

An untrusted NIL operand must never supply an unchecked host payload address,
root-slot address, or executable jump target. Retain runtime defensive checks
where host embedding can construct instruction objects without a loader.

## Required behavioral evidence

Use fixed stdout/stderr/exit oracles and retained per-route reports. A source
fixture is not qualified by a parser pass, expected failure, skip, or successful
compilation alone.

| Family | Required cases |
| --- | --- |
| Ordinary execution | No fault; recover-only attempt; selected handler; no-match propagation; deterministic uncaught fault. |
| Exact type identity | Library and user structs; imported aliases; qualified equal-short-name types; concrete generic faults; mismatched nominal shapes. |
| Dispatch | First matching handler; false guard then later match; strict boolean guard; catch-all; catch-all rethrow; unreachable handler diagnostic. |
| Nested execution | Same-frame nested attempts; faults across several calls; recursion; rethrow retaining type and payload identity. |
| Operand stacks | Fault in a later call argument or nested expression; different partial operand suffixes reaching one handler; typed and void returns. |
| Cleanup | Return-value evaluation before cleanup; normal/caught/unmatched paths; nested recover order; break/continue leaving one or several attempts; transfer remaining inside an attempt. |
| Replacement faults/exits | Guard raises; handler raises; recover raises; recover return/loop transfer; no same-attempt recatch or duplicate cleanup. |
| Managed lifetime | Forced collection in guards/recover; nested struct and text-array payloads; suspended outer fault during a nested handled fault; saved managed return; balanced shadow roots after propagation. |
| Resources | Explicit cleanup runs exactly once; a surviving alias/global is not implicitly freed; manual resource release remains explicit. |
| Invalid input | All malformed region/CFG/payload cases above, through both NIL formats and loader-bypassing VM tests. |
| Tooling | Debugger stepping and call-stack snapshots through unwind; clean restart; source lines and canonical fault type in uncaught diagnostics. |

Positive source fixtures must pass textual and binary interpreter routes,
bundled routes, LLVM JIT, and native AOT. Exercise serialized text and binary
NIL through standalone assembler/linker and the installed SDK as well. Add
runtime ABI unit tests plus sanitizer coverage of unwinding and managed roots.

Assertions and postfix propagation remain explicitly unavailable unless their
own lowering and semantic tests are implemented. Remove the existing blanket
fault-control-flow rejection fixture only when it is replaced by this positive
matrix and targeted negative contracts.

## Implementation ownership and sequencing

1. **Front end and lowering:** `parser.cpp/.hpp`, `ast.hpp`,
   `type_checker.cpp/.hpp`, `codegen.cpp/.hpp`. Extend normal type parsing,
   canonical fault identities, opaque catch-all rules, boolean guards, and
   cleanup lowering around return and loop transfers.
2. **NIL and interpreter:** `vm.hpp/.cpp`, `nil_loader.cpp`, `nil_contract.hpp`,
   loader and VM tests. Define shared operations and validate region dataflow
   before introducing interpreter unwinding.
3. **Native execution:** `native_runtime.hpp/.cpp`, `llvm_backend.cpp`, runtime
   ABI tests. Add rooted pending/suspended state, exceptional return paths,
   handler stack-prefix merges, entry-wrapper handling, and JIT symbol binding.
4. **Evidence and documentation:** source contracts, malformed NIL tests,
   six-route and installed-toolchain matrices, capability catalog, language
   reference, and this notebook's observed implementation follow-up.

The front-end files overlap class-dispatch work; native files overlap managed-GC
work. Land and verify those checkpoints before assigning overlapping edits.
The four slices share one protocol and cannot independently declare fault
execution finished. Implement and verify one complete cross-route vertical
slice, then expand the matrix before advertising the feature for RC.
