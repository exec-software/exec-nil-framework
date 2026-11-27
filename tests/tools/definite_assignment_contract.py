#!/usr/bin/env python3
"""Source-time definite assignment, lexical scope and reachable-exit contracts."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import textwrap

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests"))
from native_output_staging import linked_executable_path

CASES = {
    "text-read": '''
        @DECL@let blank: text
        @READ@println(blank)
    ''',
    "integer-read": '''
        @DECL@let blank: i32
        @READ@println(blank)
    ''',
    "boolean-read": '''
        @DECL@let blank: bool
        @READ@println(blank)
    ''',
    "assignment-needs-prior-value": '''
        @DECL@let mut blank: i32
        @READ@blank = blank + 1
    ''',
    "one-branch": '''
        @DECL@let mut blank: text
        if flag then
            blank = "one"
        end if
        @READ@println(blank)
    ''',
    "constant-false-does-not-initialize": '''
        @DECL@let mut blank: text
        if false then
            blank = "one"
        end if
        @READ@println(blank)
    ''',
    "computed-condition-is-not-proven": '''
        @DECL@let mut blank: text
        if flag == flag then
            blank = "one"
        end if
        @READ@println(blank)
    ''',
    "sibling-branch": '''
        @DECL@let mut blank: text
        if flag then
            blank = "one"
        else
            @READ@println(blank)
        end if
    ''',
    "zero-trip-while": '''
        @DECL@let mut blank: text
        while flag
            blank = "one"
            break
        end while
        @READ@println(blank)
    ''',
    "first-loop-iteration": '''
        @DECL@let mut blank: text
        loop
            @READ@println(blank)
            blank = "one"
            break
        end loop
    ''',
    "break-before-assignment": '''
        @DECL@let mut blank: text
        loop
            if flag then
                break
            end if
            blank = "one"
            break
        end loop
        @READ@println(blank)
    ''',
    "continue-does-not-certify-break": '''
        @DECL@let mut blank: text
        loop
            if flag then
                break
            end if
            blank = "one"
            continue
        end loop
        @READ@println(blank)
    ''',
    "zero-trip-for": '''
        @DECL@let mut blank: text
        for value in [1, 2]
            blank = "one"
        end for
        @READ@println(blank)
    ''',
    "loop-binding-shadow": '''
        @DECL@let blank: text
        for blank in ["inner"]
            println(blank)
        end for
        @READ@println(blank)
    ''',
    "inner-initializer-does-not-write-outer": '''
        @DECL@let blank: text
        if true then
            let blank: text = "inner"
            println(blank)
        end if
        @READ@println(blank)
    ''',
    "inner-uninitialized-shadows-outer": '''
        let blank: text = "outer"
        if true then
            @DECL@let blank: text
            @READ@println(blank)
        end if
    ''',
    "switch-without-default": '''
        @DECL@let mut blank: text
        switch 1
            case 1:
                blank = "one"
                break
        end switch
        @READ@println(blank)
    ''',
    "switch-partial-break": '''
        @DECL@let mut blank: text
        switch 1
            case 1:
                break
            default:
                blank = "one"
        end switch
        @READ@println(blank)
    ''',
    "switch-direct-case-entry": '''
        @DECL@let mut blank: text
        switch 1
            case 1:
                blank = "one"
            default:
                @READ@println(blank)
        end switch
    ''',
    "guarded-pattern-is-not-exhaustive": '''
        @DECL@let mut blank: text
        match 1
            _ when flag -> blank = "one"
        end match
        @READ@println(blank)
    ''',
    "uninitialized-array-receiver": '''
        @DECL@let mut blank: array<i32>
        @READ@blank[0] = 1
    ''',
    "uninitialized-record-receiver": '''
        @DECL@let mut blank: AssignmentFault
        @READ@blank.code = 1
    ''',
    "uninitialized-pointer": '''
        @DECL@let blank: ptr<i32>
        @READ@println(*blank)
    ''',
    "uninitialized-reference": '''
        @DECL@let blank: i32
        @READ@let address = &blank
    ''',
    "fault-before-assignment": '''
        @DECL@let mut blank: text
        attempt
            can_fail()
            blank = "one"
        fault as problem
            @READ@println(blank)
        end attempt
    ''',
    "handler-only-assignment": '''
        @DECL@let mut blank: text
        attempt
            can_fail()
        fault as problem
            blank = "handled"
        end attempt
        @READ@println(blank)
    ''',
    "recovery-on-early-return": '''
        @DECL@let mut blank: text
        attempt
            if flag then
                return 0
            end if
            blank = "one"
        recover
            @READ@println(blank)
        end attempt
    ''',
    "short-circuit-does-not-initialize": '''
        @DECL@let mut blank: bool
        let accepted = flag && (blank = true)
        @READ@println(blank)
    ''',
    "uninitialized-lambda-capture": '''
        @DECL@let blank: text
        let callback = fn() -> text
            @READ@return blank
        end fn
    ''',
    "expression-match-value-pattern": '''
        @DECL@let blank: i32
        @READ@println(match 1 { blank -> 2; _ -> 3; })
    ''',
    "expression-match-call-argument": '''
        @DECL@let blank: i32
        @READ@println(match 1 { identity(blank) -> 2; _ -> 3; })
    ''',
    "expression-match-field-receiver": '''
        @DECL@let blank: AssignmentFault
        @READ@println(match 1 { blank.code -> 2; _ -> 3; })
    ''',
    "expression-match-guard": '''
        @DECL@let blank: bool
        @READ@println(match 1 { 1 when blank -> 2; _ -> 3; })
    ''',
    "boolean-match-partial": '''
        @DECL@let mut blank: text
        match flag
            true -> blank = "one"
        end match
        @READ@println(blank)
    ''',
    "boolean-match-guarded": '''
        @DECL@let mut blank: text
        match flag
            true -> blank = "one"
            false when flag -> blank = "two"
        end match
        @READ@println(blank)
    ''',
    "enum-match-duplicate": '''
        @DECL@let mut blank: text
        match AssignmentState.Ready
            AssignmentState.Ready -> blank = "one"
            AssignmentState.Ready -> blank = "two"
        end match
        @READ@println(blank)
    ''',
    "enum-match-guarded": '''
        @DECL@let mut blank: text
        match AssignmentState.Ready
            AssignmentState.Ready -> blank = "one"
            AssignmentState.Idle when flag -> blank = "two"
        end match
        @READ@println(blank)
    ''',
    "tuple-pattern-literal": '''
        @DECL@let mut blank: text
        match (1, 2)
            (1, second) -> blank = "one"
        end match
        @READ@println(blank)
    ''',
    "guarded-typed-handler-recovery": '''
        @DECL@let mut blank: text
        attempt
            raise AssignmentFault { code: 1 }
        fault AssignmentFault as problem when flag
            blank = "one"
        recover
            @READ@println(blank)
        end attempt
    ''',
    "different-typed-handler-recovery": '''
        @DECL@let mut blank: text
        attempt
            raise OtherFault { code: 1 }
        fault AssignmentFault as problem
            blank = "one"
        recover
            @READ@println(blank)
        end attempt
    ''',
    "unknown-call-typed-handler-recovery": '''
        @DECL@let mut blank: text
        attempt
            can_fail()
            blank = "normal"
        fault AssignmentFault as problem
            blank = "one"
        recover
            @READ@println(blank)
        end attempt
    ''',
}
PREFIX = """struct AssignmentFault
    code: i32
end struct
struct OtherFault
    code: i32
end struct
enum AssignmentState
    Ready
    Idle
end enum
fn can_fail()
    raise AssignmentFault { code: 1 }
end fn
fn identity(value: i32) -> i32
    return value
end fn
fn probe(flag: bool) -> i32
"""
SUFFIX = "\nreturn 0\nend fn\nfn main() -> i32\nreturn probe(true)\nend fn\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--interpreter", type=Path, required=True)
    parser.add_argument("--assembler", type=Path)
    parser.add_argument("--llvm", choices=("ON", "OFF"), required=True)
    parser.add_argument("--output", type=Path,
                        help="retain generated cases and raw results in a fresh child")
    args = parser.parse_args()
    compiler, interpreter = args.compiler.resolve(), args.interpreter.resolve()
    if args.output:
        args.output.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="nil-definite-assignment-", dir=args.output))
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(("NIL_", "NY_"))}
    report = {"status": "FAIL", "commands": [], "negative_cases": [], "positive_routes": [],
              "native_admission": []}

    def run(label, argv, timeout=60):
        result = subprocess.run([str(value) for value in argv], cwd=work,
            capture_output=True, timeout=timeout, stdin=subprocess.DEVNULL, env=environment)
        (work / (label + ".stdout")).write_bytes(result.stdout)
        (work / (label + ".stderr")).write_bytes(result.stderr)
        report["commands"].append({"name": label, "argv": list(map(str, argv)),
            "exit_code": result.returncode})
        return result

    try:
        for name, body in CASES.items():
            source = PREFIX + textwrap.dedent(body).strip() + SUFFIX
            lines = source.splitlines()
            declaration = next(index for index, line in enumerate(lines, 1) if "@DECL@" in line)
            read = next(index for index, line in enumerate(lines, 1) if "@READ@" in line)
            source = source.replace("@DECL@", "").replace("@READ@", "")
            path = work / (name + ".ny")
            path.write_text(source, encoding="utf-8", newline="\n")
            output = work / (name + ".nil")
            result = run(name, [compiler, "--quiet", "--no-stdlib", "--no-bundle",
                               "--output", output, path])
            diagnostic = "Local 'blank' may be read before definite assignment"
            assert result.returncode > 0 and diagnostic.encode() in result.stderr, (name, result)
            assert not output.exists(), (name, "rejected source produced NIL")
            analyzed = run(name + "-analysis", [compiler, "--analyze", "--no-stdlib", path])
            parsed = json.loads(analyzed.stdout)
            assert analyzed.returncode > 0 and parsed["success"] is False, (name, analyzed)
            found = [row for row in parsed["diagnostics"] if diagnostic in row["message"]]
            assert found, (name, parsed)
            assert any(source.encode()[:row["start"]].count(b"\n") + 1 == read and
                       Path(row["path"]).resolve() == path.resolve() and
                       "declared at line " + str(declaration) + ")" in row["message"] for row in found), (name, found, read, declaration)
            report["negative_cases"].append(name)
        # Each unknown call can fault. Recovery must merge equivalent outgoing
        # destinations instead of multiplying work at each nested attempt.
        nested_body = "\n".join(["tick()"] * 8)
        for _ in range(12):
            nested_body = "attempt\n" + nested_body + "\nrecover\n" + "\n".join(["tick()"] * 4) + "\nend attempt"
        nested = work / "nested-recovery.ny"
        nested.write_text("fn tick() -> i32\nreturn 1\nend fn\nfn main() -> i32\n" +
            nested_body + "\nreturn 0\nend fn\n", encoding="utf-8", newline="\n")
        analyzed = run("nested-recovery", [compiler, "--analyze", "--no-stdlib", nested], timeout=10)
        assert analyzed.returncode == 0 and json.loads(analyzed.stdout)["success"] is True, analyzed
        report["nested_recovery_depth"] = 12
        for fixture in ("definite-assignment", "definite-assignment-matrix"):
            suffix = ".matrix" if fixture.endswith("-matrix") else ".ny"
            path = ROOT / "tests/language" / (fixture + suffix)
            expected = (path.with_suffix(".stdout")).read_bytes()
            for encoding in ("text", "binary"):
                output = work / (fixture + "-" + encoding + ".nil")
                result = run(fixture + "-" + encoding + "-compile", [compiler,
                    "--quiet", "--no-stdlib", "--no-bundle",
                    *(["--emit-binary-nil"] if encoding == "binary" else []),
                    "--output", output, path])
                assert result.returncode == 0 and result.stderr == b"", result
                result = run(fixture + "-" + encoding + "-run", [interpreter, "--no-ai", output])
                assert result.returncode == 0 and result.stdout.replace(b"\r\n", b"\n") == expected and result.stderr == b"", result
                report["positive_routes"].append(fixture + "-" + encoding)
                if encoding == "text" and args.llvm == "ON":
                    assert args.assembler, "--assembler is required with --llvm ON"
                    result = run(fixture + "-nil-admission", [args.assembler.resolve(),
                        "-o", work / (fixture + "-from-nil.o"), output])
                    assert result.returncode == 0, result
                    report["native_admission"].append(fixture + "-source-nil")
            if args.llvm == "ON":
                for route in ("aot", "jit"):
                    output = work / (fixture + "-" + route)
                    result = run(fixture + "-" + route + "-compile", [compiler,
                        "--quiet", "--no-stdlib", "--native",
                        *(["--jit"] if route == "jit" else []), "--output", output, path])
                    assert result.returncode == 0 and result.stderr == b"", result
                    if route == "aot":
                        assert result.stdout == b"", result
                        result = run(fixture + "-aot-run", [linked_executable_path(output)])
                    assert result.returncode == 0 and result.stdout.replace(b"\r\n", b"\n") == expected and result.stderr == b"", result
                    report["positive_routes"].append(fixture + "-" + route)
        # Matrix continues to reject declarations without values at its grammar
        # boundary; it must not acquire an implicit default through the shared pass.
        matrix = work / "matrix-missing-initializer.matrix"
        matrix.write_text("main returns number begin paragraph\nvariable blank as text\nsay blank\ngive 0\nend paragraph\n")
        result = run("matrix-missing-initializer", [compiler, "--quiet", "--no-stdlib",
            "--no-bundle", "--output", work / "matrix-missing.nil", matrix])
        assert result.returncode > 0 and b"expected 'is'" in result.stderr, result
        if args.llvm == "ON":
            assert args.assembler, "--assembler is required with --llvm ON"
            native_cases = {
                "constant-true": (True,
                    "PUSH_CONST 1\nJMP_IF_NOT use\nPUSH_CONST 7\nSTORE_LOCAL value\n"
                    "LABEL use\nLOAD_LOCAL value\nRET"),
                "constant-false": (True,
                    "PUSH_CONST 0\nJMP_IF_NOT assign\nJMP use\nLABEL assign\n"
                    "PUSH_CONST 7\nSTORE_LOCAL value\nLABEL use\nLOAD_LOCAL value\nRET"),
                "constant-edge-into-live-join": (True,
                    "LOAD_LOCAL flag\nJMP_IF_NOT assign\nPUSH_CONST 1\nJMP_IF_NOT use\n"
                    "LABEL assign\nPUSH_CONST 7\nSTORE_LOCAL value\n"
                    "LABEL use\nLOAD_LOCAL value\nRET"),
                "dynamic-branch": (False,
                    "LOAD_LOCAL flag\nJMP_IF_NOT use\nPUSH_CONST 7\nSTORE_LOCAL value\n"
                    "LABEL use\nLOAD_LOCAL value\nRET"),
                "computed-dynamic-branch": (False,
                    "LOAD_LOCAL flag\nPUSH_CONST 0\nCMP_EQ\nJMP_IF_NOT use\n"
                    "PUSH_CONST 7\nSTORE_LOCAL value\nLABEL use\nLOAD_LOCAL value\nRET"),
                "first-iteration": (False,
                    "JMP test\nLABEL body\nPUSH_CONST 7\nSTORE_LOCAL value\n"
                    "LABEL test\nLOAD_LOCAL value\nPOP\nLOAD_LOCAL flag\n"
                    "JMP_IF body\nPUSH_CONST 0\nRET"),
            }
            for name, (accepted, body) in native_cases.items():
                source = work / ("admission-" + name + ".nil")
                source.write_text("module assignment_native\nfunction probe(flag: i32) -> i32:\n" +
                    body + "\nend function\nfunction main() -> i32:\nPUSH_CONST 1\n"
                    "CALL probe 1\nRET\nend function\n", encoding="utf-8", newline="\n")
                output = source.with_suffix(".o")
                sentinel = b"prior native output\n"
                if not accepted:
                    output.write_bytes(sentinel)
                result = run("admission-" + name,
                    [args.assembler.resolve(), "-o", output, source])
                if accepted:
                    assert result.returncode == 0 and output.stat().st_size > 0, result
                else:
                    assert result.returncode > 0 and b"not initialized on every incoming path" in result.stderr, result
                    assert output.read_bytes() == sentinel, (name, "rejection replaced prior output")
                report["native_admission"].append(name)
        report["status"] = "PASS"
    except BaseException as error:
        report["error"] = str(error)
        raise
    finally:
        (work / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        print(report["status"] + ": " + str(work / "report.json"))
    print(f"definite assignment: {len(CASES)} rejection cases, source/analysis diagnostics, "
          f"{len(report['positive_routes'])} positive routes and Matrix grammar control passed")


if __name__ == "__main__":
    main()
