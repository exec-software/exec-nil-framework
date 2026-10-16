# NIL-0004: debugger state belongs to the VM

- **Status:** Implemented as a preview
- **Date:** 2026-09-13

## Question

How should `nildebugger`, editor integrations, and future AI inspection APIs
observe a running Ny program without drifting away from interpreter semantics?

## Evidence

The recovered debugger maintained a second locals stack and a separate function
name stack. Normal execution used the VM's call frames instead, so locals and
backtraces could describe state that the program did not actually have. Global
inspection recursed through the debugger rather than reading the VM, a false
conditional breakpoint still stopped execution, the first `run` could skip
`main`, and a restart retained program-owned state. In JSON mode, program output
shared stdout with control messages without an explicit framing boundary.

These are architectural failures rather than isolated command bugs. Any second
mutable model of runtime state will eventually disagree with calls, returns,
allocation, or restart behavior. That disagreement is especially harmful to an
AI client, which needs observations to be causal and reproducible.

## Decision

The VM is the only owner of execution state. It exposes read-only snapshots of
its real call frames for debugger inspection; the debugger does not mirror
locals or function names. A debugger `run` creates a pristine invocation through
an explicit VM reset operation, while host configuration such as arguments,
memory mode, callbacks, and output routing remains configuration. A single-step
primitive executes exactly the current instruction, and higher-level step-over
and step-out policies are derived from the VM's real call depth.

The JSON Lines mode parses one complete JSON object per request and redirects
ordinary Ny output into structured `output` events. Global evaluation reads VM
globals directly. Breakpoint callbacks decide whether execution pauses, so a
false condition genuinely continues. Until NIL carries per-instruction source
identities, a requested breakpoint file must resolve to the loaded NIL artifact
and an unresolved or ambiguous line must fail honestly.

An embedded debugger must also retain control of its own process and protocol
streams. `std_sys.exit` is therefore trapped per VM in `nildebugger` and becomes
a terminated event with the requested exit code; the standalone
`nilinterpreter` retains normal process-exit behavior. JSON Lines stdin belongs
only to the control protocol, so `console.read_line` receives an isolated EOF
stream until an explicit debuggee-input exchange exists. Breakpoint conditions
compare typed runtime values, preserving exact `i64` behavior above `2^53`, and
JSON inspection bounds recursive values, replaces non-finite numbers with
`null`, and emits finite doubles with round-trip precision.

## Verification contract

Component tests cover first execution, clean restart, real nested frames and
locals, global evaluation, false conditional breakpoints, invalid file/line
breakpoints, missing `main`, NIL-library rejection, per-VM callback isolation,
step-over/out behavior, exact mixed numeric conditions, bounded JSON values,
and routed input. A process-level JSON test covers strict parsing, program
arguments, two runs, register/local queries, framed stdout, global evaluation,
trapped program exit, debuggee-input isolation, protocol termination, and CLI
rejection paths. The standalone system-exit route guards the interpreter side
of the split.

## Follow-up

Add a versioned source map to NIL before claiming source-file breakpoints. Then
define a shared, capability-negotiated inspection protocol for `nildebugger` and
`nilinterpreter --ai`, with a richer expression grammar, asynchronous pause,
streaming output, process attach where the host can support it safely, and a
standard Debug Adapter Protocol bridge for editors. Those clients should all
consume the same VM snapshots rather than grow new runtime mirrors. Move the
remaining process-global PostgreSQL connection registry behind VM-owned
resource lifetime before treating multiple long-lived debug sessions as
isolated.
