# Native toolchain and debugger milestone — 2026-09-13

- **Status:** Implemented and verified on Linux
- **Date:** 2026-09-13

## Question

Can Ny's secondary tools be trusted as real stages of the language, rather
than commands that merely accept input and create files?

## Evidence

The recovered `nilassembler` and `nillinker` were disconnected prototypes. The
assembler reduced a few textual opcode names into an unconsumed `NYOBJ`
container, while the linker produced a short `NYEXE` marker that no Ny runtime
could execute. Both could report success without preserving the module or
performing a host link. The debugger kept a second locals/function stack that
could disagree with the VM, parsed JSON with substring searches, and allowed a
debuggee to consume protocol input or terminate the debugger process.

The repaired tools are now exercised as a continuous pipeline:

```text
Ny source -> serialized executable NIL -> nilassembler -> host object
          -> nillinker + nilruntime       -> host executable -> exact result
```

Textual NIL carries the schemas, annotations, signatures, instructions, and
debug lines needed by the current native backend. Binary NIL v1 is accepted
only for the subset whose required metadata it actually serializes. The
assembler's reader validates section ownership, exact function ABI metadata,
instruction shape, and format limits before LLVM sees the module. The linker
accepts genuine host relocatable objects, validates that the selected runtime
archive's symbol index resolves the versioned Ny ABI anchors to real members,
and invokes the configured driver without a command shell. Both tools use
private same-directory staging and preserve an existing output on failure.

The debugger now observes VM-owned frames, locals, globals, stack, and call
depth. Restart creates a pristine invocation; stepping derives from real call
depth; false conditional breakpoints continue; conditions preserve exact
integer values; and JSON values are finite, bounded, and cycle-safe. Program
stdout is framed separately from the JSON Lines control stream. Debuggee input
is isolated, and `std_sys.exit` becomes a debugger termination event while the
standalone interpreter retains process-exit behavior.

The SDK seal applies the same philosophy. A command name or file magic is not
proof of capability: packaging validates complete NIL v2 library structure and
real archive symbol indexes, then runs installed compiler, interpreter,
debugger, assembler, linker, and native-executable contracts appropriate to the
build's LLVM capability.

## Decision

Secondary tools are part of Ny's executable conformance surface. A feature is
not complete merely because `nilcompiler` can reach it in memory; serialized NIL
must retain the required meaning, each consuming process must fail closed when
it does not, and the installed SDK must reproduce the route without relying on
the source tree.

For human and AI tooling, runtime observations follow the same rule: there is
one authoritative state owner and one framed protocol boundary. New inspection
clients should consume VM snapshots instead of reconstructing state from trace
text or maintaining mirrors.

## Verification

The focused assembler, linker, debugger, native-object pipeline, system-exit,
route-matrix, and production-application contracts pass in an LLVM-enabled
build. The complete registered CTest suite passes. A clean SDK generation is
also built and validated before publication; its exact revision and counts are
reported by `manifest.json` rather than duplicated in this note.

## Follow-up

1. Design a new executable binary NIL version, without reusing the existing
   v2 library tag, with first-class type schemas, annotations, source maps, and
   relocatable multi-module symbols.
2. Add an SDK toolchain descriptor recording supported NIL versions, target
   triple, runtime ABI, linker-driver requirements, and debugger protocol
   capabilities. This should become machine-readable input for both editors
   and AI agents, not prose inferred from a build host.
3. Stream debugger output in causal order, add an explicit debuggee-input
   exchange and asynchronous pause, and bridge the shared inspection model to
   the Debug Adapter Protocol.
4. Move every remaining process-global runtime registry behind VM-owned
   lifetime before advertising concurrent isolated debug sessions.
5. Extend impl-method specialization requests with explicit method-generic
   bindings and reject undersupplied specializations before emission. Receiver
   and impl-level substitutions are exact now, but a higher-order method such
   as `map<U>` must not depend on reconstructing `U` from a flattened call-site
   type or serialize an unresolved type parameter as if it were concrete.
6. Add native Windows CI for the Unicode path, COFF/archive, timeout, and Job
   Object descendant contracts before claiming equivalent Windows support.
   Command-line recovery, executable discovery, secure snapshot I/O,
   publication, and driver launch now stay wide-path safe, but current
   verification is a strict MinGW cross-build rather than Windows execution.

## Artifact-boundary hardening update — 2026-09-13

- **Status:** Implemented and verified on Linux

A follow-up production audit found that debugger execution semantics were
covered, but the program loader still opened its target with an ordinary
`ifstream`. That let `nildebugger` follow a symlink that `nilassembler` rejected
and let an unopened FIFO block the debugger before it could identify the input
as a special file.

`nildebugger` now uses the same bounded no-follow regular-file reader as the
other Ny artifact consumers. It captures one immutable snapshot of at most
256 MiB, rejects invalid paths, symlinks, FIFOs, directories, non-disk Windows
handles, growth, metadata mutation, and oversized inputs, and parses textual or
binary NIL only from that snapshot.
Secure-read and parse failures remain failure-atomic: an already validated VM
program and its selected inspection frame survive a rejected reload. The CLI
also has strict standalone `--help` and `--version` contracts. The unused
`DebugSession` wrapper was removed because both REPLs ignored it and the VM plus
`Debugger` already own the real lifecycle.

The same audit separated three larger milestones from this bounded fix:

1. A future executable NIL revision should add type schemas, annotations,
   source maps, target identity, runtime ABI metadata, and relocatable symbol
   policy together. Binary NIL v1 is deliberately too lossy for aggregate
   assembly, and library NIL v2 must not be repurposed as an executable format.
2. `nillinker` currently links a whole Ny program that was combined before
   object emission. Ordinary Ny functions are internal and every generated
   module requires `main`, so separately compiled Ny modules are not yet a
   supported linker input. Export policy and library-object semantics must be
   designed before claiming modular native compilation.
3. The linker still needs explicit object-count and aggregate-byte budgets,
   immutable input snapshots where practical, and a bounded external-driver
   wait. Required SDK stages (`nilassembler` and `nillinker`) should also be split
   from genuinely experimental tools in the root build option.

The next editor-facing debugger milestone remains source maps followed by a
capability-negotiated Debug Adapter Protocol bridge. Registering a VS Code
debugger before those contracts exist would create a polished but dishonest
source-debugging surface.

## Native-link boundary update — 2026-09-13

- **Status:** Implemented and verified on Linux; Windows cross-compiled

The next tool audit found that `nillinker` validated object and runtime paths and
then handed those same paths to the host driver. That was a time-of-check /
time-of-use gap: a file could be replaced after preflight, making the driver
consume different bytes or block on a special file. The driver wait itself was
also unbounded.

The linker now treats admission metadata only as a cheap early rejection. Its
authoritative inputs are no-follow regular-file snapshots captured after a
private output workspace exists. Each snapshot is validated and atomically
staged under a generated name; only those private paths reach the driver.
Admission and capture both enforce 4,096 objects, 512 MiB per object, and 4 GiB
in aggregate. The matching runtime archive has its own 512 MiB bound and is
revalidated from its captured bytes. Peak capture memory is one input rather
than the aggregate, though a future streaming bounded-copy primitive would
reduce the current worst case further.

Host execution now has a 300-second default deadline (configurable from 100 ms
through one hour). POSIX launches a dedicated process group and performs
bounded TERM/KILL/reap cleanup. Windows uses a suspended wide-character process
launch inside a kill-on-close Job Object and confirms that the job becomes
empty within a bounded cleanup interval. A driver leader that exits while a
contained descendant remains is rejected on both platforms. Timeout, residual
descendants, and all validation failures refuse publication and preserve a
caller's existing output. A configured POSIX driver remains trusted not to
escape its assigned group by deliberately creating a new session.

Standalone builds now place `nilruntime` where the build-tree `nillinker` finds
it automatically; standalone installation publishes `bin/nillinker` with the
matching archive under the configured library directory. Both real layouts run
the same default-discovery link-and-execute contract, rather than relying on a
test that always supplies `--runtime`.

This hardening does not turn `nillinker` into a separately compiled Ny module
linker. It remains a host-object linker for a whole Ny program assembled after
source/NIL combination. Export policy, executable NIL vNext metadata, and
library-object semantics remain the prerequisites for real modular compilation.
