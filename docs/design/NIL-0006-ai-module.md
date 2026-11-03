# NIL-0006: opt-in AI control for NIL programs

- **Status:** Shared local control and restricted same-process execution checkpoints (2026-11-03)
- **Components:** `nilinterpreter`, `nilagent`, VM execution engine
- **Scope:** Development API, not a general process snapshot or code hot-reload system

## Enable and connect

```bash
nilinterpreter --ai program.nil
nilinterpreter --ai-wait program.nil
```

`--ai` starts the local control server and executes normally. `--ai-wait`
pauses at the first instruction of `main`, before executing it. `--no-ai`
disables both options. Ordinary interpreter runs do not open a control channel.

Windows uses `\\\\.\\pipe\\ny_ai_<pid>`; Unix uses
`/tmp/ny_ai_<pid>.sock`. Endpoint creation and access restrictions complete
before the interpreter announces readiness. Windows uses a protected
current-user DACL, rejects remote clients and requires the first pipe instance.
Linux checks the connecting user's UID; its socket is owner-only. Existing
endpoint paths are never unlinked to make startup succeed. Cleanup removes
only the socket instance this server created.

This remains the explicit **JSON Lines v1** endpoint, not ETP framing.
Requests contain string `id` and `method` fields. Responses contain the same
`id`, and either a `result` or an `error` string (the other field is null).
Duplicate fields, invalid UTF-8, trailing JSON, and unrecognized arguments
are rejected. One frame is at most 64 KiB; response payloads are at most
60 KiB. Clients must read the initial `connected` handshake.

The server handles one client at a time, closes an idle connection after five
seconds and bounds writes to one second. Disconnect does **not** resume a
paused VM or discard its checkpoint; reconnect explicitly. Requests that cannot
reach an instruction boundary within two seconds are cancelled before dispatch.
An operation already started may finish after a response deadline; query state
before retrying. A blocking foreign operation is not forcibly interrupted.

## Execution control

All VM reads and mutations run on its execution thread between instructions.
The transport thread never changes debug state or copies a running heap.

| Method | Result |
| --- | --- |
| `vm.status` | PID, version, PC, running/paused state and capabilities. |
| `vm.pause` | Acknowledges only after execution has reached a safe boundary. |
| `vm.resume` | Continues from the current PC. |
| `vm.step` | Executes one instruction, then pauses at the next boundary. |
| `vm.inspect` | Bounded PC, operand stack, frames/locals and globals summary. |
| `vm.stack`, `vm.locals`, `vm.globals` | Aliases for the same real inspection summary. |
| `vm.snapshot` | Captures a checkpoint of the paused VM; returns its string ID and retained-byte accounting. |
| `vm.restore` | Restores the supplied string `checkpoint` ID and remains paused. |
| `vm.send_msg` | Existing Windows WebView-specific message injection into handle 1; not a general UI protocol. |

`vm.eval` and `vm.reload` are unsupported and return errors. This does not
claim that restoring execution state replaces code or updates a DLL.

Example requests, after the handshake:

```json
{"id":"1","method":"vm.pause"}
{"id":"2","method":"vm.snapshot"}
{"id":"3","method":"vm.step"}
{"id":"4","method":"vm.restore","checkpoint":"1"}
{"id":"5","method":"vm.resume"}
```

Discover `checkpoint.same-process.v1` in status before checkpoint commands.
Only the **latest successful checkpoint** is retained by the standalone
interpreter. A failed capture preserves the previous checkpoint. Checkpoint
IDs are session-local; they are not filenames or portable serialized artifacts.

Inspection is distinct from storage: integer values are decimal strings,
text is bounded hex with explicit byte length/truncation, and aggregate values
are summaries. Inspection has independent frame/binding/stack limits and can
reject a large state without invalidating a checkpoint.

## What a checkpoint restores

The VM captures the PC, real call frames, return positions, local/global values,
operand stack, constants, heap allocations and their identities, owned scopes,
active/pending/saved fault state, allocation/GC counters, execution arguments,
memory mode, strict-bounds setting and requested exit state. Accepted aggregate
aliases are preserved and independent of subsequent execution. Restore clones
and validates before swapping state; the saved checkpoint can be reused.

The first profile is deliberately **deterministic VM execution only**:

- It rejects programs containing interop, console, time, inline-assembly or
  parallel instructions, and builtin/dynamic calls. This is a whole-program
  restriction, even when the excluded instruction has not executed yet.
- Foreign function values, boxed values and cyclic aggregate graphs are rejected.
- Open file handles and active/requested memory profiling are rejected.
- Capture is limited to 16 MiB of accounted retained storage, 65,536 value
  visits and a maximum value depth of 64. Accounting is not an RSS measurement.
- A checkpoint belongs to one VM and one loaded program generation; it cannot
  be imported into another VM or used after loading another program.
- Host callbacks, debugger configuration, native libraries, OS handles, UI,
  database transactions and external effects are not captured or rolled back.
  Embedders remain responsible for their callbacks' external effects.
  Restoring does not erase diagnostics already written to stderr.

Thus this is real, reusable **same-process execution restore for the admitted
profile**, not full arbitrary-application checkpointing, disk persistence,
cross-process migration, native/JIT process rewind or state-preserving hot reload.
Extending the profile requires explicit ownership and external-resource
contracts, not removing admission checks.

## Tooling and future protocol work

`nilagent` negotiates checkpoint capability through status. Its commands
`app.pause`, `app.resume`, `app.step`, `app.state.inspect`,
`app.state.snapshot` and `app.state.restore` bridge the corresponding methods.
Capture and restore require an explicitly paused VM; restore takes the returned
`checkpoint` ID. `app.reload` still reports unsupported.

Debugger JSON Lines/DAP and editor LSP remain separate APIs. The bounded ETP/CBOR
codec is available, but live control migration still needs an explicitly
versioned NIL profile and separate endpoint/protocol negotiation; do not send
binary ETP to this JSON endpoint. Existing AI context annotations, crash-report
helpers and FFI DLL reload experiments are distinct features, not evidence of
execution-checkpoint support.

## Verification

`nil.vm-control-checkpoints` tests real execution, heap/alias/fault restoration,
repeatability, owner-thread callbacks and rejection boundaries.
`nilinterpreter.ai-checkpoint-contract` exercises a live interpreter over its
local transport: pause, nested-state capture, mutation, repeated exact restore
and continuation. `nil.ai-transport` and `nilagent.ipc` separately test framing,
capabilities, deadlines, privacy, malformed peers and cleanup.
