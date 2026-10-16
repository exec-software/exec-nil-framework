#!/usr/bin/env python3
"""Reject unsound class construction/dispatch before publishing NIL."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    args = parser.parse_args()
    compiler = args.compiler.resolve(strict=True)

    def program(declarations: str, body: str) -> str:
        return declarations + "\nfn main() -> i32\n" + body + "\nreturn 0\nend fn\n"

    base = "class Base\nvalue: i32\npub virtual fn score(self) -> i32 { return self.value }\nend class\n"
    derived = "class Derived inherits Base\nextra: i64\nend class\n"
    abstract_base = "abstract class AbstractBase\npub abstract fn read(self) -> i32\nend fn\npub fn relay(self) -> i32 { return self.read() }\nend class\n"
    reabstract = ("class Parent\npub virtual fn read(self) -> i32 { return 1 }\n"
                  "pub fn relay(self) -> i32 { return self.read() }\nend class\n"
                  "abstract class Middle inherits Parent\npub abstract override fn read(self) -> i32\nend fn\nend class\n")
    cases = {
        "missing_inherited_field": (program(base + derived, "let value = Derived { extra: 9 }"), "missing required field 'value'"),
        "unknown_field": (program(base, "let value = Base { value: 1, missing: 2 }"), "contains unknown field 'missing'"),
        "duplicate_field": (program(base, "let value = Base { value: 1, value: 2 }"), "initializes field 'value' more than once"),
        "wrong_field_type": (program(base + derived, 'let value = Derived { value: "wrong", extra: 2 }'), "Type mismatch in class field 'value'"),
        "inherited_write_type": (program(base + derived, 'let value = Derived { value: 1, extra: 2 }\nvalue.value = "wrong"'), "Type mismatch in field assignment"),
        "downcast": (program(base + derived, "let value: Derived = Base { value: 1 }"), "Type mismatch in variable initialization"),
        "same_spelling_nominal_identity": (
            "module left\npub class Item\nvalue: i32\nend class\n"
            "module right\npub class Item\nvalue: i32\nend class\n"
            "fn main() -> i32\nlet value: left::Item = Item { value: 1 }\nreturn 0\nend fn\n",
            "Type mismatch in variable initialization"),
        "private_method": (program("class Secret\nfn read(self) -> i32 { return 7 }\nend class", "let value = Secret {}\nprintln(value.read())"), "is private"),
        "inherited_private_method": (program("class Secret\nfn read(self) -> i32 { return 7 }\nend class\nclass Child inherits Secret\nend class", "let value = Child {}\nprintln(value.read())"), "is private"),
        "method_as_field": (program(base, "let value = Base { value: 1 }\nlet invalid = value.score"), "Class method values are unsupported"),
        "class_type_receiver": (program(base, "println(Base.score())"), "requires a value receiver"),
        "method_extra_argument": (program(base, "let value = Base { value: 1 }\nprintln(value.score(3))"), "expects 1 arguments, got 2"),
        "method_argument_type": (program("class Item\npub fn add(self, amount: i32) -> i32 { return amount }\nend class", 'let value = Item {}\nprintln(value.add("wrong"))'), "argument 1 has incompatible type"),
        "duplicate_method": (program("class Item\nfn read(self) -> i32 { return 1 }\nfn read(self) -> i32 { return 2 }\nend class", ""), "Duplicate class method"),
        "field_method_collision": (program("class Item\nread: i32\nfn read(self) -> i32 { return 1 }\nend class", ""), "is both a field and a method"),
        "inherited_field_collision": (program(base + "class Child inherits Base\nvalue: i64\nend class", ""), "Duplicate or ambiguous inherited class field"),
        "diamond_field_collision": (program(base + "class Left inherits Base\nend class\nclass Right inherits Base\nend class\nclass Diamond inherits Left, Right\nend class", ""), "Duplicate or ambiguous inherited class field"),
        "nonvirtual_hiding": (program("class Parent\npub fn read(self) -> i32 { return 1 }\nend class\nclass Child inherits Parent\npub fn read(self) -> i32 { return 2 }\nend class", ""), "Cannot hide non-virtual inherited method"),
        "abstract_construction": (program("abstract class Item\nend class", "let value = Item {}"), "Cannot construct abstract class"),
        "generic_construction": (program("class Item<T>\nvalue: T\nend class", "let value = Item<i32> { value: 1 }"), "Generic class construction is not supported"),
        "receiver_shape": (program("class Item\npub fn read(self: ref<Self>) -> i32 { return 1 }\nend class", "let value = Item {}\nprintln(value.read())"), "Class method receiver type mismatch"),
        "variadic_method": (program("class Item\npub fn read(self, ..values: i32) -> i32 { return 1 }\nend class", "let value = Item {}\nprintln(value.read(1, 2))"), "Variadic class method execution is not supported"),
        "float_argument_pointer": (program("class Item\npub fn read(self, value: f64) -> f64 { return value }\nend class", "let item = Item {}\nlet pointer = allocate<i32>(1)\nprintln(item.read(pointer))\ndeallocate(pointer)"), "argument 1 has incompatible type"),
        "byte_argument_text": (program("class Item\npub fn read(self, value: u8) -> u8 { return value }\nend class", 'let item = Item {}\nprintln(item.read("wrong"))'), "argument 1 has incompatible type"),
        "float_argument_implicit_integer": (program("class Item\npub fn read(self, value: f64) -> f64 { return value }\nend class", "let item = Item {}\nprintln(item.read(7))"), "argument 1 has incompatible type"),
        "byte_argument_implicit_integer": (program("class Item\npub fn read(self, value: u8) -> u8 { return value }\nend class", "let item = Item {}\nprintln(item.read(511))"), "argument 1 has incompatible type"),
        "byte_argument_implicit_negative": (program("class Item\npub fn read(self, value: u8) -> u8 { return value }\nend class", "let item = Item {}\nprintln(item.read(-1))"), "argument 1 has incompatible type"),
        "concrete_own_abstract_method": (program("class Item\npub abstract fn read(self) -> i32\nend fn\nend class", "let item = Item {}"), "must implement abstract method 'read'"),
        "concrete_own_reabstract_method": (program("class Parent\npub virtual fn read(self) -> i32 { return 1 }\nend class\nclass Item inherits Parent\npub abstract override fn read(self) -> i32\nend fn\nend class", "let item = Item {}"), "must implement abstract method 'read'"),
        "reabstract_inherited_relay": (program(reabstract + "class Leaf inherits Middle\nend class", "let item = Leaf {}\nprintln(item.relay())"), "must implement abstract method 'read'"),
        "reabstract_transitive": (program(reabstract + "abstract class Gap inherits Middle\nend class\nclass Leaf inherits Gap\nend class", "let item = Leaf {}"), "must implement abstract method 'read'"),
        "reabstract_forward_declaration": (program("class Leaf inherits Middle\nend class\n" + reabstract, "let item = Leaf {}"), "must implement abstract method 'read'"),
        "reabstract_module_qualified": (
            "module provider\n" + reabstract.replace("class Parent", "pub class Parent").replace("abstract class Middle", "pub abstract class Middle") +
            "module consumer\nclass Leaf inherits provider::Middle\nend class\nfn main() -> i32\nlet item = Leaf {}\nreturn 0\nend fn\n",
            "must implement abstract method 'read'"),
        "abstract_method_hidden_by_field": (program(abstract_base + "class Child inherits AbstractBase\nread: i32\nend class", "let item = Child { read: 1 }"), "is both a field and an inherited method"),
    }
    environment = {name: value for name, value in os.environ.items() if not name.startswith("NY_")}
    sentinel = b"existing artifact must survive rejection\n"
    with tempfile.TemporaryDirectory(prefix="ny-class-execution-") as temporary:
        root = Path(temporary)
        for name, (source, diagnostic) in cases.items():
            path = root / (name + ".ny")
            path.write_text(source, encoding="utf-8")
            for mode, extra in (("text", []), ("binary", ["--emit-binary-nil"]),
                                ("unchecked-text", ["--no-typecheck"]),
                                ("unchecked-binary", ["--no-typecheck", "--emit-binary-nil"])):
                output = root / (name + "-" + mode + ".nil")
                output.write_bytes(sentinel)
                result = subprocess.run([str(compiler), "--no-stdlib", "--no-bundle", "--quiet",
                                         *extra, "--output", str(output), str(path)],
                                        cwd=root, env=environment, stdin=subprocess.DEVNULL,
                                        capture_output=True, text=True, timeout=30, check=False)
                diagnostic_found = diagnostic in result.stderr or (
                    mode.startswith("unchecked-") and
                    "class lowering requires checked class identity" in result.stderr)
                if result.returncode <= 0 or not diagnostic_found:
                    raise AssertionError(f"{name}/{mode}: expected {diagnostic!r}; exit={result.returncode}\n{result.stdout}\n{result.stderr}")
                if output.read_bytes() != sentinel:
                    raise AssertionError(f"{name}/{mode}: rejection replaced prior artifact")
        unchecked = root / "unchecked.ny"
        unchecked.write_text(program(base, "let value = Base {}\nprintln(value.score())"), encoding="utf-8")
        for extra in ([], ["--emit-binary-nil"]):
            output = root / "unchecked.nil"
            output.write_bytes(sentinel)
            result = subprocess.run([str(compiler), "--no-stdlib", "--no-bundle", "--quiet", "--no-typecheck",
                                     *extra, "--output", str(output), str(unchecked)],
                                    cwd=root, env=environment, stdin=subprocess.DEVNULL,
                                    capture_output=True, text=True, timeout=30, check=False)
            if result.returncode <= 0 or "class lowering requires checked class identity" not in result.stderr:
                raise AssertionError(f"unchecked class escaped fail-closed lowering: {result.stdout}\n{result.stderr}")
            if output.read_bytes() != sentinel:
                raise AssertionError("unchecked rejection replaced prior artifact")
        fixture = Path(__file__).resolve().parents[1] / "language" / "class-execution.ny"
        output = root / "checked-schema.nil"
        result = subprocess.run([str(compiler), "--no-stdlib", "--no-bundle", "--quiet",
                                 "--output", str(output), str(fixture)],
                                cwd=root, env=environment, stdin=subprocess.DEVNULL,
                                capture_output=True, text=True, timeout=30, check=False)
        if result.returncode != 0:
            raise AssertionError(f"checked class schema compile failed: {result.stdout}\n{result.stderr}")
        schema = output.read_text(encoding="utf-8")
        for exact in ("struct model::Base\n", "struct other::Base\n", "struct client::Leaf\n",
                      "function model::Base::keep_base(self: model::Base, other: model::Base) -> model::Base:",
                      "function model::Base::make_base(self: model::Base) -> model::Base:",
                      "function model::Middle::score(self: model::Middle) -> i32:",
                      "struct abstract_model::Reader\n", "struct abstract_model::Suspended\n",
                      "function abstract_model::Reader::relay(self: abstract_model::Reader) -> i32:"):
            if exact not in schema:
                raise AssertionError(f"exact class NIL schema missing: {exact!r}")
        for inherited in ("client::Leaf", "client::ResumedLeaf"):
            if f"function {inherited}::" in schema:
                raise AssertionError(f"inherited method was cloned into receiver: {inherited}")
        for abstract in ("abstract_model::Reader", "abstract_model::Suspended"):
            if f"function {abstract}::read(" in schema:
                raise AssertionError(f"abstract method has an executable body: {abstract}")
    print(f"class execution: {len(cases) * 4 + 2} rejected compilations; exact NIL class schemas checked")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
