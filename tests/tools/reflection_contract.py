#!/usr/bin/env python3
"""Consume compiler-generated reflection from a real Matrix analyzer."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--interpreter", type=Path, required=True)
    parser.add_argument("--stdlib", type=Path, required=True)
    parser.add_argument("--llvm", choices=("ON", "OFF"), default="OFF")
    args = parser.parse_args()
    args.compiler = args.compiler.resolve(strict=True)
    args.interpreter = args.interpreter.resolve(strict=True)
    args.stdlib = args.stdlib.resolve(strict=True)
    environment = os.environ.copy()
    for prefix in ("NY_", "NIL_"):
        for suffix in ("LIBRARY_PATH", "STDLIB_PATH", "LEX_DUMP", "PARSE_TRACE"):
            environment.pop(prefix + suffix, None)

    with tempfile.TemporaryDirectory(prefix="nil-reflection-") as temporary:
        work = Path(temporary)

        def run(command, *, source=None, status=0):
            result = subprocess.run([str(part) for part in command], cwd=work,
                                    env=environment, input=source, text=True,
                                    capture_output=True, timeout=60)
            assert result.returncode == status, (command, result.returncode, result.stdout, result.stderr)
            return result

        def analyze(path, *, destination=None, status=0):
            command = [args.compiler, "--analyze", "--no-stdlib", "-I", args.stdlib, path]
            if destination is not None:
                command.extend(("--emit-reflection", destination))
            result = run(command, status=status)
            if status:
                return result
            assert not result.stderr, result.stderr
            value = json.loads(result.stdout)
            assert value["success"] and not value["diagnostics"], value
            assert not (work / "analysis-unused.nil").exists()
            return value

        matrix = work / "target.matrix"
        matrix.write_text('''book demo.render
context "Draw a frame."
public temp-render receives width as override integer returns override integer begin paragraph
    give width
end paragraph
public point begin record
    field x as number
    field y as number
end record
''', encoding="utf-8")
        ny = work / "target.ny"
        ny.write_text('''module demo.render
@ai_context("Draw a frame.")
pub fn temp_render(width: i32) -> i32
    return width
end fn
pub struct point
    x: i64
    y: i64
end struct
''', encoding="utf-8")
        snapshot = work / "analysis_snapshot.ny"
        report = analyze(matrix, destination=snapshot)
        reflection = report["reflection"]
        assert reflection["schema"] == "nil.reflection.v1"
        assert reflection["semantic_checked"] and not reflection["truncated"]
        declarations = reflection["declarations"]
        assert [item["kind"] for item in declarations] == ["function", "parameter", "record", "field", "field"]
        assert declarations[0]["qualified_name"] == "demo::render::temp_render"
        assert declarations[1]["parent_id"] == 0 and declarations[3]["parent_id"] == 2
        assert all(item["semantic_checked"] and item["source_path"] == str(matrix)
                   for item in declarations)
        assert reflection["annotations"][0]["name"] == "ai_context"
        assert reflection["annotations"][0]["owner_id"] == 0
        assert reflection["annotations"][0]["arguments_parsed"]
        assert reflection["annotations"][0]["parsed_arguments"] == [
            {"name": None, "kind": "text", "value": "Draw a frame."}]
        keys = ("kind", "name", "qualified_name", "type_name", "parent_id", "is_public", "semantic_checked")
        twin = analyze(ny)["reflection"]["declarations"]
        assert [[item[key] for key in keys] for item in declarations] == [[item[key] for key in keys] for item in twin]

        routes = [False, True] if args.llvm == "ON" else [False]

        def execute(source: Path, native: bool, *, should_panic=False):
            output = work / ("program-native" if native else "program.nil")
            command = [args.compiler, "--quiet", "--no-stdlib", "--no-bundle",
                       "-I", args.stdlib, "-I", work, "-o", output, source]
            if native:
                command.append("--native")
            compiled = run(command)
            assert not compiled.stdout and not compiled.stderr, (compiled.stdout, compiled.stderr)
            command = [output] if native else [args.interpreter, output]
            result = subprocess.run([str(part) for part in command], cwd=work,
                                    env=environment, capture_output=True, text=True, timeout=60)
            if should_panic:
                assert result.returncode != 0 and not result.stdout, result
                assert "Invalid reflection declaration ID" in result.stderr, result.stderr
            else:
                assert result.returncode == 0 and not result.stderr, result
            return result.stdout

        for native in routes:
            output = execute(ROOT / "nilstd/examples/analyzer.matrix", native)
            assert json.loads(output) == {
                "code": "EXAMPLE001", "severity": "warning",
                "message": "Public functions should use a lasting name.",
                "path": str(matrix), "line": 3,
            }
            for index in (-1, len(declarations)):
                invalid = work / "invalid.ny"
                invalid.write_text("import analysis_snapshot\nfn main() -> i32\n"
                                   f" let rejected = declaration({index})\n"
                                   " println(rejected.name)\n return 0\nend fn\n", encoding="utf-8")
                execute(invalid, native, should_panic=True)

        properties = work / "properties.ny"
        properties.write_text('''@custom("label", enabled=true, count=-3, ratio=1.5, provider=io.console.write)
pub fn described() -> i32
 return 1
end fn
@ai_context(intent="legacy", assumptions=["preserved"])
pub fn legacy() -> i32
 return 2
end fn
''', encoding="utf-8")
        property_report = analyze(properties, destination=snapshot)["reflection"]
        attribute = property_report["annotations"][0]
        assert attribute["line"] == 1 and attribute["arguments_parsed"], attribute
        assert attribute["parsed_arguments"] == [
            {"name": None, "kind": "text", "value": "label"},
            {"name": "enabled", "kind": "boolean", "value": "true"},
            {"name": "count", "kind": "integer", "value": "-3"},
            {"name": "ratio", "kind": "floating", "value": "1.5"},
            {"name": "provider", "kind": "symbol", "value": "io.console.write"},
        ], attribute
        opaque = property_report["annotations"][1]
        assert not opaque["arguments_parsed"] and opaque["parsed_arguments"] == [], opaque
        assert 'assumptions=["preserved"]' in opaque["arguments"], opaque
        property_consumer = work / "properties_consumer.ny"
        property_consumer.write_text('''import analysis_snapshot
fn main() -> i32
 if not annotation_arguments_parsed(0) then
  panic("literal annotation arguments unavailable")
 end if
 if annotation_arguments_parsed(1) then
  panic("legacy metadata must remain opaque")
 end if
 println(annotation_argument_count())
 println(annotation_argument(0).value)
 println(annotation_argument(1).name)
 println(annotation_argument(1).value)
 println(annotation_argument(3).kind)
 println(annotation_argument(4).value)
 return 0
end fn
''', encoding="utf-8")
        matrix_property_consumer = work / "properties_consumer.matrix"
        matrix_property_consumer.write_text('''book property-consumer
read everything from nil.std.reflection
read everything from analysis_snapshot

main returns override integer begin paragraph
    when not (analysis_snapshot::annotation-arguments-parsed with id being 0) then
        evaluate panic with "literal annotation arguments unavailable"
    end when
    when analysis_snapshot::annotation-arguments-parsed with id being 1 then
        evaluate panic with "legacy metadata must remain opaque"
    end when
    say analysis_snapshot::annotation-argument-count with nothing
    value positional is analysis_snapshot::annotation-argument with id being 0
    value enabled-property is analysis_snapshot::annotation-argument with id being 1
    value ratio-property is analysis_snapshot::annotation-argument with id being 3
    value provider-property is analysis_snapshot::annotation-argument with id being 4
    say positional.`value`
    say enabled-property.name
    say enabled-property.`value`
    say ratio-property.kind
    say provider-property.`value`
    give 0
end paragraph
''', encoding="utf-8")
        for native in routes:
            for consumer in (property_consumer, matrix_property_consumer):
                assert execute(consumer, native) == "5\nlabel\nenabled\ntrue\nfloating\nio.console.write\n"

        included = work / "included.ny"
        included.write_text('@ai_context("included declaration")\npub fn included() -> i32\n return 1\nend fn\n', encoding="utf-8")
        owner = work / "owner.ny"
        owner.write_text('module included_owner\n@include "included.ny"\nfn main() -> i32\n return included()\nend fn\n', encoding="utf-8")
        expanded = analyze(owner)["reflection"]
        assert expanded["declarations"] and expanded["annotations"]
        assert all(item["line"] == 0 for item in expanded["declarations"] + expanded["annotations"])

        # A short input must not amplify into unbounded retained qualified names.
        large = work / "bounded.ny"
        long_name = "record_" + "x" * 16384
        large.write_text("pub struct " + long_name + "\n" +
                         "".join(f" field_{index}: i32\n" for index in range(128)) +
                         "end struct\n", encoding="utf-8")
        bounded = analyze(large)["reflection"]
        assert bounded["semantic_checked"] and bounded["truncated"]
        assert 1 <= len(bounded["declarations"]) < 129
        assert len(json.dumps(bounded).encode()) < 2 * 1024 * 1024
        rejected = work / "truncated_snapshot.ny"
        result = analyze(large, destination=rejected, status=2)
        assert "snapshot exceeds the export limit" in result.stderr and not rejected.exists()

        empty = work / "empty.ny"
        empty.write_text("module empty\n", encoding="utf-8")
        assert analyze(empty, destination=snapshot)["reflection"]["declarations"] == []
        invalid = work / "invalid.ny"
        invalid.write_text("import analysis_snapshot\nfn main() -> i32\n let rejected = declaration(0)\n return 0\nend fn\n", encoding="utf-8")
        for native in routes:
            execute(invalid, native, should_panic=True)

    print("reflection metadata, bounded export, source provenance, Matrix analyzer and ID checks passed")


if __name__ == "__main__":
    main()
