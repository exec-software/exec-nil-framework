#!/usr/bin/env python3
"""Finite framed-protocol checks for document-local Ny semantic editor support."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import unittest


LSP: Path
URI = "file:///ny-protocol/current.ny"


def position(source: str, marker: str) -> dict:
    offset = source.index(marker)
    before = source[:offset]
    return {"line": before.count("\n"),
            "character": len(before.rsplit("\n", 1)[-1].encode("utf-16-le")) // 2}


def query(method: str, source: str, marker: str, identifier: int = 2) -> dict:
    return {"jsonrpc": "2.0", "id": identifier, "method": f"textDocument/{method}",
            "params": {"textDocument": {"uri": URI}, "position": position(source, marker)}}


def opened(source: str, uri: str = URI, version: int = 1) -> dict:
    return {"jsonrpc": "2.0", "method": "textDocument/didOpen",
            "params": {"textDocument": {"uri": uri, "languageId": "ny", "version": version, "text": source}}}


def session(messages: list[dict], *, ascii_json: bool = False) -> tuple[dict, list[dict]]:
    requests = [{"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}}]
    requests += messages
    requests += [{"jsonrpc": "2.0", "id": 999, "method": "shutdown"},
                 {"jsonrpc": "2.0", "method": "exit"}]
    wire = bytearray()
    for message in requests:
        payload = json.dumps(message, ensure_ascii=ascii_json).encode("utf-8")
        wire += f"Content-Length: {len(payload)}\r\n\r\n".encode("ascii") + payload
    result = subprocess.run([str(LSP)], input=bytes(wire), capture_output=True, timeout=10, check=False)
    if result.returncode != 0 or result.stderr:
        raise AssertionError(f"nillsp failed: {result.returncode}, {result.stderr!r}")
    remaining = result.stdout
    responses = {}
    notifications = []
    while remaining:
        header, separator, remaining = remaining.partition(b"\r\n\r\n")
        if not separator:
            raise AssertionError(f"unframed stdout: {header!r}")
        fields = dict(line.split(b":", 1) for line in header.split(b"\r\n"))
        count = int(fields[b"Content-Length"].strip())
        payload, remaining = remaining[:count], remaining[count:]
        message = json.loads(payload)
        if "error" in message:
            raise AssertionError(message)
        if "id" in message:
            if message["id"] in responses:
                raise AssertionError("duplicate response identifier")
            responses[message["id"]] = message.get("result")
        else:
            notifications.append(message)
    expected = {message["id"] for message in requests if "id" in message}
    if set(responses) != expected:
        raise AssertionError(f"response identifiers: {set(responses)} != {expected}")
    return responses, notifications


def completion(source: str, marker: str = "|", *, extra: list[dict] | None = None,
               ascii_json: bool = False) -> tuple[dict, list[dict]]:
    request = query("completion", source, marker)
    source = source.replace(marker, "")
    replies, notes = session((extra or []) + [opened(source), request], ascii_json=ascii_json)
    return {item["label"]: item for item in replies[2]["items"]}, notes


class SemanticProtocol(unittest.TestCase):
    def test_reference_parameter_name_and_definition(self) -> None:
        for modifier in ('ref', 'reference', 'mut', 'mutable'):
            with self.subTest(modifier=modifier):
                source = f'fn update({modifier} amount: i64) -> i64\n amount = amount + 1\n return amo|unt\nend fn\n'
                result, notes = completion(source)
                self.assertEqual(set(result), {'amount'})
                self.assertEqual(result['amount']['detail'], modifier + ' amount: i64')
                replies, _ = session([opened(source.replace('|', '')), query('definition', source, '|')])
                self.assertEqual(replies[2][0]['range']['start'], {'line': 0, 'character': 11 + len(modifier)})
                self.assertEqual(notes[0]['params']['diagnostics'], [])

    def test_long_mutability_and_reference_type_aliases(self) -> None:
        result, _ = completion('fn main()\n let mutable total: i64 = 1\n println(tot|al)\nend fn\n')
        self.assertEqual(set(result), {'total'})
        source = 'struct Point\n count: i64\nend struct\nfn main(point: reference<Point>)\n println(point.|)\nend fn\n'
        result, _ = completion(source)
        self.assertEqual(set(result), {'count'})

    def test_formatted_text_holes_and_nested_literals(self) -> None:
        prefix = 'fn main()\n let height: f64 = -1.6\n'
        for value in ('$"height: {hei|ght}"', '$"{height|}"',
                      '$"{describe("nested")} {hei|ght}"',
                      '$"{$"nested {hei|ght}"}"'):
            with self.subTest(value=value):
                result, notes = completion(prefix + ' println(' + value + ')\nend fn\n')
                self.assertEqual(set(result), {'height'})
                self.assertFalse([note for note in notes if note['params']['diagnostics']])
        for value in ('$"hei|ght {height}"', '$"{{hei|ght}} {height}"',
                      '$"{describe("hei|ght")}"'):
            with self.subTest(value=value):
                result, _ = completion(prefix + ' println(' + value + ')\nend fn\n')
                self.assertEqual(result, {})

    def test_destructured_local_bindings(self) -> None:
        result, _ = completion('fn main()\n let (width, height, _) = (1, 2, 3)\n |\nend fn\n')
        self.assertTrue({'width', 'height'} <= set(result))
        self.assertNotIn('_', result)
        result, _ = completion('fn main()\n let (width, height) = (wi|, 2)\nend fn\n')
        self.assertNotIn('width', result)

    def test_typed_fault_guard_members(self) -> None:
        source = """struct Problem
    code: i32
    note: text
end struct
fn main()
    attempt
        let body_local = 1
    fault Problem as problem when problem.co| == 3
        println(problem.code)
    end attempt
end fn
"""
        result, _ = completion(source)
        self.assertEqual(set(result), {"code"})

    def test_fault_binding_and_recover_scope(self) -> None:
        source = """struct Problem
    code: i32
end struct
fn main()
    let outer = 1
    attempt
        let body_local = 2
    fault Problem as problem when (problem.code as i32) > 0
        |
    recover
        println(outer)
    end attempt
end fn
"""
        result, _ = completion(source)
        self.assertIn("problem", result)
        self.assertIn("outer", result)
        self.assertNotIn("body_local", result)
        self.assertNotIn("i32", result)
        recover = source.replace("        |\n", "        println(problem.code)\n").replace("println(outer)", "|")
        result, _ = completion(recover)
        self.assertIn("outer", result)
        self.assertNotIn("problem", result)
        self.assertNotIn("body_local", result)

    def test_opaque_catchall_has_no_guessed_members(self) -> None:
        source = """fn main()
    attempt
        println(1)
    fault as token
        token.|
    end attempt
end fn
"""
        result, _ = completion(source)
        self.assertEqual(result, {})

    def test_capabilities_and_keyword_spellings(self) -> None:
        source = "function old()\nend function\nfn new()\n |\nend fn\n"
        result, _ = completion(source)
        self.assertEqual(set(result), {"old", "new"})
        self.assertEqual(result["old"]["kind"], 3)
        replies, _ = session([])
        self.assertEqual(replies[1]["capabilities"]["positionEncoding"], "utf-16")
        self.assertTrue(replies[1]["capabilities"]["documentSymbolProvider"])

    def test_parameters_locals_and_foreign_functions(self) -> None:
        source = """const global: i32 = 1
fn other(secret: i32)
    let foreign = secret
end fn
fn main(argument: i32, typed: ref<Record>)
    let prior = argument
    |
    let future = 4
end fn
"""
        result, _ = completion(source, extra=[opened("let alien = 3\n", "file:///ny-protocol/foreign.ny")])
        self.assertEqual(set(result), {"global", "other", "main", "argument", "typed", "prior"})
        self.assertEqual(result["argument"]["detail"], "argument: i32")
        self.assertEqual(result["global"]["kind"], 21)

    def test_nested_branch_scope_and_shadow_definition(self) -> None:
        source = """let shared = 0
fn main()
    let shared: i32 = 1
    if true then
        let branch = 2
        let shared: f64 = 1.5
        sha|red
    else
        let alternative = 3
    end if
end fn
"""
        result, _ = completion(source)
        self.assertEqual(set(result), {"shared"})
        self.assertEqual(result["shared"]["detail"], "let shared: f64 = 1.5")
        replies, _ = session([opened(source.replace("|", "")), query("definition", source, "|")])
        self.assertEqual(replies[2][0]["range"]["start"], {"line": 5, "character": 12})
        for marker in ("        let alternative", "end fn"):
            result, _ = completion(source.replace("|", ""), marker)
            self.assertNotIn("branch", result)

    def test_for_binding_is_scoped(self) -> None:
        inside, _ = completion("fn main()\nfor item in values then\n |\nend for\nend fn\n")
        outside, _ = completion("fn main()\nfor item in values then\nend for\n |\nend fn\n")
        self.assertIn("item", inside)
        self.assertNotIn("item", outside)

    def test_closed_scope_at_end_of_file(self) -> None:
        result, _ = completion("fn main(argument: i32)\n let local = 1\nend fn|")
        # The cursor is after the block-end keyword, so prefix filtering is empty;
        # a blank suffix makes the scope-boundary assertion independent of prefix.
        self.assertNotIn("argument", result)
        result, _ = completion("fn main(argument: i32)\n let local = 1\nend fn |")
        self.assertEqual(set(result), {"main"})
        result, _ = completion("module app\nlet global = 1\n|")
        self.assertIn("global", result)

    def test_braces_and_elseif_scopes(self) -> None:
        source = "fn helper(secret: i32) { let hidden = secret }\nfn main() {\n let local = 1; |\n}\n"
        result, _ = completion(source)
        self.assertEqual(set(result), {"helper", "main", "local"})
        result, _ = completion("fn main()\nif true then\n let first = 1\nelseif false then\n |\nend if\nend fn\n")
        self.assertNotIn("first", result)
        result, notes = completion("fn main()\nif true then\n let first = 1\nelse if false then\n |\nend if\nend fn\n")
        self.assertNotIn("first", result)
        self.assertEqual(notes[0]["params"]["diagnostics"], [])

    def test_standard_library_declaration_forms(self) -> None:
        source = '''enum Maybe<T>
    Some(T)
    None
end enum
@intrinsic("host.first")
fn first(value: text) -> i32
@intrinsic("host.second")
fn second(value: text) -> i32
end fn
fn inline() -> i64 return 10000 end fn
fn main()
    |
end fn
'''
        result, notes = completion(source)
        self.assertEqual(set(result), {"Maybe", "first", "second", "inline", "main"})
        self.assertEqual(notes[0]["params"]["diagnostics"], [])

    def test_actual_standard_library_structural_diagnostics(self) -> None:
        root = Path(__file__).resolve().parents[1] / "nilstd" / "src"
        sources = sorted(root.rglob("*.ny"))
        self.assertGreater(len(sources), 0)
        _, notes = session([opened(source.read_text(encoding="utf-8"), source.as_uri()) for source in sources])
        diagnostics = [(note["params"]["uri"], note["params"]["diagnostics"]) for note in notes
                       if note["params"]["diagnostics"]]
        self.assertEqual(diagnostics, [])

    def test_module_scopes_are_separate(self) -> None:
        result, _ = completion("module first\nlet foreign = 1\nfn elsewhere()\nend fn\n"
                               "module second\nlet local = 2\nfn main()\n |\nend fn\n")
        self.assertIn("local", result)
        self.assertNotIn("foreign", result)
        self.assertNotIn("elsewhere", result)

    def test_field_and_method_completion(self) -> None:
        source = """struct Record
    value: f64
    count: i32
end struct
impl Record
    fn read(self) -> f64
        return self.value
    end fn
end impl
fn main(record: ref<Record>)
    record.|
end fn
"""
        result, _ = completion(source)
        self.assertEqual(set(result), {"value", "count", "read"})
        self.assertEqual(result["value"]["kind"], 5)
        self.assertEqual(result["read"]["kind"], 2)
        self.assertEqual(result["value"]["textEdit"]["range"],
                         {"start": {"line": 10, "character": 11}, "end": {"line": 10, "character": 11}})
        member_source = source.replace("record.|", "record.va|lue")
        replies, _ = session([opened(member_source.replace("|", "")), query("definition", member_source, "|")])
        self.assertEqual(replies[2][0]["range"]["start"], {"line": 1, "character": 4})
        result, _ = completion(source.replace("return self.value", "self.|").replace("record.|", "record.value"))
        self.assertEqual(set(result), {"value", "count", "read"})

    def test_constructor_and_generic_base_members(self) -> None:
        source = """struct Box<T> { value: T, count: i32 }
impl<T> Box<T>
    fn read(self) -> T { return self.value }
end impl
fn main()
    let box = Box<i32> { value: 4, count: 1 }
    box.va|lue
end fn
"""
        result, _ = completion(source)
        self.assertEqual(set(result), {"value"})
        self.assertEqual(result["value"]["detail"], "value: T")
        self.assertEqual(result["value"]["textEdit"]["range"]["end"]["character"], 13)

    def test_unknown_and_qualified_receivers_do_not_guess(self) -> None:
        for expression in ("unknown.", "external::", "object.field.", "factory."):
            with self.subTest(expression=expression):
                result, _ = completion("struct Record { field: i32 }\nfn factory() -> Record { return Record { field: 1 } }\n"
                                       "fn main(object: Record)\n" + expression + "|\nend fn\n")
                self.assertEqual(result, {})

    def test_comments_strings_and_fake_declarations(self) -> None:
        source = """# let hash_ghost = 1
// fn line_ghost()
### struct block_ghost
end struct ###
/* let c_ghost = 3 */
let literal = "# fake fn string_ghost()"
fn main()
    |
end fn
"""
        result, _ = completion(source)
        self.assertEqual(set(result), {"literal", "main"})
        for body in ('# ghost|', '// ghost|', '### ghost| ###', '/* ghost| */',
                     'let text = "escaped \\\" ghost|"', 'let character = \'g|\'',
                     'let text = "unterminated|', '# eof|'):
            with self.subTest(body=body):
                result, _ = completion("let global = 1\nfn main()\n" + body)
                self.assertEqual(result, {})

    def test_utf16_edits_and_ascii_json_surrogates(self) -> None:
        source = 'let value = 1\nfn main()\n    println("é🛠"); va|lue\nend fn\n'
        for ascii_json in (False, True):
            with self.subTest(ascii_json=ascii_json):
                result, _ = completion(source, ascii_json=ascii_json)
                self.assertEqual(set(result), {"value"})
                self.assertEqual(result["value"]["textEdit"],
                                 {"newText": "value", "range": {"start": {"line": 2, "character": 20},
                                                                   "end": {"line": 2, "character": 25}}})

    def test_bad_positions_and_unknown_document(self) -> None:
        for point in ({"line": -1, "character": 0}, {"line": 99, "character": 0},
                      {"line": 0, "character": 999}, {"line": 0.5, "character": 0},
                      {"line": 0, "character": 2 ** 40}):
            with self.subTest(point=point):
                request = query("completion", "|", "|")
                request["params"]["position"] = point
                replies, _ = session([opened("let value = 1\n"), request])
                self.assertEqual(replies[2]["items"], [])
        replies, _ = session([query("completion", "|", "|")])
        self.assertEqual(replies[2]["items"], [])

    def test_outline_ranges_and_hierarchy(self) -> None:
        source = 'module app\nstruct Record { value: i32 }\nfn main(argument: Record)\n let local = 1\nend fn\n'
        request = {"jsonrpc": "2.0", "id": 2, "method": "textDocument/documentSymbol",
                   "params": {"textDocument": {"uri": URI}}}
        replies, notes = session([opened(source), request])
        self.assertEqual(notes[0]["params"]["diagnostics"], [])
        module = replies[2][0]
        self.assertEqual(module["name"], "app")
        children = {child["name"]: child for child in module["children"]}
        self.assertEqual(set(children), {"Record", "main"})
        self.assertEqual(children["Record"]["children"][0]["name"], "value")
        self.assertEqual({child["name"] for child in children["main"]["children"]}, {"argument", "local"})
        self.assertEqual(children["main"]["range"]["end"], {"line": 4, "character": 6})

    def test_changes_close_and_no_stale_symbols(self) -> None:
        source = "fn main()\n |\nend fn\n"
        request = query("completion", source, "|")
        change = {"jsonrpc": "2.0", "method": "textDocument/didChange", "params": {
            "textDocument": {"uri": URI, "version": 2},
            "contentChanges": [{"text": source.replace("|", "")} ]}}
        close = {"jsonrpc": "2.0", "method": "textDocument/didClose", "params": {"textDocument": {"uri": URI}}}
        after = dict(request, id=3)
        replies, notes = session([opened("let stale = 1\n"), change, request, close, after])
        self.assertEqual([item["label"] for item in replies[2]["items"]], ["main"])
        self.assertEqual(replies[3]["items"], [])
        self.assertEqual(notes[-1]["params"]["diagnostics"], [])

    def test_incomplete_function_remains_useful(self) -> None:
        result, notes = completion("fn main(argument: i32)\n let local = 1\n |")
        self.assertEqual(set(result), {"main", "argument", "local"})
        self.assertIn("Unclosed block", notes[0]["params"]["diagnostics"][0]["message"])

    def test_unclosed_comment_and_literal_diagnostics(self) -> None:
        for source, expected in (("### unclosed", "Unclosed block comment"),
                                 ("/* unclosed", "Unclosed block comment"),
                                 ('let text = "unclosed', "Unclosed string or character literal")):
            with self.subTest(source=source):
                _, notes = session([opened(source)])
                self.assertEqual(notes[0]["params"]["diagnostics"][0]["message"], expected)


if __name__ == "__main__":
    arguments = argparse.ArgumentParser(description=__doc__)
    arguments.add_argument("--lsp", required=True, type=Path)
    options, remaining = arguments.parse_known_args()
    LSP = options.lsp.resolve(strict=True)
    unittest.main(argv=[__file__, *remaining])
