# NIL-0006: opt-in AI control for NIL programs

- **Status:** Partially implemented prototype; substantial work remains proposed
- **Priority:** Research and core-tooling direction
- **Components:** `nilinterpreter`, `nilagent`, `nildebugger`, `nilaix`

## Summary

NIL-0006 explores a machine-readable control channel for a running NIL program.
The goal is to let a human-operated tool or software agent launch a program,
observe execution, inspect state, and issue narrowly scoped debug or application
commands without screen-coordinate automation.

The complete loop is not implemented. Today the repository contains an opt-in,
Windows-only control server in `nilinterpreter`, a separate JSON-mode debugger, and
application-control experiments. Runtime introspection, a working Unix
transport, and a finished `nilagent` bridge remain future work.

## How to enable the current prototype

The implemented switch belongs to the interpreter, not the compiler:

```bash
nilinterpreter --ai program.nil
```

`nilcompiler --ai` does not exist; the interpreter switch is the only documented way to
enable this prototype.

Without `--ai`, the interpreter does not start this control channel. The prototype is
therefore opt-in.

## Implemented boundary

On Windows, `nilinterpreter --ai` creates this named pipe:

```text
\\.\pipe\ny_ai_<pid>
```

Messages are newline-delimited JSON objects. The parser is deliberately minimal
and this is not a complete JSON-RPC 2.0 implementation. A client currently needs
quoted `id` and `method` fields, for example:

```json
{"id":"1","method":"vm.status"}
```

After connection the server writes a `connected` event. It then recognizes:

| Method | Current implementation |
| --- | --- |
| `vm.status` | Returns process, protocol-version, and Ny-version fields. |
| `vm.pause` | Sets the interpreter debug state to paused. |
| `vm.resume` | Sets the interpreter debug state to running. |
| `vm.send_msg` | On Windows, tries to resolve the loaded WebView native module and inject a JSON string into handle 1. This is application-specific. |
| `vm.stack` | Returns a Phase 2 placeholder with no frames. |
| `vm.locals` | Returns a Phase 2 placeholder. |
| `vm.globals` | Returns a Phase 2 placeholder. |
| `vm.eval` | Returns a Phase 2 placeholder; it does not evaluate an expression. |

The current prototype has important limits:

- The Unix branch does not create its advertised domain socket.
- Stack, local, global, source-map, breakpoint, and expression introspection are
  unfinished.
- `vm.send_msg` depends on a particular Windows WebView export and assumes the
  first window handle.
- The `nilagent` IPC client does not yet provide a complete bridge to the Windows
  named pipe.
- Authentication, authorization, protocol hardening, concurrency guarantees,
  and compatibility versioning are not complete.

Do not expose the prototype to untrusted processes or present it as a stable
debug API.

## Related AI-facing capabilities

NIL-0006 is only one part of the framework's AI tooling direction:

- The parser retains function-level `@ai_context` annotations. Debug/test NIL
  code generation contains hooks for registering their structured payload with
  the test runtime.
- `nilcompiler/src/context_extractor.cpp` and its header implement richer
  annotation extraction and JSON/Markdown/HTML rendering, but no context
  extractor command is shipped.
- `nildebugger run --json program.nil` provides a separate JSON Lines interface
  for breakpoints, stepping, locals, registers, stack frames, watches, and
  evaluation. It is currently the more developed automation surface for NIL
  debugging.
- `nilaix/src/ny_crash.ny` contains explicit helpers for writing `.nycrash`
  artifacts in NyData form. These helpers are not automatic crash reporting for
  every Ny binary.

## Remaining proposal

Development should proceed in small, testable stages:

1. Specify and test a real framed protocol, including errors, capabilities,
   authentication, and version negotiation.
2. Implement the Unix-domain-socket server and matching Windows/Unix clients in
   `nilagent`.
3. Reuse the debugger's inspection model for stack, locals, globals,
   breakpoints, and evaluation instead of maintaining two incompatible debug
   semantics.
4. Add source maps and define safe pause/resume synchronization in `nilinterpreter`.
5. Generalize application message injection so it is registered explicitly and
   is not tied to WebView handle 1.
6. Connect `.nycrash` artifacts and `@ai_context` metadata through documented,
   opt-in APIs.
7. Add cross-platform integration tests before exposing the protocol as a
   supported SDK surface.

Possible future methods such as `vm.break`, `vm.call`, `vm.set_global`, and
`vm.crash_info` are design ideas only; they are not accepted by the current
server.

## Source locations

- `nilinterpreter/src/main.cpp` — current `--ai` server and request dispatcher
- `nilcompiler/src/vm.hpp` and `vm.cpp` — internal execution-engine and debug state
- `nildebugger/` — interactive and JSON Lines debugger
- `nilagent/` — experimental application controller and IPC client
- `nilaix/experimental/ai_ui.ny` — quarantined application/UI experiment
- `nilaix/src/ny_crash.ny` — structured crash-artifact helpers
- `nilcompiler/src/context_extractor.*` — unshipped annotation extractor
