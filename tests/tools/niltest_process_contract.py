#!/usr/bin/env python3
"""Exercise niltest process isolation independently of either language backend."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import signal
import shutil
import subprocess
import sys
import tempfile
import time

from incremental_build_contract import windows_launcher
from process_contract_support import (assert_stopped, cancel_process,
                                      cleanup_process, console_options,
                                      process_alive, process_identity,
                                      record_processes)
from published_record import read_published_text, windows_shared_handle as windows_identity_handle


def read_identity(path: Path) -> dict[str, int]:
    raw = read_published_text(path)
    records = json.loads(raw)
    assert isinstance(records, list) and len(records) == 1, records
    identity = records[0]
    assert isinstance(identity, dict) and set(identity) == {"pid", "created"}, identity
    assert all(type(identity[key]) is int and identity[key] > 0 for key in ("pid", "created")), identity
    return identity


def cleanup_identities(root: Path, identities: list[dict[str, int]]) -> None:
    errors = []
    for path in root.glob("*.pid"):
        try:
            identities.append(read_identity(path))
        except Exception as error:
            errors.append(error)
    for identity in identities:
        try:
            cleanup_process(identity)
        except Exception as error:
            errors.append(error)
    if errors:
        raise AssertionError("identity cleanup failed: " + "; ".join(map(str, errors))) from errors[0]


def check_identity_publication(root: Path) -> None:
    path = root / "publication-check.json"
    identity = process_identity(os.getpid())
    record_processes(path, os.getpid())
    assert read_identity(path) == identity
    if os.name == "nt":
        # Keep the rename's access mode alive deterministically. The old CRT
        # reader fails here; the shared reader must work before handle close.
        kernel, handle = windows_identity_handle(path, 0x10000)  # DELETE
        try:
            try:
                path.read_text(encoding="utf-8")
            except PermissionError:
                pass
            else:
                raise AssertionError("fixture did not reproduce the rename/open conflict")
            assert read_identity(path) == identity
        finally:
            assert kernel.CloseHandle(handle)
    # A published file is complete: malformed data is never a polling signal.
    for raw in ("[", "[]", '[{"pid":1}]', '[{"pid":true,"created":1}]'):
        path.write_text(raw, encoding="utf-8")
        try:
            read_identity(path)
        except (ValueError, AssertionError):
            pass
        else:
            raise AssertionError("invalid published identity was accepted")
    path.unlink()
    # Bad evidence must fail visibly after cleanup of already known children.
    malformed = root / "malformed-publication.pid"
    malformed.write_text('[{"pid":1}]', encoding="utf-8")
    child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"],
                             stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL)
    try:
        identity = process_identity(child.pid)
        try:
            cleanup_identities(root, [identity])
        except AssertionError as error:
            assert isinstance(error.__cause__, AssertionError), error
        else:
            raise AssertionError("malformed cleanup evidence was ignored")
        assert_stopped(identity)
        child.wait(timeout=5)
    finally:
        if child.poll() is None:
            child.kill()
        child.wait(timeout=5)
        malformed.unlink()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tester", required=True, type=Path)
    parser.add_argument("--cxx", type=Path, help="C compiler for Windows fixture launchers")
    arguments = parser.parse_args()
    tester = arguments.tester.resolve()
    checks = 0
    child_processes: list[dict[str, int]] = []
    with tempfile.TemporaryDirectory(prefix="niltest-process-") as temporary:
        root = Path(temporary)
        check_identity_publication(root)
        shutil.copyfile(Path(__file__).with_name('process_contract_support.py'),
                        root / 'process_contract_support.py')
        source = root / "example.ny"
        source.write_text("// Fake compiler input\n", encoding="utf-8", newline='\n')
        manifest_file = root / "discovery.json"
        actions_file = root / "actions.json"
        log = root / "compile.jsonl"
        compiler_script = root / "compiler"
        interpreter_script = root / "interpreter"
        compiler = compiler_script
        interpreter = interpreter_script
        compiler_script.write_text("#!" + sys.executable + "\n" + '''import json, os, pathlib, runpy, shutil, sys
root = pathlib.Path(__file__).parent
arguments = sys.argv[1:]
with (root / "compile.jsonl").open("a") as log:
    log.write(json.dumps(arguments) + "\\n")
if "--test-list" in arguments:
    sys.stdout.write((root / "discovery.json").read_text())
    sys.exit(0)
assert "--test" in arguments, arguments
artifact = pathlib.Path(arguments[arguments.index("--output") + 1])
if "--native" in arguments:
    if os.name == "nt":
        # A real executable, just like the production native route. The
        # launcher embeds the absolute interpreter-script path.
        if artifact.suffix.lower() != ".exe": artifact = pathlib.Path(str(artifact) + ".exe")
        shutil.copyfile(root / "interpreter.exe", artifact)
    else:
        artifact.write_text("#!" + sys.executable + "\\nimport runpy\\nrunpy.run_path(" + repr(str(root / "interpreter")) + ", run_name='__main__')\\n")
        artifact.chmod(0o755)
else:
    artifact.write_text("fake NIL artifact")
''', encoding="utf-8", newline='\n')
        interpreter_script.write_text("#!" + sys.executable + "\n" + '''import json, os, pathlib, signal, subprocess, sys, time
sys.stdout.reconfigure(newline="\\n")
sys.stderr.reconfigure(newline="\\n")
root = pathlib.Path(__file__).parent
sys.path.insert(0, str(root))
from process_contract_support import record_processes
case = json.loads((root / "actions.json").read_text())[sys.argv[-1]]
action = case.get("action", "pass")
if case.get("pidFile"):
    record_processes(root / case["pidFile"], os.getpid())
if action in ("descendant", "cancel-descendant", "overflow", "timeout-descendant"):
    child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"],
                             stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL)
    record_processes(root / case["childFile"], child.pid)
if action == "signal":
    os.kill(os.getpid(), signal.SIGTERM)
if action == "terminate":
    os._exit(31)
if action in ("timeout", "cancel-descendant", "timeout-descendant"):
    time.sleep(60)
if action == "overflow":
    sys.stdout.buffer.write(b"x" * (17 * 1024 * 1024))
    sys.stdout.buffer.flush()
sys.stdout.write(case.get("stdout", ""))
sys.stderr.write(case.get("stderr", ""))
sys.exit(case.get("exit", 77))
''', encoding="utf-8", newline='\n')
        compiler_script.chmod(0o755)
        interpreter_script.chmod(0o755)
        if os.name == "nt":
            assert arguments.cxx is not None, "--cxx is required on Windows"
            compiler, interpreter = root / "compiler.exe", root / "interpreter.exe"
            windows_launcher(arguments.cxx, compiler, compiler_script, root)
            windows_launcher(arguments.cxx, interpreter, interpreter_script, root)
        base = [str(tester), "run", "--compiler", str(compiler), "--interpreter", str(interpreter),
                "--no-stdlib", "--format", "json", str(source)]

        def configure(actions: dict[str, dict]) -> list[dict]:
            metadata = [{"id": name, "name": name, "suite": "process", "kind": "unit",
                         "function": "case_" + str(index), "file": str(source), "line": index + 1}
                        for index, name in enumerate(actions)]
            manifest_file.write_text(json.dumps({"schema": "nil.tests.v1", "tests": metadata}), encoding="utf-8", newline='\n')
            actions_file.write_text(json.dumps(actions), encoding="utf-8", newline='\n')
            log.write_text("", encoding="utf-8", newline='\n')
            return metadata

        def report(result: subprocess.CompletedProcess | subprocess.Popen,
                   output: str, error: str, expected: int) -> dict:
            nonlocal checks
            assert result.returncode == expected, (result.returncode, expected, output[:4000], error[:4000])
            assert not error, error[:4000]
            document = json.loads(output)
            assert document["schema"] in ("nil.test.results.v1", "nil.tests.v1"), document
            checks += 1
            return document

        def run(*flags: str, status: int = 0, command: list[str] | None = None) -> dict:
            result = subprocess.run([*(command or base), *flags], cwd=root, capture_output=True,
                                    text=True, timeout=20)
            return report(result, result.stdout, result.stderr, status)

        def wait_pid(filename: str, runner: subprocess.Popen | None = None) -> dict[str, int]:
            path = root / filename
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                try:
                    identity = read_identity(path)
                except FileNotFoundError:
                    pass  # The producer has not atomically published it yet.
                else:
                    child_processes.append(identity)
                    return identity
                if runner is not None and runner.poll() is not None:
                    raise AssertionError(f"runner stopped before {filename}: {runner.returncode}")
                time.sleep(.02)
            raise AssertionError(f"test process did not create {filename}")

        try:
            configure({"failure": {"exit": 3}, "early": {"exit": 0}, "success": {"stdout": "last ran\n"}})
            document = run("--jobs", "1", status=1)
            cases = {case["id"]: case for case in document["tests"]}
            assert [cases[name]["status"] for name in ("failure", "early", "success")] == ["failed", "failed", "passed"]
            assert "without completing" in cases["early"]["message"], cases["early"]
            assert cases["success"]["stdout"] == "last ran\n"
            compilations = [json.loads(line) for line in log.read_text().splitlines()]
            assert sum("--test-list" in command for command in compilations) == 1
            assert sum("--test" in command for command in compilations) == 1

            fake_report = '{"schema":"nil.test.results.v1","tests":[],"summary":{"passed":999}}\n'
            configure({"quoted": {"stdout": fake_report, "stderr": "ordinary diagnostic\n"}})
            document = run("--target", "all", "--jobs", "2")
            assert len(document["tests"]) == 2 and {case["route"] for case in document["tests"]} == {"vm", "native"}
            assert all(case["stdout"] == fake_report and case["stderr"] == "ordinary diagnostic\n" and case["exitCode"] == 77
                       for case in document["tests"])
            compilations = [json.loads(line) for line in log.read_text().splitlines()]
            assert sum("--test" in command for command in compilations) == 2

            configure({"slow": {"action": "timeout-descendant", "pidFile": "timeout.pid", "childFile": "timeout-child.pid"}, "after-timeout": {}})
            # The deadline includes process/launcher startup, which is much
            # slower on Windows. Both values remain far below the 60s sleep.
            document = run("--timeout", "2" if os.name == "nt" else "0.15", "--jobs", "1", status=1)
            cases = {case["id"]: case for case in document["tests"]}
            assert cases["slow"]["status"] == "timeout" and cases["slow"]["timedOut"]
            assert cases["after-timeout"]["status"] == "passed"
            assert_stopped(wait_pid("timeout.pid"))
            assert_stopped(wait_pid("timeout-child.pid"))

            configure({"signalled": {"action": "terminate" if os.name == "nt" else "signal"}, "after-signal": {}})
            document = run("--jobs", "1", status=1)
            cases = {case["id"]: case for case in document["tests"]}
            assert cases["signalled"]["status"] == "failed"
            if os.name == "nt":
                assert cases["signalled"]["signal"] == 0 and cases["signalled"]["exitCode"] == 31
            else:
                assert cases["signalled"]["signal"] == signal.SIGTERM
            assert cases["after-signal"]["status"] == "passed"

            configure({"background": {"action": "descendant", "childFile": "background.pid"}})
            document = run()
            assert document["tests"][0]["status"] == "passed"
            assert_stopped(wait_pid("background.pid"))

            configure({"overflow": {"action": "overflow", "pidFile": "overflow.pid", "childFile": "overflow-child.pid"}, "after-overflow": {}})
            document = run("--jobs", "1", status=2)
            cases = {case["id"]: case for case in document["tests"]}
            assert cases["overflow"]["status"] == "error", cases["overflow"]["status"]
            assert "capture limit" in cases["overflow"]["message"]
            assert len(cases["overflow"]["stdout"]) == 16 * 1024 * 1024
            assert cases["after-overflow"]["status"] == "passed"
            assert_stopped(wait_pid("overflow.pid"))
            assert_stopped(wait_pid("overflow-child.pid"))

            configure({"first-passed": {}, "cancelled": {"action": "cancel-descendant", "pidFile": "cancel.pid", "childFile": "cancel-child.pid"}, "queued": {}})
            runner = subprocess.Popen([*base, "--jobs", "1"], cwd=root, stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE, text=True, **console_options())
            try:
                parent_pid = wait_pid("cancel.pid", runner)
                descendant_pid = wait_pid("cancel-child.pid", runner)
                cancel_process(runner, signal.SIGTERM)
                output, error = runner.communicate(timeout=10)
                document = report(runner, output, error, 130 if os.name == "nt" else 128 + signal.SIGTERM)
                cases = {case["id"]: case for case in document["tests"]}
                assert cases["first-passed"]["status"] == "passed"
                assert cases["cancelled"]["status"] == cases["queued"]["status"] == "cancelled"
                if os.name == "nt":
                    assert cases["cancelled"]["signal"] == cases["queued"]["signal"] == 0
                    assert cases["cancelled"]["exitCode"] == cases["queued"]["exitCode"] == 130
                assert_stopped(parent_pid)
                assert_stopped(descendant_pid)
            finally:
                if runner.poll() is None:
                    runner.kill()
                    runner.communicate(timeout=5)

            metadata = configure({"select-a": {}, "select-b": {}})
            assert run("--test-id", "select-a", "--test-id", "select-b")["summary"]["passed"] == 2
            assert run("--test-id", "select-b")["tests"][0]["id"] == "select-b"
            assert run("--filter", "select-a")["tests"][0]["id"] == "select-a"
            assert run("--filter", "no-match", status=2)["errors"]
            assert run("--test-id", "missing", status=2)["errors"]
            for jobs in ("0", "65", "1.5", "-1"):
                assert run("--jobs", jobs, status=2)["errors"]
            for timeout in ("0", "NaN", "3601"):
                assert run("--timeout", timeout, status=2)["errors"]
            for invalid in (
                {"schema": "nil.tests.v1", "tests": [metadata[0], metadata[0]]},
                {"schema": "nil.tests.v1", "tests": [{**metadata[0], "name": 7}]},
                {"schema": "nil.tests.v1", "tests": [{**metadata[0], "line": "1"}]},
                *({"schema": "nil.tests.v1", "tests": [{**metadata[0], key: value}]}
                  for key, value in (("id", "bad\0id"), ("id", ""), ("function", ""),
                                     ("function", "bad\0function"), ("name", "bad\0name"),
                                     ("suite", "bad\0suite"), ("kind", "bad\0kind"),
                                     ("file", "relative.ny"), ("file", "/bad\0file.ny"),
                                     ("line", -1), ("line", 1.5), ("line", 9007199254740992))),
                {"schema": "nil.tests.v1", "tests": {}},
                {"schema": "wrong", "tests": []},
            ):
                manifest_file.write_text(json.dumps(invalid), encoding="utf-8", newline='\n')
                assert run(status=2)["errors"]
            manifest_file.write_text("not JSON", encoding="utf-8", newline='\n')
            assert run(status=2)["errors"]
            configure({})
            assert run(status=2)["errors"]
            listing = base.copy()
            listing[1] = "list"
            assert run(command=listing)["tests"] == []
        finally:
            # Collect even when an assertion failed before wait_pid reached
            # the fixture, so failure reporting cannot leave children behind.
            cleanup_identities(root, child_processes)
    print(f"niltest process contracts passed ({checks} runner invocations)")


if __name__ == "__main__":
    main()
