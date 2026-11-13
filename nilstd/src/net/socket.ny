@schema
@pragma once

# =============================================================================
# nil.std.net — stream sockets
# =============================================================================
#
# TCP, as the runtime provides it. This is the transport nil.std.net.http has
# been describing without one: until these intrinsics existed, no NIL or Matrix
# program could accept a connection, and the socket design under experimental/
# had nothing beneath it to build on.
#
# Operations that can fail return a Result. A network call fails for reasons entirely
# outside the program — the port is taken, the peer went away, the name does
# not resolve — and for a server those are ordinary answers on an ordinary
# day, not exceptional conditions. None of these raise.
#
# A program is handed an opaque handle, never a host descriptor. It cannot
# read from or close something it was not given, and the runtime closes
# whatever is still open when the run ends, so a program that exits without
# closing its listener does not leave the port bound against the next one.
#
# Interpreter-backed. The native route does not carry these yet.

module nil.std.net

import nil.std.core

# -----------------------------------------------------------------------------
# Listening and accepting
# -----------------------------------------------------------------------------

# Open a socket, bind it and listen, in one step.
#
# `host` is numeric, empty, or "localhost". Binding never resolves a name
# through DNS: a server binds an address it already knows, and a lookup on
# that path is both a blocking call and a way to end up listening somewhere
# unintended. Empty means every interface — right behind a firewall or a
# reverse proxy, and wrong on a laptop on a shared network. The caller
# chooses; this does not choose for them.
#
# A backlog below one becomes 128. The kernel clamps the ceiling regardless.
@intrinsic("net.listen")
pub fn listen(host: text, port: i32, backlog: i32) -> Result<i64, text>

# Bind an explicitly shared listener. Every listener sharing the endpoint must
# opt in before binding; setting reuse-port after listen cannot do this.
# Uses SO_REUSEPORT on Linux/macOS; unsupported hosts return Result.Error.
@intrinsic("net.listen_reuse_port")
pub fn listen_reuse_port(host: text, port: i32, backlog: i32) -> Result<i64, text>

# Wait for a connection and return a handle to it.
#
# Blocks until one arrives. An interrupted call is retried inside the runtime
# rather than reported, so an unrelated signal does not look like a failure.
@intrinsic("net.accept")
pub fn accept(handle: i64) -> Result<i64, text>

# -----------------------------------------------------------------------------
# Connecting
# -----------------------------------------------------------------------------

# Connect to a host, resolving a name if it is one.
#
# Unlike listen, this resolves: a client is handed names. A host with both an
# IPv4 and an IPv6 address is tried in turn and the first to connect wins.
@intrinsic("net.connect")
pub fn connect(host: text, port: i32) -> Result<i64, text>

# -----------------------------------------------------------------------------
# Reading and writing
# -----------------------------------------------------------------------------

# Read what has arrived, up to `maximum_bytes`.
#
# A short read is normal and is not an error: TCP is a stream, and a request
# may arrive in several pieces. An orderly close by the peer comes back as
# empty text. A timeout or would-block result is an error for this blocking
# interface. is_open only reports whether this VM still owns the handle;
# receive distinguishes bytes, EOF and would-block for nonblocking callers.
#
# The bytes are returned as text and are not necessarily valid UTF-8 — they
# are whatever the peer sent. Read them with nil.std.text's byte_at rather
# than with substring if they may not be text.
#
# One call never transfers more than 8 MiB however much is asked for.
@intrinsic("net.read")
pub fn read(handle: i64, maximum_bytes: i64) -> Result<text, text>

# Send all of it, and report how many bytes went.
#
# A single send is permitted to take less than it was given; this loops until
# everything is gone or the attempt fails, because the number of servers that
# have shipped a truncated response from the one-call version is large.
# Failure returns error text without a partial byte count. An event loop
# should use send_some and retain its own progress offset.
#
# A peer that closes mid-write is reported, not fatal: the runtime suppresses
# the signal that would otherwise end the process.
@intrinsic("net.write")
pub fn write(handle: i64, data: text) -> Result<i64, text>

# -----------------------------------------------------------------------------
# Lifetime and options
# -----------------------------------------------------------------------------

# Close a socket and release its handle.
#
# Closing something already closed succeeds and reports zero, because that is
# what a cleanup path does when an earlier one has already run.
@intrinsic("net.close")
pub fn close(handle: i64) -> Result<i64, text>

# Whether the runtime still holds this handle.
#
# Not whether the peer is reachable: nothing short of a write can tell you
# that, and anything claiming otherwise is guessing.
@intrinsic("net.is_open")
pub fn is_open(handle: i64) -> bool

# Set each blocking receive/send timeout, in milliseconds. Zero removes it.
# Values must be between 0 and 2147483647 on every supported host.
#
# A single-threaded server without this is one slow client away from serving
# nobody: a connection that opens and then says nothing holds the accept loop
# for as long as it likes.
@intrinsic("net.set_timeout")
pub fn set_timeout(handle: i64, milliseconds: i64) -> Result<i64, text>

# Send small writes immediately instead of waiting to coalesce them.
#
# Worth setting on a connection serving a request/response protocol, where
# the last small write of a response is the one the client is waiting for.
@intrinsic("net.set_no_delay")
pub fn set_no_delay(handle: i64, enabled: bool) -> Result<i64, text>

# The peer's address and port, as text, for a log line.
#
# For logging and diagnostics. Not for an access decision: an address is not
# an identity, and behind any proxy it is the proxy's.
@intrinsic("net.peer_address")
pub fn peer_address(handle: i64) -> Result<text, text>

# -----------------------------------------------------------------------------
# Serving more than one connection at a time
# -----------------------------------------------------------------------------
#
# This runtime is single-threaded by construction: the interpreter holds one
# stack, one heap and one set of globals, with no lock anywhere near them, and
# nil.std.threading offers only sleep. So concurrency here is an event loop,
# not a thread per connection — poll says which sockets can be acted on, and
# one pass round the loop acts on each in turn.
#
# For more than one core, run more than one process and let them share the
# port with set_reuse_port. The kernel load-balances accepts between them.
# That is nginx's shape rather than Apache's prefork, and it is the one this
# runtime can actually keep.
#
# The watch set lives in the runtime rather than being passed in per call, so
# no array has to cross the intrinsic boundary. That is not tidiness: a Matrix
# page cannot index an Ny generic, and an event loop that could only be
# written in Ny would be one half the language could not use.

# Readiness flags. Deliberately not poll(2)'s numbering, which differs between
# platforms — a program that learned the host's values would break on a move.
pub const ready_readable: i64 = 1
pub const ready_writable: i64 = 2
# The peer hung up, or the socket is in error. Either way it is finished.
pub const ready_finished: i64 = 4

# Whether a socket blocks when there is nothing to do.
#
# An event loop wants its listener non-blocking: poll has already said a
# connection is pending, and a blocking accept that then finds nothing —
# because the peer withdrew in between — would stop the whole loop on behalf
# of one socket.
#
# A fully nonblocking loop also sets this on accepted connections, then uses
# receive and send_some to distinguish no progress from an orderly close.
# Readiness can change before the transfer; it is not a promise that a
# blocking read or write will return immediately.
@intrinsic("net.set_blocking")
pub fn set_blocking(handle: i64, blocking: bool) -> Result<i64, text>

# Change SO_REUSEPORT on an existing socket where supported.
#
# This runs after binding and cannot make a second ordinary listener opt in
# before its bind. Use listen_reuse_port for every shared listener instead.
# Windows returns an error when enabling this option.
@intrinsic("net.set_reuse_port")
pub fn set_reuse_port(handle: i64, enabled: bool) -> Result<i64, text>

# Watch a socket, for the given flags. Watching it again replaces the flags.
#
# A listener is watched for ready_readable, which means a connection is
# pending. A connection is watched for ready_readable while a request is being
# read, and for ready_writable while a response is going out to a peer that is
# not taking it fast enough.
@intrinsic("net.watch")
pub fn watch(handle: i64, flags: i64) -> Result<i64, text>

# Stop watching. Unwatching something not watched succeeds, because that is
# what a close path does when it does not track whether it registered.
@intrinsic("net.unwatch")
pub fn unwatch(handle: i64) -> Result<i64, text>

# How many sockets are watched.
@intrinsic("net.watch_count")
pub fn watch_count() -> i64

# Wait until a watched socket can be acted on; gives back how many are ready.
#
# A negative timeout waits indefinitely, which is what an idle server does
# between requests. Zero polls and returns at once.
#
# Each wait replaces whatever the last one found. A program that did not drain
# the previous set has moved on, and stale readiness would hand it a handle
# that may since have been closed and its number reissued.
#
# A socket closed since the last wait is dropped from the watch set rather
# than polled, so a loop that forgets to unwatch a connection it closed is
# untidy rather than broken.
@intrinsic("net.wait")
pub fn wait(timeout_milliseconds: i64) -> Result<i64, text>

# One ready socket per call, packed; zero when the set is drained.
#
# Packed because an intrinsic returns one value, and two calls whose meaning
# depended on being made in the right order would be worse than arithmetic the
# caller can see. Use ready_handle and ready_flags rather than writing it out.
@intrinsic("net.next_ready")
pub fn next_ready() -> Result<i64, text>

# The socket from a packed next_ready value.
pub fn ready_handle(packed: i64) -> i64
    return packed / 8
end fn

# The readiness flags from a packed next_ready value.
pub fn ready_flags(packed: i64) -> i64
    return packed % 8
end fn

# Whether a packed value says the socket has something to read.
pub fn is_readable(packed: i64) -> bool
    return (ready_flags(packed) & ready_readable) != 0
end fn

# Whether a packed value says the socket will take a write.
pub fn is_writable(packed: i64) -> bool
    return (ready_flags(packed) & ready_writable) != 0
end fn

# Whether a packed value says the socket is finished.
#
# Both this and is_readable can be true at once: a peer that hung up may still
# have sent something, and that something has to be read before the connection
# is dropped or the last request of a session is lost.
pub fn is_finished(packed: i64) -> bool
    return (ready_flags(packed) & ready_finished) != 0
end fn

# -----------------------------------------------------------------------------
# Reading and writing without blocking
# -----------------------------------------------------------------------------
#
# `read` and `write` above suit a blocking socket: read waits for something,
# write loops until everything has gone. An event loop cannot use write that
# way — a large response to a client that reads slowly fills the socket buffer,
# the call blocks, and one slow reader stops the whole loop. These are the
# non-blocking pair.

# Take what has arrived, and say which of three things happened.
#
# Returns the byte count on success, 0 when the peer closed its side, and -1
# when nothing is available yet. Call `received` for the bytes themselves.
#
# Two calls rather than one because a single Result<text, text> has room for
# two answers and a non-blocking read has three. Conflating "nothing yet" with
# "the peer closed" is the classic event-loop bug: a spurious wakeup then
# looks like a hangup and the connection is dropped in the middle of a request.
@intrinsic("net.receive")
pub fn receive(handle: i64, maximum_bytes: i64) -> Result<i64, text>

# The bytes from the last successful `receive` on any socket.
#
# Valid immediately after a `receive` that returned a positive count; empty
# after one that reported a close or a would-block. Read it before the next
# receive, which replaces it.
@intrinsic("net.received")
pub fn received() -> Result<text, text>

# Send what the socket will take right now, starting at an offset.
#
# Returns the number of bytes sent, or -1 when the socket would block. The
# caller keeps the remainder and sends it when `wait` reports the socket
# writable, which is what stops a slow reader from holding up everyone else.
@intrinsic("net.send_some")
pub fn send_some(handle: i64, data: text, offset: i64) -> Result<i64, text>
