#!/usr/bin/env python3
"""CLI admission contract: check Matrix/NILData without executing a program."""
import argparse
import json
import os
import re
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
                name = "item" if key is None else json.dumps(key)
                out += lines(child, name + " is ", indent + "  ")
            return out + [indent + "end " + kind]
        return [indent + prefix + json.dumps(item, ensure_ascii=False)]
    return "nildata 1\n" + "\n".join(lines(value)) + "\n"


def number(value):
    return {"kind": "number", "value": str(value)}


def name(value):
    return {"kind": "name", "name": value}


def binary(op, left, right):
    return {"kind": "binary", "operator": op, "left": left, "right": right}


def envelope(value, bindings=()):
    return {"schema": "nil.matrix.submission-request.v1", "profile": "matrix.pure.v1",
            "bindings": [{"name": key, "value": val} for key, val in bindings], "value": value}


def instructions(text):
    # Matrix and request nodes have different source locations. Retain every
    # operation/schema/constant while ignoring textual source-line comments.
    return "\n".join(re.sub(r"\s+; line [0-9]+$", "", line).rstrip()
                     for line in text.splitlines())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True)
    compiler = str(Path(parser.parse_args().compiler).resolve())
    checks = 0
    with tempfile.TemporaryDirectory(prefix="matrix-submission-") as temporary:
        work = Path(temporary)

        def run(source=b"", *, request=False, status="complete", result_type=None, path="-"):
            nonlocal checks
            if isinstance(source, str):
                source = source.encode()
            option = "--check-submission-request" if request else "--check-submission"
            result = subprocess.run([compiler, option, str(path)], input=source,
                                    cwd=work, capture_output=True, timeout=10)
            assert result.stderr == b"", (source, result.stderr)
            report = json.loads(result.stdout)
            assert report["schema"] == "nil.matrix.submission-check.v1", report
            assert report["profile"] == "matrix.pure.v1", report
            assert report["input_kind"] == ("nildata-request" if request else "matrix"), report
            assert report["executed"] is False, report
            assert report["status"] == status, (source, report)
            assert result.returncode == {"complete": 0, "incomplete": 2, "invalid": 1}[status], report
            if status == "complete":
                assert not report["diagnostics"] and report["nil"] and report["instruction_count"] > 0, report
                if result_type is not None:
                    assert report["result_type"] == result_type, report
            else:
                assert report["nil"] is None and report["result_type"] is None, report
                assert report["diagnostics"], report
            checks += 1
            return report

        # These would fail or have effects if the checker ran their generated NIL.
        run("1 over 0", result_type="i32")
        run("[1, 2] at 1000", result_type="i32")
        for source, expected in [
            ("", "void"), ("!!! comment only\n", "void"),
            ("42", "i32"), ("2147483648", "i64"), ("negative 1.6", "f64"), ("-2147483648", "i32"),
            ("-9223372036854775808", "i64"), ("true and false", "bool"),
            ('"hello"', "text"), ("value amount is 40\namount plus 2", "i32"),
            ("value Amount is 2\nAMOUNT times 3", "i32"),
            ("value amount is 2\n", "void"), ("[1, 2, 3]", "array<i32>"),
            ("value (Frame-Count, other) is (1, 2)\nFRAME_COUNT", "i32"),
            ('value amount is 2\nformatted "amount: {amount}"', "text"),
            ("(when true give 1 otherwise give 2 end when)", "i32"),
            ("when true then\nvalue amount is 1\nend when", "void"),
            ("1 becomes floating", "f64"),
        ]:
            run(source, result_type=expected)

        for source in ["1 plus", "value answer is", '"unfinished',
                       "when true then\nvalue x is 1\n", "[1,", "(1,"]:
            run(source, status="incomplete")
        for source in ["1 2", "value x is 1\nvalue X is 2\nx", "unknown_name plus 1",
                       "-9223372036854775809", "1 plus true"]:
            run(source, status="invalid")
        for source in ['say "host output"', "evaluate dangerous with 1",
                       "variable x is 1", "value x is 1\nset x to 2",
                       "while true\nend while", "book demo", "use nil.std.io",
                       'when false then\nsay "dead effect"\nend when']:
            run(source, status="invalid")

        pairs = [
            ("40 plus 2", envelope(binary("plus", number(40), number(2)))),
            ("value amount is 40\namount plus 2",
             envelope(binary("plus", name("amount"), number(2)), [("amount", number(40))])),
            ("negative 1.6", envelope(number("-1.6"))),
            ("-2147483648", envelope(number("-2147483648"))),
            ("-9223372036854775808", envelope(number("-9223372036854775808"))),
            ('"hello"', envelope({"kind": "text", "value": "hello"})),
            ("[1, 2] at 0", envelope({"kind": "index", "target": {
                "kind": "array", "items": [number(1), number(2)]}, "index": number(0)})),
            ("(1, true)", envelope({"kind": "tuple", "items": [number(1),
                                                          {"kind": "boolean", "value": True}]})),
            ("(1, true).0", envelope({"kind": "member", "target": {
                "kind": "tuple", "items": [number(1), {"kind": "boolean", "value": True}]}, "index": "0"})),
            ("1 becomes floating", envelope({"kind": "cast", "value": number(1), "type": "f64"})),
            ("(when true give 1 otherwise give 2 end when)", envelope({
                "kind": "conditional", "condition": {"kind": "boolean", "value": True},
                "then": number(1), "otherwise": number(2)})),
            ("bitwise not 1", envelope({"kind": "unary", "operator": "bitwise-not", "operand": number(1)})),
            ("", envelope(None)),
        ]
        for source, request in pairs:
            human = run(source)
            machine = run(nildata(request), request=True)
            assert instructions(human["nil"]) == instructions(machine["nil"]), (source, human, machine)
            assert human["result_type"] == machine["result_type"], (human, machine)

        bad = envelope(number(1))
        bad["authority"] = "root"
        report = run(nildata(bad), request=True, status="invalid")
        assert report["diagnostics"][0]["location"] == '$["authority"]', report
        bad = envelope({"kind": "call", "name": "execute"})
        run(nildata(bad), request=True, status="invalid")
        bad = envelope(number(1))
        bad["profile"] = "matrix.unrestricted"
        run(nildata(bad), request=True, status="invalid")
        report = run(nildata(envelope({"kind": "array", "items": [
            binary("plus", number(1), {"kind": "boolean", "value": True})]})),
            request=True, status="invalid")
        assert report["diagnostics"][0]["stage"] == "type", report
        assert report["diagnostics"][0]["location"] == "$.value.items[0]", report
        report = run(nildata(envelope(name("missing"))), request=True, status="invalid")
        assert report["diagnostics"][0]["location"] == "$.value", report

        for source in [b"\xff", b"1\0", b"1\x1a invalid suffix", b"#" + b"x" * 65536,
                       b"(" * 80 + b"1" + b")" * 80,
                       b"1" + b" plus 1" * 3000]:
            run(source, status="invalid")
        for source in [b"\xff", b"nildata 1\nnull\0", b" " * 65537]:
            run(source, request=True, status="invalid")

        source_file = work / "input.mat"
        source_file.write_text("1 plus 2")
        run(path=source_file, result_type="i32")
        run(path=work, status="invalid")
        run(path=work / "missing", status="invalid")
        if os.name == "posix":
            link = work / "linked.mat"
            link.symlink_to(source_file)
            run(path=link, status="invalid")
            fifo = work / "fifo.mat"
            os.mkfifo(fifo)
            run(path=fifo, status="invalid")

        # Compilation switches cannot weaken or redirect the admission command.
        sentinel = work / "untouched.nil"
        sentinel.write_text("sentinel")
        for extra in [["--no-typecheck"], ["--native"], ["--jit"],
                      ["--output", str(sentinel)], ["--sdk-path", str(work)],
                      ["--check-submission-request", "-"]]:
            result = subprocess.run([compiler, "--check-submission", "-", *extra],
                                    input=b"1", cwd=work, capture_output=True, timeout=10)
            assert result.returncode != 0, (extra, result)
            assert sentinel.read_text() == "sentinel"
            checks += 1
        assert sorted(p.name for p in work.iterdir()) == sorted([
            "input.mat", "untouched.nil", *(["linked.mat", "fifo.mat"] if os.name == "posix" else [])])
    print(f"Matrix submission CLI: {checks} checks passed")


if __name__ == "__main__":
    main()
