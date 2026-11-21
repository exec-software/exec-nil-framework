# NIL-0029: Private directory creation, metadata verdicts, and content digests

- **Status:** Implemented POSIX and Windows profiles, with a portable digest.
  Windows source and [installed development SDK qualification](../../notebook/2026-11-21-windows-durable-parity.md)
  pass; stable-release qualification remains separate.

## Motivation

NIL-0028 gave applications durable publication and cooperative single-writer
locking, and explicitly left private-directory creation, owner/mode inspection,
and a standalone SHA-256 as separate APIs. A storage application still had to
supply a platform host for them, which is the gap NIL-0028 set out to close.

Each mechanism already existed inside the framework and was already exercised:
owner-only directory creation for toolchain scratch space, the private-regular
predicate used when acquiring a lock, and SHA-256 for module fingerprints. The
gap was a supported language surface, not an algorithm.

## Decision

Add `create_private_directory`, `is_private_regular_file`,
`is_private_directory`, and `sha256_hex`. The filesystem operations join
`nil.std.io`; the digest joins `nil.std.io`'s checksum surface beside
`crc32`, which already draws the line between corruption detection and
authentication.

Directory creation applies POSIX mode `0700` or a protected Windows owner-only
security descriptor at creation, without a later permission-repair window. An existing entry is
reported, never adopted, so success means the caller created the directory.
Parents are not created and must remain trusted. The created entry is verified;
failure can leave it in place rather than repairing or removing a potentially
replaced pathname. A restrictive POSIX umask is not changed.

The predicates return `Result<bool, text>` so three outcomes stay distinct: the
entry was inspected and matches, it was inspected and does not, or it could not
be inspected. Collapsing absence into a false verdict would let a caller read
"missing" as "unsafe" or the reverse. Inspection does not follow a final
symlink or Windows reparse point, so a link is judged as itself and never
inherits its target's verdict.

Windows inspection uses the effective thread user, or the process user only
when there is no thread token. The entry owner must equal that user SID, not a
default owner group. Its DACL must contain exactly one ordinary allow ACE
granting `FILE_ALL_ACCESS` to the same SID: explicit in a protected DACL, or
inherited from an owner-only parent. Directory ACEs propagate to files and
directories; file ACEs have no propagation flags. Additional, deny, callback,
inherit-only, null and empty ACL profiles fail the predicate. This is a
conservative accepted profile rather than a general access-check engine.
[Handle-based owner/DACL queries](https://learn.microsoft.com/en-us/windows/win32/api/aclapi/nf-aclapi-getsecurityinfo)
never repair permissions. Files must also be disk objects, regular and singly
linked; both kinds reject reparses and delete-pending entries.

Windows takes strict UTF-8 ordinary relative or drive-absolute DOS paths. UNC,
device and extended namespaces, alternate data streams, drive-relative or
current-drive-rooted paths, empty/dot/parent components, reserved device names,
leading spaces and trailing dots/spaces are rejected. Directory inspection may
accept an explicit drive root; other trailing separators are rejected. See the
[Win32 naming rules](https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file)
and the [public API's pathname profile](../standard-library/durable-files.md#windows-owner-and-pathname-profile).
An inspected final component does not establish safe ancestry or containment.

`sha256_hex` returns 64 lowercase hexadecimal characters over every supplied
byte, including embedded NULs. It reuses the existing portable implementation,
and on Apple platforms the existing CommonCrypto path, so a digest does not
depend on which route computed it.

## Boundaries and validation

A path predicate describes an entry at inspection time. It is not a safe
substitute for a no-follow open, because it cannot close the
window between inspection and use. Publication and lock acquisition keep their
own checked opens; these surfaces are for preflight and diagnosis. A caller that
needs a guarantee must act on an open descriptor, not on a prior verdict.

A digest detects accidental corruption and identifies content. It is not a
signature, a message authentication code, encryption, or rollback protection:
whoever can replace the bytes can replace the digest beside them. Authenticating
a digest requires a secret these functions do not take.

On POSIX, mode and owner verdicts describe ownership and permission bits only. They
say nothing about ACLs, extended attributes, capabilities, or mandatory access
control. Windows verdicts describe the explicit ACL profile above. Neither
profile is a security boundary against the owner or a privileged administrator.

Tests cover creation mode and exclusivity, cross-kind verdicts, loosened file
and directory modes, symlinks to otherwise-private targets, multiply-linked
files, FIFOs, uninspectable entries, unsupported-platform behavior before any
mutation, and RFC 6234 digest vectors including embedded NUL bytes. Windows
adds protected and inherited owner ACLs, wrong-owner descriptors, extra/denied
rights, junctions, sharing behavior and ambiguous-path rejection. The paired
Ny and Matrix fixtures exercise every surface across textual and binary VM, AOT,
and JIT, with independent host oracles checking POSIX modes or Windows owner/ACL
metadata. This describes validation coverage, not completed platform evidence.

Windows source and installed development SDK checks pass with independently
verified owner/ACL and byte oracles; the digest is platform-independent. The
qualification record pins the SDK and helper revisions, and does not update an
older installation. Stable-release qualification remains independent work.
See the [public API reference](../standard-library/durable-files.md).
