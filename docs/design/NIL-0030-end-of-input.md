# NIL-0030: Reading a line without faulting at end of input

- **Status:** Implemented additive surface on every execution route; release
  SDK qualification remains separate.

## Motivation

`read_line` faults at end of input. That suits a program that requires input,
but not a service that reads requests until its caller closes the stream: a
clean shutdown became a fault, so a long-running reader written in Ny or Matrix
could not terminate normally.

The Matrix-authored Enigma durable host surfaced this. Its Python predecessor
exits zero when standard input closes, and its recovery suite closes the
stream to end a session, so a host that faulted there would report failure for
an ordinary shutdown and leave a crash report behind.

## Decision

Add `read_line_option`, which reads a line exactly as `read_line` does but
answers `None` at end of input instead of faulting. `read_line` keeps its
contract; nothing that reads input today changes behavior.

A genuine input error still faults. This is the substance of the decision
rather than a detail: a reader that must fail closed cannot be handed "no more
input" when the truth is "cannot read". Shutting down cleanly on a broken
stream would silently convert an I/O failure into a successful, quiet exit.

## Distinguishing the two is not free

A C++ stream reports a genuine end of input and a host read error identically:
`eofbit` and `failbit` set, `badbit` clear. Reading a directory descriptor as
standard input, for example, is indistinguishable from an empty pipe by stream
state alone. Only `errno` separates them, so both providers clear `errno`
immediately before the read and inspect it only when no characters were
extracted. A nonzero `errno` is a failure; zero is end of input.

Treating the ambiguous case as a failure is deliberate. A reader that stops
early on a healthy stream is a visible bug; a reader that reports success after
an unread request is a silent one.

A final line without a terminator is still a line, as with `read_line`. Native
text cannot contain NUL, so native input rejects that byte explicitly.

## Validation

The surface rides the existing checked ordinary-I/O bridge, so it is lowered
once and reaches the interpreter, AOT and JIT routes without separate codegen.
Paired Ny and Matrix fixtures cover several lines then a clean stop, a final
line without a terminator, and empty input, across those routes. A separate
case feeds a directory descriptor as standard input and requires a fault with
no clean-stop output, which is the check that would fail if the implementation
read stream state instead of `errno`.

As with NIL-0028 and NIL-0029, source declarations alone do not give an
installed SDK the new provider, and release qualification is independent work.
See the [console reference](../standard-library/index.md).
