# nypm: Ny package-manager proposal

- **Status:** Proposal
- **Implementation:** No `nypm` executable is currently built or shipped

`nypm` is the canonical name for Ny's future package manager. The earlier
`NYSLong`/`nyslong` name is retired and should not appear in commands, file
locations, or new code.

## Goal

`nypm` is intended to make dependencies reproducible while staying compatible
with Ny's source-first, systems-programming workflow. A complete implementation
would eventually own:

- package identity, versions, and dependency constraints;
- deterministic resolution and a lock file;
- source and native-artifact selection by platform;
- compiler search paths and ordered source inputs;
- local development dependencies;
- build, test, package, and publish hooks; and
- integrity metadata and repository trust policy.

None of those behaviors should be inferred from the current compiler. In
particular, `nilcompiler` does not implement the older proposed `--deps`,
`--package`, or `--package-mode` flags.

## Current repository experiments

Opt-in first-party package sources live under:

```text
packages/
  ny.engine/
  ny.godot/
  ny.ui.mimic/
  registry/
```

`packages/ny.godot/nypackage.ny` is an exploratory package descriptor, and
`packages/registry/packages.ny` is an exploratory registry fixture. They
are design inputs, not proof of a working resolver, registry, or publishing
service. Some entries describe planned files or artifacts that are not present.

The standard library remains under `nilstd`; it is not a package-manager
installation and should be bootstrapped separately.

## Proposed package shape

The manifest format is not yet frozen. The current direction uses a Ny-readable
`nypackage.ny` descriptor containing identity, dependency, source, native
artifact, and native-build metadata. Before implementation, the design must
settle:

- whether manifests are executable Ny, declarative NILData, or a restricted
  manifest grammar;
- the canonical package and module naming rules;
- semantic-version constraint behavior;
- target and feature selection;
- source ordering versus module-based discovery;
- native artifact checksums, signatures, and build isolation; and
- the lock-file format and reproducibility guarantees.

The retired `nyspec.yaml`, `nylock.lock`, `.nyslong`, and `NYS_LONG_TOKEN` names
from the old draft are not current conventions.

## Proposed command surface

Names such as the following are planning placeholders only:

```text
nypm init
nypm add <package>
nypm remove <package>
nypm resolve
nypm build
nypm test
nypm publish
```

They must not be used in setup instructions until an executable, command tests,
and a versioned manifest specification exist. Virtual environments, remote
search, global installs, publishing channels, and CMake package discovery are
also unimplemented ideas rather than current features.

## Implementation milestones

1. Publish a small manifest and lock-file specification with compatibility
   tests.
2. Implement local-path packages and deterministic dependency graphs without a
   remote service.
3. Integrate resolved source roots with `nilcompiler` through one documented
   interface.
4. Add target-aware native artifacts with checksums and safe build isolation.
5. Add a content-addressed cache, offline operation, and lock-file verification.
6. Design registry authentication, signing, yanking, and namespace ownership
   before enabling publication.
7. Promote commands from proposal to supported status only with end-to-end
   tests and migration documentation.

Until those milestones are complete, manage dependencies explicitly and pass
source search roots to `nilcompiler` with its current `-I`/`--sdk-path` option.
