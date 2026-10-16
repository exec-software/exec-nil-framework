# AI-native runtime and executable context

**Status:** Proposed design track

The bounded annotation-only context manifest is now implemented; see
[its contract](executable-context-manifest.md).
The broader trace, source-map, replay and diagnostic systems below remain
proposals. Annotation contents are disclosed verbatim, not automatically
redacted or authenticated.

## Intent

Ny should be unusually legible and controllable to both people and software
agents without making ordinary programs depend on an AI service. The useful
building blocks already visible in the repository include `@ai_context`
annotations, NIL metadata hooks, structured debugger commands, crash artifacts,
and a prototype interpreter control channel.

The goal is a coherent, opt-in observability contract rather than a collection
of unrelated AI-branded features.

## Design principles

- **Local and deterministic first.** Compilation and execution never require a
  network model.
- **Structured and inspectable.** Human-readable views must have stable
  machine-readable forms.
- **Opt-in disclosure.** Context embedding has explicit profiles; source,
  secrets, and user data are excluded by default.
- **Semantic parity.** Instrumentation may observe execution but cannot change
  program meaning.
- **Budgeted evidence.** Trace volume, retained context, and runtime overhead
  have visible limits.
- **Replay before speculation.** A diagnosis should point to reproducible
  source, NIL, inputs, tool versions, and events.

## Candidate system: Ny Context Manifest

Compile selected annotations and symbol summaries into a versioned manifest
associated with NIL and native artifacts. A manifest could include:

- build/toolchain identity and source-content hashes;
- module, public symbol, effect, invariant, and ownership summaries;
- selected `@ai_context` intent, assumptions, constraints, and recovery hints;
- NIL/native source maps and capability declarations; and
- redaction policy plus an integrity hash.

The first prototype should use a sidecar file so the schema can evolve safely.
Embedding it in binary NIL or a native executable should happen only after
round-trip, size, privacy, and stripping behavior are tested.

## Candidate system: structured runtime trace

Define a common event envelope for `nilinterpreter`, the debugger, JIT, and native
runtime:

```text
sequence, monotonic_time, route, thread, module, function, source_span,
event_kind, values, causal_parent, context_id
```

Initial event kinds should be deliberately small: function enter/exit, fault,
allocation/free, assertion, external call, log, and user checkpoint. Values
need type tags and configurable redaction. JSON Lines is appropriate for an
early sidecar transport; a compact trace format can follow measured need.

## Candidate system: execution capsule

On a conformance failure, the route harness can retain a bounded capsule:

- source and dependency hashes;
- compiler flags and exact route;
- textual or binary NIL;
- stdout, stderr, exit status, and structured diagnostics;
- context manifest and trace tail; and
- deterministic input/seed information.

Capsules make bugs replayable by a human, CI worker, or coding agent. They must
have an explicit retention directory and never capture environment variables by
default.

## Candidate system: AI-facing diagnostics

All tools should eventually offer the same versioned diagnostic object while
retaining excellent terminal rendering. Required fields include stable code,
severity, phase, source span, message, related spans, fix candidates, and a
small evidence payload. An AI should not need to scrape colored prose to locate
a type or backend failure.

## First safe experiments

1. Make the conformance harness write a route result manifest and retain
   artifacts only on failure.
2. Unify debugger and compiler diagnostic envelopes without changing language
   semantics.
3. Round-trip existing `@ai_context` data to a versioned sidecar and measure
   its size on the production applications.
4. Add interpreter trace events for function boundaries and faults after binary
   function identity is repaired.

Each experiment graduates only when it has schema-version tests, privacy
defaults, bounded overhead, and at least one real debugging use case.
