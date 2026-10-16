# NIL-0003: serialized NIL is a toolchain boundary

- **Status:** Implemented as a preview
- **Date:** 2026-09-13

## Question

What should `nilassembler` and `nillinker` mean now that the compiler's LLVM
native route is functional, and how should their tests improve the language?

## Evidence

The recovered commands did not form a pipeline. The assembler scanned NIL as
unstructured lines, recognized only a few opcode names, discarded functions,
constants, types, and call arity, and wrote an unversioned `NYOBJ` byte stream.
The linker read that binary stream with line-oriented text operations, decoded
no sections or symbols, and wrote a 15-byte `NYEXE` header with no repository
consumer. Both commands accepted arbitrary input and reported success; even an
output write failure could be reported as successful.

The compiler already has one real native backend and one versioned runtime ABI.
Maintaining another object and executable representation would add a format
without adding capability. More importantly, keeping the LLVM module only in
compiler memory prevented route tests from showing whether serialized NIL
retained enough information to serve another backend process.

## Decision

`nilassembler` is the strict executable-NIL-to-host-object stage. It reads only
current compiler-emitted textual or binary executable NIL, reconstructs the
serialized module, lowers through `NativeCodegen`, and emits the platform's
genuine relocatable object format. `nillinker` validates host objects, locates a
matching `nilruntime` archive, invokes the configured compiler driver with an
argument vector, and publishes a native executable. Neither command reads or
writes a Ny-specific object or executable container.

A filename or archive magic is not enough to establish a matching runtime.
`nillinker` validates archive member framing and requires its GNU/COFF or BSD
symbol index to map `nyrt_abi_version`, `nyrt_context_begin`, and
`nyrt_context_end` to real non-metadata members. Automatic discovery skips an
invalid shadow archive and continues through the supported SDK, build, and
installed-prefix layouts.

Both tools fail closed and stage output privately beside its destination before
atomic publication. A failed parse, lowering, host link, write, or validation
must leave an existing destination unchanged. Linux is the verified route;
other host implementations remain experimental. This is host compilation, not
cross-target compilation.

Serialized NIL is therefore an executable architecture boundary, not only a
debug dump. Textual NIL now carries function return types and enum schemas in
addition to its existing function, struct, annotation, constant, operand, and
line records. Binary NIL v1 retains its existing encoding; its reserved type
and symbol sections are not silently repurposed. A binary module that needs
metadata absent from v1 must fail native lowering rather than having metadata
invented. NIL v2 libraries remain library-resolution artifacts and are
rejected by `nilassembler` until relocatable library semantics are designed.

## Verification contract

The required route is:

```text
Ny source
  -> textual NIL -> nilassembler -> host object
  -> binary NIL  -> nilassembler -> host object
  -> nillinker + version-matched nilruntime
  -> native executable -> exact stdout/stderr/exit status
```

That diagram describes two independently checked paths, not a promise that
binary NIL v1 can encode every program accepted by textual NIL. Programs in the
v1 metadata-independent subset must pass both paths. A program that needs a
struct or enum schema or function annotation must pass the textual path and the
binary path must fail with the specific missing-metadata boundary while
preserving any existing output. This positive/negative pairing prevents a
lossy binary artifact from being mistaken for native support.

Tests must also cover malformed NIL, invalid host objects, missing runtime,
unresolved symbols, hostile path characters, symlink-safe replacement, private
file modes, and preservation of an older output on failure. SDK validation
runs the installed commands through this route rather than checking only that
their filenames exist.

## Follow-up

Design a new binary NIL version before encoding type schemas, annotations, or
linkable library interfaces in the reserved v1 sections. Completion requires a
written compatibility table, old-version fixtures, round-trip tests, and an
end-to-end multi-module native application that links without rebuilding a
combined source module. Before calling SDKs relocatable across hosts, replace
the configured build-host linker-driver path with an explicit SDK toolchain
descriptor or supported `--driver` override, and give Windows/macOS the same
host-path, process, archive, and publication evidence now required on Linux.
