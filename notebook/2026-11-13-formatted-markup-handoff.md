# Formatted text and structured markup handoff — 2026-11-13

## Status and question

Implemented for both Ny and Matrix; the completion review adds rejection and
route controls before the NIL-0027 work is handed back for independent review.
The existing installed SDK predates these language/library additions. This
entry records source validation, not SDK installation or release promotion.

Claude's implementation is committed through `010872548d1bd7b3c2900615306b1bf4af3c92ae`.
The interruption left the final Ny-parity paragraph in
`matrix/docs/next-version-findings.md` uncommitted. That paragraph is retained
and included in this handoff.

## What the completion review found

- Ny accepted text after a multiline opener and silently discarded it. It now
  requires the opener to end its line. A keyword suffix such as
  `end markupExtra` remains content rather than prematurely closing the block.
- The markup scanner always used Matrix hole rules. It now uses its actual
  frontend's quote, character-literal and comment rules. Raw-text detection
  walks tags and skips comments, quoted attributes and expressions rather than
  finding `<script` anywhere in the block.
- Whole-URL checks did not remove embedded tab/LF/CR or all leading ASCII
  controls. The check now accounts for those browser normalization rules.
  See the [WHATWG URL parser](https://url.spec.whatwg.org/#concept-basic-url-parser).
- Escaping each fragment cannot establish the scheme of a composed URL.
  Ordinary holes now supply either one complete dynamic URL or components
  after a fixed safe prefix. Code-bearing and URL-list attributes require an
  explicit trusted `raw` bypass.
- HTML ignores self-closing syntax on non-void elements such as `<script/>`.
  The initial HTML profile now rejects it, and conditional boundaries compare
  the complete element stack so an arm cannot replace a parent at the same depth.
- Conditional arms could leave an attribute quote open or contain another
  alternative after the final else. The checker now tracks lexical context
  as well as element balance and rejects unfinished tags/comments. Raw holes
  cannot supply tag/attribute names or comment content.

The guide, language/library references and design record distinguish the
implemented serialized-document profile from the original runtime-tree
proposal. XML, compact rendering, dynamic attribute sets, automatic optional
attribute omission and a DOM remain unavailable. This is context-specific
escaping for trusted source templates, not a general HTML sanitizer.

## Evidence

The unmodified implementation's hosted Source validation run
[43385549165](https://github.com/exec-software/exec-nil-framework/actions/runs/43385549165)
reported 595 LLVM-enabled CTests with no failures. Its existing four-host web
runtime and both Mac application/editor jobs also passed; those focused
platform jobs did not yet exercise the new markup contracts explicitly.

The completion review rebuilt the source locally. Its focused formatted-text
run passes all seven CTests, including 216 rejection controls, 49 semantic
index checks and 54 completion checks. Ny CRLF/UTF-8 multiline editing controls
verify source positions, literal doubled braces and closing-call continuation.
A separate direct scanner review passes 25 controls under ASan/UBSan and
`-Wall -Wextra -Werror`.

The final markup contract passes 392 LLVM-enabled checks and 200 checks in its
interpreter-only selection. Both Ny and Matrix run positive rendering cases
through text/binary NIL, LLVM JIT and AOT; rejected inputs preserve an existing
output artifact. All ten final language/route/toolchain CTests pass, including
HTML void-element and conditional element-stack regressions.

The broader local run passed 593 CTests with zero failures and two PostgreSQL
skips. This existing build has no PostgreSQL development headers/provider.
An attempt to supply only server binaries correctly failed its provider phase;
its isolated server shut down and its evidence is retained. The hosted source
workflow installs those dependencies and requires zero LLVM-enabled skips.
The changed-source Gitleaks pass found no credential findings in 20 files.

Hosted source validation passed on the exact pushed commit
`e38145cb1d543320df7e7cba8deca1d9584cbffc` and on PR merge revision
`7e0af8613e5762bf51fa0080490149325ee13fa4`: all 595 LLVM-enabled CTests
passed without failures or skips. The LLVM-disabled suite completed with its
expected unavailable-native skips; extension tests and builds also passed.
See the [push run](https://github.com/exec-software/exec-nil-framework/actions/runs/43462076837)
and [PR run](https://github.com/exec-software/exec-nil-framework/actions/runs/43462083069).

Linux x86-64, Windows x86-64, Intel macOS and Apple Silicon macOS each passed
all eight focused runtime/language contracts, including 200 markup and 150
formatted-text controls. Each also executed four Ny/Matrix fixtures through
text NIL, binary NIL and both bundle forms: 16 positive route observations
per host. All four downloaded artifacts passed manifest/hash and unchanged
source checks. See the [four-host interpreter run](https://github.com/exec-software/exec-nil-framework/actions/runs/43462083117).

Intel and Apple Silicon macOS each completed the full native applications/editor
campaign. All ten language contracts passed without skips, including the 392
markup and 216 formatted-text checks. Project, assembler, context, SDK
packaging and incremental-build checks passed. Each actual VS Code host
passed 88 checks across nine modules without skips. All 313 manifest entries
and the unchanged source seal were verified in each architecture's artifact.
See the [native/editor run](https://github.com/exec-software/exec-nil-framework/actions/runs/43462082930).

The first ARM interpreter and native/editor attempts could not acquire hosted
runners and never started any checks. Their failure evidence is retained.
Both passed their single same-source retry. These allocation failures are
distinct from source test failures; no requested handoff campaign is pending.

## Decision and follow-up

Keep these changes on the existing review branch and preserve the independent
review gate. A consumer needs a compiler and matching `nilstd` source built
from this branch; the old installed SDK is not sufficient for the new syntax.
No accepted SDK, installer or Lich release is changed.

The [Matrix WebView repository](https://github.com/exec-software/exec-nil-framework-webview)
is now public, with source secret/path audits, four required platform checks
and an independent review requirement matching the web server repository.
Linux and both Mac architectures have passed real browser consumers through
text/binary NIL, AOT and JIT. Windows has passed compilation, shared/framework
contracts and synthetic routes; its real browser initialization correction is
being qualified. The repository's platform notes record exact source evidence.
This follows the language handoff without activating an accepted SDK.
