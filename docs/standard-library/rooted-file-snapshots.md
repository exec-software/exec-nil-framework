# Rooted file snapshots

`nil.std.io.read_rooted_snapshot(root, relative, maximum_bytes)` reads one
bounded regular file below a trusted directory. It returns
`Result<FileSnapshot, text>`:

| Field | Type | Meaning |
| --- | --- | --- |
| `content` | `array<u8>` | The exact bytes read, including NUL and arbitrary high bytes. |
| `modified_ms` | `i64` | Modification time observed on the final file handle, in Unix milliseconds. |
| `sha256` | `text` | The 64 lowercase hexadecimal digits of SHA-256 over `content`. |

The caller supplies a limit from zero through 512 MiB. A zero limit accepts
an empty file. Oversized input is rejected before allocating its contents.
SHA-256 is computed over the loaded bytes, so replacing content while keeping
its size and timestamp does not preserve its digest.

```ny
import nil.std.io.file

fn inspect_asset(root: text)
    match nil::std::io::read_rooted_snapshot(root, "assets/site.css", 1048576)
        Result.Ok(snapshot) ->
            println(snapshot.content.len())
            println(snapshot.modified_ms)
            println(snapshot.sha256)
        Result.Error(message) -> println(message)
    end match
end fn
```

The `relative` path must be nonempty strict UTF-8, with `/` separating its
components. Empty components, `.` and `..`, absolute paths, backslashes,
colons, and NUL bytes are rejected. The root selects the trusted directory;
every descendant component is opened relative to an already-open directory.
Descendant symlinks and Windows reparse points are rejected, including the
final component. Directories and other special files cannot be returned as
content. Errors do not include caller-supplied paths.

The final handle supplies both bytes and metadata, and the runtime checks for
observed changes around the read. This does not make a file continuously
immutable or prevent privileged mount changes, hard links already placed in
the root, or every concurrent write. The returned digest describes the bytes
actually returned; it is not a separate path lookup or a metadata hash.

The snapshot record uses the same byte-array representation for interpreter,
LLVM JIT, and native executable routes. Code using the interpreter's raw-byte
text representation can convert `snapshot.content` with
[`nil.std.text.from_bytes`](text-spans.md#byte-values). That conversion remains
interpreter-only: native text has a NUL-terminated representation and must not
silently truncate binary content. Native consumers can inspect, hash-check, or
write the byte array without converting it to text.
