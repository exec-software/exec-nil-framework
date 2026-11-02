# Platform behavior

The same Ny or Matrix source is meant to produce the same result on every
supported host. Where the framework had a choice, it chose the portable
behavior rather than the host's local convention — so a program's output, a
recorded transcript, and a byte offset in a diagnostic all mean the same thing
on Linux and on Windows.

Linux is the development and reference host. Windows is held to it: a
difference between the two is a bug unless it appears on this page.

## Line endings

Text output uses `\n`, on every host. `println`, the `io`
surface, `--to-nildata`, the JSON reports, and a native program's own output
are all byte-identical across platforms.

Protocols with specified terminators, such as LSP/DAP frame headers, write
their required CRLF bytes explicitly. Binary stream mode preserves those bytes
too; it does not rewrite them according to the host convention.

Windows C stdio would normally translate `\n` to `\r\n` on the way out and
collapse `\r\n` to `\n` on the way in. Every tool turns that translation off
at startup (`common/standard_streams.hpp`). Rust and Go make the same choice.
Without it, one recorded transcript could not describe both hosts, anything
that hashes or counts the bytes of a report would disagree per platform, and
`console.read_line` would hand a program different text than it gets on Linux
for the same input bytes.

Reading is untranslated too, which means a source file with CRLF endings keeps
them. The compiler reports byte offsets into the file it was actually given, so
a CRLF file's offsets are larger than the same file with LF endings. This is
not a difference between hosts — an LF file and a CRLF file differ the same way
on Linux.

When you want the host's own terminator — writing a file a person will open in
a Windows editor, or speaking a protocol that specifies CRLF — ask for it:

```ny
import ny.lang.system.*

fn main() -> i32
    let line = "first" + newline() + "second" + newline()
    return 0
end fn
```

`newline()` returns `"\r\n"` on Windows and `"\n"` elsewhere. It is the only
place in the runtime that knows the host's terminator, and nothing uses it by
default. The model is C#'s `Environment.NewLine`: the portable behavior is what
you get without asking, and the local convention is available where a reader
can see the choice was made.

## Path separators

Every function in `ny.lang.io`'s path module produces `/`, on every host, and
accepts either separator on input. `separator()` returns `"/"`.

```ny
combine("/tmp", "file.ny")           # "/tmp/file.ny" on every host
normalize("alpha/./beta/../gamma")   # "alpha/gamma" on every host
dirname("C:\\users\\sample\\a.ny")   # "C:/users/sample" on Windows
```

Windows accepts `/` in every file API, so nothing is lost at the syscall
boundary, and a path built by these functions opens on either host unchanged.

The alternative — returning the host separator, as Go's `filepath` does — was
rejected deliberately. It makes identical source produce different output per
platform, and it yields mixed spellings such as `/tmp\file.ny` as soon as a
POSIX-looking literal meets `combine()`.

This is not the host's own spelling. If you need to show a path to a Windows
user or paste one into `cmd.exe`, substitute the separator yourself; the value
`separator()` returns is for splitting and rebuilding paths, not for
presentation.

## Whole-file byte API filenames

`nil.std.io.read_regular_bytes` and `write_bytes_atomic` accept non-empty,
NUL-free, strict UTF-8 filenames on both Linux and Windows. The same filename
bytes identify the same Unicode spelling: no normalization is performed, so
composed and decomposed spellings remain distinct where the filesystem allows
them. Malformed encodings fail with a path-redacted error before any file is
opened or staged. File **contents** remain arbitrary bytes, including NUL.

The Windows provider explicitly converts UTF-8 to wide paths, even when the
native runtime is embedded in an executable without a UTF-8 process manifest.
This is a contract for these two language APIs, not a claim about every STL
file operation. Internal C++ host-path helpers retain their existing native
path / legacy narrow-string behavior. Parent-directory containment, ACLs and
durability limitations remain documented in the [standard library](standard-library/index.md).

## Known differences

These are host behaviors the framework cannot reconcile. Everything else should
match; report a difference that is not listed here.

### Environment values

Empty environment values remain present on both hosts:

```ny
set_environment_variable(name, "")       # Ok on both hosts
get_environment_variable(name)           # Some("") on both hosts
```

Only `unset_environment_variable` removes the entry. The Windows provider uses
the Unicode Win32 environment APIs, preserving UTF-8 names and values without
the CRT's empty-string deletion behavior. Win32 distinguishes an empty string
from a null pointer; [SetEnvironmentVariableW](https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-setenvironmentvariablew)
deletes only for the latter. Names still follow the host's case-sensitivity
rules: Windows names are case-insensitive, unlike Unix names.

### Signals have no Windows equivalent

A process result carries `signal` and reports `128 + signal` as the status of a
child killed by one. Windows has no signals and no wait status to decode, so
`signal` is always `0` there and cancellation is reported through the exit code
the framework terminates the process with. Interruption still works: `nilbuild`
and `niltest` install a console control handler, and Ctrl+C cancels a build on
either host.

### A linked executable carries `.exe`

`nilcompiler --native -o program` writes `program.exe` on Windows, and
`nillinker` appends the suffix when the requested name has no extension. A
script that builds a native program and then runs it by the name it asked for
needs to account for the suffix. `nil::detail::native_executable_path` is the
compiler's rule, and `tests/native_output_staging.py` has it plus the two
others the toolchain uses — they are not interchangeable.

### SQLite is not installed with Windows

The database provider loads `sqlite3.dll` at run time, the way it loads
`libsqlite3.so.0` on Linux. Windows ships `winsqlite3.dll`, which Microsoft
documents as being for its own use, and no `sqlite3.dll`. Install one for
`db_available("sqlite")` to report the provider. Building the provider at all
also requires `sqlite3.h` on the include path, exactly as on Linux.
