# Compiler correctness follow-up — November 20, 2026

The `7419ac1` source and four-host runtime campaigns completed successfully,
including both Mac application/editor jobs. Goetic records the immutable
development SDK delivered from that revision. The shared SDK default and
release publication remain unchanged; the integration PR still requires
independent review.

## Reproduce old findings before changing code

The audit reproduced F34: a renamed module function passed analysis but emitted
an unresolved alias in NIL. F36 was more subtle: a conversion intrinsic declared
to return a user-defined `Result` produced the standard runtime tag, so matching
the claimed custom enum silently missed. Both the interpreter and native route
returned the reproducer's fallthrough value instead of the parsed integer.

F21 also still reproduces: Ny accepts a text local read before assignment, then
fails during execution/native generation. A proper source-level
definite-assignment analysis remains separate work.

F33 no longer reproduces. Typed array-expression lowering already corrected it
during the tuple-array implementation. Expanded checks on the unchanged
`7419ac1` SDK pass all 226 tuple-array contract checks, including literal and
returned text arrays, exactly-once evaluation, retained text, wide integers,
floating-point elements and empty returned arrays in both languages on six
execution routes. The findings entry and stale F25 release-profile warning are
corrected rather than presenting existing behavior as a new implementation.

## Module aliases and conversion providers

Function references now retain their checker-selected declaration separately
from source spelling. Lowering uses that identity for calls and generic
specialization; function values use the same declaration. Local values retain
precedence, and unrelated constructors/globals cannot replace the selected
function. Matrix `read module as alias` loads its target, and qualified paragraph
values use the ordinary name parser and preserve their final-name source span.
Review also reproduced a root generic function selecting another module's
same-named declaration when explicit source-file order changed. The registry now
preserves the root function's canonical bare name; both input orders have
interpreted and native regression coverage.

Checked conversion providers require the nominal standard Result owner and
their exact payload/error types. Source checking, unchecked emission, NIL
loading and native generation validate that boundary. Existing standard
namespace aliases and tested legacy wire forms remain supported. This is
validation of the existing providers, not custom enum construction or additional
native conversion support.

The module-alias contract covers Ny/Matrix providers and consumers, source and
binary libraries, direct/generic calls, interpreted function values, module
isolation and rejection of inaccessible or incorrectly called functions. The
conversion contract covers legitimate results and malformed provider owners,
schemas and signatures, with existing output artifacts protected on failure.
The four-host interpreter workflow now selects fourteen contracts; the Mac
language campaigns select twelve, retaining their previous assertions and
deadlines. Build-tree checks do not update Goetic's installed SDK automatically.

The complete local LLVM-enabled Release suite passes all 601 CTests with zero
failures or skips. This includes 142 module-alias checks, 129 conversion-provider
checks and the embedding loader controls. The existing integer-conversion,
tuple-array, module-context and editor-analysis contracts also pass. Hosted
results for the final commit are tracked in
[the integration PR](https://github.com/exec-software/exec-nil-framework/pull/41);
the local result does not by itself establish the other hosts' current-source
acceptance. The changed-file audit found no credentials or new private paths.

## Hosted build dependency correction

Both the push and PR source campaigns for `332b6f5` passed: 601 LLVM-enabled
tests with zero skips, and 450 LLVM-disabled tests with 145 expected native
skips. Each extension unit campaign passed 167 tests. The SDK from that exact
revision also passed 601 source tests and all 1,184 installed-library executions.

The first fourteen-contract runtime campaign passed on Linux, Windows and Intel
macOS. Apple Silicon passed thirteen contracts; CTest did not run the conversion
contract because the focused build omitted its declared `nilassembler`
dependency. The original failed run and artifacts are retained:
[runtime run 44858633125](https://github.com/exec-software/exec-nil-framework/actions/runs/44858633125).
Add the assembler to the focused build targets on every host. The test inventory,
timeouts and assertions stay unchanged; the replacement campaign must pass
before claiming this revision's four-host runtime acceptance. This is a workflow
correction, so the separately qualified SDK retains its `332b6f5` source identity.

## Development SDK handoff and editor include caches

The immutable Linux development SDK built from `332b6f5` has manifest SHA256
`c82b532b1ef48b9c91c4ca0022f94fb25bb57d5253928cf177a7426875fd7a98`.
Its eleven tools, nineteen modules, 84 source files and 72 HTML pages passed
601 source tests, 1,184 installed executions, 32 additional markup executions,
and the installed rooted-file contract. All 275 payload hashes were verified
after relocation and consumer testing.

An isolated committed Goetic snapshot passed its native checks, 24 Matrix
fixtures, nine Node tests and 94 HTTP tests. The Goetic chat independently
verified the physical generation and committed its next-build pin in `c229656`,
preserving its existing application edits. No live application was restarted;
the shared SDK default and stable/Lich publication remain unchanged.

The first Mac application run passed all non-editor contracts on Apple Silicon,
then failed the include-file restoration navigation check after 43 successful
editor checks. Its original evidence is retained in
[run 44858633162](https://github.com/exec-software/exec-nil-framework/actions/runs/44858633162).
A deterministic regression reproduced a matching failure mode: semantic
validation treated a retained clean closed-tab include model as authoritative
even after the compiler had checked restored disk contents.

Validation now defers those clean closed-tab caches to exact disk snapshots.
Dirty include aliases still block navigation, and clean open include tabs must
match. Arbitrary local text/diff tab membership now invalidates pending results,
while compiler overlays and providers remain limited to supported languages.
Ten added regression tests cover direct/alias caches, dirty/open tabs and changes
during asynchronous reads. All 177 extension unit tests and the production build
pass; real editor-host acceptance remains a separate check. No host assertion,
deadline or dependency version was changed.

## Editor dependency follow-up

The existing [minimatch advisory](https://github.com/isaacs/minimatch/security/advisories/GHSA-g3hv-75wg-m249)
requires optimization level 2 or greater. Inspection of the pinned language
client and packaging tool found default level 1 options; none of those inspected
calls enables the affected optimization. This is a scoped applicability finding,
not an alert dismissal or a blanket dependency safety claim.

Minimatch 11 removes `makeRe()`, which the language client still calls. An
unconditional major override would break those paths. Keep this dependency
change separate: it needs an explicit consumer adaptation, glob compatibility
checks, packaging comparison and actual editor-host validation. No dependency
override or alert dismissal is part of this compiler change.
