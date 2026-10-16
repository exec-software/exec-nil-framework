# Opt-in executable context manifest

Status: implemented, 2026-09-26; integration evidence and clean SDK qualification
are tracked with the implementation.

Ny's AI-facing metadata must be inspectable without executing the program or
contacting a model. Complete the sidecar experiment from `ai-native-runtime.md`
with a deterministic JSON view, then carry the same records in native objects.

## Contract

- `nilcompiler --inspect-context artifact` prints `ny.context.v1` JSON from
  executable text/binary NIL or an ELF64 little-endian x86_64 object/executable.
  Redirecting this read-only output produces the initial sidecar. Libraries,
  bundles without a native context section, and other host formats are not
  covered by the first native-inspection profile.
- `nilcompiler --native --embed-context` and `nilassembler --embed-context` opt
  into a read-only `.ny.context` section. Native embedding defaults off.
  This does not change NIL versions or require runtime/model support. NIL
  already preserves annotations; the assembler receives its opt-in explicitly.
- Include only emitted functions carrying exact `@ai_context` annotations:
  canonical function name, parameter signatures, result type, declaration line,
  original annotation text, and the NIL module identity. Sort functions by name.
  No source bodies/paths, build environment, locals, runtime values or automatic
  secret discovery. User-written annotation contents are disclosed verbatim;
  the switch is consent to include them, not a promise to redact secrets.
- JSON is a deterministic view of bounded versioned binary records, not an
  executable script or trusted instruction. The embedded representation starts
  with eight bytes `NYCTX\0\r\n`, little-endian u32 version 1, then module string,
  u32 function count and records. Strings are u32 byte length + valid UTF-8.
  A function record is name, u32 declaration line, u32 parameter count + parameter
  strings, result string, u32 annotation count + annotation strings.
- Limits: 1 MiB total context bytes, 16 KiB per string, 4096 functions, 256
  parameters/annotations per function, signed-32-bit source lines. Reject
  duplicate symbols, missing/invalid required fields, wrong versions, trailing
  bytes, malformed UTF-8 or oversized records. ELF inspection uses a bounded
  regular-file snapshot and validates section-table ranges/names before access;
  no subprocess invocation or artifact execution.
- The native section is retained through normal Ny object/link pipelines.
  Deliberate metadata stripping remains possible and inspect must report absence
  clearly. A manifest is self-description, not authenticated provenance: SDK
  hashes bind packaged artifacts separately.

## Evidence gate

Compare NIL sidecars, text/binary standalone objects and linked executables,
and compiler AOT output byte-for-byte after JSON parsing. Verify execution is
unchanged, default-off behavior, unused annotated functions, class methods,
Unicode/escapes, deterministic ordering and negative bounds/container cases.
Malformed input must not execute or hang, and failed embedding must preserve
an existing output. Run LLVM OFF inspection/option tests, native tests when
available, sanitizer parser tests and installed-tool checks. Source maps,
runtime tracing, native debugging and cryptographic attestation are separate.
