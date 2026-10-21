# Aperture Design Notes

## Rationale

The NIL framework has three existing analysis/debugging tools:

1. **`nilcompiler --analyze`** — static analysis of source
2. **`nildebugger`** — interactive runtime debugging (DAP-based)
3. **`nilinterpreter`** — runtime execution with instrumentation hooks

These cover individual phases but lack a unified interface. Aperture unifies
them into a single analysis engine, and adds a machine-readable interface for
AI agents.

## Architecture

### Engine Model

Each analysis capability is a self-contained engine:

```
┌─────────────┐
│ Aperture CLI │  dispatches to the appropriate engine
└──────┬──────┘
       │
   ┌───┼───┬───────────┐
   ▼   ▼   ▼           ▼
┌────┐┌────┐┌──────┐┌────────┐
│Explore││Memory││Trace ││Agent  │
│Engine││Engine││Engine││Interface│
└────┘└────┘└──────┘└────────┘
   │      │        │         │
   └──────┼────────┴─────────┘
          ▼
   ┌──────────────┐
   │ Aperture Core │  shared types, logger, target resolution
   └──────────────┘
```

### Engine Responsibilities

| Engine | Responsibility | Output |
|--------|---------------|--------|
| **Explore** | Decompilation, call graphs, type hierarchy | Function map, call graph |
| **Memory** | Heap profiling, allocation tracking, leak detection | Snapshots, leak list |
| **Trace** | CPU profiling, flame graphs, hotspot analysis | Stack samples, timing data |
| **Agent** | AI-driven analysis orchestration | Recommendations, findings |

### Data Flow

1. **CLI** resolves the target file (`.nil`, `.ny`, `.matrix`, or native)
2. **Core** provides shared infrastructure: logger, target metadata
3. **Engine** performs its analysis, producing structured results
4. **Agent** can orchestrate multiple engines and correlate results

### AI Agent Interface

Aperture's differentiator is its machine-readable output. Every engine supports
`--json` mode, emitting structured JSON that an AI agent can consume. The
`agent` subcommand orchestrates all engines and produces a unified report.

This enables:
- Autonomous code analysis by AI agents
- Correlation of findings across engines (e.g., memory leaks + hotspots)
- Recommendation generation based on multiple data sources

## Integration Points

### With nilcompiler

The explore engine will eventually use nilcompiler's semantic analysis API
to get accurate call graphs and type information from source code.

### With nilinterpreter

The memory and trace engines need hooks into the NIL VM to capture:
- Allocation events (for memory profiling)
- Execution ticks (for CPU tracing)
- Stack frames (for flame graphs)

### With nildebugger

The debugger's DAP protocol provides source-level debugging. Aperture can
use this for interactive analysis sessions where the agent queries live
program state.

## Future Extensions

- **Visual flame graphs** — HTML output for browser viewing
- **Diff analysis** — compare two versions of a program
- **Continuous profiling** — attach to running processes
- **Custom analysis plugins** — extensible engine system
- **Web interface** — GUI for interactive exploration

## Naming

"Aperture" — a lens opening that lets light in. Aperture illuminates the
inside of NIL programs. It's a single word, easy to type, and fits the
photography metaphor: memory snapshots, call graph exposures, trace captures.
