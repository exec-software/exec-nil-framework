# nilaix

`nilaix` is Ny's experimental UI, IDE, and AI-integration sandbox. It preserves
larger application experiments that are useful for evolving the compiler and
runtime, but it is not a stable framework, standard-library module, or shipped
SDK command.

## Layout

```text
nilaix/
  src/       Ny UI, editor, platform-wrapper, and AI integration sources
  experimental/  Quarantined prototypes with unavailable legacy dependencies
  native/    C++ platform and window/runtime experiments
  examples/  larger demonstration programs
  tests/     Ny and native UI fixtures
```

The root build does not currently install `nilaix` as a cohesive library. Files
may require unfinished syntax, manually built native dependencies, or a
particular desktop platform.

The out-of-tree process and WebView bridges still use legacy `ml_*` DLL/FFI
identities. Ny-facing module and file names are canonicalized here, but those
external ABI names remain until compatible `ny_*` bridge exports exist.

## AI-related experiments

- `src/ai_control.ny` explores programmatic widget discovery and control.
  `experimental/ai_ui.ny` is quarantined because it depends on the unavailable
  legacy `nilagentcc` module.
- `src/ny_crash.ny` provides explicit helpers for writing `.nycrash` reports in
  an older INI-like format historically labeled NyData, including reports
  synthesized by a host from interpreter stderr. This is not NILData 1 and does
  not install a universal crash handler.
- `src/nycromicon_integration.ny` and `src/language_server.ny` contain editor
  integration experiments; they are distinct from the supported core-tool names
  `nilcompiler`, `nilinterpreter`, and `nillsp`.

The opt-in `nilinterpreter --ai` control server lives in `nilinterpreter`.
Its current limits are documented in
[`docs/design/NIL-0006-ai-module.md`](../docs/design/NIL-0006-ai-module.md).

Treat this tree as prototype source: validate a specific example and all of its
native dependencies before using it as evidence that an API is supported.
