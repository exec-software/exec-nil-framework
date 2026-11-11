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
| `set_blocking(handle: i64, blocking: bool) -> Result<i64, text>` | Whether a call waits when there is nothing to do. |
| `set_reuse_port(handle: i64, enabled: bool) -> Result<i64, text>` | Let other processes bind this same port. |
| `watch(handle: i64, flags: i64) -> Result<i64, text>` | Watch a socket for readability, writability, or both. |
| `unwatch(handle: i64) -> Result<i64, text>` | Stop watching. |
| `watch_count() -> i64` | How many sockets are watched. |
| `wait(timeout_milliseconds: i64) -> Result<i64, text>` | Wait for readiness; gives back how many are ready. |
| `next_ready() -> Result<i64, text>` | One ready socket per call, packed; zero when drained. |
| `ready_handle` / `ready_flags` / `is_readable` / `is_writable` / `is_finished` | Unpack what `next_ready` gave. |
| `receive(handle: i64, maximum_bytes: i64) -> Result<i64, text>` | Take what arrived: count, 0 for closed, -1 for nothing yet. |
| `received() -> Result<text, text>` | The bytes from the last `receive`. |
| `send_some(handle: i64, data: text, offset: i64) -> Result<i64, text>` | Send what the socket takes now; -1 when it would block. |

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

## Serving many connections at once

An event loop, not a thread per connection. This runtime is single-threaded by
construction — the interpreter holds one stack, one heap and one set of
globals, with no lock anywhere near them, and `nil.std.threading` offers only
`sleep` — so concurrency here is `poll` telling you which sockets can be acted
on, and one pass round a loop acting on each in turn.

That is nginx's shape rather than Apache's prefork, and it is the one this
runtime can keep. It is also the one that scales: 64 simultaneous connections,
each sending its request in two parts with a quarter-second pause between, are
served in about one pause rather than sixty-four of them. The contract test
asserts exactly that difference, because a throughput figure would measure the
machine while this measures the shape.

The watch set lives in the runtime rather than being passed in per call, so no
array crosses the intrinsic boundary. That is not tidiness: a Matrix page
cannot index an Ny generic, and an event loop that could only be written in Ny
would be one that half the language could not use.

### The shape of the loop

```matrix
evaluate nil.std.net::set_blocking with handle being listener, blocking being false
evaluate nil.std.net::watch with handle being listener, flags being nil.std.net::ready_readable

repeat forever
    evaluate nil.std.net::wait with timeout_milliseconds being 10000
    variable draining as boolean is true
    repeat while draining
        value packed is nil.std.net::next_ready with nothing
        !!! zero means the ready set is drained; otherwise unpack and act
    end repeat
end repeat
```

`next_ready` packs the handle and the flags into one number — `handle * 8 +
flags` — because an intrinsic returns one value, and two calls whose meaning
depended on being made in the right order would be worse than arithmetic the
caller can see. `ready_handle`, `ready_flags`, `is_readable`, `is_writable` and
`is_finished` are ordinary Ny functions over it, so nobody has to write the
division.

### Three things the loop has to get right

**Drain the listener.** `poll` reports a listener readable once however many
connections are waiting, so accept until it answers zero. A loop that accepts
one per wait falls behind under load and never catches up.

**Use `receive` and `send_some`, not `read` and `write`, on a non-blocking
connection.** `read` and `write` suit a blocking socket: read waits for
something, write loops until everything has gone. In an event loop `write` is
the dangerous one — a large response to a client that reads slowly fills the
socket buffer, the call blocks, and one slow reader stops everybody.

`receive` exists because a non-blocking read has three outcomes and a
`Result<text, text>` has room for two. It reports the byte count, `0` for a
peer that closed, or `-1` for nothing yet, and `received` hands over the bytes.
Conflating the last two is the classic event-loop bug: a spurious wakeup then
looks like a hangup and the connection is dropped mid-request.

**`is_readable` and `is_finished` can both be true.** A peer that hung up may
still have sent something, and that something has to be read before the
connection is dropped, or the last request of a session is lost.

### More than one core

A second core means a second process, because a second thread would need an
interpreter that could survive one. `set_reuse_port` lets several processes
bind the same port and the kernel load-balances accepts between them.

It is opt-in and distinct from the `SO_REUSEADDR` that `listen` always sets:
that one only allows rebinding a port left in `TIME_WAIT`, while this one
deliberately allows two servers to share a port. Switched on by accident it
would silently split traffic between a running server and a stray one.

## What is not here

No TLS. No datagrams. No `sendfile`, so a file goes out through the program
rather than straight from the page cache.

`poll` is O(n) in the watch set per call, which starts to matter in the
thousands. `epoll` and `kqueue` do not; the interface here takes the whole set
per call precisely so that either can replace `poll` underneath without a
caller noticing.

The contract these keep is checked by `tests/tools/net_socket_contract.py`,
which runs a Matrix page as a process and plays the other end from Python.
