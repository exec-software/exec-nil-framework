# NIL-0029: Private directory creation, metadata verdicts, and content digests

- **Status:** Implemented additive POSIX source-runtime profile, with a
  portable digest; release SDK qualification remains separate.

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

Directory creation applies its mode at creation rather than through a later
chmod, which would expose a writable directory in between. An existing entry is
reported, never adopted, so success means the caller created the directory.
Parents are neither created nor trusted beyond existing ancestry rules.

The predicates return `Result<bool, text>` so three outcomes stay distinct: the
entry was inspected and matches, it was inspected and does not, or it could not
be inspected. Collapsing absence into a false verdict would let a caller read
"missing" as "unsafe" or the reverse. Inspection does not follow a final
symlink, so a link is judged as itself and never inherits its target's verdict.

`sha256_hex` returns 64 lowercase hexadecimal characters over every supplied
byte, including embedded NULs. It reuses the existing portable implementation,
and on Apple platforms the existing CommonCrypto path, so a digest does not
depend on which route computed it.

## Boundaries and validation

A path predicate describes an entry at inspection time. It is not a safe
substitute for opening the entry under `O_NOFOLLOW`, because it cannot close the
window between inspection and use. Publication and lock acquisition keep their
own checked opens; these surfaces are for preflight and diagnosis. A caller that
needs a guarantee must act on an open descriptor, not on a prior verdict.

A digest detects accidental corruption and identifies content. It is not a
signature, a message authentication code, encryption, or rollback protection:
whoever can replace the bytes can replace the digest beside them. Authenticating
a digest requires a secret these functions do not take.

Mode and owner verdicts describe POSIX ownership and permission bits only. They
say nothing about ACLs, extended attributes, capabilities, or mandatory access
control, and they are not a security boundary against the owner or root.

Tests cover creation mode and exclusivity, cross-kind verdicts, loosened file
and directory modes, symlinks to otherwise-private targets, multiply-linked
files, FIFOs, uninspectable entries, unsupported-platform behavior before any
mutation, and RFC 6234 digest vectors including embedded NUL bytes. The paired
Ny and Matrix fixtures exercise every surface across textual and binary VM, AOT,
and JIT, with an independent host oracle checking directory modes.

Windows returns an explicit unsupported-platform error before filesystem
mutation; the digest is platform-independent. Other POSIX platforms need their
own execution evidence. As with NIL-0028, source declarations alone do not give
an installed SDK the new provider, and release qualification is independent
work. See the [public API reference](../standard-library/durable-files.md).
