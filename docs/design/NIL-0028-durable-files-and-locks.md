# NIL-0028: Durable publication and context-owned file locks

- **Status:** Implemented POSIX and Windows source providers. Windows runtime and
  route qualification passes; installed/release SDK qualification remains
  separate. Windows durable publication requires local NTFS.

## Motivation

The Matrix-authored Enigma database exposed a general library gap. A checked
atomic write synchronized the candidate inode and published its name, but lacked
parent-directory durability. A stream flush was not a filesystem barrier, and no
supported advisory-lock API existed. Applications had to supply a platform host.

General filesystem mechanisms belong in `nil.std.io`. Database schemas, evidence
semantics, permission policy, and recovery decisions stay in the application.

## Decision

Add `write_bytes_durable`, `sync_directory`, `acquire_file_lock`,
`release_file_lock`, and `file_lock_is_held`. Preserve the existing `write_bytes_atomic` and `File.flush`
contracts. POSIX durable writes reuse the atomic publication helper and
synchronize the exact publication directory descriptor. Windows uses a private
staging file created relative to a held directory, flushes file data, renames
relative to the same handle, closes the writing handle and synchronizes that
directory. A post-publication synchronization or Windows close failure returns
an explicitly uncertain outcome; the final directory flush is attempted even
after a reported close failure.

Before mutation, Windows must positively identify the held directory as local,
writable NTFS with persistent ACLs. This follows
[MS-FSA directory-flush semantics](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-fsa/0de7dc40-9627-437e-a4df-c4696cdc3d02)
and [Appendix A note 80](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-fsa/4e3695bd-7574-4f24-a223-b4679c065b63):
success on another filesystem does not establish directory persistence. No
volume-wide flush or enabled privilege substitutes for that check. Cleanup of
unpublished staging uses its held handle, never a potentially replaced name.

File locks are nonblocking exclusive advisory locks on private regular backing
files. POSIX uses `flock`; Windows uses nonblocking exclusive
[`LockFileEx`](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-lockfileex)
over byte zero, length one. Windows handles are not inheritable and pin the
backing entry against deletion/rename. Runtime-owned registries hold the
descriptors or handles. Tokens are never recycled,
are checked against their owning execution context, and are closed at invocation
termination and context teardown. An execution checkpoint cannot clone one.
Unsupported platforms fail closed before mutation.

`file_lock_is_held` asks whether the current execution context still retains
the token. Missing, stale, foreign, zero, and negative tokens return `Ok(false)`;
unsupported platforms return `Error`. The query does not inspect a pathname,
release or reacquire a descriptor, or change ownership. An application object
can survive an embedded VM invocation while that invocation's locks have been
retired, so retaining a positive token in an object is not evidence of a live
lock. Applications check current ownership before using that object's state.

Use the existing checked ordinary-I/O bridge for native execution, with exact
parameter/result and enum ownership validation. The VM checks the new result
metadata and concrete enum schema, and validates dynamic arguments before host
effects. The shared helper implements the OS contract once.

## Boundaries and validation

The Enigma consumer also requires the existing `text.byte_at` and `from_byte`
operations on native routes. Their native providers use the ordinary-I/O bridge
and preserve byte/range behavior within the native NUL-free text boundary.

These are filesystem mechanisms, not a production database protocol,
power-loss certification, pathname containment service, credential, or general
rollback defense. Lock participants must cooperate and control the directory
ancestry. Fork-inherited descriptors retain ordinary POSIX lifetime semantics.
An uncertain publication requires caller recovery rather than transparent retry.
The POSIX mode/ownership rules are unchanged. Windows uses the exact effective
user's owner-only ACL profile and strict ordinary DOS pathname grammar specified
in [NIL-0029](NIL-0029-private-entries-and-digests.md); the six private-entry/lock
APIs are distinct from the two publication APIs' local-NTFS restriction.

Tests cover shared OS helpers, embedded VM invocation/reset/destructor cleanup,
static/shared native context lifetimes, ownership queries before and after
retirement, Ny/Matrix VM text/binary and AOT/JIT
routes, malformed ABI declarations, private file modes, contention, and Linux
file/directory-sync fault injection. Windows adds independent ACL/file-byte
oracles, actual byte-range lock contention and teardown, and test-only
file/directory-flush and close-error injection. Production has no fault switches.
These coverage requirements do not declare all campaigns completed. Existing
secure-byte and ordinary-I/O regressions remain part of focused validation.

The [public API reference](../standard-library/durable-files.md) defines exact
limits. Existing SDKs do not gain the new provider from source declarations
alone. A development build must ship matching compiler, runtime, and library
sources; release/installed-SDK qualification and the remaining application-host
APIs are independent work. The private-directory, owner/mode, and standalone
SHA-256 surfaces reserved here are decided in
[NIL-0029](NIL-0029-private-entries-and-digests.md).
