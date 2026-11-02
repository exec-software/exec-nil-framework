#!/usr/bin/env python3
"""Run a tool with a byte-exact stdin payload."""

from __future__ import annotations

import subprocess
from typing import Optional, Sequence


def run_with_exact_stdin(command: Sequence[object], payload: Optional[str],
                         **keywords) -> subprocess.CompletedProcess:
    """Pipe `payload` to the child byte for byte, and decode its replies.

    subprocess's text mode translates '\\n' to the host line ending on the way
    *in* as well as out, so on Windows a piped source arrives as CRLF. The
    tools here report byte offsets into the source they were given, and the
    callers compute the offsets they expect from LF-terminated literals, so
    that translation silently moves every diagnostic, completion and span past
    the first line. Encode the payload here and decode stdout/stderr instead.

    Reading is deliberately left alone: a tool's own output is already
    untranslated, and a source that genuinely contains CRLF must reach the
    tool intact for the CRLF cases to mean anything.
    """
    result = subprocess.run(
        [str(item) for item in command],
        input=None if payload is None else payload.encode("utf-8"),
        capture_output=True,
        **keywords,
    )
    result.stdout = result.stdout.decode("utf-8")
    result.stderr = result.stderr.decode("utf-8")
    return result
