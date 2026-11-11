# Stream sockets

Import `nil.std.net.socket` from Ny or Matrix. This is TCP as the runtime
provides it: the transport `nil.std.net.http` has been describing without one.

Interpreter-backed. The native route does not carry these yet, and a program
compiled through it will not find them.

| Ny API | Behavior |
| --- | --- |
| `listen(host: text, port: i32, backlog: i32) -> Result<i64, text>` | Open, bind and listen in one step; gives back a handle. |
| `accept(handle: i64) -> Result<i64, text>` | Block until a connection arrives; gives back a handle to it. |
| `connect(host: text, port: i32) -> Result<i64, text>` | Connect out, resolving a name if it is one. |
| `read(handle: i64, maximum_bytes: i64) -> Result<text, text>` | Read what has arrived, up to a ceiling. |
| `write(handle: i64, data: text) -> Result<i64, text>` | Send all of it; gives back the byte count. |
| `close(handle: i64) -> Result<i64, text>` | Close and release the handle. |
| `is_open(handle: i64) -> bool` | Whether the runtime still holds this handle. |
| `set_timeout(handle: i64, milliseconds: i64) -> Result<i64, text>` | Deadline for reads and writes; zero removes it. |
| `set_no_delay(handle: i64, enabled: bool) -> Result<i64, text>` | Send small writes immediately. |
| `peer_address(handle: i64) -> Result<text, text>` | The peer's address and port, as text. |

Matrix uses the same names; `maximum_bytes` is spelled `maximum_bytes` because
it is an Ny declaration and Matrix keeps a foreign name's spelling.

## Everything returns a Result

A network call fails for reasons entirely outside the program — the port is
taken, the peer went away, the name does not resolve. For a server those are
ordinary answers on an ordinary day, so none of these raise.

## Handles are opaque

A program is handed a token into a runtime-owned table, never a host
descriptor. It cannot read from or close something it was not given, including
a descriptor the host opened for its own purposes, and a number it never
received is refused with a sentence rather than used.

A listener and a connection are told apart: accepting on a connection, or
reading from a listener, is a named error and not an `EINVAL`.

The runtime closes whatever a run leaves open. A program that exits without
closing its listener — or one that crashes — does not leave the port bound
against the next run, which during development is the difference between a
server that restarts and one that fails every second time for a reason
unrelated to the change just made.

## Binding does not resolve names

`host` is numeric, empty, or `localhost`. A server binds an address it already
knows, and a DNS lookup on that path is both a blocking call and a way to end
up listening somewhere unintended. An empty host means every interface, which
is right behind a firewall and wrong on a laptop on a shared network; the
caller chooses and the library does not choose for them.

`connect` does resolve, because a client is handed names. A host with both an
IPv4 and an IPv6 address is tried in turn and the first to connect wins.

## Reads are short, and a close is not an error

TCP is a stream. A request may arrive in several pieces, so a read returning
less than asked for is normal. An orderly close by the peer comes back as
empty text rather than an error, so a loop reading to the end of a request
sees one shape; `is_open` distinguishes them when it matters.

What `read` gives back is bytes, not necessarily valid UTF-8 — it is whatever
the peer sent. Read it with `nil.std.text`'s `byte_at` rather than `substring`
if it may not be text. One call never transfers more than 8 MiB.

## Writes complete or report how far they got

A single `send` is permitted to take less than it was given. `write` loops
until everything is gone or the attempt fails, because the number of servers
that have shipped a truncated response from the one-call version is large. On
failure the count says how far it reached.

A peer that closes mid-write is reported rather than fatal: the runtime
suppresses the signal whose default disposition would end the process. A
server that dies when a browser closes a tab is not a server, and this is the
case that a test which never closes a connection early will not catch.

## Timeouts are not optional in practice

A single-threaded server without `set_timeout` is one slow client away from
serving nobody: a connection that opens and then says nothing holds the accept
loop for as long as it likes. Set a deadline on every accepted connection.

## An echo server

```matrix
book example.echo

read nil.std.core
read nil.std.text.string_utils
read nil.std.net.socket

main returns number begin paragraph
    value opened is nil.std.net::listen with host being "127.0.0.1", port being 8080, backlog being 16
    consider opened
        when Result.Error holding why then begin block
            say "cannot listen: " plus why
            give 1
        end block
        when Result.Ok holding listener then begin block
            repeat forever
                value taken is nil.std.net::accept with handle being listener
                consider taken
                    when Result.Ok holding client then begin block
                        evaluate nil.std.net::set_timeout with handle being client, milliseconds being 5000
                        value heard is nil.std.net::read with handle being client, maximum_bytes being 65536
                        consider heard
                            when Result.Ok holding incoming then
                                evaluate nil.std.net::write with handle being client, data being incoming
                            otherwise say "read failed"
                        end consider
                        evaluate nil.std.net::close with handle being client
                    end block
                    when Result.Error holding why then say "accept failed: " plus why
                    otherwise say "accept gave neither"
                end consider
            end repeat
            give 0
        end block
        otherwise give 1
    end consider
end paragraph
```

## What is not here

One connection at a time. There are no threads to hand a connection to and no
async in the language, so a server built on this accepts, serves and closes in
order. That is enough for a development server, a local tool, or anything
behind a reverse proxy that holds the slow clients.

No TLS. No datagrams. No `select` or `poll`, so a program cannot wait on
several sockets at once — which is the piece that would have to arrive before
concurrent serving is possible.

The contract these keep is checked by `tests/tools/net_socket_contract.py`,
which runs a Matrix page as a process and plays the other end from Python.
