# NIL-0009: Workspace source snapshots for analysis

- **Status:** Implemented initial source-overlay contract
- **Scope:** Compiler analysis and editor input consistency
- **Date:** 2026-10-19

## Context

A primary-buffer overlay cannot describe a human editing an application and
its dependencies together. Reading unsaved imports from disk produces checks
against a different program from the one visible in the editor. Reliable
semantic services need consistent source inputs before they can publish
bindings or propose changes across files.

## Decision

`nilcompiler --analyze --workspace-stdin` accepts a bounded
`nil.analysis.workspace.v1` JSON envelope of absolute source paths and exact
texts. The command-line source identifies the primary file. The source provider
uses these texts during normal import resolution and Ny textual inclusion,
including files not yet present on disk. Unreachable overlays do not become
compilation units. Canonical identities prevent conflicting aliases; output
retains the established logical primary identity and `nil.analysis.v1` schema.

The editor captures an immutable set containing the primary source, every dirty
named local source and clean sources with open text or diff tabs. Clean cached
documents left behind after closing a tab are excluded. The editor tracks source
versions and tab membership. Changes to any input or project context invalidate queued,
running and asynchronously publishing results. Imported diagnostics may refer
to dirty buffers only when the compiler snapshot matches the captured text.
Quick fixes remain limited to the primary document and its checked version.

## Consequences

Live checks can now account for unsaved dependencies without a mirrored source
tree or writes to user files. The compiler remains a process per check; project
builds continue to use saved sources. Raw `--source-stdin` clients retain their
existing single-buffer contract.

Request limits and source-provider limits make oversized input explicit errors.
Textual expansion still lacks a source map, so it cannot provide editable
locations. Semantic indexing needs precise name spans and retained resolution
identities in both frontends and the checker; overlays alone do not establish
references, rename or semantic completion.

The [analysis API](../analysis-api.md) defines the request and bounds. The
[developer-tooling plan](../developer-tooling.md) records the remaining stages.
