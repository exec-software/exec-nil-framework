# Durable files and advisory locks

`nil.std.io` provides additive POSIX primitives for applications that need a
durable checkpoint and a cooperative single-writer protocol. Ny and Matrix use
the same runtime provider and checked results.

| Function | Result |
| --- | --- |
| `write_bytes_durable(path, content, replace)` | `Result<bool, text>` |
| `sync_directory(path)` | `Result<bool, text>` |
| `acquire_file_lock(path, create)` | `Result<i64, text>` |
| `release_file_lock(token)` | `Result<bool, text>` |
| `file_lock_is_held(token)` | `Result<bool, text>` |
| `create_private_directory(path)` | `Result<bool, text>` |
| `is_private_regular_file(path)` | `Result<bool, text>` |
| `is_private_directory(path)` | `Result<bool, text>` |
| `sha256_hex(data)` | `Result<text, text>` |

Matrix uses the same names with underscores or hyphens. Machine-width lock
tokens appear as `number`/`override long`; they are runtime handles, not host
descriptors or authentication credentials.

## Publication and durability

`write_bytes_durable` keeps the existing atomic byte-write rules: bounded byte
arrays, strict UTF-8 nonempty NUL-free filenames, a fresh private `0600` staging
inode, and no-clobber publication when `replace` is false. Replacement preflight
requires a regular destination, and publication replaces the entry without
following it. Existing ownership, mode, ACLs, and extended attributes are not
copied. It adds synchronization of the **same directory descriptor** used for
publication, after synchronizing the candidate file and publishing its entry.

Success means those synchronization calls succeeded. It assumes a filesystem
and device stack honoring `fsync` and atomic rename/link. It does not prove
hardware power-cut behavior, protect against privileged namespace replacement,
or authenticate data. Parent-directory ancestry must be trusted. Cleanup can
leave an orphaned `.ny-write-*` entry after a host failure.

If directory synchronization fails after publication, the operation returns
`Error("file.write_bytes_durable: publication durability uncertain")`. The
candidate may already be visible. A caller must stop or poison its session and
recover rather than assume the previous file remains or retry blindly.

`sync_directory` synchronizes an existing directory selected without following
its final component. It is useful after separately controlled namespace changes,
including initial directory/lock creation. Its ancestors remain trusted.
Directory synchronization and inspection reject trailing `/` separators except
for filesystem-root paths such as `/`. Pass a named directory without trailing
separators: POSIX otherwise resolves a slash-terminated symlink before applying
the final-component no-follow rule.

`write_bytes_atomic` retains its visibility-only contract. `File.flush` retains
its stream-flush contract; neither acquires the new durability guarantee.

## Lock ownership

`acquire_file_lock(path, true)` exclusively creates a new private backing file;
it fails if an entry already exists. Pass `false` to open an existing lock file.
The file must be regular, owned by the effective user, have one hard link, and
have exactly mode `0600`. Symlinks, special files, and unsafe permissions fail.
The runtime acquires a nonblocking exclusive advisory `flock`, with close-on-exec
and no-follow/nonblocking open flags, and checks the acquired inode still names
the inspected entry. Paths never appear in returned diagnostics.

All writers must use the same persistent lock entry. Do not unlink or replace
that file while participants may still hold it. Creating the file does not
durably publish its name; synchronize the parent when the application needs it.
The limit is 4,096 held locks per execution context.

Returned tokens are positive, monotonically allocated, and never recycled
within a runtime instance. `release_file_lock` checks ownership in the current
execution context; wrong, foreign, stale, and repeated-release tokens fail.
`file_lock_is_held` returns `Ok(true)` only while that context's registry retains
the token. Unknown, stale, foreign, zero, and negative tokens return `Ok(false)`.
It is a query without pathname access or release/reacquisition; it does not
change the lock or verify a new path identity. Unsupported platforms return
an explicit `Error`, including when the token would otherwise be absent.

An embedded application can retain an object after its execution invocation
has finished. Its saved positive token must be checked again before guarded
state is used; the object does not extend the runtime lock's lifetime.
Runtime teardown closes remaining locks on VM completion, failure, reset,
replacement, destruction, or native context end. VM execution checkpoints reject
open locks. Normal POSIX descriptor inheritance still applies across `fork`:
an inherited descriptor can keep a lock held after its parent's copy closes.

These are cooperative filesystem locks, not a security boundary against the
owner, root, arbitrary same-process memory access, or nonparticipating writers.

## Evidence and platform limits

The existing `nil.std.text.byte_at` and `from_byte` helpers also have native
providers for byte-oriented checkpoint serialization. Byte lookup returns `-1`
outside the string; out-of-range byte construction returns empty text. Native
`from_byte(0)` faults explicitly because native text is NUL-free. Use byte arrays
for arbitrary binary content. Joining nonzero byte fragments preserves their
bytes, including UTF-8 sequences.

The source-runtime tests exercise Ny and Matrix through textual and binary VM,
AOT, and JIT, with exact host byte/mode oracles, checked ABI failures, runtime
context teardown, and process death. Linux helper tests inject file and directory
sync failures, including uncertain publication. Windows returns an explicit
unsupported-platform error before filesystem mutation. Other POSIX platforms
need their own execution evidence; this addition does not qualify an installed
or release SDK.

## Private entries and digests

`create_private_directory` exclusively creates one owner-only directory: POSIX
mode `0700`, applied by creation rather than a later chmod, which would leave a
writable window. An existing entry is reported, never adopted, so success means
this call created it. Parents are not created and remain trusted. Creation does
not durably publish the new name; call `sync_directory` when that matters.

`is_private_regular_file` asks whether an entry is regular, owned by the
effective user, singly linked, and exactly mode `0600`. `is_private_directory`
asks whether it is a directory owned by the effective user with exactly mode
`0700`. Neither follows a final symlink, so a link is judged as itself and does
not inherit its target's verdict. Directory inspection uses the same
trailing-separator operand rule as `sync_directory`.

An `Error` means the entry could not be inspected, which is distinct from a
verdict of `false`. Both are distinct from being safe to use: these inspect a
path, so the entry can change between inspection and use. Treat them as
preflight and diagnosis. A caller needing a guarantee must act on an open
descriptor; durable publication and lock acquisition keep their own checked
no-follow opens. The verdicts describe POSIX owner and permission bits only,
not ACLs, extended attributes, capabilities, or mandatory access control.

`sha256_hex` returns 64 lowercase hexadecimal characters over every supplied
byte, embedded NULs included, bounded like the other byte-array operations. It
detects accidental corruption and identifies content. It is **not** a
signature, a message authentication code, encryption, or rollback protection:
anyone able to replace the bytes can replace the digest stored beside them.
Authenticating a digest needs a secret this function does not take. For
accidental-corruption checks over a window of a byte array, `crc32` remains
cheaper and is equally unauthenticated.

A complete storage host remains the application's own. A consuming application
should migrate only the parts it has qualified. See
[NIL-0028](../design/NIL-0028-durable-files-and-locks.md) and
[NIL-0029](../design/NIL-0029-private-entries-and-digests.md).
