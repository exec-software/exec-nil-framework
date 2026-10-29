# NIL-0008: Shared project context and default analysis

- **Status:** Implemented initial local-project profile
- **Scope:** Compiler, project driver, SDK and editor integration
- **Date:** 2026-10-18

The initial behavior below is extended by
[NIL-0022](NIL-0022-incremental-project-builds.md), which adds project references,
incremental and parallel builds, and NILData as the preferred manifest format.

## Context

Compiler analysis already exists, but single-file editor tasks and framework
CMake scripts do not provide an application project contract. The language
server's document index is intentionally separate from full semantic checking.
Human developers need consistent import paths, diagnostics and build behavior
before more advanced editor features can become dependable.

## Decision

Ship `nilbuild` as a core SDK tool and use a declarative `nil.project.v1` JSON
manifest for an executable entrypoint, import roots, SDK/output locations and
default-analyzer policy. The compiler resolves source dependencies; this
profile does not create another module graph or package resolver.

`nilbuild check` uses `nilcompiler --analyze`; build/run require that check to
succeed. Compilation stages its output and publishes it only after success.
VS Code uses the same manifest context for direct compiler checks, preserving
its existing subprocess cancellation and source-version guards.

Default semantic rules run on the compiler's checked AST. Rule identifiers and
configuration are shared by CLI and editor. Imported diagnostics carry bounded
source snapshots; the editor publishes locations only when the snapshot matches
current disk content, and never applies imported fixes to a dirty document.

## Consequences

The first profile supports one entrypoint with imports and explicit VM/native
targets. Builds are complete rebuilds. Project-reference graphs, caches,
third-party analyzer hosting and semantic language-server indexing are future
increments, not implied features of the manifest. The project driver currently
executes on Linux. Compiler/runtime artifact formats and the product version are
unchanged.

The [project reference](../tools/projects.md) defines the supported command and
configuration surface; the [developer-tooling plan](../developer-tooling.md)
records the sequence toward workspace-wide language services.
