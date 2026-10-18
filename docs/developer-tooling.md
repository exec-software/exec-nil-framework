# Developer tooling

Ny and Matrix need one semantic view across compilation, analysis, navigation
and editing. The compiler is the source of truth for language meaning. Editor
services should consume its results while retaining tolerant parsing for
incomplete code.

## Current foundation

The compiler provides parse, import, type and ownership diagnostics, declaration
reflection, and a built-in analyzer with stable rule codes. `nilbuild` supplies
an explicit local project configuration and requires checking before building
or running. VS Code consumes that configuration for live analysis and project
tasks, displays matching imported-file diagnostics, and offers guarded style
fixes. See [projects](tools/projects.md) and the [analysis API](analysis-api.md).

The language server currently has a separate tolerant, document-local index.
It supplies useful local completion, hover, definitions and outline information,
but it does not have the compiler's resolved import graph or reference bindings.
Declaration snapshots alone are insufficient to implement reliable cross-file
rename, references or semantic completion.

## Next implementation stages

| Stage | Deliverable | Acceptance evidence |
| --- | --- | --- |
| Shared workspace model | Compiler snapshots with stable per-snapshot symbol IDs, declarations, references, resolved types and precise source spans; unsaved overlays for all open inputs. | Mixed Ny/Matrix projects, dirty imports, deletion/rename, shadowing, overloads, generics and incomplete code cannot produce stale bindings. |
| Semantic editing | Import-aware completion and signatures, cross-file definition/references, and transactional rename edits built on that model. | Real extension-host tests exercise edit/undo, Unicode offsets, cancellation and multi-root workspaces against an installed SDK. |
| Analyzer extensions | Versioned read-only semantic queries and an explicit, bounded host for language-authored analyzers; shared diagnostics, configuration and fix protocol. | Malformed output, limits, analyzer failure and source-version mismatch cannot apply incorrect edits or corrupt compilation. |
| Project graphs | Explicit local project references, library/executable targets and dependency ordering, followed by a content-based incremental cache. | Cycles, output collisions and changed dependencies fail or rebuild predictably; a failed build cannot run stale artifacts. |
| Integrated debugging | Project launch profiles, consistent source maps, richer variable inspection and native-debugger integration. | End-to-end stepping and breakpoint tests cover both languages and each claimed execution route. |

Package resolution, lock files and distribution publication are separate from
the local build driver. Their contracts belong to the package-manager design.
Native compiler plugins are not a prerequisite for a useful analyzer host.

The near-term priority is the shared workspace model. Adding editor-only
heuristics for references or rename would create a third interpretation of the
languages and make those features difficult to trust. Each stage should ship
with a small set of executable acceptance tests before its support claim grows.
