# Aperture — NIL Framework Analysis & Debugging Suite

Aperture is an all-in-one static and dynamic analysis environment for the NIL
framework. It unifies code exploration, memory profiling, performance tracing,
and interactive debugging in a single tool designed for both human developers
and AI agents.

## Vision

Aperture brings together what JetBrains split across dotPeek (decompilation and
exploration), dotMemory (memory profiling), and dotTrace (performance tracing) —
but with the ability for AI agents to drive analysis, correlate findings, and
surface actionable insights.

## Capabilities

| Module | Purpose | JetBrains Analog |
|--------|---------|-----------------|
| `aperture-explore` | Code navigation, decompilation, call graphs | dotPeek |
| `aperture-memory` | Heap profiling, allocation tracing, leak detection | dotMemory |
| `aperture-trace` | CPU profiling, flame graphs, hotspot analysis | dotTrace |
| `aperture-agent` | AI-driven analysis and recommendation engine | — |

## Architecture

```
aperture/
├── CMakeLists.txt          # Build configuration
├── README.md               # This file
├── src/                    # Core source
│   ├── explore/            # Code exploration engine
│   ├── memory/             # Memory profiling engine
│   ├── trace/              # Performance tracing engine
│   ├── agent/              # AI agent interface
│   ├── core/               # Shared utilities and types
│   └── main.cpp            # CLI entry point
└── docs/                   # Design documentation
```

## Building

Aperture is built as part of the NIL SDK:

```bash
./scripts/build-sdk.sh
```

## Usage

```bash
# Explore code structure and generate call graphs
sdk/aperture explore --call-graph example.nil

# Profile memory allocations
sdk/aperture memory --profile example.nil

# Trace execution performance
sdk/aperture trace --flame example.nil

# Agent-assisted analysis (interactive)
sdk/aperture agent analyze --target example.nil
```

## AI Agent Integration

Aperture exposes structured analysis output (JSON, NDJSON) designed for
machine consumption. The `agent` mode provides a conversational interface
where an AI agent can:

1. **Query the codebase** — ask about call graphs, dependencies, or types
2. **Correlate findings** — combine memory and trace data to spot patterns
3. **Generate reports** — produce structured analysis summaries
4. **Recommend fixes** — suggest code changes based on detected issues

This is Aperture's differentiator: analysis that doesn't stop at the human
screen. An AI agent can operate Aperture autonomously, producing insights
that no single human analyst could gather in the same time.

## Status

Prototype. Core architecture is being defined alongside the NIL 1.0 release.
