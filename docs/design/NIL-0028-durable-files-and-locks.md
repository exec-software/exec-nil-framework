# NIL-0028: Durable publication and context-owned file locks

- **Status:** Implemented additive POSIX source-runtime profile; release SDK
  qualification remains separate.

## Motivation

The Matrix-authored Enigma database exposed a general library gap. A checked
atomic write synchronized the candidate inode and published its name, but lacked
parent-directory durability. A stream flush was not a filesystem barrier, and no
supported advisory-lock API existed. Applications had to supply a platform host.

General filesystem mechanisms belong in `nil.std.io`. Database schemas, evidence
semantics, permission policy, and recovery decisions stay in the application.

## Decision

Add `write_bytes_durable`, `sync_directory`, `acquire_file_lock`, and
`release_file_lock`. Preserve the existing `write_bytes_atomic` and `File.flush`
contracts. Durable writes reuse the atomic publication helper and synchronize
the exact publication directory descriptor. A post-publication sync failure
returns an explicitly uncertain outcome.

File locks are nonblocking exclusive advisory locks on private regular backing
files. Runtime-owned registries hold the descriptors. Tokens are never recycled,
are checked against their owning execution context, and are closed at invocation
termination and context teardown. An execution checkpoint cannot clone one.
Unsupported platforms fail closed before mutation.

Use the existing checked ordinary-I/O bridge for native execution, with exact
parameter/result and enum ownership validation. The VM checks the new result
metadata and concrete enum schema, and validates dynamic arguments before host
effects. The shared helper implements the OS contract once.

## Boundaries and validation

The Enigma consumer also requires the existing `text.byte_at` and `from_byte`
operations on native routes. Their native providers use the ordinary-I/O bridge
and preserve byte/range behavior within the native NUL-free text boundary.

This is a Linux-tested POSIX mechanism, not a production database protocol,
power-loss certification, pathname containment service, credential, or general
rollback defense. Lock participants must cooperate and control the directory
ancestry. Fork-inherited descriptors retain ordinary POSIX lifetime semantics.
An uncertain publication requires caller recovery rather than transparent retry.

Tests cover shared OS helpers, embedded VM invocation/reset/destructor cleanup,
static/shared native context lifetimes, Ny/Matrix VM text/binary and AOT/JIT
routes, malformed ABI declarations, private file modes, contention, and Linux
file/directory-sync fault injection. Existing secure-byte and ordinary-I/O
regressions remain part of focused validation.

The [public API reference](../standard-library/durable-files.md) defines exact
limits. Existing SDKs do not gain the new provider from source declarations
alone. A development build must ship matching compiler, runtime, and library
sources; release/installed-SDK qualification and the remaining application-host
APIs are independent work.
