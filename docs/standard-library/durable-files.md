# Durable files and advisory locks

`nil.std.io` provides filesystem primitives for applications that need a
durable checkpoint and a cooperative single-writer protocol. Ny and Matrix use
the same runtime provider and checked results. POSIX and Windows providers are
implemented and pass source-runtime checks. The [Windows development SDK
campaign](../../notebook/2026-11-21-windows-durable-parity.md) also passes installed
checks against both source and binary libraries. This is not stable-release or
power-cut qualification.
Windows durable publication and directory synchronization require local NTFS.

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

`write_bytes_durable` takes bounded byte arrays and strict UTF-8, nonempty,
NUL-free filenames. It creates a fresh private staging file and uses no-clobber
publication when `replace` is false. Replacement preflight requires a regular
destination, and publication replaces the entry without
following it. Existing ownership, mode, ACLs, and extended attributes are not
copied. On POSIX, the existing atomic helper creates mode `0600` and synchronizes
the **same directory descriptor** used for publication, after synchronizing the
candidate file and publishing its entry. The POSIX contract is unchanged.

Windows first opens the parent directory without following its final reparse
point and verifies that the held handle identifies writable local NTFS with
persistent ACLs. An unknown, remote or other filesystem is rejected before
staging. The fresh file receives the effective user's protected owner-only
security descriptor at creation. Creation, replacement inspection and rename
are relative to the retained directory handle. The runtime flushes file data,
publishes the name, closes the writing handle, and flushes that same directory.
Failed staging is deleted through its held handle rather than a pathname.

Success means those synchronization calls succeeded. It assumes a filesystem
and device stack honoring the platform's flush and publication operations. It
does not prove hardware power-cut behavior, protect against privileged namespace replacement,
or authenticate data. Parent-directory ancestry must be trusted. Cleanup can
leave an orphaned `.ny-write-*` entry after a host failure.

If directory synchronization fails after publication, or the Windows writing
handle reports a close failure after rename, the operation returns
`Error("file.write_bytes_durable: publication durability uncertain")`. The
candidate may already be visible. A caller must stop or poison its session and
recover rather than assume the previous file remains or retry blindly.
Windows still attempts the final directory flush after a reported close failure.

The Windows restriction follows [MS-FSA directory-flush semantics](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-fsa/0de7dc40-9627-437e-a4df-c4696cdc3d02)
and [Appendix A, note 80](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-fsa/4e3695bd-7574-4f24-a223-b4679c065b63),
which identify NTFS as implementing directory persistence. A successful flush
on another filesystem is insufficient. The implementation uses
[file/directory flushes](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers)
and [native handle-relative rename](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_rename_information),
without a volume-wide flush or enabling privileges.

`sync_directory` synchronizes an existing directory selected without following
its final component. It is useful after separately controlled namespace changes,
including initial directory/lock creation. Its ancestors remain trusted.
On POSIX, directory synchronization and inspection reject trailing `/` separators except
for filesystem-root paths such as `/`. Pass a named directory without trailing
separators: POSIX otherwise resolves a slash-terminated symlink before applying
the final-component no-follow rule. Windows path restrictions are described below.

`write_bytes_atomic` retains its visibility-only contract. `File.flush` retains
its stream-flush contract; neither acquires the new durability guarantee.

## Lock ownership

`acquire_file_lock(path, true)` exclusively creates a new private backing file;
it fails if an entry already exists. Pass `false` to open an existing lock file.
The file must be regular, owned by the effective user, have one hard link, and
have exactly mode `0600` on POSIX, or match the Windows owner-only ACL profile
below. Symlinks/reparse points, special files and nonmatching permissions fail.
On POSIX the runtime acquires a nonblocking exclusive advisory `flock`, with
close-on-exec and no-follow/nonblocking open flags, and checks the acquired inode still names
the inspected entry. On Windows it requests an exclusive, nonblocking
[`LockFileEx`](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-lockfileex)
lock at byte zero with length one, including on an empty backing file. The
handle is not inheritable and denies delete sharing, pinning the final entry
against rename/deletion while held. The runtime checks the held and visible
file identities and private metadata. Paths never appear in returned diagnostics.

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

The source-runtime contracts cover Ny and Matrix through textual and binary VM,
AOT, and JIT, with exact host byte/mode oracles, checked ABI failures, runtime
context teardown, and process death. Linux helper tests inject file and directory
sync failures, including uncertain publication. Windows contracts cover owner
ACLs, reparse and hard-link rejection, context-owned byte-range locks, local-NTFS
publication, and failures before and after publication. Test-only Win32 wrappers
inject flush/close failures; the production provider has no fault switches.
An independent Windows oracle checks held file bytes, owner/DACL, file identity,
namespace and externally reacquired locks. Implemented tests are not a claim
that every platform campaign has completed. Current-source, installed-SDK and
release evidence remain separate, including for macOS and Windows.

## Private entries and digests

`create_private_directory` exclusively creates one owner-only directory: POSIX
mode `0700` or a protected Windows owner-only descriptor, applied at creation.
An existing entry is reported, never adopted. On POSIX success requires an
owned directory with exactly mode `0700`. If the process
umask removes owner permissions, creation returns an error and can leave the
new directory in place; it does not change the umask or repair permissions.
Windows likewise verifies the created entry through a no-follow handle and can
leave the new entry on a verification failure; it does not repair its ACL or
delete through a potentially replaced pathname.
Parents are not created and remain trusted. Creation does
not durably publish the new name; call `sync_directory` when that matters.

`is_private_regular_file` asks whether an entry is regular, owned by the
effective user and singly linked. On POSIX it additionally requires exactly
mode `0600`; `is_private_directory` requires effective-user ownership and mode
`0700`. On Windows both require the owner-only ACL profile below. Neither follows
a final symlink/reparse point, so a link is judged as itself and does not inherit
its target's verdict. Directory inspection rejects trailing separators except
filesystem roots.

An `Error` means the entry could not be inspected, which is distinct from a
verdict of `false`. Both are distinct from being safe to use: these inspect a
path, so the entry can change between inspection and use. Treat them as
preflight and diagnosis. A caller needing a guarantee must act on an open
descriptor; durable publication and lock acquisition keep their own checked
no-follow opens. POSIX verdicts describe owner and permission bits only, not ACLs,
extended attributes, capabilities or mandatory access control.

### Windows owner and pathname profile

The three private-entry APIs and three lock APIs use the effective thread user,
falling back to the process user only when no thread token exists. The entry
owner must equal that user's SID, not a default owner group. The DACL must
contain exactly one ordinary allow ACE granting `FILE_ALL_ACCESS` to that SID.
It must be explicit in a protected DACL or inherited from an owner-only parent.
Directory ACEs propagate to both files and directories; file ACEs have no
propagation flags. Null, empty, extra, deny, callback and inherit-only ACE
profiles are rejected. This conservative profile can reject other restrictive
ACL arrangements; it is not a general effective-permissions calculator.
[Win32 owner/DACL inspection](https://learn.microsoft.com/en-us/windows/win32/api/aclapi/nf-aclapi-getsecurityinfo)
does not modify the entry. Reparse points, wrong kinds, delete-pending entries
and multiply-linked regular files return a false verdict when inspectable;
missing entries and inspection failures return `Error`.

These Windows APIs accept ordinary relative names and drive-absolute DOS paths
with either separator. They reject UNC/device/extended namespaces, alternate
data streams, drive-relative or current-drive-rooted paths, empty/dot/parent
components, reserved DOS basenames, leading spaces, and trailing dots/spaces.
A trailing separator is accepted only for an explicit drive root where a
directory operand is allowed. `sync_directory(".")` additionally selects the
current directory; a bare durable-write filename uses that same parent
internally. Suffixes such as `dir/.` are rejected. These restrictions avoid
[Win32 name normalization and device aliases](https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file).
They do not validate ancestor ownership or create a pathname sandbox. The
local-NTFS restriction applies specifically to durable writes and directory
synchronization; all six private-entry/lock APIs still require their checked
Windows metadata and handle operations to succeed.

### Content digests

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
