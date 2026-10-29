# NIL-0022: Incremental project builds and NILData configuration

**Status:** Implemented initial local project graph and target scheduling profile.

## Context

The initial project driver checks and recompiles one executable on every build.
It has no dependency ordering between projects and no safe parallel scheduling
boundary. Its project manifest also predates NILData, leaving the framework's
own configuration outside the use cases that exercise the new notation.

The compiler combines an entrypoint and all imported units into one checked
program. It does not expose a separate-object contract for compiling individual
source modules independently. Parallelism must respect that compilation model.

## Decision

Retain one entrypoint per `nil.project.v1` manifest and add optional
`projectReferences`, an array of local manifest paths. References establish
dependency ordering, with canonical project identities, cycle rejection and
output conflict checks. They do not change source import lookup or imply a
package/library linker. Existing `includePaths` remain authoritative for
shared source modules.

The driver checks and builds projects in dependency waves. Independent checks
and compiler processes can run concurrently within a wave, bounded by
`--jobs` (1–64, default up to four hardware threads). `build --target all` adds
independent VM and native output tasks. Each compiler still handles a complete
program; this is not concurrent parsing or code generation within one program.

Every build runs fresh compiler analysis. Its additive dependency report lists
canonical native file paths and SHA-256 digests of the exact bytes consumed:
primary and imported sources, arbitrary-extension textual includes and binary
library containers. `dependenciesComplete` certifies successful disk analysis
with a complete, consistent list. Editor overlays and failed/incomplete checks
cannot certify a disk build. The native runtime archive and configured linker
driver are identified separately for native toolchain fingerprints.

The driver uses those inputs, effective project settings, tool identity and
artifact content to decide whether compilation and linking can be skipped.
Timestamp equality alone never justifies reuse. Fresh analysis also discovers
changed import resolution, including newly created higher-priority candidates.
Missing, invalid or incompatible cache state causes a rebuild. Older compilers
without complete dependency reports remain usable without cache reuse.
`--rebuild` bypasses the optimization explicitly.

Artifacts remain staged and are published only after a successful compiler
process produces a regular artifact of the required kind. Output locks prevent
two drivers from building the same outputs simultaneously; contention fails
with a diagnostic rather than waiting. Process-group cancellation, bounded
child output and timeouts apply to all active jobs. The complete graph is not
one transaction: successful independent projects may publish before another
project fails. A failed build never launches a stale program.

## Configuration format

`nil.project.nildata` is the preferred manifest, and NILData is the default for
new manifests, `describe` output and incremental state under
`<outputDirectory>/.nilbuild/`. `init` creates only a manifest; it does not
generate application source. `convert` validates the schema, keeps the manifest
in the same project directory to preserve import lookup and working directory,
and never overwrites an existing file.

JSON and a bounded declarative YAML subset remain supported for loading and
explicit creation/conversion. Deprecation applies to their role as the default
`nilbuild` configuration format, not to the standard library. The YAML subset
supports block mappings/sequences and JSON-compatible scalar/flow values,
without tags, anchors, aliases, merge keys or block strings. The
[project guide](../tools/projects.md) defines the complete surface.

Discovery selects the nearest ancestor directory with a supported manifest,
preferring NILData before JSON, YAML and YML in that directory. Explicit paths
remain available. The compiler/editor JSON protocols, SDK/release artifacts
and VS Code's own host configuration keep their established formats. JSON
machine consumers can continue to request `--format json`.

## Limits and follow-up

Analysis is intentionally not cached, so warm builds still pay for parsing and
checking. Further acceleration needs a compiler-owned persistent checked graph
or separate compilation contract, rather than inferring module boundaries in
the build driver. Project references do not yet produce reusable library
targets or resolve packages. Scheduling uses dependency waves rather than
starting a dependent immediately when its last prerequisite finishes.

Build inputs and host tools must remain stable during a build. Content checks
detect observed changes, but the driver does not create an immutable filesystem
snapshot or certify every host system library used by the linker. Lock
contention does not queue or recover another driver's work. Comment-preserving
manifest editing and richer NILData schema completion remain future tooling.

Qualification covers first/warm/forced builds, changed source and tool inputs,
arbitrary include filenames, import shadowing, binary libraries, damaged state
and artifacts, graph cycles and output collisions, bounded process overlap,
failure/cancellation, and cross-format conversion/discovery. The earlier
[NIL-0008](NIL-0008-project-tooling.md) remains the record of the initial
single-project behavior; this decision extends it.
