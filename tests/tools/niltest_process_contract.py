#!/usr/bin/env python3
"""Exercise niltest process isolation independently of either language backend."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tester", required=True, type=Path)
    arguments = parser.parse_args()
    tester = arguments.tester.resolve()
    if os.name != "posix":
        print("niltest process contracts require a POSIX host")
        raise SystemExit(77)
    checks = 0
    child_pids: set[int] = set()
    with tempfile.TemporaryDirectory(prefix="niltest-process-") as temporary:
        root = Path(temporary)
        source = root / "example.ny"
        source.write_text("// Fake compiler input\n", encoding="utf-8")
        manifest_file = root / "discovery.json"
        actions_file = root / "actions.json"
        log = root / "compile.jsonl"
        compiler = root / "compiler"
        interpreter = root / "interpreter"
        compiler.write_text("#!" + sys.executable + "\n" + '''import json, pathlib, runpy, sys
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
    artifact.write_text("#!" + sys.executable + "\\nimport runpy\\nrunpy.run_path(" + repr(str(root / "interpreter")) + ", run_name='__main__')\\n")
    artifact.chmod(0o755)
else:
    artifact.write_text("fake NIL artifact")
''', encoding="utf-8")
        interpreter.write_text("#!" + sys.executable + "\n" + '''import json, os, pathlib, signal, subprocess, sys, time
root = pathlib.Path(__file__).parent
case = json.loads((root / "actions.json").read_text())[sys.argv[-1]]
action = case.get("action", "pass")
if case.get("pidFile"):
    (root / case["pidFile"]).write_text(str(os.getpid()))
if action in ("descendant", "cancel-descendant"):
    child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"],
                             stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL)
    (root / case["childFile"]).write_text(str(child.pid))
if action == "signal":
    os.kill(os.getpid(), signal.SIGTERM)
if action in ("timeout", "cancel-descendant"):
    time.sleep(60)
sys.stdout.write(case.get("stdout", ""))
sys.stderr.write(case.get("stderr", ""))
sys.exit(case.get("exit", 77))
''', encoding="utf-8")
        compiler.chmod(0o755)
        interpreter.chmod(0o755)
        base = [str(tester), "run", "--compiler", str(compiler), "--interpreter", str(interpreter),
                "--no-stdlib", "--format", "json", str(source)]

        def configure(actions: dict[str, dict]) -> list[dict]:
            metadata = [{"id": name, "name": name, "suite": "process", "kind": "unit",
                         "function": "case_" + str(index), "file": str(source), "line": index + 1}
                        for index, name in enumerate(actions)]
            manifest_file.write_text(json.dumps({"schema": "nil.tests.v1", "tests": metadata}), encoding="utf-8")
            actions_file.write_text(json.dumps(actions), encoding="utf-8")
            log.write_text("", encoding="utf-8")
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

        def alive(pid: int) -> bool:
            # A reaped parent's child can briefly remain a zombie under PID 1.
            # It has terminated and cannot execute more code.
            stat = Path("/proc") / str(pid) / "stat"
            if stat.exists():
                try:
                    if stat.read_text().rsplit(")", 1)[1].strip().split()[0] == "Z":
                        return False
                except (OSError, IndexError):
                    pass
            try:
                os.kill(pid, 0)
                return True
            except ProcessLookupError:
                return False

        def stopped(pid: int) -> None:
            deadline = time.monotonic() + 5
            while alive(pid) and time.monotonic() < deadline:
                time.sleep(.02)
            assert not alive(pid), f"test child {pid} survived runner completion"

        def wait_pid(filename: str, runner: subprocess.Popen | None = None) -> int:
            path = root / filename
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                if path.exists():
                    try:
                        pid = int(path.read_text())
                        child_pids.add(pid)
                        return pid
                    except ValueError:
                        pass  # Writer can be between open and write.
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

            configure({"slow": {"action": "timeout", "pidFile": "timeout.pid"}, "after-timeout": {}})
            document = run("--timeout", "0.15", "--jobs", "1", status=1)
            cases = {case["id"]: case for case in document["tests"]}
            assert cases["slow"]["status"] == "timeout" and cases["slow"]["timedOut"]
            assert cases["after-timeout"]["status"] == "passed"
            stopped(wait_pid("timeout.pid"))

            configure({"signalled": {"action": "signal"}, "after-signal": {}})
            document = run("--jobs", "1", status=1)
            cases = {case["id"]: case for case in document["tests"]}
            assert cases["signalled"]["status"] == "failed" and cases["signalled"]["signal"] == signal.SIGTERM
            assert cases["after-signal"]["status"] == "passed"

            configure({"background": {"action": "descendant", "childFile": "background.pid"}})
            document = run()
            assert document["tests"][0]["status"] == "passed"
            stopped(wait_pid("background.pid"))

            configure({"first-passed": {}, "cancelled": {"action": "cancel-descendant", "pidFile": "cancel.pid", "childFile": "cancel-child.pid"}, "queued": {}})
            runner = subprocess.Popen([*base, "--jobs", "1"], cwd=root, stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE, text=True)
            try:
                parent_pid = wait_pid("cancel.pid", runner)
                descendant_pid = wait_pid("cancel-child.pid", runner)
                runner.send_signal(signal.SIGTERM)
                output, error = runner.communicate(timeout=10)
                document = report(runner, output, error, 128 + signal.SIGTERM)
                cases = {case["id"]: case for case in document["tests"]}
                assert cases["first-passed"]["status"] == "passed"
                assert cases["cancelled"]["status"] == cases["queued"]["status"] == "cancelled"
                stopped(parent_pid)
                stopped(descendant_pid)
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
                manifest_file.write_text(json.dumps(invalid), encoding="utf-8")
                assert run(status=2)["errors"]
            manifest_file.write_text("not JSON", encoding="utf-8")
            assert run(status=2)["errors"]
            configure({})
            assert run(status=2)["errors"]
            listing = base.copy()
            listing[1] = "list"
            assert run(command=listing)["tests"] == []
        finally:
            for pid in child_pids:
                if alive(pid):
                    try:
                        os.kill(pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
    print(f"niltest process contracts passed ({checks} runner invocations)")


if __name__ == "__main__":
    main()
