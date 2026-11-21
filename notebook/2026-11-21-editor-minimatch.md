# Editor minimatch advisory remediation

## Scope and decision

Dependabot alert 9 identifies minimatch 10.2.6 in the editor lockfile.
[GHSA-g3hv-75wg-m249](https://github.com/isaacs/minimatch/security/advisories/GHSA-g3hv-75wg-m249)
describes glob-parts amplification when `optimizationLevel >= 2`; the
[first patched release is 11.0.1](https://github.com/isaacs/minimatch/releases/tag/v11.0.1).
The inspected language-client and VSIX-packaging consumers use the default
optimization level, but retaining the affected dependency leaves the advisory
unresolved.

The editor now pins minimatch 11.0.9 through its existing npm override facility.
The reviewed consumer versions remain vscode-languageclient 10.1.1 and
@vscode/vsce 4.0.0. The language client is pinned exactly because its two
compatibility adaptations require a fresh review when that implementation
changes. At investigation time, the current upstream language client 11.0.0
and VSCE 4.1.1 still request minimatch 10, so upgrading either parent alone
does not remove the affected dependency.

Minimatch 11 uses a nonrecursive matcher and removes `makeRe()`. Its
[documented API](https://github.com/isaacs/minimatch/blob/v11.0.9/README.md)
retains `Minimatch.set`, `Minimatch.match()` and `minimatch()`.
Both inspected language-client calls use `makeRe()` only as a boolean
validation guard before matching; neither consumes a regular expression.
An npm override alone therefore breaks ordinary language-client matching.

The esbuild helper replaces those two guards with an empty parsed-set check
in the production bundle. It preserves rejection of empty/comment patterns,
leaves constructor errors intact and does not add a fake regular-expression
API or mutate the matcher prototype. The helper checks exact consumer/matcher
versions, exact guard count and inclusion of both reviewed modules, failing
the build if an update changes that contract. Incremental rebuilds are checked
too. Installed upstream packages remain untouched. VSCE uses the supported
`minimatch()` function and needs no adaptation.

The extension's VS Code minimum remains `^1.95.0`; the existing Node 20 bundle
target and CI Node 22 configuration are unchanged. The helper is excluded
from the VSIX along with source, tests and installed dependencies.

## Local evidence

- All 213 extension unit tests pass, including nine new focused compatibility
  tests. They exercise the actual bundled language-client consumers, ordinary
  and relative globs, Unicode, dotfiles, case and scheme filters, file kinds,
  Windows drives/UNC paths and incremental bundling. An unadapted consumer
  negative control demonstrates the removed-API failure. A small explicit
  expansion limit checks the patched optimization-level-2 rejection.
- The nine focused tests also pass on official Node 20.20.2. Production
  bundling succeeds, and `npm audit --json` reports zero vulnerabilities.
- Baseline and candidate VSIX packaging succeeds on official Node 22.23.5.
  Both archives have the same seventeen files; only the package manifest and
  bundled extension change. The candidate has no `makeRe()` calls and includes
  the nonrecursive matcher. The Node archives used for these checks were
  verified against the official SHA256 manifests.
- The host's older Node 22.11 reproduces a VSCE/parse5 ESM loading failure in
  the untouched baseline. The isolated newer Node packaging check changes
  neither the extension engine nor the machine's Node installation.
- The isolated Linux VS Code 1.144.1 campaign passes all 88 checks across all
  nine modules without skips, exits zero and confirms complete cleanup. The
  immutable SDK's 279 payload files are unchanged.

## External test-owner correction

Three earlier candidate runs passed every functional check but failed the
private outer process owner's cleanup classification. All three failures and
an untouched baseline control remain preserved. The observed child was an
editor zygote, with a recorded signal attempt followed by natural exit status
zero. Extension-host termination took eight milliseconds in the observed
candidate runs and seven in the baseline. Review found no additional matcher
timers, listeners or child processes.

A bounded real-process reproduction identifies the external harness defect:
`pidfd_send_signal()` can succeed for an already-exited, unreaped child. A
natural exit between the owner's readiness check and signal call was therefore
misclassified as required termination. A separately versioned private owner
retains signal attempts and classifies termination from verified child
identity, successful reap, an exited pidfd and authoritative wait status.
Unknown ownership or exit evidence fails closed. Its real escaped-child
positive control passes for natural exit; the live-child negative control
fails when SIGKILL actually terminates the child. Nine incomplete/unknown
evidence cases fail closed too.

The final unchanged candidate campaign passes with that correction, retaining
the zygote's signal attempt and verified natural exit in the report. No child
was terminated by a signal and no survivors remain. Ownership checks, signals,
deadlines and cleanup windows are unchanged. This correction is confined to
the private evidence driver; no repository runner or extension lifecycle code
changes are included in the dependency patch.

These changes do not replace the framework's prior source/SDK qualification.
Independent review accepts the guarded adaptation. Hosted platform checks
remain pending for this dependency revision.
The default-branch alert is not claimed closed before the dependency change
reaches that branch and GitHub reevaluates it.
