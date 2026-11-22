# Web server boundaries — 2026-11-12

## Status and question

Implemented in framework `93515c3d4c2b6e0e7c3b870eb283fce26392d613`
([PR 39](https://github.com/exec-software/exec-nil-framework/pull/39)) and the
[web server PR](https://github.com/exec-software/exec-nil-framework-web/pull/1).
The application exposed gaps that isolated language examples did not: request
boundaries, file identity, filesystem containment and binary-value storage.

The question was how to keep protocol policy in Matrix while giving it one
reliable filesystem observation on Linux, Windows and both macOS architectures.

## Decision

`read_rooted_snapshot(root, relative, maximum_bytes)` returns a typed
`FileSnapshot`: byte-array content, same-handle modification time and SHA-256 of
those exact bytes. It opens each descendant relative to an already-open
directory and rejects symlinks or Windows reparse points. Root spellings ending
in a separator or `/.` cannot disguise a final root link. Ancestors of the
trusted root, hard links and privileged mount changes remain outside this
boundary; the API is not a filesystem sandbox.

Matrix retains HTTP framing, precondition ordering, range decisions and response
construction. A native content digest and one bulk byte-to-text copy avoid a
per-byte interpreter loop on every request. The pure Matrix SHA-256 code remains
a reference checked against independent vectors. The observed local 1 MiB
loopback response took about 10 ms; this is a measurement, not a service SLA.

Raw `text.from_bytes` is explicitly interpreter-only until native text can
represent embedded NUL. The snapshot itself remains binary-safe on native
routes because its content is an array. Native compilation rejects unsupported
raw-text conversion instead of truncating data or silently changing its type.

The work also corrected compact byte-array validation inside boxed records.
Exact `array<u8>` values from file operations are valid storage; raw pointer
allocations do not become arrays merely because their bytes look compatible.
Forced-GC and function-return controls verify that a boxed snapshot retains its
content and digest on the supported execution routes.

## Evidence

- Linux: 87 host filesystem checks; five focused CTest contracts, including
  existing UTF-8, dynamic-value and native-runtime regressions.
- The rooted-file language contract checks textual and binary NIL, LLVM JIT
  and AOT before and after an equal-size edit with its timestamp restored:
  eight positive observations, four wrong-type extraction refusals, two raw
  byte-text interpreter cases and six native declaration/provider refusals.
- [Four-host runtime run 43158309597](https://github.com/exec-software/exec-nil-framework/actions/runs/43158309597)
  passed the exact eight-contract inventory on Linux, Windows, Intel macOS
  and Apple Silicon macOS. These jobs use LLVM OFF; they are not native-route
  or installed-SDK qualification.
- A fresh Windows LLVM build of `93515c3` passed 90 host checks and the
  complete AOT/JIT snapshot contract. Existing checkouts and accepted SDKs
  remained unchanged. Both macOS application/editor jobs also passed; the new
  native snapshot contract is qualified separately on both Mac architectures.
- The web platform campaign requires 1,128 unit checks, 38 real-wire checks
  including the actual first-byte deadline, and 15 static HTTP checks. Its
  Windows run exposed CRLF checkout conversion of a byte-exact CSS fixture.
  `.gitattributes` now requires LF text rather than weakening length or digest
  expectations. The original failure remains in the CI record. A second run
  found a test-oracle gap: Winsock reports deadline closure as error 10053.
  The corrected oracle accepts that exact Windows closure, observes readiness
  continuously, and rejects early disconnects outside the 19–25 second window.
  Sixteen synthetic controls and 76 actual Windows wire checks passed across
  two library locations, with closure observed at 20.004 and 20.027 seconds.
- Owned-process timeout controls passed on Linux and Windows, including a real
  listening Matrix child, nested Windows Jobs and failed Job assignment before
  command release. Test cleanup is part of the pass condition.

The test implementations live in `tests/rooted_file_snapshot_tests.cpp` and
`tests/tools/rooted_file_snapshot_contract.py`; web consumer tests live in its
own repository. No accepted SDK or release deployment is replaced by this work.

## Follow-up

Native snapshot qualification passed on both Mac architectures in
[run 43162404951](https://github.com/exec-software/exec-nil-framework/actions/runs/43162404951),
with the exact 87 host and 8/4/2/6 language-route inventories against fixed
source `93515c3`. The corrected [web run 43162279298](https://github.com/exec-software/exec-nil-framework-web/actions/runs/43162279298)
passed all four hosts at web head `4bcb43f`: 1,128 unit, 16 oracle, 38 wire
and 15 static checks each. The four `web-*` checks now join `secrets` as
required checks; review and the other existing branch protections are unchanged.

The next project is a clean Matrix WebView package. The archived implementation
has no macOS provider and advertises some Linux-only functions through a shared
wrapper. Its replacement needs a checked provider ABI, bounded event queues and
observable asynchronous completion; generic legacy FFI and provider hot reload
do not establish those guarantees. Browser-origin provenance must come from
native evidence or be marked unavailable, never inferred from a page message.

For future AI diagnostics, content identities can correlate a file observation
with an action without duplicating its bytes. This is a proposed use, not an
automatic logging or executable-context feature; fingerprints can themselves
be sensitive and need the same deliberate disclosure policy as other context.
