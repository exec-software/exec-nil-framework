# nildebugger

`nildebugger` debugs runnable textual or binary NIL programs using the canonical
Ny interpreter. `nildebugger --dap` provides source debugging over standard DAP
`Content-Length` framed stdio; `--json run program.nil` retains the Ny-specific
JSON Lines interface. Neither protocol starts a network server.

## Source debugging

Compile with `nilcompiler --debug-info --no-bundle --output program.nil main.ny`
(add `--emit-binary-nil` for NIL v5). Source metadata is opt-in and contains absolute
source paths and lexical names; do not distribute it when those are private.
The VS Code extension automatically makes a temporary debug artifact for a
`.ny` launch, or accepts an existing `.nil` built with `--debug-info`.

The DAP adapter supports launch/configuration, exact source-path and line
breakpoints, scalar conditional breakpoints, continue, source step-in/over/out,
pause, stack frames, lexical locals, read-only name/member evaluation, aggregate
inspection, and termination. Inner shadows hide outer names only while in
scope. Values are snapshots of actual VM frames, not reconstructed execution.
Missing executable lines are reported as unverified breakpoints; no synthetic
source locations or variables are invented. Instruction-only NIL remains
usable in text/JSON modes, but DAP rejects it with a request to recompile.

Stepping into libraries requires their source-backed files (the full SDK's
`nilstd` directory supplies these). Binary-only library imports retain function
names and line hints but do not supply an editor-openable source map; source
step-in skips those bodies. A runtime failure inside one still exposes its
real function frame without inventing a source path. Synthetic `nillib://`
identities are diagnostic keys only; this adapter cannot serve embedded-source
documents until the debug artifact carries a source capsule.

DAP `launch` accepts `program`, `args`, `cwd`, `stopOnEntry` (default true), and
`memoryMode` (`safe`/`gc`, default safe; `unsafe`/`manual` explicitly opt in).
Only one launch is accepted per adapter process. It exposes the main VM as one
thread; isolated parallel workers finish before their parent instruction
returns and are not separately step-debugged.

DAP execution runs on one worker; the control thread never inspects a running
VM. `pause` and `terminate` request a cooperative stop between instructions, so
blocking foreign calls, sleeps, and a running parallel-map instruction must
return before they take effect. Program stdout is streamed incrementally as
bounded output events, including while execution is running. The adapter
rejects oversized or malformed framing (8 KiB headers, 1 MiB requests).

Native-executable attach/DWARF, data breakpoints, hit counts, logpoints, variable
mutation, reverse execution, arbitrary expression execution, and interactive
program stdin are not implemented. DAP and JSON stdin belong exclusively to
their control protocol; a program input read encounters explicit EOF.

## Current protocol boundaries

- Program artifacts are read once as a no-follow regular-file snapshot, capped
  at 256 MiB. Symlinks, FIFOs, directories, oversized inputs, and files that
  change during the read are rejected before parsing. Secure-read, parse, and
  semantic-validation failures leave the previously validated VM program
  intact.
- In JSON Lines mode, program output is buffered until an execution command stops or terminates.
  Output-event ordering is therefore command-bounded, not live streaming.
- JSON Lines commands remain synchronous; use DAP for asynchronous pause.
- `console.read_line` deliberately receives EOF in JSON mode until an explicit
  input request/reply protocol is added. Text mode continues to use the host
  console normally.
- The PostgreSQL native runtime's connection registry remains process-global.
  Its ownership and cleanup should move behind a per-VM resource boundary
  before multiple long-lived debug sessions use it concurrently.
