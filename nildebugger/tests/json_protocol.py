#!/usr/bin/env python3
"""End-to-end JSONL and CLI regression checks for nildebugger."""

from __future__ import annotations

import json
import os
import pathlib
import subprocess
import sys
import tempfile


PROGRAM = """\
module debugger_protocol
const S1 = "hello"

function main:
    PUSH_CONST_STR S1 ; line 10
    CALL println 1 ; line 10
    INTEROP_CALL std_sys arg_count 0 i64 ; line 11
    CALL println 1 ; line 11
    PUSH_CONST 7 ; line 12
    STORE_GLOBAL G ; line 12
    PUSH_CONST 11 ; line 13
    RET ; line 13
end function
"""

EXIT_PROGRAM = """\
module debugger_exit

function main:
    PUSH_CONST 23 ; line 20
    INTEROP_CALL std_sys exit 1 void ; line 20
    RET ; line 21
end function
"""

INPUT_PROGRAM = """\
module debugger_input

function main:
    INTEROP_CALL intrinsic console.read_line 0 text ; line 30
    STORE_GLOBAL INPUT ; line 30
    RET ; line 31
end function
"""

MAXIMUM_PROGRAM_BYTES = 256 * 1024 * 1024


def fail(message: str, *, stdout: str = "", stderr: str = "") -> None:
    details = [message]
    if stdout:
        details.append(f"stdout:\n{stdout}")
    if stderr:
        details.append(f"stderr:\n{stderr}")
    raise AssertionError("\n".join(details))


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def run_line_endings(command: list[str], lines: list[str], context: str
                     ) -> subprocess.CompletedProcess[str]:
    baseline: bytes | None = None
    for terminator in (b"\n", b"\r\n"):
        completed = subprocess.run(
            command,
            input=terminator.join(line.encode("utf-8") for line in lines) +
                  terminator,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=10,
            check=False,
        )
        if completed.returncode != 0 or completed.stderr:
            fail(
                f"{context} with {terminator!r} failed: {completed.returncode}",
                stdout=repr(completed.stdout),
                stderr=repr(completed.stderr),
            )
        require(b"\r\n" not in completed.stdout,
                f"{context} translated output LF bytes into CRLF")
        if baseline is None:
            baseline = completed.stdout
        else:
            require(completed.stdout == baseline,
                    f"{context} produced different bytes for LF and CRLF input")
    # Decode only after raw-byte checks; do not normalize newlines on either
    # side of the subprocess boundary (including on Windows).
    return subprocess.CompletedProcess(
        completed.args, completed.returncode,
        completed.stdout.decode("utf-8"), completed.stderr.decode("utf-8")
    )


def run_protocol(debugger: pathlib.Path, fixture: pathlib.Path) -> None:
    requests = [
        "",
        " \t",
        '{"cmd":"regs"} trailing',
        json.dumps({"cmd": "regs"}),
        json.dumps({"cmd": "locals"}),
        json.dumps(
            {"cmd": "break", "file": str(fixture) + ".wrong", "line": 10}
        ),
        json.dumps(
            {
                "cmd": "break",
                "file": str(fixture),
                "line": 12,
                "condition": "G == 7junk",
            }
        ),
        json.dumps(
            {
                "cmd": "break",
                "file": str(fixture),
                "line": 12,
                "condition": 7,
            }
        ),
        json.dumps(
            {
                "cmd": "break",
                "file": str(fixture),
                "line": 12,
                "condition": "G == 999",
            }
        ),
        json.dumps(
            {"cmd": "cond", "id": 1, "condition": "G == 7 == 7"}
        ),
        json.dumps({"cmd": "list_bps"}),
        json.dumps({"cmd": "run"}),
        json.dumps({"cmd": "regs"}),
        json.dumps({"cmd": "eval", "expr": "G"}),
        json.dumps({"cmd": "run"}),
        json.dumps({"cmd": "regs"}),
        json.dumps({"cmd": "quit"}),
    ]
    command = [
        str(debugger),
        "--json",
        "--memory-mode=safe",
        "run",
        str(fixture),
        "--",
        "alpha",
        "two words",
    ]
    completed = run_line_endings(command, requests, "JSON debugger session")
    if completed.returncode != 0:
        fail(
            f"JSON debugger exited with {completed.returncode}",
            stdout=completed.stdout,
            stderr=completed.stderr,
        )
    if completed.stderr:
        fail(
            "valid JSON debugger session wrote diagnostics to stderr",
            stdout=completed.stdout,
            stderr=completed.stderr,
        )

    events: list[dict[str, object]] = []
    for line_number, line in enumerate(completed.stdout.splitlines(), start=1):
        try:
            event = json.loads(line)
        except json.JSONDecodeError as error:
            fail(
                f"stdout line {line_number} corrupted JSONL: {error}",
                stdout=completed.stdout,
            )
        if not isinstance(event, dict):
            fail(f"stdout line {line_number} is not a JSON object")
        events.append(event)

    kinds = [event.get("event") for event in events]
    require(kinds[0] == "started", "protocol did not begin with started")
    require(kinds[-2:] == ["quit", "detached"],
            "protocol did not end with quit then detached")

    errors = [event for event in events if event.get("event") == "error"]
    require(len(errors) == 5,
            "expected malformed-JSON, breakpoint, and condition errors")
    require("Invalid JSON request" in str(errors[0].get("message")),
            "trailing JSON input was not rejected by the parser")
    require("breakpoint file must name" in str(errors[1].get("message")),
            "mismatched breakpoint filename was not rejected honestly")
    require("invalid breakpoint condition" in str(errors[2].get("message")) and
            "complete finite number" in str(errors[2].get("message")),
            "partial numeric RHS was not rejected explicitly")
    require(errors[3].get("message") == "break condition must be a string",
            "non-string breakpoint condition was not rejected")
    require("invalid breakpoint condition" in str(errors[4].get("message")) and
            "exactly one comparator" in str(errors[4].get("message")),
            "multi-comparator update was not rejected explicitly")

    bp_sets = [event for event in events if event.get("event") == "bp_set"]
    require(len(bp_sets) == 1 and bp_sets[0].get("id") == 1,
            "valid breakpoint condition was not accepted")
    bp_lists = [event for event in events if event.get("event") == "bp_list"]
    require(len(bp_lists) == 1,
            "missing breakpoint-list response after failed condition update")
    listed_breakpoints = bp_lists[0].get("breakpoints")
    require(isinstance(listed_breakpoints, list) and
            len(listed_breakpoints) == 1 and
            listed_breakpoints[0].get("id") == 1 and
            listed_breakpoints[0].get("file") == str(fixture) and
            listed_breakpoints[0].get("line") == 12 and
            listed_breakpoints[0].get("enabled") is True and
            listed_breakpoints[0].get("condition") == "G == 999",
            "failed condition update did not preserve the prior expression")

    locals_events = [event for event in events if event.get("event") == "locals"]
    require(len(locals_events) == 1, "missing initial locals response")
    require(locals_events[0].get("frame") == 0 and
            locals_events[0].get("fn") == "main" and
            locals_events[0].get("vars") == {},
            "initial locals did not describe the real main frame")

    outputs = [event for event in events if event.get("event") == "output"]
    require(len(outputs) == 2, "each run must emit one framed stdout event")
    require(all(event.get("category") == "stdout" for event in outputs),
            "program output used the wrong protocol category")
    require([event.get("output") for event in outputs] ==
            ["hello\n2\n", "hello\n2\n"],
            "program stdout or delivered program arguments were incorrect")

    eval_events = [event for event in events if event.get("event") == "eval_result"]
    require(len(eval_events) == 1 and eval_events[0].get("value") == "7",
            "global evaluation did not return the stored value")

    regs = [event for event in events if event.get("event") == "regs"]
    require(len(regs) == 3, "expected initial and post-run register responses")
    require(regs[0].get("call_depth") == 1 and
            regs[0].get("stack_depth") == 0,
            "initial register state was not a clean main invocation")
    require(all(event.get("call_depth") == 1 and
                event.get("stack_depth") == 1 for event in regs[1:]),
            "second run retained stack or call-frame state")
    require(kinds.count("running") == 2 and kinds.count("terminated") == 2,
            "both run commands did not complete cleanly")
    require("stopped" not in kinds,
            "false validated condition unexpectedly stopped execution")


def run_cli_failures(debugger: pathlib.Path, fixture: pathlib.Path) -> None:
    cases = [
        (
            [str(debugger), "run", str(fixture), "stray"],
            "run requires exactly one program path",
        ),
        (
            [str(debugger), "--not-a-real-option", "run", str(fixture)],
            "Unknown nildebugger option",
        ),
        (
            [str(debugger), "help", "extra"],
            "help takes no arguments",
        ),
        (
            [str(debugger), "--help", "run"],
            "--help and --version must be used alone",
        ),
        (
            [str(debugger), "--version", "run", str(fixture)],
            "--help and --version must be used alone",
        ),
        (
            [str(debugger), "--version", "--version"],
            "--version was provided more than once",
        ),
    ]
    for command, expected in cases:
        completed = subprocess.run(
            command,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=5,
            check=False,
        )
        if completed.returncode == 0 or expected not in completed.stderr:
            fail(
                f"strict CLI case did not fail with {expected!r}",
                stdout=completed.stdout,
                stderr=completed.stderr,
            )


def run_cli_metadata(debugger: pathlib.Path) -> None:
    for option in ("-h", "--help"):
        completed = subprocess.run(
            [str(debugger), option],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=5,
            check=False,
        )
        require(completed.returncode == 0 and not completed.stderr,
                f"{option} did not succeed cleanly")
        require("NIL debugger (nildebugger)" in completed.stdout and
                "--version" in completed.stdout,
                f"{option} omitted debugger help or version guidance")

    completed = subprocess.run(
        [str(debugger), "--version"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=5,
        check=False,
    )
    require(completed.returncode == 0 and not completed.stderr,
            "--version did not succeed cleanly")
    version = (pathlib.Path(__file__).resolve().parents[2] / "VERSION").read_text().strip()
    require(completed.stdout == f"nildebugger {version}\n",
            "--version did not report the configured project version")


def run_artifact_loading_boundaries(
    debugger: pathlib.Path,
    root: pathlib.Path,
    fixture: pathlib.Path,
) -> None:
    rejected: list[tuple[pathlib.Path, tuple[str, ...]]] = [
        (root, ("no-follow regular file", "not a regular file"))
    ]

    symlink = root / "program-link.nil"
    fixture_snapshot = fixture.read_bytes()
    try:
        symlink.symlink_to(fixture)
    except (NotImplementedError, OSError) as error:
        raise AssertionError(
            "required symlink fixture could not be created; "
            "Windows needs Developer Mode or symlink privilege"
        ) from error
    require(symlink.is_symlink(), "artifact input fixture is not a symlink")
    symlink_target = symlink.readlink()
    # Windows reports reparse points after opening with no-follow semantics,
    # while POSIX rejects the no-follow open itself. Both must run this check.
    rejected.append(
        (symlink, ("no-follow regular file", "not a regular file"))
    )

    oversized = root / "oversized.nil"
    with oversized.open("wb") as stream:
        stream.truncate(MAXIMUM_PROGRAM_BYTES + 1)
    rejected.append((oversized, ("exceeds maximum_bytes",)))

    # FIFOs have no equivalent filesystem fixture on Windows.
    if hasattr(os, "mkfifo"):
        fifo = root / "program.fifo"
        os.mkfifo(fifo)
        rejected.append((fifo, ("not a regular file",)))

    for path, expected_fragments in rejected:
        completed = subprocess.run(
            [str(debugger), "run", str(path)],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=5,
            check=False,
        )
        require(completed.returncode != 0,
                f"nildebugger accepted unsafe artifact input {path.name}")
        require(any(fragment in completed.stderr
                    for fragment in expected_fragments),
                f"nildebugger did not explain rejection of {path.name}")
        if path == symlink:
            require(symlink.is_symlink() and
                    symlink.readlink() == symlink_target and
                    fixture.read_bytes() == fixture_snapshot,
                    "artifact rejection modified the symlink or its target")


def run_text_condition_failures(
    debugger: pathlib.Path, fixture: pathlib.Path
) -> None:
    commands = [
        "",
        " \t",
        f"b {fixture}:12 if G == 7junk",
        f"b {fixture}:12 if G == 999",
        "cond 1 G == 7junk",
        f"b {fixture}:12 if",
        f"b {fixture}:12 unexpected",
        "bl",
        "cond 1",
        "bl",
        "p",
        "watch",
        "quit",
    ]
    completed = run_line_endings(
        [str(debugger), "run", str(fixture)],
        commands,
        "text debugger condition-validation session",
    )
    if completed.returncode != 0 or completed.stderr:
        fail(
            "text debugger condition-validation session failed",
            stdout=completed.stdout,
            stderr=completed.stderr,
        )
    require("Breakpoint 1 set" in completed.stdout,
            "text protocol rejected a valid condition")
    require("Could not set breakpoint: invalid breakpoint condition:" in
            completed.stdout,
            "text protocol did not explain an invalid condition at creation")
    require("Could not set condition: invalid breakpoint condition:" in
            completed.stdout and "complete finite number" in completed.stdout,
            "text protocol did not explain an invalid condition update")
    require("Breakpoint 'if' requires a condition." in completed.stdout,
            "text protocol accepted an empty explicit condition")
    require("Unexpected breakpoint argument 'unexpected'" in completed.stdout,
            "text protocol ignored an unsupported breakpoint suffix")
    require("if G == 999" in completed.stdout,
            "failed text condition update did not preserve the valid condition")
    require("Condition cleared on breakpoint 1." in completed.stdout,
            "text protocol did not clear a condition without an expression")
    require(f"  [1] {fixture}:12\n" in completed.stdout,
            "cleared condition remained on the listed breakpoint")
    require("Usage: p <expression>\n" in completed.stdout and
            "Usage: watch <expression>\n" in completed.stdout,
            "text protocol treated a line terminator as an expression")
    require("\r" not in completed.stdout,
            "text protocol retained a CRLF terminator in a command")

    literal = run_line_endings(
        [str(debugger), "run", str(fixture)],
        ["watch marker\rvalue", "watches", "quit"],
        "text debugger embedded-CR session",
    )
    require("Watch 1 added: marker\rvalue\n" in literal.stdout and
            "[1] marker\rvalue = " in literal.stdout,
            "text protocol discarded an embedded payload CR")

    eof = subprocess.run(
        [str(debugger), "run", str(fixture)],
        input=b"watch marker\r",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=5,
        check=False,
    )
    require(eof.returncode == 0 and not eof.stderr and
            b"Watch 1 added: marker\r\n" in eof.stdout,
            "text protocol discarded a bare payload CR at EOF")


def parse_json_lines(completed: subprocess.CompletedProcess[str], context: str
                     ) -> list[dict[str, object]]:
    if completed.returncode != 0 or completed.stderr:
        fail(
            f"{context} failed with exit code {completed.returncode}",
            stdout=completed.stdout,
            stderr=completed.stderr,
        )
    events: list[dict[str, object]] = []
    for line_number, line in enumerate(completed.stdout.splitlines(), start=1):
        try:
            event = json.loads(line)
        except json.JSONDecodeError as error:
            fail(
                f"{context} line {line_number} corrupted JSONL: {error}",
                stdout=completed.stdout,
            )
        require(isinstance(event, dict),
                f"{context} line {line_number} was not a JSON object")
        events.append(event)
    return events


def run_exit_trapping(debugger: pathlib.Path, fixture: pathlib.Path) -> None:
    completed = subprocess.run(
        [str(debugger), "--json", "run", str(fixture)],
        input=json.dumps({"cmd": "run"}) + "\n" +
              json.dumps({"cmd": "quit"}) + "\n",
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=5,
        check=False,
    )
    events = parse_json_lines(completed, "exit-trapping session")
    terminations = [event for event in events
                    if event.get("event") == "terminated"]
    require(len(terminations) == 1 and
            terminations[0].get("exit_code") == 23,
            "std_sys.exit was not exposed as a clean terminated event")
    require([event.get("event") for event in events][-2:] ==
            ["quit", "detached"],
            "debuggee exit terminated the debugger host process")


def run_input_isolation(debugger: pathlib.Path, fixture: pathlib.Path) -> None:
    # The quit command must remain protocol input; console.read_line receives a
    # dedicated EOF stream until an explicit debugger input protocol exists.
    completed = subprocess.run(
        [str(debugger), "--json", "run", str(fixture)],
        input=json.dumps({"cmd": "run"}) + "\n" +
              json.dumps({"cmd": "quit"}) + "\n",
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=5,
        check=False,
    )
    events = parse_json_lines(completed, "input-isolation session")
    errors = [event for event in events if event.get("event") == "error"]
    require(len(errors) == 1 and
            "input is unavailable or reached EOF" in
            str(errors[0].get("message")),
            "debuggee console.read_line did not fail explicitly on isolated EOF")
    require([event.get("event") for event in events][-2:] ==
            ["quit", "detached"],
            "debuggee console.read_line stole or blocked the quit command")


def run_arbitrary_byte_output(debugger: pathlib.Path, root: pathlib.Path) -> None:
    fixture = root / "invalid-utf8.nil"
    fixture.write_bytes(
        b'module debugger_invalid_utf8\n'
        b'const BAD = "bad:\xff"\n'
        b'function main:\n'
        b'    PUSH_CONST_STR BAD ; line 40\n'
        b'    INTEROP_CALL intrinsic console.writeln 1 void ; line 40\n'
        b'    RET ; line 41\n'
        b'end function\n'
    )
    commands = (
        json.dumps({"cmd": "run"}) + "\n" +
        json.dumps({"cmd": "quit"}) + "\n"
    ).encode("utf-8")
    completed = subprocess.run(
        [str(debugger), "--json", "run", str(fixture)],
        input=commands,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=5,
        check=False,
    )
    if completed.returncode != 0 or completed.stderr:
        fail(
            "arbitrary-byte output session failed",
            stdout=repr(completed.stdout),
            stderr=repr(completed.stderr),
        )
    try:
        stdout = completed.stdout.decode("utf-8", errors="strict")
    except UnicodeDecodeError as error:
        fail(
            f"arbitrary-byte output corrupted the UTF-8 JSONL stream: {error}",
            stdout=repr(completed.stdout),
        )

    events: list[dict[str, object]] = []
    for line_number, line in enumerate(stdout.splitlines(), start=1):
        try:
            event = json.loads(line)
        except json.JSONDecodeError as error:
            fail(
                f"arbitrary-byte output line {line_number} corrupted JSONL: {error}",
                stdout=stdout,
            )
        require(isinstance(event, dict),
                "arbitrary-byte output produced a non-object JSONL event")
        events.append(event)
    outputs = [event for event in events if event.get("event") == "output"]
    require(len(outputs) == 1 and outputs[0].get("output") == "bad:\u00ff\n",
            "invalid program-output byte was not represented deterministically")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: json_protocol.py <nildebugger>", file=sys.stderr)
        return 2
    debugger = pathlib.Path(sys.argv[1]).resolve()
    if not debugger.is_file():
        print(f"nildebugger does not exist: {debugger}", file=sys.stderr)
        return 2

    try:
        with tempfile.TemporaryDirectory(prefix="nildebugger-json-") as temp:
            fixture = pathlib.Path(temp) / "protocol.nil"
            fixture.write_text(PROGRAM, encoding="utf-8", newline='\n')
            exit_fixture = pathlib.Path(temp) / "exit.nil"
            exit_fixture.write_text(EXIT_PROGRAM, encoding="utf-8", newline='\n')
            input_fixture = pathlib.Path(temp) / "input.nil"
            input_fixture.write_text(INPUT_PROGRAM, encoding="utf-8", newline='\n')
            run_protocol(debugger, fixture)
            run_cli_failures(debugger, fixture)
            run_cli_metadata(debugger)
            run_artifact_loading_boundaries(
                debugger, pathlib.Path(temp), fixture
            )
            run_text_condition_failures(debugger, fixture)
            run_exit_trapping(debugger, exit_fixture)
            run_input_isolation(debugger, input_fixture)
            run_arbitrary_byte_output(debugger, pathlib.Path(temp))
    except (AssertionError, OSError, subprocess.SubprocessError) as error:
        print(f"nildebugger JSON protocol regression failed: {error}", file=sys.stderr)
        return 1

    print("nildebugger JSON protocol regressions passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
