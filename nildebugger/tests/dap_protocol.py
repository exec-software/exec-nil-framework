#!/usr/bin/env python3
"""End-to-end source debugging through the actual compiler and framed adapter."""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import queue
import subprocess
import struct
import sys
import tempfile
import threading
import time


class Client:
    def __init__(self, debugger: Path):
        self.process = subprocess.Popen([str(debugger), "--dap"], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.messages: queue.Queue = queue.Queue()
        self.pending = []
        self.seq = 0
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        try:
            while True:
                header = self.process.stdout.readline()
                if not header:
                    raise AssertionError("adapter stdout ended unexpectedly")
                assert header.startswith(b"Content-Length: ") and header.endswith(b"\r\n"), header
                size = int(header[len(b"Content-Length: "):])
                assert self.process.stdout.read(2) == b"\r\n"
                body = self.process.stdout.read(size)
                assert len(body) == size
                self.messages.put(json.loads(body))
        except BaseException as error:
            self.messages.put(error)

    def wait(self, predicate, timeout=8):
        deadline = time.monotonic() + timeout
        while True:
            for index, message in enumerate(self.pending):
                if predicate(message):
                    return self.pending.pop(index)
            remaining = deadline - time.monotonic()
            assert remaining > 0, f"timed out; queued messages: {self.pending}"
            message = self.messages.get(timeout=remaining)
            if isinstance(message, BaseException):
                raise message
            self.pending.append(message)

    def request(self, command, arguments=None, success=True):
        self.seq += 1
        message = {"seq": self.seq, "type": "request", "command": command}
        if arguments is not None:
            message["arguments"] = arguments
        body = json.dumps(message).encode()
        self.process.stdin.write(f"Content-Length: {len(body)}\r\n\r\n".encode() + body)
        self.process.stdin.flush()
        response = self.wait(lambda item: item.get("type") == "response" and item.get("request_seq") == self.seq)
        assert response["success"] is success, response
        return response

    def event(self, name):
        return self.wait(lambda item: item.get("event") == name)["body"]

    def initialize(self, program, stop_on_entry=True):
        capabilities = self.request("initialize", {"adapterID": "ny", "pathFormat": "path"})["body"]
        assert capabilities["supportsConfigurationDoneRequest"]
        assert capabilities["supportsConditionalBreakpoints"]
        self.request("launch", {"program": str(program), "stopOnEntry": stop_on_entry})
        self.event("initialized")

    def breakpoints(self, source, *points):
        response = self.request("setBreakpoints", {"source": {"path": str(source)},
                               "breakpoints": [point if isinstance(point, dict) else {"line": point} for point in points]})
        return response["body"]["breakpoints"]

    def stack(self):
        return self.request("stackTrace", {"threadId": 1})["body"]["stackFrames"]

    def evaluate(self, expression, frame=None):
        args = {"expression": expression}
        if frame is not None:
            args["frameId"] = frame
        return self.request("evaluate", args)["body"]["result"]

    def close(self):
        if self.process.poll() is None:
            try:
                self.request("disconnect")
                self.process.wait(timeout=3)
            except BaseException:
                self.process.kill()
                self.process.wait(timeout=3)
        self.process.stdin.close()
        self.process.stdout.close()
        errors = self.process.stderr.read().decode(errors="replace")
        if errors:
            print(errors, file=sys.stderr)
        self.process.stderr.close()


def compile_program(compiler, output, *sources, binary=False):
    command = [str(compiler), "--no-stdlib", "--no-bundle", "--quiet", "--debug-info",
               "--output", str(output)]
    if binary:
        command.append("--emit-binary-nil")
    result = subprocess.run([*command, *map(str, sources)], text=True, capture_output=True, timeout=30)
    assert result.returncode == 0, (command, result.stdout, result.stderr)


def source_session(debugger, program, main, helper):
    client = Client(debugger)
    try:
        client.initialize(program)
        assert client.breakpoints(main, 5)[0]["verified"]
        assert client.breakpoints(helper, {"line": 5, "condition": "value == 43"})[0]["verified"]
        assert not client.breakpoints(main.parent / "absent.ny", 5)[0]["verified"]
        client.request("configurationDone")
        assert client.event("stopped")["reason"] == "entry"
        assert Path(client.stack()[0]["source"]["path"]) == main
        client.request("continue", {"threadId": 1})
        assert client.event("stopped")["reason"] == "breakpoint"
        frames = client.stack()
        assert Path(frames[0]["source"]["path"]) == helper and frames[0]["line"] == 5, frames
        assert Path(frames[1]["source"]["path"]) == main and frames[1]["line"] == 3, frames
        assert client.evaluate("value", frames[0]["id"]) == "43"
        assert client.evaluate("value", frames[1]["id"]) == "40"
        scope = client.request("scopes", {"frameId": frames[0]["id"]})["body"]["scopes"][0]
        variables = client.request("variables", {"variablesReference": scope["variablesReference"]})["body"]["variables"]
        assert [(item["name"], item["value"]) for item in variables] == [("value", "43")], variables
        client.request("next", {"threadId": 1})
        assert client.event("stopped")["reason"] == "step"
        assert client.evaluate("value") == "41", "expired inner shadow remained visible"
        client.request("variables", {"variablesReference": scope["variablesReference"]}, success=False)
        client.request("scopes", {"frameId": frames[0]["id"]}, success=False)
        client.request("stepOut", {"threadId": 1})
        client.event("stopped")
        assert Path(client.stack()[0]["source"]["path"]) == main
        client.request("continue", {"threadId": 1})
        assert client.event("stopped")["reason"] == "breakpoint"
        frames = client.stack()
        assert Path(frames[0]["source"]["path"]) == main and frames[0]["line"] == 5
        assert client.evaluate("value") == "40"
        assert client.evaluate("result") == "41"
        client.request("evaluate", {"expression": "not_visible"}, success=False)
        client.request("continue", {"threadId": 1})
        assert client.event("exited")["exitCode"] == 0
        client.event("terminated")
    finally:
        client.close()


def stepping_session(debugger, program, main, helper, over):
    client = Client(debugger)
    try:
        client.initialize(program, stop_on_entry=False)
        assert client.breakpoints(main, 3)[0]["verified"]
        client.request("configurationDone")
        assert client.event("stopped")["reason"] == "breakpoint"
        client.request("next" if over else "stepIn", {"threadId": 1})
        assert client.event("stopped")["reason"] == "step"
        frame = client.stack()[0]
        assert Path(frame["source"]["path"]) == (main if over else helper), frame
        if over:
            assert frame["line"] == 4 and client.evaluate("result") == "41", frame
    finally:
        client.close()


def asynchronous_session(debugger, program):
    client = Client(debugger)
    try:
        client.initialize(program, stop_on_entry=False)
        client.request("configurationDone")
        # Output must arrive while the infinite loop is still running.
        assert "alive" in client.event("output")["output"]
        client.request("stackTrace", {"threadId": 1}, success=False)
        client.request("pause", {"threadId": 1})
        assert client.event("stopped")["reason"] == "pause"
        assert int(client.evaluate("value")) >= 0
        client.request("continue", {"threadId": 1})
        client.request("terminate")
        client.event("terminated")
    finally:
        client.close()


def binary_library_session(debugger, compiler, root):
    producer = root / "producer"
    consumer = root / "consumer"
    sdk = root / "sdk"
    for directory in (producer, consumer, sdk):
        directory.mkdir()
    library = producer / "library.ny"
    library.write_text("module custom\npub fn value() -> i32\n    return 7\nend fn\npub fn failing(value: i32) -> i32\n    return 10 / value\nend fn\n")
    result = subprocess.run([str(compiler), "--no-stdlib", "--no-bundle", "--quiet",
                             "--emit-binary-nil", "--emit-library", "--output", str(sdk / "custom.nil"),
                             str(library)], capture_output=True, timeout=30)
    assert result.returncode == 0, result.stderr
    source = consumer / "main.ny"
    source.write_text("import custom\nfn main() -> i32\n    let answer = custom::value()\n    println(answer)\n    return 0\nend fn\n")
    program = consumer / "program.nil"
    result = subprocess.run([str(compiler), "--no-stdlib", "--no-bundle", "--quiet", "--debug-info",
                             "--sdk-path", str(sdk), "--output", str(program), str(source)],
                            capture_output=True, timeout=30)
    assert result.returncode == 0, result.stderr
    assert "nillib://" not in program.read_text(), "synthetic library identity leaked into source map"
    client = Client(debugger)
    try:
        client.initialize(program, stop_on_entry=False)
        assert client.breakpoints(source, 3)[0]["verified"]
        assert not client.breakpoints("nillib://custom/library.ny", 3)[0]["verified"]
        client.request("configurationDone")
        assert client.event("stopped")["reason"] == "breakpoint"
        client.request("stepIn", {"threadId": 1})
        assert client.event("stopped")["reason"] == "step"
        frames = client.stack()
        assert len(frames) == 1 and Path(frames[0]["source"]["path"]) == source and frames[0]["line"] == 4, frames
        assert client.evaluate("answer") == "7"
    finally:
        client.close()
    source.write_text("import custom\nfn main() -> i32\n    return custom::failing(0)\nend fn\n")
    result = subprocess.run([str(compiler), "--no-stdlib", "--no-bundle", "--quiet", "--debug-info",
                             "--sdk-path", str(sdk), "--output", str(program), str(source)],
                            capture_output=True, timeout=30)
    assert result.returncode == 0, result.stderr
    client = Client(debugger)
    try:
        client.initialize(program, stop_on_entry=False)
        client.request("configurationDone")
        assert client.event("stopped")["reason"] == "exception"
        frames = client.stack()
        assert frames[0]["name"] == "custom__failing" and "source" not in frames[0] and frames[0]["line"] == 0, frames
        assert client.request("scopes", {"frameId": frames[0]["id"]})["body"]["scopes"] == []
        client.request("evaluate", {"expression": "value", "frameId": frames[0]["id"]}, success=False)
    finally:
        client.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("debugger", type=Path)
    parser.add_argument("compiler", type=Path)
    args = parser.parse_args()
    debugger, compiler = args.debugger.resolve(), args.compiler.resolve()
    specification = importlib.util.spec_from_file_location("ny_dap_routes", Path(__file__).resolve().parents[2] / "tests/routes/ny_route_matrix.py")
    routes = importlib.util.module_from_spec(specification)
    sys.modules[specification.name] = routes
    specification.loader.exec_module(routes)
    with tempfile.TemporaryDirectory(prefix="ny-dap-regression-") as temporary:
        root = Path(temporary)
        main_source = root / "main.ny"
        helper_source = root / "space λ helper.ny"
        main_source.write_text("fn main() -> i32\n    let value: i64 = 40\n    let result = helper(value)\n    println(result)\n    println(value)\n    return 0\nend fn\n")
        helper_source.write_text("fn helper(value: i64) -> i64\n    let value: i64 = value + 1\n    if true then\n        let value: i64 = value + 2\n        println(value)\n    end if\n    return value\nend fn\n")
        for binary in (False, True):
            program = root / ("binary.nil" if binary else "text.nil")
            compile_program(compiler, program, main_source, helper_source, binary=binary)
            valid, errors, metadata = (routes.validate_binary_nil_bytes(program.read_bytes()) if binary
                                       else routes.validate_text_nil_bytes(program.read_bytes()))
            assert valid, errors
            assert set(metadata["debug_source_paths"]) == {str(main_source), str(helper_source)}, metadata
            source_session(debugger, program, main_source, helper_source)
            stepping_session(debugger, program, main_source, helper_source, over=False)
            stepping_session(debugger, program, main_source, helper_source, over=True)
            private = root / ("ordinary-binary.nil" if binary else "ordinary-text.nil")
            result = subprocess.run([str(compiler), "--no-stdlib", "--no-bundle", "--quiet",
                                     *(["--emit-binary-nil"] if binary else []), "--output", str(private),
                                     str(main_source), str(helper_source)], capture_output=True, timeout=30)
            assert result.returncode == 0, result.stderr
            assert str(root).encode() not in private.read_bytes(), "default artifact disclosed absolute source paths"
            if binary:
                assert struct.unpack_from("<I", private.read_bytes(), 4)[0] == 3
            client = Client(debugger)
            try:
                client.request("initialize", {})
                failure = client.request("launch", {"program": str(private)}, success=False)
                assert "--debug-info" in failure["message"], failure
            finally:
                client.close()
        loop = root / "loop.ny"
        loop.write_text('fn main() -> i32\n    let mut value: i64 = 0\n    println("alive")\n    while true\n        value = value + 1\n    end while\n    return 0\nend fn\n')
        loop_nil = root / "loop.nil"
        compile_program(compiler, loop_nil, loop)
        asynchronous_session(debugger, loop_nil)
        binary_library_session(debugger, compiler, root)
        for malformed in (b"Content-Length: 1048577\r\n\r\n", b"Content-Length: 2\r\nContent-Length: 2\r\n\r\n{}",
                          b"Content-Length: -1\r\n\r\n", b"Content-Length: 10\r\n\r\n{}", b"Content-Length: 1\r\n\r\nx",
                          b"Bad\r\n\r\n", b"Content-Length: 2\n\n{}", b"X: " + b"a" * 8193):
            result = subprocess.run([str(debugger), "--dap"], input=malformed, capture_output=True, timeout=5)
            assert result.returncode != 0 and result.stdout == b"", result
    print("DAP: text/binary source paths, shadows, frames, stepping, output, pause, termination, and malformed framing passed")


if __name__ == "__main__":
    main()
