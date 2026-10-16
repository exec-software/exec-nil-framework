#!/usr/bin/env python3
"""Check field-default rejection and inherited class method signatures.

These controls qualify declaration checking, not virtual runtime dispatch.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--interpreter", type=Path, required=True)
    args = parser.parse_args()
    compiler = args.compiler.resolve(strict=True)
    interpreter = args.interpreter.resolve(strict=True)

    def program(declarations: str) -> str:
        return declarations + "\nfn main() -> i32\nprintln(7)\nreturn 0\nend fn\n"

    def inherited(base: str, derived: str, *, prefix: str = "",
                  abstract: bool = False) -> str:
        return program(prefix + ("abstract " if abstract else "") +
                       "class Base\n" + base + "\nend class\n" +
                       "class Derived inherits Base\n" + derived + "\nend class\n")

    base = "virtual fn value(self, argument: i32) -> i32 { return argument }"
    signature_error = "has an incompatible override signature for base class"
    cases = {
        "struct_default": (program("struct Record\nvalue: i32 = 37\nend struct"),
                           "Struct field default initializers are unsupported"),
        "class_default": (program("class Record\nvalue: i32 = 37\nend class"),
                          "Class field default initializers are unsupported"),
        "struct_discarded_unknown_call": (
            program("struct Record\nvalue: i32 = undefined_default()\nend struct"),
            "Struct field default initializers are unsupported"),
        "class_discarded_side_effect": (
            program("class Record\nvalue: i32 = undefined_default()\nend class"),
            "Class field default initializers are unsupported"),
        "override_arity": (
            inherited(base, "override fn value(self) -> i32 { return 1 }"), signature_error),
        "override_return": (
            inherited(base, 'override fn value(self, argument: i32) -> String { return "bad" }'),
            signature_error),
        "override_parameter": (
            inherited(base, "override fn value(self, argument: i64) -> i32 { return 1 }"),
            signature_error),
        "override_receiver_shape": (
            inherited(base, "override fn value(self: ref<Self>, argument: i32) -> i32 { return 1 }"),
            signature_error),
        "override_receiver_missing": (
            inherited(base, "override fn value(other: i32, argument: i32) -> i32 { return 1 }"),
            signature_error),
        "override_receiver_mutability": (
            inherited(base, "override fn value(mut self, argument: i32) -> i32 { return 1 }"),
            signature_error),
        "override_variadic": (
            inherited("virtual fn value(self, arguments: array<i32>) -> i32 { return 1 }",
                      "override fn value(self, ..arguments: i32) -> i32 { return 1 }"),
            signature_error),
        "override_generic_count": (
            inherited("virtual fn value<T>(self, argument: T) -> T { return argument }",
                      "override fn value<T, U>(self, argument: T) -> T { return argument }"),
            signature_error),
        "override_generic_bounds": (
            inherited("virtual fn value<T: Marker>(self, argument: T) -> T { return argument }",
                      "override fn value<T>(self, argument: T) -> T { return argument }",
                      prefix="trait Marker\nend trait\n"), signature_error),
        "unmarked_override_signature": (
            inherited(base, "fn value(self, argument: i64) -> i32 { return 1 }"),
            signature_error),
        "abstract_name_only_implementation": (
            inherited("abstract fn value(self, argument: i32) -> i32\nend fn",
                      'fn value(self, argument: i32) -> String { return "bad" }',
                      abstract=True), signature_error),
        "nonvirtual_override": (
            inherited("fn value(self) -> i32 { return 1 }",
                      "override fn value(self) -> i32 { return 2 }"),
            "marked override but does not override any base method"),
        "absent_override": (
            inherited(base, "override fn absent(self) -> i32 { return 1 }"),
            "marked override but does not override any base method"),
        "transitive_override_signature": (
            program("class Base\n" + base + "\nend class\n"
                    "class Middle inherits Base\nend class\n"
                    "class Derived inherits Middle\n"
                    "override fn value(self, argument: i64) -> i32 { return 1 }\nend class"),
            signature_error),
        "multiple_base_signature": (
            program("class Base\n" + base + "\nend class\n"
                    "class Other\nvirtual fn value(self, argument: i64) -> i32 { return 1 }\nend class\n"
                    "class Derived inherits Base, Other\n"
                    "override fn value(self, argument: i32) -> i32 { return argument }\nend class"),
            signature_error),
        "same_spelling_module_types": (
            "module origin\npub struct Record { original: i32 }\n"
            "pub class Base\nvirtual fn value(self, argument: Record) -> i32 { return 1 }\nend class\n"
            "module client\npub struct Record { different: i64 }\n"
            "class Derived inherits origin::Base\n"
            "override fn value(self, argument: Record) -> i32 { return 2 }\nend class\n"
            "fn main() -> i32 { return 0 }\n", signature_error),
        "module_return_aliases": (
            "module origin\npub type Number = i32\n"
            "pub class Base\nvirtual fn value(self) -> Number { return 1 }\nend class\n"
            "module client\npub type Number = i64\n"
            "class Derived inherits origin::Base\n"
            "override fn value(self) -> Number { return 2 }\nend class\n"
            "fn main() -> i32 { return 0 }\n", signature_error),
        "module_transitive_signature": (
            "module origin\npub class Base\n" + base + "\nend class\n"
            "pub class Middle inherits Base\nend class\n"
            "module client\nclass Derived inherits origin::Middle\n"
            "override fn value(self, argument: i64) -> i32 { return 2 }\nend class\n"
            "fn main() -> i32 { return 0 }\n", signature_error),
        "class_unknown_generic_bound": (
            program("class Box<T: Missing>\nvalue: T\nend class"),
            "Unknown trait 'Missing' in generic bound"),
        "class_nontrait_generic_bound": (
            program("struct NotTrait { value: i32 }\nclass Box<T: NotTrait>\nvalue: T\nend class"),
            "Unknown trait 'NotTrait' in generic bound"),
        "inheritance_cycle": (
            program("class Left inherits Right\nend class\nclass Right inherits Left\nend class"),
            "Inheritance cycle involving class"),
        "alias_cycle": (program("type First = Second\ntype Second = First"),
                        "is cyclic or exceeds the alias resolution limit"),
        "expanding_generic_alias_cycle": (
            program("type Expanding<T> = Expanding<tuple<T, T>>"),
            "is cyclic or exceeds the alias resolution limit"),
        "imported_callable_alias_return": (
            "module alias_client\nimport alias_model.{make}\ntype Word = i32\n"
            "fn main() -> i32\nlet result: Word = make()\nreturn 0\nend fn\n",
            "Type mismatch in variable initialization. Expected 'i32', got 'String'"),
        "imported_qualified_alias_return": (
            "module alias_client\nimport alias_model\ntype Word = i32\n"
            "fn main() -> i32\nlet result: Word = alias_model::make()\nreturn 0\nend fn\n",
            "Type mismatch in variable initialization. Expected 'i32', got 'String'"),
        "imported_qualified_default_argument": (
            "module alias_client\nimport alias_model\n"
            "fn main() -> i32\nprintln(alias_model::defaulted())\nreturn 0\nend fn\n",
            "Default arguments are not lowered yet"),
        "imported_qualified_missing_argument": (
            "module alias_client\nimport alias_model\n"
            "fn main() -> i32\nprintln(alias_model::one())\nreturn 0\nend fn\n",
            "expects 1 arguments, got 0"),
        "imported_qualified_extra_argument": (
            "module alias_client\nimport alias_model\n"
            "fn main() -> i32\nprintln(alias_model::one(1, 2))\nreturn 0\nend fn\n",
            "expects 1 arguments, got 2"),
    }
    controls = {
        "public_method_call":
            "class Example\npub fn value(self) -> i32 { return 7 }\nend class\n"
            "fn main() -> i32\nlet object = Example {}\nprintln(object.value())\nreturn 0\nend fn\n",
        "matching_override": inherited(base, "override fn value(self, argument: i32) -> i32 { return argument }"),
        "unmarked_matching_override": inherited(base, "fn value(self, argument: i32) -> i32 { return argument }"),
        "parameter_rename_and_alias": inherited(base, "override fn value(self, renamed: int) -> int { return renamed }"),
        "generic_alpha_rename": inherited(
            "virtual fn value<T>(self, argument: T) -> T { return argument }",
            "override fn value<U>(self, renamed: U) -> U { return renamed }"),
        "matching_abstract_implementation": inherited(
            "abstract fn value(self, argument: i32) -> i32\nend fn",
            "override fn value(self, argument: i32) -> i32 { return argument }", abstract=True),
        "matching_reference_receiver": inherited(
            "virtual fn value(self: ref<Self>, argument: i32) -> i32 { return argument }",
            "override fn value(self: ref<Self>, argument: i32) -> i32 { return argument }"),
        "matching_aliased_base": program(
            "class Base\n" + base + "\nend class\ntype Parent = Base\n"
            "class Derived inherits Parent\n"
            "override fn value(self, argument: i32) -> i32 { return argument }\nend class"),
        "matching_module_aliases":
            "module origin\npub type Number = i32\n"
            "pub class Base\nvirtual fn value(self: ref<Base>, argument: Number) -> Number { return argument }\nend class\n"
            "module client\ntype Number = i32\n"
            "class Derived inherits origin::Base\n"
            "override fn value(self: ref<Derived>, argument: Number) -> Number { return argument }\nend class\n"
            "fn main() -> i32 { println(7) return 0 }\n",
        "nested_identity_alias": program("type Identity<T> = T")
            .replace("println(7)", "let value: Identity<Identity<i32>> = 7\nprintln(value)"),
        "alias_scalar_storage": """type Word = i32
struct Record { value: Word }
enum Choice
Value(Word)
end enum
fn echo(value: Word) -> Word { return value }
fn main() -> i32
let record = Record { value: 4294967303 }
let choice = Choice.Value(echo(record.value))
match choice
Choice.Value(value) -> println(value)
end match
return 0
end fn
""",
        "imported_alias_qualified": """module alias_client
import alias_model.{Record}
fn same<T>(value: T) -> T { return value as T }
fn main() -> i32
let record = Record { value: 7 }
let alias = record as alias_model::Alias
println(same(alias).value)
return 0
end fn
""",
        "imported_alias_named": """module alias_client
import alias_model.{Record, Alias}
fn same<T>(value: T) -> T { return value as T }
fn main() -> i32
let record = Record { value: 7 }
let alias: Alias = record as Alias
println(same(alias).value)
return 0
end fn
""",
        "nested_open_generic_signature": program("""struct Pair<Key, Value>
first: Key
second: Value
end struct
struct Store<Element>
items: array<Element>
end struct
fn keep<TKey, TValue>(value: Store<Pair<TKey, TValue>>) -> Store<Pair<TKey, TValue>>
return value
end fn
"""),
        "forward_alias_signature": """fn main() -> i32
let value: Word = make()
println(value)
return 0
end fn
fn make() -> Word { return 7 }
type Word = i32
""",
        "imported_callable_alias_identity": """module alias_client
import alias_model.{make}
type Word = i32
fn main() -> i32
let value: text = make()
if value == "owner" then
println(7)
else
return 1
end if
return 0
end fn
""",
        "matching_transitive_override": program(
            "class Base\n" + base + "\nend class\n"
            "class Middle inherits Base\nend class\n"
            "class Derived inherits Middle\n"
            "override fn value(self, argument: i32) -> i32 { return argument }\nend class"),
        "explicit_field_values": """struct Record
value: i32
end struct
class Object
value: i32
end class
fn main() -> i32
let record = Record { value: 3 }
let object = Object { value: 4 }
println(record.value + object.value)
return 0
end fn
""",
    }
    environment = os.environ.copy()
    for variable in ("NY_LIBRARY_PATH", "NIL_LIBRARY_PATH", "NY_LEX_DUMP", "NY_PARSE_TRACE", "NY_STDLIB_PATH"):
        environment.pop(variable, None)
    sentinel = b"previous artifact must survive rejected source\n"
    with tempfile.TemporaryDirectory(prefix="ny-class-contract-") as temporary:
        root = Path(temporary)
        alias_provider = root / "alias_model.ny"
        alias_provider.write_text(
            "module alias_model\npub struct Record { pub value: i32 }\npub type Alias = Record\n"
            "pub type Word = text\npub fn make() -> Word { return \"owner\" }\n"
            "pub fn one(value: i32) -> i32 { return value }\n"
            "pub fn defaulted(value: i32 = 7) -> i32 { return value }\n",
            encoding="utf-8")

        def run(command: list[str]) -> subprocess.CompletedProcess[str]:
            return subprocess.run(command, cwd=root, env=environment, stdin=subprocess.DEVNULL,
                                  capture_output=True, text=True, timeout=60, check=False)

        for name, (source, diagnostic) in cases.items():
            path = root / (name + ".ny")
            path.write_text(source, encoding="utf-8")
            for mode, extra in (("text", []), ("binary", ["--emit-binary-nil"])):
                output = root / (name + "-" + mode + ".nil")
                output.write_bytes(sentinel)
                inputs = [str(path)]
                if name.startswith("imported_"):
                    inputs.append(str(alias_provider))
                result = run([str(compiler), "--no-stdlib", "--no-bundle", "--quiet", *extra,
                              "--output", str(output), *inputs])
                if result.returncode <= 0 or diagnostic not in result.stderr:
                    raise AssertionError(f"{name}/{mode}: expected {diagnostic!r}; exit={result.returncode}\n"
                                         f"{result.stdout}\n{result.stderr}")
                if output.read_bytes() != sentinel:
                    raise AssertionError(f"{name}/{mode}: rejected source replaced prior artifact")
        for name, source in controls.items():
            path = root / (name + ".ny")
            path.write_text(source, encoding="utf-8")
            for mode, extra in (("text", []), ("binary", ["--emit-binary-nil"])):
                output = root / (name + "-" + mode + ".nil")
                inputs = [str(path)]
                if name.startswith("imported_"):
                    inputs.append(str(alias_provider))
                result = run([str(compiler), "--no-stdlib", "--no-bundle", "--quiet", *extra,
                              "--output", str(output), *inputs])
                if result.returncode != 0:
                    raise AssertionError(f"{name}/{mode}: positive compile failed\n{result.stdout}\n{result.stderr}")
                result = run([str(interpreter), str(output)])
                if result.returncode != 0 or result.stdout != "7\n" or result.stderr:
                    raise AssertionError(f"{name}/{mode}: positive execution failed\n{result.stdout}\n{result.stderr}")
    print(f"class contracts: {len(cases) * 2} rejected compilations; {len(controls) * 2} positive controls")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
