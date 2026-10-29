#!/usr/bin/env python3
"""Bounded Matrix evaluator and retained-value shell CLI contract."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def nildata(value):
    def lines(item, prefix="", indent=""):
        if isinstance(item, (dict, list)):
            kind = "object" if isinstance(item, dict) else "array"
            out = [indent + prefix + ("an " if prefix else "") + kind]
            entries = item.items() if isinstance(item, dict) else ((None, v) for v in item)
            for key, child in entries:
                out += lines(child, ("item" if key is None else json.dumps(key)) + " is ", indent + "  ")
            return out + [indent + "end " + kind]
        return [indent + prefix + json.dumps(item, ensure_ascii=False)]
    return "nildata 1\n" + "\n".join(lines(value)) + "\n"


def number(value):
    return {"kind": "number", "value": str(value)}


def envelope(value, bindings=()):
    return {"schema": "nil.matrix.submission-request.v1", "profile": "matrix.pure.v1",
            "bindings": [{"name": name, "value": value} for name, value in bindings], "value": value}


def scalar(value, kind, spelling):
    assert value == {"type": kind, "scalar": spelling}, value


def evaluation(report, status="complete"):
    assert report["schema"] == "nil.matrix.submission-evaluation.v1", report
    assert report["profile"] == "matrix.pure.v1", report
    assert report["status"] == status and "nil" not in report, report
    if status == "complete":
        assert report["executed"] and report["committed"] and not report["diagnostics"], report
        assert report["instructions_executed"] > 0, report
    else:
        assert not report["committed"] and report["value"] is None and not report["added"], report
        assert report["diagnostics"], report
        if status in {"invalid", "incomplete"}:
            assert not report["executed"] and report["instructions_executed"] == 0, report
    return report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True)
    compiler = str(Path(parser.parse_args().compiler).resolve())
    checks = 0
    with tempfile.TemporaryDirectory(prefix="matrix-session-") as temporary:
        work = Path(temporary)
        profile = work / "must-not-create-profile.jsonl"
        env = {**os.environ, "NIL_MEMORY_PROFILE": str(profile)}

        def command(arguments, source="", code=0):
            nonlocal checks
            if isinstance(source, str):
                source = source.encode()
            result = subprocess.run([compiler, *arguments], input=source, cwd=work,
                                    env=env, capture_output=True, timeout=15)
            assert result.returncode == code, (arguments, source[:500], result)
            assert not result.stderr, (arguments, result.stderr)
            assert not profile.exists(), "bounded evaluation must ignore ambient memory-profile output"
            checks += 1
            return result

        def once(source="", request=False, status="complete", path="-"):
            flag = "--evaluate-submission-request" if request else "--evaluate-submission"
            result = command([flag, str(path)], source,
                             0 if status == "complete" else 2 if status == "incomplete" else 1)
            report = evaluation(json.loads(result.stdout), status)
            assert report["input_kind"] == ("nildata-request" if request else "matrix"), report
            return report

        def shell(source, code=0):
            result = command(["--matrix-shell"], source, code)
            # Pipe mode has no prompt, command echo, history, or human prose.
            reports = [json.loads(line) for line in result.stdout.splitlines()]
            return reports, result.stdout

        for source, kind, spelling in [
            ("40 plus 2", "i32", "42"),
            ("-9223372036854775808", "i64", "-9223372036854775808"),
            ("9223372036854775807", "i64", "9223372036854775807"),
            ("false and (1 over 0 equals 0)", "bool", "false"),
            ("true or (1 over 0 equals 0)", "bool", "true"),
            ("[1, 2] at 1", "i32", "2"),
            ("[1, 2] at 1000", "i32", "0"),
            ('"line" plus newline plus "end"', "text", "line\nend"),
        ]:
            scalar(once(source)["value"], kind, spelling)
        assert once("")["value"] is None
        for source in ["1 over 0", "[(1, true)] at 2"]:
            report = once(source, status="runtime-error")
            assert report["executed"] and report["binding_count"] == 0, report
        for source in ["1 plus true", 'say "host output"', "variable amount is 1",
                       "value amount is 1\nset amount to 2", "repeat forever\nend repeat"]:
            once(source, status="invalid")
        once("1 plus", status="incomplete")

        pairs = [
            ("42", envelope(number(42))),
            ("-9223372036854775808", envelope(number("-9223372036854775808"))),
            ('"text"', envelope({"kind": "text", "value": "text"})),
            ("value amount is 40\namount plus 2", envelope(
                {"kind": "binary", "operator": "plus", "left": {"kind": "name", "name": "amount"},
                 "right": number(2)}, [("amount", number(40))])),
            ("(1, true)", envelope({"kind": "tuple", "items": [number(1), {"kind": "boolean", "value": True}]})),
        ]
        for source, request in pairs:
            human, machine = once(source), once(nildata(request), request=True)
            for key in ["value", "added", "binding_count", "result_type"]:
                assert human[key] == machine[key], (key, human, machine)
        once(nildata(envelope({"kind": "call", "name": "host"})), request=True, status="invalid")

        reports, _ = shell(
            "value Frame-Count is 40\nFRAME_count plus 2\n"
            'value pairs is [(1, "one"), (2, "two")]\n'
            "pairs at 1\n(pairs at 0).1\n:bindings\n:quit\nthis must not run\n")
        assert len(reports) == 6, reports
        for report in reports[:5]:
            evaluation(report)
        scalar(reports[1]["value"], "i32", "42")
        scalar(reports[3]["value"]["elements"][1], "text", "two")
        scalar(reports[4]["value"], "text", "one")
        assert reports[5]["schema"] == "nil.matrix.session-bindings.v1", reports[5]
        assert reports[5]["binding_count"] == 2, reports[5]
        assert [item["name"] for item in reports[5]["bindings"]] == ["Frame_Count", "pairs"], reports[5]

        nested = "[" * 20 + "1" + "]" * 20
        reports, _ = shell("value nested is " + nested + "\n:bindings\n:quit\n")
        evaluation(reports[0])
        assert len(reports) == 2 and reports[1]["binding_count"] == 1, reports
        item = reports[1]["bindings"][0]["value"]
        for _ in range(20):
            assert len(item["elements"]) == 1, item
            item = item["elements"][0]
        scalar(item, "i32", "1")

        reports, _ = shell("value answer is\n40 plus 2\nanswer\n:quit\n")
        assert len(reports) == 2, reports
        scalar(evaluation(reports[1])["value"], "i32", "42")
        reports, _ = shell("value kept is 7\nvalue missing is\n", code=2)
        assert len(reports) == 2, reports
        assert evaluation(reports[1], "incomplete")["binding_count"] == 1
        reports, _ = shell(":begin\nvalue never-committed is 3\n", code=2)
        assert len(reports) == 1, reports
        assert evaluation(reports[0], "incomplete")["binding_count"] == 0

        reports, _ = shell(
            "value kept is 7\n"
            ":begin\nvalue staged is 9\n1 over 0\n:submit\n:bindings\nkept\n"
            ":begin\nvalue invalid-stage is 2\n1 plus true\n:submit\n:bindings\n"
            ":begin\nvalue accepted is kept plus 3\naccepted times 2\n:submit\naccepted\n:quit\n", code=1)
        assert len(reports) == 8, reports
        evaluation(reports[1], "runtime-error")
        evaluation(reports[4], "invalid")
        for index in [2, 5]:
            assert reports[index]["binding_count"] == 1, reports[index]
            assert [item["name"] for item in reports[index]["bindings"]] == ["kept"], reports[index]
        scalar(evaluation(reports[3])["value"], "i32", "7")
        scalar(evaluation(reports[6])["value"], "i32", "20")
        scalar(evaluation(reports[7])["value"], "i32", "10")
        assert [item["name"] for item in reports[6]["added"]] == ["accepted"], reports[6]

        reports, _ = shell(
            "value kept is 7\n:begin\nvalue cancelled is 8\n:cancel\n:bindings\n"
            "value unfinished is\n:cancel\n:begin\nvalue discarded is 1\n:reset\n"
            ":bindings\nvalue fresh is 9\nfresh\n:quit\n")
        assert len(reports) == 5, reports
        assert [item["name"] for item in reports[1]["bindings"]] == ["kept"], reports[1]
        assert reports[2]["bindings"] == [] and reports[2]["binding_count"] == 0, reports[2]
        scalar(evaluation(reports[4])["value"], "i32", "9")
        reports, _ = shell("value gone is 1\n:reset\ngone\n", code=1)
        assert len(reports) == 2 and evaluation(reports[1], "invalid")["binding_count"] == 0

        # A raw text block owns every payload line, including shell command
        # spellings. Only after end text may the shell interpret commands again.
        payload = "\n".join([":reset", ":quit", ":submit", ":bindings", ":cancel", ":begin", ":help"])
        reports, _ = shell("value kept is 7\nvalue message is begin text\n" + payload +
                           "\nend text\n:bindings\nkept\n:quit\n")
        assert len(reports) == 4, reports
        evaluation(reports[1])
        assert reports[2]["binding_count"] == 2, reports[2]
        scalar(reports[2]["bindings"][1]["value"], "text", payload)
        scalar(evaluation(reports[3])["value"], "i32", "7")
        reports, _ = shell("value kept is 7\n:begin\nvalue message is begin text\n" + payload +
                           "\nend text\nmessage\n:submit\n:bindings\n:quit\n")
        assert len(reports) == 3, reports
        scalar(evaluation(reports[1])["value"], "text", payload)
        assert reports[2]["binding_count"] == 2, reports[2]

        # Oversized atomic input must not turn later block lines into separate
        # submissions, and one oversized physical line is drained with a cap.
        comments = "!!!" + "x" * 40000 + "\n"
        reports, _ = shell("value kept is 7\n:begin\n" + comments + comments +
                           "value escaped is 99\n:submit\nkept\n:bindings\n", code=1)
        assert len(reports) == 4, reports
        evaluation(reports[1], "invalid")
        scalar(evaluation(reports[2])["value"], "i32", "7")
        assert [item["name"] for item in reports[3]["bindings"]] == ["kept"], reports[3]
        reports, _ = shell("value kept is 7\n" + "x" * 65537 +
                           "\nvalue escaped is 99\n:cancel\nkept\n", code=1)
        assert len(reports) == 3, reports
        evaluation(reports[1], "invalid")
        scalar(evaluation(reports[2])["value"], "i32", "7")

        # Implicit multiline input has the same discard boundary as :begin.
        # Repeated oversized lines cannot clear discard mode accidentally.
        reports, _ = shell("value kept is 7\nvalue message is begin text\n" + "x" * 65537 +
                           "\n" + "y" * 65537 +
                           "\nvalue escaped is 99\nend text\n:cancel\n:bindings\nkept\n", code=1)
        assert len(reports) == 5, reports
        evaluation(reports[1], "invalid")
        evaluation(reports[2], "invalid")
        assert [item["name"] for item in reports[3]["bindings"]] == ["kept"], reports[3]
        scalar(evaluation(reports[4])["value"], "i32", "7")
        reports, _ = shell("value kept is 7\nwhen true then\n" + comments + comments +
                           "value escaped is 99\nend when\n:submit\n:bindings\n", code=1)
        assert len(reports) == 3, reports
        evaluation(reports[1], "invalid")
        assert [item["name"] for item in reports[2]["bindings"]] == ["kept"], reports[2]

        # ESC/newline/tab data remains JSON text, never active terminal control.
        reports, output = shell('value message is "\x1b[31m" plus newline plus tab\n:bindings\n:quit\n')
        assert b"\x1b" not in output, output
        scalar(reports[1]["bindings"][0]["value"], "text", "\x1b[31m\n\t")
        reports, _ = shell(":quit extra\n1\n:quit\n", code=1)
        assert len(reports) == 2, reports
        evaluation(reports[0], "invalid")
        scalar(evaluation(reports[1])["value"], "i32", "1")
        reports, _ = shell(":help\n:quit\r\n")
        assert len(reports) == 1 and reports[0]["schema"] == "nil.matrix.shell-help.v1", reports

        for source in [b"\xff", b"1\0", b"1\x1a trailing", b" " * 65537]:
            once(source, status="invalid")
        source_file = work / "input.matrix"
        source_file.write_text("40 plus 2")
        scalar(once(path=source_file)["value"], "i32", "42")
        once(path=work, status="invalid")
        once(path=work / "missing", status="invalid")
        if os.name == "posix":
            link = work / "linked.matrix"
            link.symlink_to(source_file)
            once(path=link, status="invalid")
            fifo = work / "fifo.matrix"
            os.mkfifo(fifo)
            once(path=fifo, status="invalid")

        sentinel = work / "untouched.nil"
        sentinel.write_text("sentinel")
        for first in [["--evaluate-submission", "-"], ["--evaluate-submission-request", "-"], ["--matrix-shell"]]:
            for extra in [["--no-typecheck"], ["--native"], ["--jit"], ["--output", str(sentinel)],
                          ["--sdk-path", str(work)], ["--check-submission", "-"], ["--matrix-shell"]]:
                result = subprocess.run([compiler, *first, *extra], input=b"1", cwd=work,
                                        env=env, capture_output=True, timeout=15)
                assert result.returncode != 0 and sentinel.read_text() == "sentinel", (first, extra, result)
                assert not profile.exists()
                checks += 1
        assert sorted(item.name for item in work.iterdir()) == sorted([
            "input.matrix", "untouched.nil", *(["linked.matrix", "fifo.matrix"] if os.name == "posix" else [])])
    print(f"Matrix session CLI: {checks} checks passed")


if __name__ == "__main__":
    main()
