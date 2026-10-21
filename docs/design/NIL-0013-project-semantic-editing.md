# NIL-0013: Project-scoped references and transactional rename

- **Status:** Proposed
- **Scope:** Compiler analysis, project context and editor semantic services
- **Date:** 2026-10-20

## Context

The current semantic index supplies checked definitions and certified references
and rename for lexical locals. It does not establish a complete set of callers
for an exported function. Checking an imported file independently follows that
file's imports; it does not discover files that import it.

`nil.project.v1` describes one executable entrypoint. Its forward import graph
defines that executable's compilation inputs. It does not enumerate every
source in a directory, additional entrypoints, downstream projects, or external
consumers of a library. A project manifest therefore provides a useful analysis
root, but does not make a public declaration globally closed to new callers.

## Proposed contract

### Explicit scope

Project references should analyze the manifest entrypoint with the same SDK,
ordered import roots and analyzer policy as `nilbuild check`, together with
captured editor overlays. Results must identify the canonical entrypoint,
manifest context and every consumed native source. The selected document must
belong to that checked graph; its nearest manifest alone is insufficient.

A new capability must state that references are complete **within this
entrypoint's checked source graph**. Do not reuse the existing unqualified
`references_complete` flag for this broader but externally open scope. UI and
documentation must make the scope visible. Files merely found beneath the
project directory and callers in other projects are outside it. Multi-target
or workspace-wide completeness requires a separate project-graph contract.

### Source roles and identity

Start with named functions whose declarations and every relevant original
identifier token can be accounted for. The compiler must distinguish callable
references, selective-import names, namespace/module aliases, parameter
declarations and named argument labels. A token's spelling cannot determine its
role. An unrepresented occurrence must withhold the applicable editing
capability, rather than become a guessed reference.

Matrix labels must bind to the parameters of the resolved callable, including
proven immutable function values. Their original spans and source ordering must
survive argument reordering. Parameter edits must include those labels or remain
unsupported. Import selectors require their own resolved source roles; changing
a call without changing a selector is not a complete function rename.

IDs remain local to a compiler snapshot. Cross-check matching should use
validated declaration locations and explicit declaration/instantiation
relationships. A shared generic source token can participate in several checked
instantiations; incompatible bindings cannot be collapsed into one editable
occurrence. Overloads and method dispatch need equivalent identity contracts
before becoming rename targets.

### Snapshot and edit invariants

Every result must retain the complete bounded input graph, including imports
that emitted no symbols. A changed manifest, SDK selection, import-root order,
source identity, open-buffer version, dependency text, or graph membership
invalidates the result. Preserve the existing UTF-8 byte-span checks and
canonical path validation. Multiple editor aliases of any edited source must
be rejected unless the editor can update them consistently.

Rename preparation must calculate edits for all affected sources and apply
each source language's spelling and reserved-name policy. Mixed Ny/Matrix
spelling changes require an explicit validated conversion; Matrix folding must
not silently introduce a Ny collision. Unchanged files also matter because a
new name can capture a previously unrelated reference.

Before returning a workspace edit, check the manifest entrypoint again with
all proposed source texts overlaid together. Compare the entire retained
binding graph using translated source positions, including unrelated bindings,
import roles, argument-label roles and applicable generic identities. A second
successful type check alone does not prove that behavior was preserved.
Revalidate the original inputs after this speculative check. Return one
multi-file workspace edit only when every file and binding passes; a failure
must produce no partial edit. Test undo as a single editor transaction.

Binary dependencies need source identity and mapping support before they can
participate in this proof. Generated or transformed sources without exact
original spans remain non-editable. Limits must fail closed without truncating
the source graph or reference set, and cancellation must discard pending edits.

## Staged acceptance

1. **Scoped references:** introduce explicit graph scope, analyze the manifest
   entry, and certify a narrow named-function profile. Cover mixed frontends,
   selective/wildcard imports, aliases, shadowing, dirty and never-saved imports,
   and an unrelated file beneath the project directory. Confirm that unsupported
   tokens, binary inputs, ambiguous generic occurrences and exhausted budgets
   withhold the capability.
2. **Transactional function rename:** add per-source plans and speculative
   overlays, then verify all binding roles after checking. Exercise Unicode and
   CRLF locations, mixed-language spelling, unopened files, alias conflicts,
   manifest/dependency changes during checking, cancellation, capture rejection,
   and complete apply/undo behavior in the real editor host.
3. **Broader declarations and project graphs:** extend parameter/label,
   overload, method and type-use coverage only with independent completeness
   evidence. Multiple entrypoints and downstream projects require explicit graph
   membership before the UI can claim references or rename across them.

## Consequences

This proposal does not change the current local editing capability or imply
that cross-file rename ships. It provides a path from the source guarantees in
[NIL-0010](NIL-0010-semantic-source-index.md) and the workspace inputs in
[NIL-0009](NIL-0009-workspace-analysis.md) to bounded project editing without
reinterpreting names in the editor.
