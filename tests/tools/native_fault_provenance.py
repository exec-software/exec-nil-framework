#!/usr/bin/env python3
"""Actual fault identities refine matching without weakening NIL admission."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile

from native_fault_pipeline import instruction as ins


TYPES = ("left::Problem", "left__Problem")


def raise_fault(name: str = TYPES[0]) -> list:
    return [ins("NEW_STRUCT", name), ins("PUSH_CONST", "7"),
            ins("SET_FIELD", "value"), ins("FAULT_RAISE", name)]


def guarded_body(kind: str) -> list:
    body = [ins("FAULT_ENTER", "body", "dispatch")]
    if kind in ("unknown-call", "declared-panic-call"):
        body += [ins("CALL", "panic" if kind == "declared-panic-call" else "explode", "0"), ins("FAULT_LEAVE", "body"),
                 ins("PUSH_CONST", "0"), ins("RET")]
    elif kind == "mixed-types":
        body += [ins("TIME_NOW"), ins("PUSH_CONST", "0"), ins("CMP_GT"),
                 ins("JMP_IF_NOT", "other"), *raise_fault(), ins("LABEL", "other"),
                 *raise_fault(TYPES[1])]
    elif kind in ("resume", "rethrow"):
        body += [ins("FAULT_ENTER", "inner", "inner_dispatch"), *raise_fault(),
                 ins("LABEL", "inner_dispatch"), ins("FAULT_SAVE", "inner_saved"),
                 ins("FAULT_RESUME" if kind == "resume" else "FAULT_RETHROW", "inner_saved")]
    elif kind in ("dead-literal-false", "dead-literal-true", "live-literal-false", "live-literal-true", "dynamic-call"):
        if kind == "dynamic-call":
            body += [ins("TIME_NOW"), ins("PUSH_CONST", "0"), ins("CMP_GT")]
            branch = "JMP_IF_NOT"
        else:
            true = kind.endswith("true")
            body += [ins("PUSH_CONST", "1" if true else "0")]
            branch = "JMP_IF" if (true == kind.startswith("dead")) else "JMP_IF_NOT"
        body += [ins(branch, "skip_call"), ins("CALL", "explode", "0"),
                 ins("LABEL", "skip_call"), *raise_fault()]
    else:
        body += raise_fault()
    body += [ins("LABEL", "dispatch"), ins("FAULT_SAVE", "saved")]
    if kind == "real-uninitialized-read":
        # Allocate the local in the IR, but leave it unassigned on the actual
        # path. A proven handler match must not hide its reachable early read.
        body += [ins("PUSH_CONST", "0"), ins("JMP_IF_NOT", "check_match"),
                 ins("PUSH_CONST", "42"), ins("STORE_LOCAL", "selected"),
                 ins("LABEL", "check_match")]
    if kind == "cross-type":
        # A same-looking sanitized name must remain a distinct nominal type.
        body += [ins("FAULT_MATCH", "saved", TYPES[1]), ins("JMP_IF", "wrong_type"),
                 ins("PUSH_CONST", "42"), ins("STORE_LOCAL", "selected"), ins("JMP", "join"),
                 ins("LABEL", "wrong_type"), ins("LOAD_LOCAL", "selected"), ins("POP"),
                 ins("LABEL", "join")]
    else:
        body += [ins("FAULT_MATCH", "saved", TYPES[0]), ins("JMP_IF_NOT", "miss")]
        if kind == "guarded":
            body += [ins("CALL", "guard", "0"), ins("JMP_IF_NOT", "miss")]
        if kind == "real-uninitialized-read":
            body += [ins("LOAD_LOCAL", "selected"), ins("POP")]
        body += [ins("PUSH_CONST", "42"), ins("STORE_LOCAL", "selected"),
                 ins("JMP", "join"), ins("LABEL", "miss"),
                 ins("LOAD_LOCAL", "selected"), ins("POP"), ins("LABEL", "join")]
    return body + [ins("FAULT_DROP", "saved"), ins("LOAD_LOCAL", "selected"),
                   ins("CALL", "println", "1"), ins("PUSH_CONST", "0"), ins("RET")]


def empty_body_invalid_landing() -> list:
    return [ins("FAULT_ENTER", "body", "dispatch"), ins("FAULT_LEAVE", "body"),
            ins("PUSH_CONST", "0"), ins("RET"), ins("LABEL", "dispatch"),
            ins("FAULT_SAVE", "saved"), ins("FAULT_MATCH", "absent", TYPES[0]),
            ins("POP"), ins("FAULT_DROP", "saved"), ins("PUSH_CONST", "0"), ins("RET")]


def terminal_panic() -> list:
    return [ins("JMP", "body"), ins("LABEL", "dispatch"), ins("FAULT_SAVE", "saved"),
            ins("FAULT_DROP", "saved"), ins("PUSH_CONST", "0"), ins("RET"),
            ins("LABEL", "body"), ins("FAULT_ENTER", "region", "dispatch"),
            ins("FAULT_LEAVE", "region"), ins("PUSH_CONST_STR", "panic_message"),
            ins("CALL", "panic", "1")]


def module(functions: list, binary: bool) -> bytes:
    if not binary:
        text = 'module fault_provenance\nconst panic_message = "terminal control"\n'
        for name in TYPES:
            text += f"struct {name}\nvalue: i64\nend struct\n"
        for name, result, body in functions:
            text += f"function {name} -> {result}:\n"
            for opcode, arguments, line in body:
                text += "    " + " ".join((opcode, *arguments)) + f" ; line {line}\n"
            text += "end function\n"
        return text.encode("utf-8")

    def u32(value: int) -> bytes:
        return struct.pack("<I", value)

    def string(value: str) -> bytes:
        encoded = value.encode("utf-8")
        return u32(len(encoded)) + encoded

    schemas = u32(len(TYPES))
    for name in TYPES:
        schemas += string(name) + u32(1) + string("value") + string("i64")
    schemas += u32(0)
    bodies = u32(len(functions))
    for name, result, body in functions:
        bodies += string(name) + u32(0) + u32(0) + string(result)
        bodies += u32(0) + u32(0) + u32(0) + u32(len(body))
        for opcode, arguments, line in body:
            bodies += string(opcode) + u32(len(arguments))
            bodies += b"".join(string(argument) for argument in arguments) + u32(line)
    constants = u32(1) + string("panic_message") + string("terminal control")
    sections = [(1, schemas), (2, bodies), (3, constants),
                (4, u32(0)), (5, string("fault_provenance") + u32(0))]
    offset = 16 + 20 * len(sections)
    table = b""
    for kind, payload in sections:
        table += struct.pack("<IQQ", kind, offset, len(payload))
        offset += len(payload)
    return b"NIL\0" + struct.pack("<III", 3, 0, len(sections)) + table + b"".join(
        payload for _, payload in sections)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("assembler", "linker", "runtime", "interpreter"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    tools = {name: str(getattr(args, name).resolve(strict=True))
             for name in ("assembler", "linker", "runtime", "interpreter")}
    if args.output:
        args.output.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="nil-fault-provenance-", dir=args.output))
    environment = {key: value for key, value in os.environ.items() if not key.startswith("NY_")}
    report = {"work": str(work), "commands": [], "positive_routes": [], "rejections": [], "compilation_controls": []}

    def run(label: str, command: list[str]) -> subprocess.CompletedProcess:
        result = subprocess.run(command, cwd=work, env=environment,
                                stdin=subprocess.DEVNULL, capture_output=True, timeout=60)
        (work / (label + ".stdout")).write_bytes(result.stdout)
        (work / (label + ".stderr")).write_bytes(result.stderr)
        report["commands"].append({"name": label, "argv": command, "exit_code": result.returncode})
        return result

    def success(result: subprocess.CompletedProcess, label: str, expected: bytes | None = None) -> None:
        stdout = result.stdout.replace(b"\r\n", b"\n")
        assert result.returncode == 0 and (expected is None or stdout == expected) and not result.stderr, (
            label, result.returncode, result.stdout, result.stderr)

    helpers = [("explode", "void", raise_fault()),
               ("guard", "bool", [ins("PUSH_CONST", "0"), ins("CAST", "bool"), ins("RET")])]
    cases = [(name, guarded_body(name), True, False)
             for name in ("exact", "cross-type", "resume", "rethrow", "dead-literal-false", "dead-literal-true")]
    cases += [(name, guarded_body(name), False, False)
              for name in ("unknown-call", "declared-panic-call", "mixed-types", "guarded", "real-uninitialized-read",
                           "live-literal-false", "live-literal-true", "dynamic-call")]
    cases += [("synthetic-landing-admission", empty_body_invalid_landing(), False, True)]
    invalid_miss = guarded_body("exact")
    miss = next(i for i, instruction in enumerate(invalid_miss)
                if instruction[0] == "LABEL" and instruction[1] == ("miss",))
    invalid_miss.insert(miss + 1, ins("FAULT_DROP", "absent"))
    cases += [("unreachable-arm-admission", invalid_miss, False, True)]
    try:
        for name, body, accepted, admission in cases:
            for binary in (False, True):
                label = name + ("-binary" if binary else "-text")
                path = work / (label + ".nil")
                declarations = helpers + ([("panic", "void", [ins("RET")])]
                    if name == "declared-panic-call" else [])
                path.write_bytes(module(declarations + [("main", "i32", body)], binary))
                if accepted:
                    success(run(label + "-vm", [tools["interpreter"], "--no-ai", str(path)]), label, b"42\n")
                    report["positive_routes"].append(label + "-vm")
                elif admission:
                    result = run(label + "-vm", [tools["interpreter"], "--no-ai", str(path)])
                    assert result.returncode > 0 and b"saved fault slot is absent" in result.stderr, (label, result)
                    report["rejections"].append(label + "-vm")
                for unmanaged in (False, True):
                    mode = label + ("-unmanaged" if unmanaged else "-managed")
                    obj = work / (mode + ".o")
                    result = run(mode + "-assemble", [tools["assembler"],
                        *(["--unmanaged"] if unmanaged else []), "-o", str(obj), str(path)])
                    if accepted:
                        success(result, mode)
                        exe = work / (mode + (".exe" if os.name == "nt" else ""))
                        success(run(mode + "-link", [tools["linker"], "--runtime", tools["runtime"],
                            "-o", str(exe), str(obj)]), mode)
                        success(run(mode + "-run", [str(exe)]), mode, b"42\n")
                        report["positive_routes"].append(mode)
                    else:
                        expected = b"saved fault slot is absent" if admission else b"not initialized on every incoming path"
                        assert result.returncode > 0 and expected in result.stderr and not obj.exists(), (mode, result)
                        report["rejections"].append(mode)
        for binary in (False, True):
            label = "terminal-panic-" + ("binary" if binary else "text")
            path = work / (label + ".nil")
            path.write_bytes(module([("main", "i32", terminal_panic())], binary))
            for unmanaged in (False, True):
                mode = label + ("-unmanaged" if unmanaged else "-managed")
                obj = work / (mode + ".o")
                success(run(mode, [tools["assembler"], *(["--unmanaged"] if unmanaged else []),
                    "-o", str(obj), str(path)]), mode)
                report["compilation_controls"].append(mode)
        report["success"] = True
    finally:
        (work / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"fault provenance: {len(report['positive_routes'])} routes, {len(report['rejections'])} rejections, "
          f"{len(report['compilation_controls'])} terminal controls passed; {work}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
