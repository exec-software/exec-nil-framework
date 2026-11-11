#!/usr/bin/env python3
"""The network intrinsics, against a real peer.

A socket cannot be checked by comparing stdout: the interesting behaviour is
what the other end of the connection sees. So each case here runs a Matrix
page as a process and plays the other side from Python.

What is being pinned down is the contract the standard library promises, not
the implementation:

  * a program can listen, accept, read and write, and what arrives on the wire
    is byte-for-byte what the page sent;
  * a handle is opaque -- a number the program was never given is refused with
    a sentence, not used as a file descriptor;
  * a listener and a connection are not interchangeable;
  * the runtime closes what a program leaves open, so a port does not stay
    bound against the next run;
  * a read deadline expires instead of hanging the process forever; and
  * a peer that vanishes mid-write is an error the program can see, rather
    than a signal that kills it.

That last one is the reason this file exists rather than a simpler fixture. A
server that dies when a browser closes a tab passes every test that does not
close a connection early.
"""

from __future__ import annotations

import argparse
import os
import queue
import struct
import threading
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def free_port() -> int:
    """A port the kernel says is free, released before the page binds it.

    There is a race here in principle. In practice the kernel does not hand
    the same ephemeral port out twice in the time between these two lines, and
    the alternative -- a fixed port -- fails whenever anything else on the
    machine happens to be using it, which is worse because it looks like a
    code failure.
    """
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return int(probe.getsockname()[1])


def build(compiler: Path, stdlib: Path, source: str, workdir: Path, name: str) -> Path:
    page = workdir / f"{name}.matrix"
    page.write_text(source)
    artifact = workdir / f"{name}.nil"
    built = subprocess.run(
        [str(compiler), "--no-bundle", "-I", str(stdlib),
         "--output", str(artifact), str(page)],
        capture_output=True, text=True, timeout=60)
    if built.returncode != 0:
        raise AssertionError(
            f"{name} did not compile:\n{built.stdout}{built.stderr}")
    return artifact


class PageOutput:
    """Bounded background drain: readiness deadlines cannot block in readline."""
    def __init__(self, process, stream):
        self.process, self.stream = process, stream
        self.events = queue.Queue()
        self.buffer = b""
        self.ended = False
        self.reader = threading.Thread(target=self._drain, name="socket-test-output")
        self.reader.start()

    def _drain(self):
        total = 0
        try:
            while True:
                chunk = os.read(self.stream.fileno(), 4096)
                if not chunk:
                    break
                total += len(chunk)
                if total > 1024 * 1024:
                    self.events.put(AssertionError("page output exceeded 1 MiB"))
                    self.process.kill()
                    break
                self.events.put(chunk)
        except Exception as error:
            self.events.put(error)
        finally:
            self.events.put(None)

    def _take(self, deadline):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise AssertionError("page output deadline expired")
        try:
            value = self.events.get(timeout=remaining)
        except queue.Empty as error:
            raise AssertionError("page output deadline expired") from error
        if isinstance(value, Exception):
            raise value
        if value is None:
            self.ended = True
        else:
            self.buffer += value

    def readline(self, timeout=20.0):
        deadline = time.monotonic() + timeout
        while b"\n" not in self.buffer and not self.ended:
            self._take(deadline)
        if b"\n" in self.buffer:
            line, self.buffer = self.buffer.split(b"\n", 1)
            return (line + b"\n").decode("utf-8", errors="replace")
        line, self.buffer = self.buffer, b""
        return line.decode("utf-8", errors="replace")

    def read(self, timeout=20.0):
        deadline = time.monotonic() + timeout
        while not self.ended:
            self._take(deadline)
        value, self.buffer = self.buffer, b""
        return value.decode("utf-8", errors="replace")

    def close(self):
        self.reader.join(timeout=5)
        if self.reader.is_alive():
            raise AssertionError("page output reader did not finish after owned process cleanup")
        self.stream.close()


def cleanup_page(page):
    if page.poll() is None:
        page.kill()
    page.wait(timeout=10)
    page.stdout.close()


def wait_for_line(process: subprocess.Popen, marker: str, timeout: float = 20.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = process.stdout.readline(timeout=max(0.001, deadline - time.monotonic()))
        if line == "":
            raise AssertionError(f"the page exited before printing {marker!r}")
        if line.strip() == marker:
            return
    raise AssertionError(f"the page never printed {marker!r}")


ECHO_PAGE = """
book net.contract.echo

read nil.std.core
read nil.std.text.string_utils
read nil.std.net.socket

main returns number begin paragraph
    value opened is nil.std.net::listen with host being "127.0.0.1", port being PORT, backlog being 8
    consider opened
        when Result.Error holding why then begin block
            say "listen failed: " plus why
            give 1
        end block
        when Result.Ok holding listener then begin block
            say "ready"
            value taken is nil.std.net::accept with handle being listener
            consider taken
                when Result.Ok holding client then begin block
                    value heard is nil.std.net::read with handle being client, maximum_bytes being 4096
                    consider heard
                        when Result.Ok holding incoming then begin block
                            evaluate nil.std.net::write with handle being client, data being incoming
                            say "echoed " plus ((nil.std.text::length with s being incoming) becomes text)
                        end block
                        when Result.Error holding why then say "read failed: " plus why
                        otherwise say "read gave neither"
                    end consider
                    evaluate nil.std.net::close with handle being client
                end block
                when Result.Error holding why then say "accept failed: " plus why
                otherwise say "accept gave neither"
            end consider
            evaluate nil.std.net::close with handle being listener
            say "done"
            give 0
        end block
        otherwise give 1
    end consider
end paragraph
"""


HANDLE_PAGE = """
book net.contract.handles

read nil.std.core
read nil.std.text.string_utils
read nil.std.net.socket

!!! Reports a Result as its error text, or "ok" when it succeeded, so the
!!! refusals can be asserted without depending on a system message. Two of
!!! them, because read gives back text and the rest give back a count, and
!!! Matrix has no overload sets.
reason-for receives outcome as Result of number, text returns text begin paragraph
    consider outcome
        when Result.Ok holding amount then give "ok"
        when Result.Error holding why then give why
        otherwise give "neither"
    end consider
end paragraph

reason-for-read receives outcome as Result of text, text returns text begin paragraph
    consider outcome
        when Result.Ok holding incoming then give "ok"
        when Result.Error holding why then give why
        otherwise give "neither"
    end consider
end paragraph

main returns number begin paragraph
    !!! A handle the program was never given. If these were descriptors, 1
    !!! would be this process's standard output.
    say reason-for-read with outcome being (nil.std.net::read with handle being 1, maximum_bytes being 10)
    say reason-for with outcome being (nil.std.net::write with handle being 1, data being "x")
    say (nil.std.net::is_open with handle being 1) becomes text

    !!! Closing something never opened is not a failure: it is what a cleanup
    !!! path does when an earlier one already ran.
    say reason-for with outcome being (nil.std.net::close with handle being 404)

    value opened is nil.std.net::listen with host being "127.0.0.1", port being PORT, backlog being 4
    consider opened
        when Result.Ok holding listener then begin block
            say (nil.std.net::is_open with handle being listener) becomes text
            !!! A listener is not a connection and will not pretend to be one.
            say reason-for-read with outcome being (nil.std.net::read with handle being listener, maximum_bytes being 4)
            say reason-for with outcome being (nil.std.net::write with handle being listener, data being "x")
            evaluate nil.std.net::close with handle being listener
            say (nil.std.net::is_open with handle being listener) becomes text
            !!! And it stays closed.
            say reason-for-read with outcome being (nil.std.net::read with handle being listener, maximum_bytes being 4)
        end block
        when Result.Error holding why then say "listen failed: " plus why
        otherwise say "listen gave neither"
    end consider

    !!! A bind address is numeric, empty or localhost; a name is refused
    !!! rather than resolved, because binding must not do DNS.
    say reason-for with outcome being
        (nil.std.net::listen with host being "example.com", port being PORT, backlog being 4)
    say reason-for with outcome being
        (nil.std.net::listen with host being "127.0.0.1", port being 0, backlog being 4)
    say reason-for with outcome being
        (nil.std.net::listen with host being "127.0.0.1", port being 70000, backlog being 4)
    give 0
end paragraph
"""


LEAK_PAGE = """
book net.contract.leak

read nil.std.core
read nil.std.net.socket

!!! Binds and then exits without closing anything. The runtime has to clean
!!! up after it, or the next run of this same page cannot bind the port.
main returns number begin paragraph
    value opened is nil.std.net::listen with host being "127.0.0.1", port being PORT, backlog being 4
    consider opened
        when Result.Ok holding listener then say "bound"
        when Result.Error holding why then say "listen failed: " plus why
        otherwise say "neither"
    end consider
    give 0
end paragraph
"""


TIMEOUT_PAGE = """
book net.contract.timeout

read nil.std.core
read nil.std.net.socket

!!! A connection that opens and says nothing must not hold the process. With
!!! no deadline this page would wait as long as the client cared to.
main returns number begin paragraph
    value opened is nil.std.net::listen with host being "127.0.0.1", port being PORT, backlog being 4
    consider opened
        when Result.Ok holding listener then begin block
            say "ready"
            value taken is nil.std.net::accept with handle being listener
            consider taken
                when Result.Ok holding client then begin block
                    evaluate nil.std.net::set_timeout with handle being client, milliseconds being 300
                    value heard is nil.std.net::read with handle being client, maximum_bytes being 16
                    consider heard
                        when Result.Ok holding incoming then say "read returned"
                        when Result.Error holding why then say "timed out"
                        otherwise say "neither"
                    end consider
                    evaluate nil.std.net::close with handle being client
                end block
                otherwise say "accept failed"
            end consider
            evaluate nil.std.net::close with handle being listener
            say "done"
            give 0
        end block
        otherwise give 1
    end consider
end paragraph
"""


HANGUP_PAGE = """
book net.contract.hangup

read nil.std.core
read nil.std.net.socket

!!! Writes a great deal to a peer that has already gone. Without the runtime
!!! suppressing SIGPIPE this page does not reach its last line: the default
!!! disposition ends the process, and a server built on it dies whenever a
!!! browser closes a tab.
main returns number begin paragraph
    value opened is nil.std.net::listen with host being "127.0.0.1", port being PORT, backlog being 4
    consider opened
        when Result.Ok holding listener then begin block
            say "ready"
            value taken is nil.std.net::accept with handle being listener
            consider taken
                when Result.Ok holding client then begin block
                    variable payload as text is "0123456789"
                    variable doubling as number is 0
                    repeat while doubling is less than 16
                        set payload to payload plus payload
                        set doubling to doubling plus 1
                    end repeat
                    variable attempt-count as number is 0
                    variable reported as boolean is false
                    repeat while attempt-count is less than 20 and not reported
                        value sent is nil.std.net::write with handle being client, data being payload
                        consider sent
                            when Result.Error holding why then set reported to true
                            otherwise set attempt-count to attempt-count plus 1
                        end consider
                    end repeat
                    say "write reported: " plus (reported becomes text)
                    evaluate nil.std.net::close with handle being client
                end block
                otherwise say "accept failed"
            end consider
            evaluate nil.std.net::close with handle being listener
            say "survived"
            give 0
        end block
        otherwise give 1
    end consider
end paragraph
"""


CLIENT_PAGE = """
book net.contract.client

read nil.std.core
read nil.std.text.string_utils
read nil.std.net.socket

!!! The other direction: connecting out, and resolving a name while doing it.
main returns number begin paragraph
    value opened is nil.std.net::connect with host being "localhost", port being PORT
    consider opened
        when Result.Error holding why then begin block
            say "connect failed: " plus why
            give 1
        end block
        when Result.Ok holding server then begin block
            evaluate nil.std.net::set_no_delay with handle being server, enabled being true
            value peer is nil.std.net::peer_address with handle being server
            consider peer
                when Result.Ok holding address then say "peer seen: "
                    plus ((nil.std.text::contains with s being address, needle being ":") becomes text)
                otherwise say "peer unavailable"
            end consider
            evaluate nil.std.net::write with handle being server, data being "ping"
            value heard is nil.std.net::read with handle being server, maximum_bytes being 64
            consider heard
                when Result.Ok holding incoming then say "heard " plus incoming
                when Result.Error holding why then say "read failed: " plus why
                otherwise say "neither"
            end consider
            !!! The orderly close of a peer is empty text, not an error, which
            !!! is how a read loop knows the request is finished.
            value after-close is nil.std.net::read with handle being server, maximum_bytes being 64
            consider after-close
                when Result.Ok holding incoming then say "closed reads empty: "
                    plus ((incoming equals "") becomes text)
                when Result.Error holding why then say "read after close failed: " plus why
                otherwise say "neither"
            end consider
            evaluate nil.std.net::close with handle being server
            say "done"
            give 0
        end block
        otherwise give 1
    end consider
end paragraph
"""



POLL_PAGE = """
book net.contract.poll

read nil.std.core
read nil.std.net.socket

reason-for receives outcome as Result of number, text returns text begin paragraph
    consider outcome
        when Result.Ok holding amount then give "ok"
        when Result.Error holding why then give why
        otherwise give "neither"
    end consider
end paragraph

amount-of receives outcome as Result of number, text returns number begin paragraph
    consider outcome
        when Result.Ok holding amount then give amount
        otherwise give 0 minus 1
    end consider
end paragraph

main returns number begin paragraph
    value opened is nil.std.net::listen with host being "127.0.0.1", port being PORT, backlog being 8
    consider opened
        when Result.Ok holding listener then begin block
            !!! An empty watch set does not wait: a loop that watched nothing
            !!! by mistake would otherwise be an invisible stall.
            say amount-of with outcome being (nil.std.net::wait with timeout_milliseconds being 10000)
            say nil.std.net::watch_count with nothing

            say reason-for with outcome being (nil.std.net::set_blocking with handle being listener, blocking being false)
            evaluate nil.std.net::watch with handle being listener, flags being nil.std.net::ready_readable
            say nil.std.net::watch_count with nothing
            !!! Watching again replaces the flags rather than adding a second entry.
            evaluate nil.std.net::watch with handle being listener, flags being nil.std.net::ready_readable
            say nil.std.net::watch_count with nothing

            !!! Nothing is connecting, so nothing is ready, and the drained
            !!! ready set reads as zero.
            say amount-of with outcome being (nil.std.net::wait with timeout_milliseconds being 50)
            say amount-of with outcome being (nil.std.net::next_ready with nothing)

            !!! A non-blocking accept with nothing pending is zero, not an error.
            say amount-of with outcome being (nil.std.net::accept with handle being listener)

            !!! Unwatching something not watched succeeds.
            say reason-for with outcome being (nil.std.net::unwatch with handle being 991)
            say reason-for with outcome being (nil.std.net::unwatch with handle being listener)
            say nil.std.net::watch_count with nothing

            !!! A closed socket is dropped from the watch set by the next wait
            !!! rather than polled on a descriptor that may have been reissued.
            evaluate nil.std.net::watch with handle being listener, flags being nil.std.net::ready_readable
            evaluate nil.std.net::close with handle being listener
            say amount-of with outcome being (nil.std.net::wait with timeout_milliseconds being 10)
            say nil.std.net::watch_count with nothing

            !!! Packing: handle and flags travel as one number.
            say nil.std.net::ready_handle with packed being 41
            say nil.std.net::ready_flags with packed being 41
            say (nil.std.net::is_readable with packed being 41) becomes text
            say (nil.std.net::is_finished with packed being 41) becomes text
            say (nil.std.net::is_writable with packed being 41) becomes text
            give 0
        end block
        when Result.Error holding why then begin block
            say "listen failed: " plus why
            give 1
        end block
        otherwise give 1
    end consider
end paragraph
"""


EVENT_LOOP_PAGE = """
book net.contract.event-loop

read nil.std.core
read nil.std.text.string_utils
read nil.std.net.socket

pending begin record
    field handle as number
    field gathered as text
    field live as boolean
end record

find-slot receives slots as array of pending, handle as number returns number begin paragraph
    variable index as number is 0
    repeat while index is less than the length of slots
        value one is slots at index
        when one.live and one.handle equals handle then
            give index
        end when
        set index to index plus 1
    end repeat
    give 0 minus 1
end paragraph

main returns number begin paragraph
    value opened is nil.std.net::listen with host being "127.0.0.1", port being PORT, backlog being 128
    consider opened
        when Result.Error holding why then begin block
            say "listen failed: " plus why
            give 1
        end block
        when Result.Ok holding listener then begin block
            evaluate nil.std.net::set_blocking with handle being listener, blocking being false
            evaluate nil.std.net::watch with handle being listener, flags being nil.std.net::ready_readable
            say "ready"

            variable slots as array of pending is []
            variable answered as number is 0
            variable live-now as number is 0
            variable most-at-once as number is 0
            variable partial-count as number is 0

            repeat while answered is less than EXPECTED
                evaluate nil.std.net::wait with timeout_milliseconds being 10000
                variable draining as boolean is true
                repeat while draining
                    value packed is nil.std.net::next_ready with nothing
                    consider packed
                        when Result.Ok holding value-held then begin block
                            when value-held equals 0 then
                                set draining to false
                            end when
                            value handle is nil.std.net::ready_handle with packed being value-held
                            when value-held is greater than 0 and handle equals listener then
                                variable accepting as boolean is true
                                repeat while accepting
                                    value taken is nil.std.net::accept with handle being listener
                                    consider taken
                                        when Result.Ok holding client then begin block
                                            when client equals 0 then
                                                set accepting to false
                                            end when
                                            when client is greater than 0 then
                                                evaluate nil.std.net::set_timeout with handle being client, milliseconds being 10000
                                                evaluate nil.std.net::watch with handle being client, flags being nil.std.net::ready_readable
                                                append build pending with handle being client, gathered being "", live being true to slots
                                                set live-now to live-now plus 1
                                                when live-now is greater than most-at-once then
                                                    set most-at-once to live-now
                                                end when
                                            end when
                                        end block
                                        otherwise set accepting to false
                                    end consider
                                end repeat
                            end when
                            when value-held is greater than 0 and not (handle equals listener) then
                                value slot is find-slot with slots being slots, handle being handle
                                when slot is at least 0 then
                                    value heard is nil.std.net::read with handle being handle, maximum_bytes being 4096
                                    consider heard
                                        when Result.Ok holding piece then begin block
                                            value held is slots at slot
                                            value whole is held.gathered plus piece
                                            value done is nil.std.text::contains with s being whole, needle being "END"
                                            value gone is piece equals ""
                                            when not done and not gone then
                                                when held.gathered equals "" then
                                                    set partial-count to partial-count plus 1
                                                    when partial-count equals EXPECTED then
                                                        say "all-partial"
                                                    end when
                                                end when
                                                set slots at slot to build pending with handle being handle,
                                                    gathered being whole, live being true
                                            end when
                                            when done then
                                                evaluate nil.std.net::write with handle being handle,
                                                    data being "ok:" plus ((nil.std.text::length with s being whole) becomes text)
                                            end when
                                            when done or gone then
                                                evaluate nil.std.net::unwatch with handle being handle
                                                evaluate nil.std.net::close with handle being handle
                                                set slots at slot to build pending with handle being 0, gathered being "", live being false
                                                set live-now to live-now minus 1
                                                set answered to answered plus 1
                                            end when
                                        end block
                                        otherwise begin block
                                            evaluate nil.std.net::unwatch with handle being handle
                                            evaluate nil.std.net::close with handle being handle
                                            set slots at slot to build pending with handle being 0, gathered being "", live being false
                                            set live-now to live-now minus 1
                                            set answered to answered plus 1
                                        end block
                                    end consider
                                end when
                            end when
                        end block
                        otherwise set draining to false
                    end consider
                end repeat
            end repeat

            evaluate nil.std.net::close with handle being listener
            say "answered " plus (answered becomes text)
            say "most-at-once " plus (most-at-once becomes text)
            give 0
        end block
        otherwise give 1
    end consider
end paragraph
"""

NONBLOCK_PAGE = """
book net.contract.nonblocking
read nil.std.core
read nil.std.text.string_utils
read nil.std.net.socket

amount receives result as Result of number, text returns number begin paragraph
    consider result
        when Result.Ok holding amount-held then give amount-held
        when Result.Error holding why then begin block
            say "error " plus why
            give 0 minus 2
        end block
        otherwise give 0 minus 3
    end consider
end paragraph

main returns number begin paragraph
    value listener is amount with result being (nil.std.net::listen with host being "127.0.0.1", port being PORT, backlog being 8)
    say "ready"
    value client is amount with result being (nil.std.net::accept with handle being listener)
    evaluate nil.std.net::set_blocking with handle being client, blocking being false
    evaluate nil.std.net::watch with handle being client, flags being nil.std.net::ready_readable
    value idle is amount with result being (nil.std.net::receive with handle being client, maximum_bytes being 64)
    say "idle " plus (idle becomes text)
    evaluate nil.std.net::wait with timeout_milliseconds being 10000
    value taken is amount with result being (nil.std.net::receive with handle being client, maximum_bytes being 64)
    say "received " plus (taken becomes text)
    consider nil.std.net::received with nothing
        when Result.Ok holding incoming then begin block
            value sent is amount with result being (nil.std.net::send_some with handle being client,
                data being "xx" plus incoming, offset being 2)
            say "sent " plus (sent becomes text)
        end block
        otherwise give 1
    end consider
    evaluate nil.std.net::wait with timeout_milliseconds being 10000
    value closed is amount with result being (nil.std.net::receive with handle being client, maximum_bytes being 64)
    say "closed " plus (closed becomes text)
    consider nil.std.net::received with nothing
        when Result.Ok holding incoming then say "cleared " plus ((nil.std.text::length with s being incoming) becomes text)
        otherwise give 1
    end consider
    evaluate nil.std.net::close with handle being client
    value stale is amount with result being (nil.std.net::next_ready with nothing)
    say "stale-ready " plus (stale becomes text)
    evaluate nil.std.net::close with handle being listener
    say "done"
    give 0
end paragraph
"""

SHARED_PAGE = """
book net.contract.shared
read nil.std.core
read nil.std.net.socket

main returns number begin paragraph
    consider nil.std.net::listen_reuse_port with host being "127.0.0.1", port being PORT, backlog being 8
        when Result.Error holding why then say "unsupported " plus why
        when Result.Ok holding first then begin block
            consider nil.std.net::listen_reuse_port with host being "127.0.0.1", port being PORT, backlog being 8
                when Result.Ok holding second then begin block
                    say "both shared"
                    evaluate nil.std.net::close with handle being second
                end block
                when Result.Error holding why then say "second failed " plus why
                otherwise give 1
            end consider
            consider nil.std.net::listen with host being "127.0.0.1", port being PORT, backlog being 8
                when Result.Error holding why then say "ordinary refused"
                when Result.Ok holding accidental then begin block
                    say "ordinary admitted"
                    evaluate nil.std.net::close with handle being accidental
                end block
                otherwise give 1
            end consider
            evaluate nil.std.net::close with handle being first
        end block
        otherwise give 1
    end consider
    give 0
end paragraph
"""


def run_page(interpreter: Path, artifact: Path) -> subprocess.Popen:
    process = subprocess.Popen(
        [str(interpreter), str(artifact)],
        stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        bufsize=0)
    process.stdout = PageOutput(process, process.stdout)
    return process


def case_echo(compiler, interpreter, stdlib, workdir) -> list[str]:
    """What the page writes is what the wire carries, bytes included."""
    problems: list[str] = []
    port = free_port()
    artifact = build(compiler, stdlib,
                     ECHO_PAGE.replace("PORT", str(port)), workdir, "echo")
    page = run_page(interpreter, artifact)
    try:
        wait_for_line(page, "ready")
        # Deliberately not valid UTF-8 and containing a zero byte: a socket
        # carries bytes, and text that has been through an encoder somewhere
        # would come back changed.
        payload = b"ping\x00\xc3\xa9\xff\xfe end"
        with socket.create_connection(("127.0.0.1", port), timeout=10) as client:
            client.sendall(payload)
            client.shutdown(socket.SHUT_WR)
            received = b""
            while len(received) < len(payload):
                chunk = client.recv(4096)
                if not chunk:
                    break
                received += chunk
        if received != payload:
            problems.append(
                f"echo: the wire carried {received!r}, not {payload!r}")
        output = page.stdout.read()
        if f"echoed {len(payload)}" not in output:
            problems.append(
                f"echo: the page did not report {len(payload)} bytes read; it said {output!r}")
        if "done" not in output:
            problems.append(f"echo: the page did not finish; it said {output!r}")
    finally:
        cleanup_page(page)
    return problems


def case_handles(compiler, interpreter, stdlib, workdir) -> list[str]:
    """A handle is a token into the runtime, not a descriptor."""
    port = free_port()
    artifact = build(compiler, stdlib,
                     HANDLE_PAGE.replace("PORT", str(port)), workdir, "handles")
    finished = subprocess.run([str(interpreter), str(artifact)],
                              capture_output=True, text=True, timeout=60)
    lines = finished.stdout.splitlines()
    expected = [
        ("read on an unknown handle", lambda v: "not open" in v),
        ("write on an unknown handle", lambda v: "not open" in v),
        ("is_open on an unknown handle", lambda v: v == "0"),
        ("close on an unknown handle", lambda v: v == "ok"),
        ("is_open on a listener", lambda v: v == "1"),
        ("read on a listener", lambda v: "listener" in v),
        ("write on a listener", lambda v: "listener" in v),
        ("is_open after close", lambda v: v == "0"),
        ("read after close", lambda v: "not open" in v),
        ("listen on a name", lambda v: "numeric" in v),
        ("listen on port zero", lambda v: "between 1 and 65535" in v),
        ("listen above the port range", lambda v: "between 1 and 65535" in v),
    ]
    problems: list[str] = []
    if len(lines) != len(expected):
        return [f"handles: expected {len(expected)} lines, got {len(lines)}: "
                f"{finished.stdout!r}{finished.stderr!r}"]
    for (label, holds), value in zip(expected, lines):
        if not holds(value):
            problems.append(f"handles: {label} gave {value!r}")
    return problems


def case_port_released(compiler, interpreter, stdlib, workdir) -> list[str]:
    """A page that leaves a listener open must not keep the port."""
    port = free_port()
    artifact = build(compiler, stdlib,
                     LEAK_PAGE.replace("PORT", str(port)), workdir, "leak")
    problems: list[str] = []
    for attempt in (1, 2, 3):
        finished = subprocess.run([str(interpreter), str(artifact)],
                                  capture_output=True, text=True, timeout=60)
        if "bound" not in finished.stdout:
            problems.append(
                f"port-released: run {attempt} could not bind; it said "
                f"{finished.stdout!r}{finished.stderr!r}")
            break
    return problems


def case_timeout(compiler, interpreter, stdlib, workdir) -> list[str]:
    """A read deadline expires rather than holding the process."""
    problems: list[str] = []
    port = free_port()
    artifact = build(compiler, stdlib,
                     TIMEOUT_PAGE.replace("PORT", str(port)), workdir, "timeout")
    page = run_page(interpreter, artifact)
    try:
        wait_for_line(page, "ready")
        with socket.create_connection(("127.0.0.1", port), timeout=10):
            # Connect and say nothing at all. The page's own deadline is what
            # has to end this, so hold the connection open across it.
            time.sleep(1.2)
        page.wait(timeout=20)
        output = page.stdout.read()
        if "timed out" not in output:
            problems.append(
                f"timeout: the read did not expire; the page said {output!r}")
    except subprocess.TimeoutExpired:
        problems.append("timeout: the page never finished, so the deadline did nothing")
    finally:
        cleanup_page(page)
    return problems


def case_peer_hangup(compiler, interpreter, stdlib, workdir) -> list[str]:
    """A peer that vanishes mid-write is reported, not fatal."""
    problems: list[str] = []
    port = free_port()
    artifact = build(compiler, stdlib,
                     HANGUP_PAGE.replace("PORT", str(port)), workdir, "hangup")
    page = run_page(interpreter, artifact)
    try:
        wait_for_line(page, "ready")
        client = socket.create_connection(("127.0.0.1", port), timeout=10)
        # Reset rather than a graceful close, so the next write fails outright
        # instead of being accepted into a buffer nobody will drain.
        client.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                          struct.pack("HH" if os.name == "nt" else "ii", 1, 0))
        client.close()
        page.wait(timeout=30)
        output = page.stdout.read()
        if "survived" not in output:
            problems.append(
                "peer-hangup: the page did not reach its last line, so a lost "
                f"peer ended the process; it said {output!r}")
        if page.returncode != 0:
            problems.append(
                f"peer-hangup: the page exited {page.returncode}, which is how "
                "an unsuppressed SIGPIPE looks")
    except subprocess.TimeoutExpired:
        problems.append("peer-hangup: the page never finished")
    finally:
        cleanup_page(page)
    return problems


def case_client(compiler, interpreter, stdlib, workdir) -> list[str]:
    """Connecting out, resolving a name, and reading a closed peer."""
    problems: list[str] = []
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", 0))
    listener.listen(4)
    port = int(listener.getsockname()[1])
    artifact = build(compiler, stdlib,
                     CLIENT_PAGE.replace("PORT", str(port)), workdir, "client")
    page = run_page(interpreter, artifact)
    try:
        listener.settimeout(20)
        connection, _ = listener.accept()
        with connection:
            greeting = connection.recv(64)
            if greeting != b"ping":
                problems.append(f"client: the page sent {greeting!r}, not b'ping'")
            connection.sendall(b"pong")
        page.wait(timeout=30)
        output = page.stdout.read()
        for marker in ("peer seen: 1", "heard pong", "closed reads empty: 1", "done"):
            if marker not in output:
                problems.append(f"client: expected {marker!r} in {output!r}")
    except (socket.timeout, subprocess.TimeoutExpired):
        problems.append("client: the page did not complete its exchange")
    finally:
        listener.close()
        cleanup_page(page)
    return problems



def case_poll(compiler, interpreter, stdlib, workdir) -> list[str]:
    """The watch set, the ready set, and the packing between them."""
    port = free_port()
    artifact = build(compiler, stdlib,
                     POLL_PAGE.replace("PORT", str(port)), workdir, "poll")
    finished = subprocess.run([str(interpreter), str(artifact)],
                              capture_output=True, text=True, timeout=120)
    lines = finished.stdout.splitlines()
    expected = [
        ("an empty watch set does not wait", lambda v: v == "0"),
        ("nothing is watched yet", lambda v: v == "0"),
        ("a listener can be made non-blocking", lambda v: v == "ok"),
        ("watching counts one", lambda v: v == "1"),
        ("watching again replaces rather than adds", lambda v: v == "1"),
        ("nothing connecting means nothing ready", lambda v: v == "0"),
        ("a drained ready set reads zero", lambda v: v == "0"),
        ("a non-blocking accept with nothing pending is zero", lambda v: v == "0"),
        ("unwatching something unwatched succeeds", lambda v: v == "ok"),
        ("unwatching a watched socket succeeds", lambda v: v == "ok"),
        ("the watch set is empty again", lambda v: v == "0"),
        ("a closed socket is not polled", lambda v: v == "0"),
        ("a closed socket leaves the watch set", lambda v: v == "0"),
        ("the packed handle is recovered", lambda v: v == "5"),
        ("the packed flags are recovered", lambda v: v == "1"),
        ("packed readability is read", lambda v: v == "1"),
        ("packed finishedness is read", lambda v: v == "0"),
        ("packed writability is read", lambda v: v == "0"),
    ]
    if len(lines) != len(expected):
        return [f"poll: expected {len(expected)} lines, got {len(lines)}: "
                f"{finished.stdout!r}{finished.stderr!r}"]
    problems: list[str] = []
    for (label, holds), value in zip(expected, lines):
        if not holds(value):
            problems.append(f"poll: {label} gave {value!r}")
    return problems


def case_event_loop(compiler, interpreter, stdlib, workdir) -> list[str]:
    """Every first fragment must be processed before any client sends its tail.

    A serial read-until-complete server cannot reach this barrier. This proves
    overlapping request processing without deriving it from a throughput number.
    """
    clients = 64
    release_tails = threading.Event()
    problems: list[str] = []
    port = free_port()
    artifact = build(compiler, stdlib,
                     EVENT_LOOP_PAGE.replace("PORT", str(port))
                                    .replace("EXPECTED", str(clients)),
                     workdir, "event_loop")
    page = run_page(interpreter, artifact)
    answered: list[bytes] = []
    failures: list[str] = []
    guard = threading.Lock()

    def one_client() -> None:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=30) as client:
                client.sendall(b"part-one ")
                if not release_tails.wait(timeout=25):
                    raise AssertionError("server did not process every partial request")
                client.sendall(b"END")
                reply = client.recv(64)
            with guard:
                (answered if reply.startswith(b"ok:") else failures).append(reply)
        except Exception as failure:  # noqa: BLE001
            with guard:
                failures.append(repr(failure))

    workers = []
    try:
        wait_for_line(page, "ready")
        workers = [threading.Thread(target=one_client) for _ in range(clients)]
        for worker in workers:
            worker.start()
        wait_for_line(page, "all-partial")
        release_tails.set()
        deadline = time.monotonic() + 35
        for worker in workers:
            worker.join(timeout=max(0, deadline - time.monotonic()))
        if any(worker.is_alive() for worker in workers):
            raise AssertionError("client threads exceeded their shared deadline")

        if len(answered) != clients:
            problems.append(
                f"event-loop: {len(answered)} of {clients} clients were answered; "
                f"first failures {failures[:2]}")
        page.wait(timeout=30)
        output = page.stdout.read()
        if f"answered {clients}" not in output:
            problems.append(f"event-loop: the page said {output!r}")
        # The point of the whole exercise: they were open simultaneously.
        if f"most-at-once {clients}" not in output:
            problems.append(
                "event-loop: the server never held all the connections at once, "
                f"so it was draining them in batches; it said {output!r}")
    except (AssertionError, subprocess.TimeoutExpired) as failure:
        problems.append(f"event-loop: {failure!r}")
    finally:
        release_tails.set()
        cleanup_page(page)
        deadline = time.monotonic() + 35
        for worker in workers:
            worker.join(timeout=max(0, deadline - time.monotonic()))
        if any(worker.is_alive() for worker in workers):
            raise AssertionError("client thread cleanup exceeded its shared deadline")
    return problems

def case_nonblocking(compiler, interpreter, stdlib, workdir) -> list[str]:
    port = free_port()
    artifact = build(compiler, stdlib, NONBLOCK_PAGE.replace("PORT", str(port)), workdir, "nonblocking")
    page = run_page(interpreter, artifact)
    try:
        wait_for_line(page, "ready")
        with socket.create_connection(("127.0.0.1", port), timeout=10) as client:
            wait_for_line(page, "idle -1")
            payload = b"abc\0\xff"
            client.sendall(payload)
            received = b""
            while len(received) < len(payload):
                piece = client.recv(64)
                if not piece: break
                received += piece
            if received != payload:
                return [f"nonblocking: actual bytes {received!r} differ from {payload!r}"]
            client.shutdown(socket.SHUT_WR)
            page.wait(timeout=20)
            output = page.stdout.read()
        expected = ["received 5", "sent 5", "closed 0", "cleared 0", "stale-ready 0", "done"]
        if page.returncode != 0 or output.splitlines() != expected:
            return [f"nonblocking: exit {page.returncode}, output {output!r}"]
        return []
    finally:
        cleanup_page(page)


def case_shared_listener(compiler, interpreter, stdlib, workdir) -> list[str]:
    artifact = build(compiler, stdlib, SHARED_PAGE.replace("PORT", str(free_port())), workdir, "shared_listener")
    result = subprocess.run([str(interpreter), str(artifact)], stdin=subprocess.DEVNULL,
                            capture_output=True, text=True, timeout=20)
    expected = (["unsupported net: this platform has no shared-listener SO_REUSEPORT support"]
                if os.name == "nt" else ["both shared", "ordinary refused"])
    if result.returncode != 0 or result.stderr or result.stdout.splitlines() != expected:
        return [f"shared listener: exit {result.returncode}, output {result.stdout!r}, error {result.stderr!r}"]
    return []


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--interpreter", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()

    compiler = args.compiler.resolve(strict=True)
    interpreter = args.interpreter.resolve(strict=True)
    stdlib = (args.root / "nilstd" / "src").resolve(strict=True)

    cases = (
        ("echo", case_echo),
        ("handles", case_handles),
        ("port-released", case_port_released),
        ("timeout", case_timeout),
        ("peer-hangup", case_peer_hangup),
        ("client", case_client),
        ("poll", case_poll),
        ("event-loop", case_event_loop),
        ("nonblocking", case_nonblocking),
        ("shared-listener", case_shared_listener),
    )

    problems: list[str] = []
    with tempfile.TemporaryDirectory() as raw:
        workdir = Path(raw)
        for name, run in cases:
            try:
                problems.extend(run(compiler, interpreter, stdlib, workdir))
            except Exception as failure:  # noqa: BLE001 - report, never abort the run
                problems.append(f"{name}: raised {failure!r}")

    if problems:
        print("Network intrinsic contract failures:")
        for problem in problems:
            print(f"  - {problem}")
        return 1

    print(f"Network intrinsics: {len(cases)} contract case(s) held — "
          "listen/accept/read/write carry bytes unchanged, handles are opaque, "
          "a listener is not a connection, the runtime releases a leaked port, "
          "a read deadline expires, a lost peer is reported rather than fatal, "
          "connecting out resolves a name, the watch set behaves, and an event "
          "loop holds many connections at once instead of draining them in "
          "turn.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
