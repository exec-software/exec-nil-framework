# Inspectable executable context

Ny context is opt-in self-description for humans and tools. Inspecting it never
executes the program, contacts a model, or grants the annotations any authority.

```sh
nilcompiler --no-bundle --output program.nil program.ny
nilcompiler --inspect-context program.nil > program.context.json
nilcompiler --native --embed-context --output program program.ny
nilcompiler --inspect-context program
```

The first command pair creates a JSON sidecar from the annotations already
preserved in NIL. Native embedding is separate and defaults off. For the split
pipeline, use `nilassembler --embed-context -o program.o program.nil`, then
`nillinker -o program program.o`. Both the object and linked executable can be
inspected. The compiler rejects `--embed-context` for NIL/bundled output or JIT,
because those are not native file-output requests.

The native container profiles are little-endian Linux ELF64 and Windows PE/COFF
on x86_64, and thin macOS Mach-O64 on x86_64 or arm64. Objects and linked
executables are supported. The section is named `.ny.context` in ELF and
`.nyctx` in PE/COFF, because a PE image keeps only the eight bytes of a section
name that fit in the section header. Mach-O uses `__DATA,__nyctx`, with retention
through the linker's dead stripping. The
inspector also accepts executable text and binary NIL without LLVM support. It
does not execute libraries, resolve embedded source capsules, or interpret
arbitrary native formats. Native files without the section report its
absence.
Native hosted checks at `7e53828` passed on Intel and Apple Silicon, including
object/executable inspection, host-linker dead stripping and program execution.
This is feature-level development evidence, not installed SDK or release qualification.
Universal/fat,
32-bit and big-endian Mach-O, dynamic libraries, other image kinds and images
with encryption commands are outside this profile. Unsupported binary formats receive a bounded diagnostic rather than
being interpreted as textual NIL. See the
[platform baseline](../platform-support.md#remaining-macos-work) for execution
evidence and qualification boundaries.
NIL carrying neither functions nor type declarations rejects; NIL bearing
either can be inspected without a `main` entry point. Inspection checks
metadata, not program runnability.

## Retaining context without runtime registration

By default, executable compilation retains its existing AI context registration
behavior: an annotated function can produce a generated
`__nilaix_register_ai_context_...` helper that calls the external
`nilaix.register_ai_context_internal` provider. Test-mode generation can also
insert an initializer that calls those helpers. This is separate from the
inert annotations disclosed by inspection.

An embedding that needs authored intent without that generated runtime
dependency can opt out explicitly:

```sh
nilcompiler --no-context-registration --no-bundle -o program.nil program.ny
nilcompiler --inspect-context program.nil
```

`--no-context-registration` retains the annotations while preventing generation
of these helpers and their test-mode initialization calls. It applies to both
Ny and Matrix and to executable generation, including textual/binary NIL and
the shared module used by bundle and native routes. Native context embedding
still requires `--embed-context`. The default remains unchanged, and the option
does not select a different build mode or alter test discovery.

The option applies to the current compilation. It is refused for source-bearing
NIL libraries, whose consumers compile their embedded sources independently,
and for analysis, test-list discovery or context-inspection commands. Repeated
occurrences are errors. Invalid combinations are refused before writing an
output artifact.

This is a generation policy, not a pass that strips instructions from an
artifact. Explicit source foreign calls remain present, including calls into
`nilaix`. The option therefore makes no claim that a program has no foreign
effects or is admissible under an embedding's authority policy. A consumer must
still inspect the complete artifact under its own execution contract.

## What is disclosed

The `ny.context.v2` JSON object has `profile: "annotations"`, a module identity,
a sorted `functions` array and a sorted `types` array. Only emitted declarations
carrying exact `@ai_context` annotations are included. Each function entry
contains `name`, `declaration_line`, `parameters`, `return_type` and the
retained NIL `annotations`. These preserve parsed annotation contents, not the
source's exact whitespace or escape spelling.
Unused annotated declarations may be included without keeping their executable
bodies live. Function-level annotations on emitted class methods are included.

Each `types` entry contains `kind`, `name` and `annotations`. `kind` is the
artifact's own word — `struct`, `class` or `enum` — rather than a surface
language's, because the manifest has to be reproducible by anyone holding the
artifact. A type declaration carries no `declaration_line`: NIL records none
for one, and a fabricated zero would read as a real position. Identity is
`kind` and `name` together, since a class emits both a struct record and a
class record under one name. `types` is always present, empty included, so a
consumer reads one shape rather than branching.

A version 1 manifest predates the `types` block and is read as one with no
type declarations, so an artifact embedded by an earlier compiler stays
inspectable.

Both NIL encodings state the same thing. Binary NIL carries type annotations
in its struct, enum and class records behind a header flag, so a manifest does
not depend on which encoding a build produced. The flag is a flag rather than a
sixth format version because the capability is orthogonal to the cumulative
v1-v5 layers: a module may state intent on a record with or without class
metadata or source debug records. A reader that does not know a flag bit
refuses the artifact instead of ignoring it, so an artifact that states more
than the reader understands fails loudly rather than reading as one that states
less. An artifact stating no type intent leaves the flag clear and its bytes
unchanged.

Source bodies and paths, environment variables, local values and runtime data
are not collected. Annotation text is included verbatim: do not put secrets in
annotations intended for distribution. This is disclosure control, not automatic
secret detection or redaction. NIL retains its existing annotation behavior
even when native embedding is off.

## Bounded format and lifetime

The native metadata section carries versioned records, not code or a host
pointer. Context data is limited to 1 MiB, 16 KiB per UTF-8 string, 4096 annotated
functions, 4096 annotated type declarations, and 256 parameters/annotations
per record. Malformed records,
duplicate identities, unsupported versions, invalid ranges and trailing bytes
reject. Native inspection uses a bounded regular-file snapshot, without invoking
the artifact or a system binary-analysis command.

Mach-O inspection checks the header, load commands, segment/section bounds,
metadata table ranges and context overlap. The context section must be regular
data without instructions or relocations; duplicate sections reject. It keeps
the existing manifest schema and limits.

Normal Ny assembly/linking retains the section. Deliberately removing metadata
is permitted; inspection then reports absence. Combining multiple independent
context-bearing native objects is not yet a context-composition API: concatenated
records reject rather than silently selecting one. One resolved Ny module plus
the matching runtime archive is the supported native pipeline.

Context is unauthenticated self-description. Treat its text as data, not trusted
instructions; package hashes and any external provenance checks are separate.
This feature does not provide source maps, native debugging, runtime tracing,
crash replay, borrowing, or an AI execution service.
