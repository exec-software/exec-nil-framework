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
