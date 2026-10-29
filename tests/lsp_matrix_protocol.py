#!/usr/bin/env python3
"""Framed-protocol checks for Matrix's document-local language service."""
from __future__ import annotations

import argparse
from pathlib import Path
import unittest

import lsp_semantic_protocol as protocol

URI = "file:///matrix-protocol/current.matrix"


def opened(source: str, uri: str = URI, language: str | None = "matrix") -> dict:
    result = protocol.opened(source, uri)
    document = result["params"]["textDocument"]
    if language is None:
        del document["languageId"]
    else:
        document["languageId"] = language
    return result


def request(method: str, source: str, marker: str = "|", *, uri: str = URI, identifier: int = 2) -> dict:
    return {"jsonrpc": "2.0", "id": identifier, "method": f"textDocument/{method}",
            "params": {"textDocument": {"uri": uri}, "position": protocol.position(source, marker)}}


def completion(source: str, *, uri: str = URI, language: str | None = "matrix", ascii_json: bool = False) -> tuple[dict, list]:
    responses, notes = protocol.session([opened(source.replace("|", ""), uri, language),
                                        request("completion", source, uri=uri)], ascii_json=ascii_json)
    items = responses[2]["items"]
    return {item["label"]: item for item in items}, notes


def symbols(items: dict) -> dict:
    return {name: item for name, item in items.items() if item.get("detail") != "Matrix language"}


class MatrixProtocol(unittest.TestCase):
    def test_boolean_vocabulary_and_literal_inference(self) -> None:
        for source, expected in (
            ('value flag as |', {'boolean', 'flag', 'anything'}),
            ('value flag is |', {'yes', 'no', 'true', 'false'}),
            ('note custom with enabled being |', {'yes', 'no'}),
        ):
            result, _ = completion('main begin paragraph\n' + source + '\nend paragraph\n')
            self.assertTrue(expected <= set(result), result)
        for literal in ('yes', 'no', 'YES', 'NO'):
            result, _ = completion('boolean begin extension\n as-text returns text begin paragraph\n'
                                   ' give "flag"\nend paragraph\nend extension\n'
                                   'main begin paragraph\n value enabled is ' + literal +
                                   '\n say enabled.|\nend paragraph\n')
            self.assertEqual(set(result), {'as-text'})
        result, _ = completion('main begin paragraph\n consider yes\n when YES then\n |\nend consider\nend paragraph\n')
        self.assertNotIn('YES', symbols(result))

    def test_switch_cases_and_nested_value_forms_keep_lexical_scopes(self) -> None:
        source = '''main receives mode as number begin paragraph
 value answer is identity with input being (switch mode
  case 1 then
   value outer as number is 1
   stop switching with switch mode
    case 1 then
     value inner as number is 2
     say in|ner
     stop switching with inner
    otherwise
     stop switching with outer
   end switch
  case 2 then
   value second as number is 2
   stop switching with second
  otherwise
   stop switching with 0
 end switch)
 value following as number is 3
 say following
end paragraph
'''
        result, notes = completion(source)
        self.assertEqual(set(result), {'inner'})
        self.assertFalse([note for note in notes if note['params']['diagnostics']])
        second = source.replace('in|ner', 'inner').replace('stop switching with second', '|\n   stop switching with second')
        result, _ = completion(second)
        self.assertIn('second', result)
        self.assertNotIn('outer', result)
        self.assertNotIn('inner', result)
        outside = source.replace('in|ner', 'inner').replace('say following', '|')
        result, _ = completion(outside)
        self.assertTrue({'answer', 'following'} <= set(result))
        self.assertFalse({'outer', 'inner', 'second'} & set(result))

    def test_switch_vocabulary_and_named_members(self) -> None:
        for fragment, expected in (('|', {'switch', 'case', 'stop switching'}),
                                   ('end |', {'switch'}), ('stop |', {'repeating', 'switching'}),
                                   ('stop switching |', {'with'}), ('value result is |', {'switch'})):
            result, _ = completion(fragment)
            self.assertTrue(expected <= set(result), result)
        source = 'Settings begin record\n field switch as number\n field default as number\nend record\nmain receives settings as Settings begin paragraph\n say settings.switch\n say settings.def|ault\nend paragraph\n'
        result, notes = completion(source)
        self.assertEqual(set(result), {'default'})
        self.assertFalse([note for note in notes if note['params']['diagnostics']])

    def test_switch_named_values_and_calls_do_not_open_control_scopes(self) -> None:
        for expression in ('switch', 'switch plus 1', 'switch with input being 1'):
            source = ('main receives switch as number begin paragraph\n value result is ' + expression +
                      '\n value following as number is 3\n say fol|lowing\nend paragraph\n')
            result, notes = completion(source)
            self.assertEqual(set(result), {'following'})
            self.assertFalse([note for note in notes if note['params']['diagnostics']])

    def test_switch_conditions_and_case_named_members_preserve_scopes(self) -> None:
        source = '''Settings begin record
 field case as number
end record
main receives settings as Settings begin paragraph
 when (switch 1
  case 1 then
   value condition-local as boolean is true
   stop switching with condition-local
  otherwise
   stop switching with false
 end switch) then
  value branch-local as number is 1
  switch 1
   case 1 then
    value case-local as number is 2
    say settings.case
    say case-|local
    stop switching
   otherwise
    say 0
  end switch
 end when
 value following as number is 3
 say following
end paragraph
'''
        result, notes = completion(source)
        self.assertEqual(set(result), {'case-local'})
        self.assertFalse([note for note in notes if note['params']['diagnostics']])
        condition = source.replace('case-|local', 'case-local').replace(
            'stop switching with condition-local', 'stop switching with condition-|local')
        result, _ = completion(condition)
        self.assertEqual(set(result), {'condition-local'})
        outside = source.replace('case-|local', 'case-local').replace('say following', '|')
        result, _ = completion(outside)
        self.assertIn('following', result)
        self.assertFalse({'condition-local', 'branch-local', 'case-local'} & set(result))

    def test_language_id_uri_suffix_and_change(self) -> None:
        source = "main begin paragraph\n value local-count as number is 4\n say local-|\nend paragraph\n"
        for uri, language in ((URI, "matrix"), ("file:///code.MAT", None), ("file:///code.MATRIX", None),
                              ("untitled:Untitled-1", "matrix")):
            with self.subTest(uri=uri, language=language):
                result, _ = completion(source, uri=uri, language=language)
                self.assertEqual(set(result), {"local-count"})
        uri = "untitled:Untitled-2"
        change = {"jsonrpc": "2.0", "method": "textDocument/didChange", "params": {
            "textDocument": {"uri": uri, "version": 2}, "contentChanges": [{"text": source.replace("|", "")}]}}
        responses, _ = protocol.session([opened("book previous\n", uri), change,
                                         request("completion", source, uri=uri)])
        self.assertEqual([item["label"] for item in responses[2]["items"]], ["local-count"])

    def test_subject_declarations_casefold_and_scopes(self) -> None:
        source = """book example.scene
constant MAX-SIZE as number is 12
other receives secret as number begin paragraph
 value hidden is secret
end paragraph
main receives Sensor-ID as number, frame-count as number returns number begin paragraph
 variable local-count as number is Sensor-ID
 |
 value later as number is 2
 give 0
end paragraph
"""
        result, notes = completion(source)
        self.assertEqual(set(symbols(result)), {"example.scene", "MAX-SIZE", "other", "main", "Sensor-ID", "frame-count", "local-count"})
        self.assertEqual(result["Sensor-ID"]["detail"], "Sensor-ID as number")
        self.assertEqual(notes[0]["params"]["diagnostics"], [])
        result, _ = completion(source.replace(" |\n", " say SENSOR_i|D\n"))
        self.assertEqual(set(result), {"Sensor-ID"})
        self.assertEqual(result["Sensor-ID"]["textEdit"]["newText"], "Sensor-ID")

    def test_casefold_shadowing_definition_and_hover(self) -> None:
        source = """value local-count as number is 1
main begin paragraph
 value Local_Count as floating is 1.5
 when true then
  value LOCALCOUNT as text is "inner"
  say local-c|ount
 otherwise
  say 0
 end when
end paragraph
"""
        result, _ = completion(source)
        self.assertEqual(set(result), {"LOCALCOUNT"})
        self.assertIn('as text', result["LOCALCOUNT"]["detail"])
        replies, _ = protocol.session([opened(source.replace("|", "")), request("definition", source),
                                       request("hover", source, identifier=3)])
        self.assertEqual(replies[2][0]["range"]["start"], {"line": 4, "character": 8})
        self.assertIn("```matrix", replies[3]["contents"]["value"])
        outside = source.replace("say local-c|ount", "say local-count").replace("say 0", "say local-c|ount")
        result, _ = completion(outside)
        self.assertEqual(set(result), {"Local_Count"})

    def test_repeat_branch_catch_and_choice_bindings(self) -> None:
        for source, present, absent in (
            ("repeat for step from 0 to 5\n |\nend repeat", {"step"}, set()),
            ("repeat for each item in values\n |\nend repeat", {"item"}, set()),
            ("repeat for step from 0 to 5\nend repeat\n |", set(), {"step"}),
            ("when true then\n value first is 1\notherwise when false then\n |\nend when", set(), {"first"}),
            ("attempt\n value body-local is 1\ncaught Problem as problem provided true\n |\nalways\nend attempt", {"problem"}, {"body-local"}),
            ("attempt\n value body-local is 1\ncaught Problem as problem\n say 0\nalways\n |\nend attempt", set(), {"body-local", "problem"}),
            ("consider state\n when State.Ready holding payload then\n |\n otherwise say 0\nend consider", {"payload"}, set()),
            ("consider state\n when State.Ready holding payload then say payload\n otherwise\n |\nend consider", set(), {"payload"}),
        ):
            with self.subTest(source=source):
                result, _ = completion("main begin paragraph\n" + source + "\nend paragraph\n")
                self.assertTrue(present <= set(result))
                self.assertFalse(absent & set(result))

    def test_known_record_method_chain_and_alias(self) -> None:
        source = """Point begin record
 field raw-value as number
 field y as number
end record
Segment begin record
 field finish as Point
end record
Point begin implementation
 read-level returns number begin paragraph
  give my raw-value
 end paragraph
end implementation
Position means Point
main receives point as reference to Point begin paragraph
 value span is build Segment with finish being (build Point with raw-value being 1, y being 2)
 value location as Position is build Point with raw-value being 1, y being 2
 say point.|
end paragraph
"""
        for expression in ("point.|", "span.finish.|", "location.|", "POINT.|"):
            with self.subTest(expression=expression):
                result, _ = completion(source.replace("point.|", expression))
                self.assertEqual(set(result), {"raw-value", "y", "read-level"})
                self.assertEqual(result["raw-value"]["kind"], 5)
                self.assertEqual(result["read-level"]["kind"], 2)
        result, _ = completion(source.replace("point.|", "point.RAW_v|alue"))
        self.assertEqual(set(result), {"raw-value"})
        self.assertEqual(result["raw-value"]["textEdit"]["range"]["end"]["character"], 20)
        result, _ = completion(source.replace("point.|", "point.raw-value").replace("my raw-value", "my |"))
        self.assertEqual(set(result), {"raw-value", "y", "read-level"})

    def test_local_factory_type_and_primitive_extensions(self) -> None:
        source = """Point begin record
 field x as number
end record
make-point returns Point begin paragraph
 give build Point with x being 0
end paragraph
text begin extension
 shouted returns text begin paragraph
  give my plus "!"
 end paragraph
end extension
main begin paragraph
 value first is make-point with nothing
 value greeting is "hello"
 say first.|
end paragraph
"""
        result, _ = completion(source)
        self.assertEqual(set(result), {"x"})
        result, _ = completion(source.replace("first.|", "greeting.|"))
        self.assertEqual(set(result), {"shouted"})

    def test_class_inheritance_and_choice_variants(self) -> None:
        source = """Base begin class
 field x as number
 public read-level returns number begin paragraph give my x end paragraph
end class
Derived inherits Base begin class
 field y as number
end class
State begin choice
 when Ready holding number
 when Empty
end choice
main begin paragraph
 value point is build Derived with x being 1, y being 2
 say point.|
end paragraph
"""
        result, _ = completion(source)
        self.assertEqual(set(result), {"x", "y", "read-level"})
        result, _ = completion(source.replace("point.|", "State.|"))
        self.assertEqual(set(result), {"Ready", "Empty"})

    def test_unknown_import_or_call_receivers_do_not_guess(self) -> None:
        source = "Point begin record\n field x as number\nend record\nmain begin paragraph\n say EXPRESSION|\nend paragraph\n"
        for expression in ("unknown.", "ny.lang.io::", "factory.", "unknown.field."):
            with self.subTest(expression=expression):
                result, _ = completion(source.replace("EXPRESSION", expression))
                self.assertEqual(result, {})

    def test_comments_text_blocks_raw_text_and_fake_declarations(self) -> None:
        source = '''!!! ghost begin record
main begin paragraph
 value banner is begin text
  fake begin paragraph
  field ghost as number
  !!! no comment inside text
  "quotes are just content"
 EnD   TeXt
 value path is "C:\\dir\\"
 say local-|
 value local-count is 1
end paragraph
'''
        result, notes = completion(source)
        self.assertEqual(result, {})  # local-count is not visible yet
        self.assertEqual(notes[0]["params"]["diagnostics"], [])
        for body in ('!!! comment|', 'value line is "raw \\ |"', 'value line is "unfinished|',
                     'value banner is begin text\n |\nend text', 'value banner is begin text\n |'):
            with self.subTest(body=body):
                result, _ = completion("value global is 1\nmain begin paragraph\n" + body)
                self.assertEqual(result, {})
        result, _ = completion('value global is 1\nmain begin paragraph\n value path is "C:\\"; say glo|\nend paragraph')
        self.assertEqual(set(result), {"global"})  # a backslash does not escape a quote

    def test_incomplete_editing_utf16_and_escaped_names(self) -> None:
        source = 'main receives Frame-ID as number begin paragraph\n value local-count as number is 1\n say "é🛠"; say LOCAL_co|unt'
        for ascii_json in (False, True):
            result, _ = completion(source, ascii_json=ascii_json)
            self.assertEqual(set(result), {"local-count"})
            self.assertEqual(result["local-count"]["textEdit"]["range"],
                             {"start": {"line": 2, "character": 16}, "end": {"line": 2, "character": 27}})
        result, _ = completion('main begin paragraph\n value `text` as text is "x"\n say `te|xt`\nend paragraph')
        self.assertEqual(set(result), {"`text`"})
        self.assertEqual(result["`text`"]["textEdit"]["newText"], "`text`")

    def test_multiline_headers_receiver_and_ownership_bindings(self) -> None:
        source = """sample receives x as number,
 y as number
 returns number begin paragraph
 value local-value is x
 say |
 give y
end paragraph
"""
        result, _ = completion(source)
        self.assertEqual(set(symbols(result)), {"sample", "x", "y", "local-value"})
        result, _ = completion("""Point begin record
 field x as number
end record
Point begin implementation
 width without receiver returns number begin paragraph
  say my |
 end paragraph
end implementation
""")
        self.assertEqual(result, {})
        result, _ = completion("""main begin paragraph
 own data as array of number sized 4
 borrow borrowed-data from data
 move data into transferred-data
 say |
end paragraph
""")
        self.assertTrue({"data", "borrowed-data", "transferred-data"} <= set(result))
        result, _ = completion("main begin paragraph\n consider 42\n when observed then\n say obs|\n end consider\nend paragraph")
        self.assertEqual(set(result), {"observed"})

    def test_mutable_and_variadic_parameters_keep_names_types_and_locations(self) -> None:
        source = """sample receives mutable current-value as number and variable old-value as text and
 more mutable remaining-values as number
 returns number begin paragraph
 say |
 give current-value
end paragraph
"""
        result, notes = completion(source)
        self.assertEqual(set(symbols(result)), {"sample", "current-value", "old-value", "remaining-values"})
        self.assertEqual(result["current-value"]["detail"], "mutable current-value as number")
        self.assertEqual(result["old-value"]["detail"], "variable old-value as text")
        self.assertEqual(result["remaining-values"]["detail"], "more mutable remaining-values as number")
        self.assertEqual(notes[0]["params"]["diagnostics"], [])
        located = source.replace("say |", "say current-v|alue")
        replies, _ = protocol.session([opened(located.replace("|", "")), request("definition", located),
                                       request("hover", located, identifier=3)])
        self.assertEqual(replies[2][0]["range"]["start"], {"line": 0, "character": 24})
        self.assertIn("mutable current-value as number", replies[3]["contents"]["value"])
        for modifiers in ("mutable", "mut", "variable", "more mutable", "more mut", "more variable"):
            with self.subTest(modifiers=modifiers):
                result, _ = completion(f"sample receives\n {modifiers} input-value as number begin paragraph\n say input-|\nend paragraph\n")
                self.assertEqual(set(result), {"input-value"})
                self.assertEqual(result["input-value"]["detail"], f"{modifiers} input-value as number")
                result, _ = completion(f"main begin paragraph\n value operation is anonymous receives {modifiers} input-value as number returns number begin paragraph\n  say input-|\n  give 0\n end paragraph\nend paragraph\n")
                self.assertEqual(set(result), {"input-value"})

    def test_parameter_modifier_keyword_completion(self) -> None:
        for source, expected in (
            ("sample receives |", {"mutable", "mut", "variable", "reference", "ref", "more"}),
            ("sample receives more mu|", {"mutable", "mut"}),
            ("sample receives first as number and mu|", {"mutable", "mut"}),
            ("sample receives first as number,\n mu|", {"mutable", "mut"}),
            ("sample receives first as number,\n more mu|", {"mutable", "mut"}),
            ("sample receives first as number and re|", {"reference", "ref"}),
            ("sample receives mutable |", set()),
            ("sample receives mut |", set()),
            ("sample receives reference |", set()),
            ("sample receives ref |", set()),
            ("sample receives value-name as reference |", {"to"}),
            ("sample receives value-name as ref |", {"to"}),
            ("main begin paragraph\n evaluate sample with reference |", {"to"}),
            ("main begin paragraph\n evaluate sample with ref |", {"to"}),
            ("main begin paragraph\n say sample with more mu|", set()),
        ):
            with self.subTest(source=source):
                result, _ = completion(source)
                keywords = {name for name, item in result.items() if item.get("detail") == "Matrix language"}
                self.assertEqual(keywords, expected)

    def test_reference_parameters_preserve_referent_type_and_definition(self) -> None:
        source = """update receives first-value as number and reference current-value as number
 returns number begin paragraph
 set current-value to current-value plus first-value
 give current-v|alue
end paragraph
"""
        result, notes = completion(source)
        self.assertEqual(set(result), {"current-value"})
        self.assertEqual(result["current-value"]["detail"], "reference current-value as number")
        self.assertEqual(notes[0]["params"]["diagnostics"], [])
        replies, _ = protocol.session([opened(source.replace("|", "")), request("definition", source)])
        self.assertEqual(replies[2][0]["range"]["start"], {"line": 0, "character": source.index("current-value")})
        result, _ = completion(source.replace("first-value as number and reference", "\n reference"))
        self.assertEqual(set(result), {"current-value"})
        result, _ = completion(source.replace("reference current-value", "ref current-value"))
        self.assertEqual(set(result), {"current-value"})
        self.assertEqual(result["current-value"]["detail"], "ref current-value as number")

    def test_short_reference_type_preserves_member_completion(self) -> None:
        source = 'Point begin record\n field count as number\nend record\nmain receives point as ref to Point begin paragraph\n say point.|\nend paragraph\n'
        result, _ = completion(source)
        self.assertEqual(set(result), {"count"})

    def test_non_capturing_paragraph_and_escaped_names_with_spaces(self) -> None:
        source = """value global-count is 1
main receives outer-param as number begin paragraph
 value outer-local is 1
 value operation is anonymous receives inner-param as number returns number begin paragraph
  say |
  give inner-param
 end paragraph
end paragraph
"""
        result, _ = completion(source)
        self.assertIn("inner-param", result)
        self.assertIn("global-count", result)
        self.assertNotIn("outer-param", result)
        self.assertNotIn("outer-local", result)
        source = 'main begin paragraph\n value `my name` as number is 1\n say `my na|me`\nend paragraph'
        result, _ = completion(source)
        self.assertEqual(set(result), {"`my name`"})
        self.assertEqual(result["`my name`"]["textEdit"]["range"],
                         {"start": {"line": 2, "character": 5}, "end": {"line": 2, "character": 14}})
        replies, _ = protocol.session([opened(source.replace("|", "")), request("definition", source)])
        self.assertEqual(replies[2][0]["range"]["start"], {"line": 1, "character": 7})
        result, _ = completion('main begin paragraph\n value local-count is 1\n say "a`"; say loc|\nend paragraph')
        self.assertEqual(set(result), {"local-count"})
        result, _ = completion('Point begin record\n field x as number\nend record\nmain begin paragraph\n value `my point` as Point is build Point with x being 1\n say `my point`.|\nend paragraph')
        self.assertEqual(set(result), {"x"})

    def test_concrete_generic_extensions_and_ambiguity(self) -> None:
        source = """Envelope of type T begin record
 field item as T
end record
Envelope of number begin extension
 numeric-only returns number begin paragraph give 1 end paragraph
end extension
Envelope begin implementation
 preferred returns number begin paragraph give 1 end paragraph
end implementation
Envelope begin extension
 preferred returns number begin paragraph give 2 end paragraph
 conflicted returns number begin paragraph give 2 end paragraph
end extension
Envelope begin extension
 conflicted returns number begin paragraph give 3 end paragraph
end extension
main begin paragraph
 value boxed as Envelope of text is build Envelope of text with item being "value"
 say boxed.|
end paragraph
"""
        result, _ = completion(source)
        self.assertEqual(set(result), {"item", "preferred"})
        self.assertIn("preferred", result)
        reference = source.replace("boxed.|", "boxed.pref|erred")
        replies, _ = protocol.session([opened(reference.replace("|", "")), request("definition", reference)])
        self.assertEqual(replies[2][0]["range"]["start"]["line"], 7)
        result, _ = completion(source.replace("Envelope of text", "Envelope of number"))
        self.assertEqual(set(result), {"item", "preferred", "numeric-only"})

    def test_keyword_context_no_ny_spellings(self) -> None:
        for fragment, expected in (("value item as |", {"number", "floating", "text", "tuple of"}),
                                   ("value result is |", {"negative", "formatted"}),
                                   ("value item as override |", {"integer", "long", "byte", "double"}),
                                   ("repeat |", {"while", "for each", "forever"}),
                                   ("say 1 bitwise |", {"and", "or", "xor", "not"}),
                                   ("main returns number begin |", {"paragraph"})):
            with self.subTest(fragment=fragment):
                result, _ = completion(fragment)
                self.assertTrue(expected <= set(result))
                self.assertFalse({"fn", "let", "i32", "i64", "struct", "impl"} & set(result))

    def test_annotation_completion_is_metadata_not_value_resolution(self) -> None:
        for fragment, expected in (
            ("|", {"note", "context"}),
            ("@|", {"doc", "ai_context", "inline"}),
            ("note |", {"doc", "intrinsic"}),
            ("note custom |", {"with"}),
            ("@doc(|", {"summary", "description", "true", "false"}),
            ("@doc(\n sum|", {"summary"}),
            ("@doc(summary=|", {"true", "false"}),
            ("note doc with sum|", {"summary being"}),
            ("note doc with summary |", {"being"}),
            ("note custom with enabled being |", {"true", "false"}),
        ):
            with self.subTest(fragment=fragment):
                result, _ = completion("value private-value is 1\n" + fragment)
                self.assertTrue(expected <= set(result), result)
                if fragment != "|": self.assertNotIn("private-value", result)
        result, _ = completion('@doc(summary="not code |")\nmain begin paragraph\nend paragraph\n')
        self.assertEqual(result, {})

    def test_multiline_annotations_do_not_create_symbols_or_change_following_spans(self) -> None:
        source = '''@company.review(
 value=Approved,
 summary="a literal ) and begin record",
 enabled=true
)
note doc with summary being "A real paragraph"
context "Kept as metadata"
main receives input as number returns number begin paragraph
 give in|put
end paragraph
'''
        replies, notes = protocol.session([
            opened(source.replace("|", "")), request("definition", source),
            {"jsonrpc": "2.0", "id": 3, "method": "textDocument/documentSymbol",
             "params": {"textDocument": {"uri": URI}}}])
        self.assertFalse([note for note in notes if note['params']['diagnostics']])
        self.assertEqual(replies[2][0]["range"]["start"], {"line": 7, "character": 14})
        self.assertEqual([symbol['name'] for symbol in replies[3]], ["main"])
        source = 'value summary is 1\n@doc(sum|mary="metadata")\nmain begin paragraph\nend paragraph\n'
        replies, _ = protocol.session([opened(source.replace('|', '')), request('definition', source),
                                       request('hover', source, identifier=3)])
        self.assertIsNone(replies[2])
        self.assertIsNone(replies[3])

    def test_incomplete_annotation_hides_payload_declarations(self) -> None:
        source = '@custom(\n value fake as number is 1\n fake|'
        result, _ = completion(source)
        self.assertNotIn('fake', result)

    def test_formatted_text_holes_and_nested_literals(self) -> None:
        prefix = 'main begin paragraph\n value height as floating is -1.6e-3\n'
        for value in ('formatted "height: {hei|ght}"', 'formatted "{height|}"',
                      'formatted "{describe with input being "nested"} {hei|ght}"',
                      'formatted "{formatted "nested {hei|ght}"}"'):
            with self.subTest(value=value):
                result, notes = completion(prefix + ' say ' + value + '\nend paragraph\n')
                self.assertEqual(set(symbols(result)), {'height'})
                self.assertFalse([note for note in notes if note['params']['diagnostics']])
        for value in ('formatted "hei|ght {height}"',
                      'formatted "{{hei|ght}} {height}"',
                      'formatted "{describe with input being "hei|ght"}"'):
            with self.subTest(value=value):
                result, _ = completion(prefix + ' say ' + value + '\nend paragraph\n')
                self.assertEqual(result, {})

    def test_destructured_local_bindings(self) -> None:
        result, _ = completion('main begin paragraph\n value (width, height, _) is (1, 2, 3)\n say |\nend paragraph\n')
        self.assertTrue({'width', 'height'} <= set(symbols(result)))
        self.assertNotIn('_', result)
        result, _ = completion('main begin paragraph\n value (width, height) is (wi|, 2)\nend paragraph\n')
        self.assertNotIn('width', result)

    def test_signed_scientific_and_hexadecimal_literal_types(self) -> None:
        prefix = ('floating begin extension\n floating-only returns number begin paragraph\n give 1\n end paragraph\nend extension\n'
                  'number begin extension\n integer-only returns number begin paragraph\n give 1\n end paragraph\nend extension\n')
        for literal, method in (('-1.6e-3', 'floating-only'), ('-1E3', 'floating-only'),
                                ('-0xFE', 'integer-only')):
            result, _ = completion(prefix + 'main begin paragraph\n value sample is ' + literal +
                                   '\n say sample.|\nend paragraph\n')
            self.assertEqual(set(result), {method})

    def test_book_boundaries_outline_and_sample_corpus(self) -> None:
        result, _ = completion("book first\nvalue foreign is 1\nbook second\nmain begin paragraph\n |\nend paragraph")
        self.assertNotIn("foreign", result)
        root = Path(__file__).resolve().parents[1]
        sources = sorted((root / "matrix" / "samples").glob("*.matrix"))
        messages = []
        for i, path in enumerate(sources, start=2):
            text = path.read_text()
            messages.extend([opened(text, path.as_uri()), {"jsonrpc": "2.0", "id": i,
                "method": "textDocument/documentSymbol", "params": {"textDocument": {"uri": path.as_uri()}}}])
        replies, notes = protocol.session(messages)
        self.assertFalse([(note["params"]["uri"], note["params"]["diagnostics"]) for note in notes if note["params"]["diagnostics"]])
        for i, path in enumerate(sources, start=2):
            with self.subTest(path=path.name):
                self.assertTrue(replies[i])
        source = "book demo\nPoint begin record\n field x as number\nend record\nmain receives p as Point begin paragraph\n value local is 1\nend paragraph\n"
        replies, _ = protocol.session([opened(source), {"jsonrpc": "2.0", "id": 2, "method": "textDocument/documentSymbol", "params": {"textDocument": {"uri": URI}}}])
        book = replies[2][0]
        self.assertEqual(book["name"], "demo")
        children = {item["name"]: item for item in book["children"]}
        self.assertEqual(set(children), {"Point", "main"})
        self.assertEqual(children["Point"]["children"][0]["name"], "x")
        self.assertEqual({item["name"] for item in children["main"]["children"]}, {"p", "local"})
        self.assertEqual(children["main"]["range"]["end"], {"line": 6, "character": 13})


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lsp", required=True, type=Path)
    options, remaining = parser.parse_known_args()
    protocol.LSP = options.lsp.resolve(strict=True)
    unittest.main(argv=[__file__, *remaining])
