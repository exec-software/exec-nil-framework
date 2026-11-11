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
# Every call returns a Result. A network call fails for reasons entirely
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
# empty text, so a loop reading to the end of a request sees one shape; use
# is_open when the difference matters.
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
# have shipped a truncated response from the one-call version is large. On
# failure the count says how far it got.
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

# Put a deadline on reads and writes, in milliseconds. Zero removes it.
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
