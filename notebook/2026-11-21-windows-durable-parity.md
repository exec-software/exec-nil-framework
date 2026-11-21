# Windows durable files and lock parity

Source runtime qualification passes; installed Windows SDK qualification remains
in progress. This work does not replace
the accepted SDK, activate a new shared SDK, or declare a stable release.

## Implementation

The eight durable/private/lock operations now have Windows implementations.
Directory synchronization and durable publication require positively identified
local NTFS. The directory handle anchors staging, relative rename and the final
flush. Published output is never removed after a failed final barrier; close or
flush failure reports `publication durability uncertain`. Owner-only ACLs apply
at creation, with no later permission-repair window.

Private-entry inspection accepts only the documented effective-user owner and
single owner-allow ACE profiles. File locks use noninherited handles and an
immediate exclusive `LockFileEx` range at byte zero, length one. Registry tokens
remain context-owned and process-monotonic. Held lock handles deny deletion;
namespace identity and privacy are checked again before returning the token.
The checked ordinary-DOS path grammar rejects device/UNC paths, alternate streams,
reserved aliases and ambiguous components. POSIX implementations are unchanged.

The Windows security import library is included in runtime/native linking and
in the native toolchain cache input closure. Cache tests cover content changes
with preserved timestamps and missing security import libraries.

## Evidence and failed attempts

Evidence is retained in an isolated Windows qualification campaign. Source
snapshots have per-file byte sizes and SHA-256 hashes. Test output and failed
attempts are retained; no accepted installation or existing worktree is changed.

- MinGW compilation checks passed for both new headers and both Win32 test
  executables. These are compilation evidence, not Windows runtime acceptance.
- A real Windows local-NTFS probe confirmed directory `FlushFileBuffers` works
  under the build account. This is not a power-cut test.
- The first private/lock standalone build had an incomplete source snapshot;
  its transitive header closure was corrected. The next build succeeded but
  exposed an ACL-negative fixture bypass through the elevated account's backup
  privilege. The fixture now uses its own restricted impersonation token;
  production policy was not relaxed.
- Initial CMake configuration attempts failed the strict owned-process cleanup
  check after successful configuration. Instrumentation identified the owned
  descendant as MSVC's `mspdbsrv.exe`, not the telemetry uploader. The isolated
  build now selects CMake CMP0141 NEW and embedded MSVC debug information.
  Configuration then passed with clean process teardown. The owner and cleanup
  deadlines were not changed.

## Acceptance required

Real Windows acceptance must cover the private/lock executable (20 contracts,
18 invalid path operands), the publication executable (15 contracts, 16 invalid
path operands), common helper and VM lifecycle tests, and all eight Ny/Matrix
text/binary/AOT/JIT routes with an independent Win32 ACL/bytes/lock oracle.
Installed qualification must repeat the routes with both source and compiled
standard libraries. Current-source hosted Linux, Windows and both macOS checks
remain necessary before broad parity or SDK adoption claims.

The accepted source revision remains `167ae3a6b0abb69c26e943a008ee6eb1eac8f13d`
with documentation closeout `fd4417dbb5e7b614ee3fd7962409395a95793521` until new
qualification is complete. Earlier 609-source/1248-installed-route and hosted
results belong to that revision and must not be attributed to this change.

## Source qualification checkpoint

The fifth isolated MSVC/LLVM build passes all seven selected CTests, with strict
owned-process cleanup and unchanged source hashes. The suites include 20 private/
lock contracts (18 invalid operands), 15 publication contracts (16 invalid
operands), the common helper, 251 VM lifecycle checks, the private-directory
contract, native runtime ABI and all eight Ny/Matrix text-VM, binary-VM, AOT and
JIT routes. The route report includes 71 owned commands, 12 ABI rejection cases
and 36 foreign-library rejection cases, with an independent Win32 host oracle.
Report SHA-256: `e648cef32ff0ae653686952255617b1f1358e1562473db569db6ed3049621418`.

Real execution caught two additional issues before this checkpoint. Replacing a
locked target correctly failed with access-denied rather than sharing-violation;
the test now accepts either refusal code and still requires unchanged bytes and
namespace. The Win32 rename wrapper rejected a non-null root directory with
invalid-parameter, even with a padded buffer. A direct native rename probe
succeeded using the same held directory. Publication now uses
`NtSetInformationFile` with a counted, padded name and checked completion status.
One-character output names are covered. No directory/path anchoring was relaxed.

The Linux suite reports 609 tests, zero failures, 606 passes and three optional
skips (PostgreSQL client/provider and desktop display). The final native ABI test
was rebuilt and rerun after adding exception-safe Windows fixture cleanup.
The updated installed qualifier also passes against the existing immutable
Linux SDK/source `167ae3a`: both libraries cover 16 durable executions, 24 ABI and
72 library rejections, 202 console checks per library, 16 nominal-global VM
executions and eight expected initialization/native rejections. SDK/helper seals
remain unchanged. That tests qualifier compatibility, not a new SDK build.
Installed regression report SHA-256:
`8c640a05bc7a7c3785359cbcd26bbc4a9484007fde5ed874b08edbb790a2fbb6`.

The source scan found no credentials or new private paths/addresses. The sole
remaining path-policy report is the unchanged public README contact address;
its original line is present in the parent commit. Public specification section
numbers were reworded to avoid being misread by the address scanner; the direct
Microsoft reference links remain.
