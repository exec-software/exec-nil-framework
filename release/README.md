# Assembling a NIL Framework release candidate

This directory contains the reviewed dependency policy and redistribution
notices. It is not an upload destination. The supported profile and installation
instructions are in [the v1 manual](../docs/release/v1-profile.md).

The candidate release is **1.0.0-rc.1**. It is not stable v1.0.0.

## Current preparation status

The framework remains **1.0.0-rc.1** during public source preparation. The
standard library now has 16 logical modules and neutral `nil.std.*` imports
(with legacy `ny.lang.*` aliases), including declaration reflection and bounded
SQLite/PostgreSQL providers. Compiler-backed editor analysis is available for
both Ny and Matrix. See the [analysis API](../docs/analysis-api.md),
[database contract](../docs/standard-library/database.md), and
[current findings](../matrix/docs/next-version-findings.md) for their scope.

The current development build uses LLVM 21.1.8 and GCC 15.2.0. The reviewed
dependency policy still covers LLVM 18.1.3 and GCC 13.3.0 on Ubuntu 24.04;
its ABI ceilings and notices have not been relaxed to accept a newer build.
An SDK that passes development tests is not a qualified replacement for that
binary release. Before stable v1, resolve or explicitly exclude known
correctness gaps, select the exact source/version identity, rebuild with the
reviewed release profile (or review a replacement), rerun the required
qualification, and verify the renamed artifact contract with the publishing
service. A private GitHub push is source preparation, not a stable release.

## Build and assemble

Commit all intended source changes first. From a clean framework checkout, build into
a candidate directory instead of replacing `sdk`:

```sh
nil_release_version="$(tr -d '\n' < VERSION)"
nil_release_revision="$(git rev-parse HEAD)"
NIL_SDK_DIR="$PWD/build/rc-candidates/$nil_release_version/sdk" \
  NIL_SDK_QUALIFICATION=rc-checks \
  CMAKE_BUILD_PARALLEL_LEVEL=6 CTEST_PARALLEL_LEVEL=6 \
  ./scripts/build-sdk.sh

(cd vscode && npm run package -- \
  --out "../build/ny-language-support-$nil_release_version.vsix")

python3 scripts/assemble-release.py \
  --sdk "build/rc-candidates/$nil_release_version/sdk" \
  --expected-revision "$nil_release_revision" \
  --dependency-policy release/dependency-policy.json \
  --notices release/THIRD_PARTY_NOTICES.txt \
  --vsix "build/ny-language-support-$nil_release_version.vsix" \
  --output "build/releases/nil-$nil_release_version"
```

The editor build requires the dependencies installed from its committed npm
lockfile. Build and test the VSIX at the same clean source revision as the SDK;
the assembler validates its version and packaged resources, not a separate
reproducible JavaScript-build attestation.

The assembler requires a new output directory and does not overwrite an old
release. It uses the exact SDK manifest and current tool execution, measures
ELF requirements with `readelf`, compares the dependency policy against the
qualified GCC/LLVM versions, and includes all SDK files with normalized archive
ordering, ownership, modes, and timestamps. It refuses missing/unreviewed
notices, dirty sources, mismatched versions, unexpected libraries, increased
ABI requirements, unsafe archives, and changed SDK input.

Output includes the SDK tarball, optional matching VSIX, release manifest,
runtime-requirements inventory, dependency policy, notices and `SHA256SUMS`.
The printed hashes of `release-manifest.json` and `SHA256SUMS` are independent
handoff anchors to retain outside the downloaded folder. Nothing is signed,
tagged, activated, uploaded, or published by this script.

Passing these local gates does not replace consumer qualification or the final
release decision. Release assets identify `NIL Framework` and use `nil-sdk-*`
archives. Publication is a deliberate, separate step taken after the release
identity has been chosen and authorized; an RC artifact is not a stable v1
artifact. The helper never creates tags, never uploads, and never activates a
consumer SDK. Source/build qualification and a successful assembly are not
evidence that a release has been published.

