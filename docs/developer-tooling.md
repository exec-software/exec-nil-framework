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

Workspace source requests now let the compiler analyze open unsaved dependencies
alongside the primary buffer. Import resolution and Ny textual includes consume
those snapshots, and the editor invalidates results when any input changes.
The optional `nil.semantic.v1` index now adds original UTF-8 name locations and
snapshot-local symbol IDs. The checker retains supported declaration/reference
links at resolution time. VS Code uses those links for Go to Definition across
the two frontends, including unsaved imports and lexical shadowing, after
validating the entire native input set. Textual include snapshots are watched
and checked even when their extensions cannot participate in overlays.

The compiler now certifies complete occurrence sets for eligible lexical local
variables and loop binders. VS Code uses those explicit capabilities for Find
All References and local rename. Rename follows compiler-provided identifier and
reserved-name policy, then checks the proposed source and verifies every retained
binding before returning an edit. Named parameters, functions and globals still
need a graph that includes their external callers. See F42 in the
[findings](../matrix/docs/next-version-findings.md).

The compiler also supplies a bounded completion query for nonempty unqualified
identifier expressions, including unresolved prefixes. It captures actual visible
bindings, imports and declaration order before checking the selected expression.
VS Code verifies the consumed sources before offering these candidates. See
[NIL-0011](design/NIL-0011-completion-queries.md) for the query contract.

The language server retains a separate tolerant, document-local index.
It supplies fallback completion, hover and outline information,
but it does not have the compiler's resolved import graph or reference bindings.
Its definition, references and rename providers are suppressed while compiler analysis is enabled;
disabling analysis retains that fallback.
Declaration snapshots alone are insufficient to implement reliable cross-file
rename, references or semantic completion.

The new index is explicitly partial. It covers supported source functions,
variables, parameters and loop binders; member/type/pattern occurrences and
failed-code recovery are still incomplete. It requires a successful check and
is withheld for binary-library dependencies, representation limits, or inputs
whose identity cannot be validated. Function `type` values describe returns,
not complete signatures or a stable type graph. See
[NIL-0010](design/NIL-0010-semantic-source-index.md) for the boundary.

## Next implementation stages

| Stage | Deliverable | Acceptance evidence |
| --- | --- | --- |
| Complete semantic coverage | Extend the implemented partial index to fields, methods, type uses, pattern bindings, precise generic/overload identities, and recoverable incomplete code. Add binary dependency identity and generated-source maps. | Mixed Ny/Matrix projects, dirty imports, deletion/rename, shadowing, overloads, generics and incomplete code cannot produce stale or guessed bindings. |
| Semantic editing | Extend completion to members, blank/type contexts and recoverable parse errors; add signatures, complete cross-file references, and transactional rename on the expanded model. Extend existing compiler-backed definition navigation as coverage grows. | Real extension-host tests exercise edit/undo, Unicode offsets, cancellation and multi-root workspaces against an installed SDK. |
| Analyzer extensions | Versioned read-only semantic queries and an explicit, bounded host for language-authored analyzers; shared diagnostics, configuration and fix protocol. | Malformed output, limits, analyzer failure and source-version mismatch cannot apply incorrect edits or corrupt compilation. |
| Project graphs | Explicit local project references, library/executable targets and dependency ordering, followed by a content-based incremental cache. | Cycles, output collisions and changed dependencies fail or rebuild predictably; a failed build cannot run stale artifacts. |
| Integrated debugging | Project launch profiles, consistent source maps, richer variable inspection and native-debugger integration. | End-to-end stepping and breakpoint tests cover both languages and each claimed execution route. |

Package resolution, lock files and distribution publication are separate from
the local build driver. Their contracts belong to the package-manager design.
Native compiler plugins are not a prerequisite for a useful analyzer host.

The near-term priority is expanding retained binding coverage beyond certified
locals and extending completion beyond supported identifier expressions. Both frontends
now preserve supported original-source name spans, and the checker retains
local bindings after their scopes are discarded. Type strings and declaration
names still cannot stand in for a complete symbol/type model. Expanded/generated
locations remain non-editable until source maps exist. Matrix rename also needs
richer spelling rules beyond the initial explicit replacement of certified local
occurrences, including cross-language declarations and named argument labels.

Adding editor-only heuristics for references or rename would create a third
interpretation of the languages and make those features difficult to trust. Each stage should ship
with a small set of executable acceptance tests before its support claim grows.
