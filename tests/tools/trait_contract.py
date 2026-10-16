#!/usr/bin/env python3
"""Require complete, exact trait contracts before publishing text/binary NIL."""

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
    record = "struct Record\nvalue: i64\nend struct\n"
    main = "\nfn main() -> i32\nreturn 0\nend fn\n"

    def implementation(signature: str, body: str = "return 0") -> str:
        return record + "impl Contract for Record\nfn " + signature + "\n" + body + "\nend fn\nend impl\n"

    trait = "trait Contract\nfn read(self, value: i64) -> i64\nend trait\n"
    cases = {
        "missing_method": (trait + record + "impl Contract for Record\nend impl\n", "missing required method"),
        "wrong_return": (trait + implementation("read(self, value: i64) -> i32"), "incompatible return type"),
        "wrong_parameter": (trait + implementation("read(self, value: i32) -> i64"), "incompatible parameter 2"),
        "wrong_count": (trait + implementation("read(self) -> i64"), "incompatible parameter count"),
        "wrong_receiver": (trait + implementation("read(mut self, value: i64) -> i64"), "incompatible parameter 1"),
        "wrong_method_generics": (trait + implementation("read<T>(self, value: i64) -> i64"), "incompatible generic parameter count"),
        "not_a_trait": (record + "impl Record for Record\nend impl\n", "Unknown trait or invalid trait arguments"),
        "unknown_trait": (record + "impl Missing for Record\nend impl\n", "Unknown trait or invalid trait arguments"),
        "duplicate_trait_method": ("trait Contract\nfn read(self) -> i64\nfn read(self) -> i64\nend trait\n", "Duplicate trait method"),
        "duplicate_impl_method": ("trait Contract\nfn read(self) -> i64\nend trait\n" + record +
                                  "impl Contract for Record\nfn read(self) -> i64\nreturn 1\nend fn\nfn read(self) -> i64\nreturn 2\nend fn\nend impl\n", "Duplicate impl method"),
        "wrong_self_return": ("trait Contract\nfn copy(self) -> Self\nend trait\n" + implementation("copy(self) -> i64"), "incompatible return type"),
        "wrong_trait_argument": ("trait Contract<T>\nfn read(self) -> T\nend trait\n" + record +
                                 "impl Contract<i64> for Record\nfn read(self) -> i32\nreturn 0\nend fn\nend impl\n", "incompatible return type"),
        "generic_bounds": ("trait Bound\nend trait\ntrait Contract\nfn echo<T>(self, value: T) -> T\nend trait\n" +
                           implementation("echo<U: Bound>(self, value: U) -> U", "return value"), "incompatible generic bounds"),
        "associated_type": ("trait Contract\ntype Item = i32\nend trait\n", "Unsupported trait member"),
        "unknown_member": ("trait Contract\nlet missing = 42\nend trait\n", "Unsupported trait member"),
        "supertrait": ("trait Parent\nend trait\ntrait Child : Parent\nend trait\n", "Unsupported trait member"),
        "self_constraint": ("trait Parent\nend trait\ntrait Child where Self: Parent\nend trait\n", "Unknown generic parameter"),
        "default_method": ("trait Contract\nfn read(self) -> i64\nreturn 1\nend fn\nend trait\n", "Default trait methods are not supported"),
        "method_self_constraint": ("trait Parent\nend trait\ntrait Contract\nfn read(self) -> i64 where Self: Parent\nend trait\n", "Unknown generic parameter"),
        "unknown_signature_type": ("trait Contract\nfn read(self, value: Missing) -> i64\nend trait\n", "Unknown type in trait method parameter"),
        "not_a_bound": (record + "fn invalid<T: Record>(value: T) -> i32\nreturn 0\nend fn\n", "Unknown trait"),
        "not_an_impl_bound": (record + "impl<T: Record> Record\nend impl\n", "Unknown trait"),
        "trait_argument_arity": ("trait Contract<T>\nend trait\n" + record +
                                  "impl Contract<i32, i64> for Record\nend impl\n", "invalid trait arguments"),
        "trait_argument_bound": ("trait Marker\nend trait\ntrait Contract<T: Marker>\nend trait\n" + record +
                                  "impl Contract<i32> for Record\nend impl\n", "does not implement required trait"),
        "variadic_changes_contract": ("trait Contract\nfn read(self, values: array<i32>) -> i64\nend trait\n" +
                                      implementation("read(self, ..values: i32) -> i64"), "incompatible parameter 2"),
        "function_unknown_where": ("fn read() -> i64 where Self: Missing\nreturn 0\nend fn\n", "Unknown generic parameter"),
        "alias_unknown_where": ("type Value<T> where Missing: Bound = T\n", "Unknown generic parameter"),
        "struct_unknown_where": ("struct Value<T> where Missing: Bound\nvalue: T\nend struct\n", "Unknown generic parameter"),
        "enum_unknown_where": ("enum Value<T> where Missing: Bound\nSome(T)\nend enum\n", "Unknown generic parameter"),
        "class_unknown_where": ("class Value<T> where Missing: Bound\nend class\n", "Unknown generic parameter"),
        "impl_unknown_where": (record + "impl<T> where Missing: Bound Record\nend impl\n", "Unknown generic parameter"),
        "impl_nonmethod_member": (record + "impl Record\nlet value = 1\nend impl\n", "Unsupported impl member"),
        "alias_nontrait_bound": (record + "type Value<T: Record> = T\n", "Unknown trait"),
        "enum_nontrait_bound": (record + "enum Value<T: Record>\nSome(T)\nend enum\n", "Unknown trait"),
        "class_nontrait_bound": (record + "class Value<T: Record>\nend class\n", "Unknown trait"),
        "impl_target_argument_arity": ("trait Contract\nend trait\nstruct Value<T>\nvalue: T\nend struct\n"
                                      "impl Contract for Value<i32, i64>\nend impl\n", "Invalid generic argument count for impl target"),
        "trait_as_impl_target": ("trait Contract\nend trait\ntrait Other\nend trait\n"
                                "impl Contract for Other\nend impl\n", "cannot be an impl target"),
        "associated_requires_bound": ("fn make<T>() -> T\nreturn T.default()\nend fn\n",
                                      "No associated method"),
        "associated_requires_arguments": ("trait Factory\nfn create(value: i64) -> Self\nend trait\n"
                                          "fn make<T: Factory>() -> T\nreturn T.create()\nend fn\n",
                                          "expects 1 arguments"),
        "associated_checks_argument_type": ("trait Factory\nfn create(value: i64) -> Self\nend trait\n"
                                           "fn make<T: Factory>() -> T\nreturn T.create(\"wrong\")\nend fn\n",
                                           "argument 1 has incompatible type"),
        "associated_requires_static_method": ("trait Reader\nfn read(self) -> i64\nend trait\n"
                                              "fn make<T: Reader>() -> i64\nreturn T.read()\nend fn\n",
                                              "requires a value receiver"),
        "associated_ambiguous_bounds": ("trait First\nfn create() -> Self\nend trait\n"
                                        "trait Second\nfn create() -> Self\nend trait\n"
                                        "fn make<T: First + Second>() -> T\nreturn T.create()\nend fn\n",
                                        "Ambiguous associated method"),
        "associated_ambiguous_specializations": ("trait Factory<A>\nfn create(value: A) -> Self\nend trait\n"
                                                 "fn make<T: Factory<i32> + Factory<i64>>() -> T\nreturn T.create(1)\nend fn\n",
                                                 "Ambiguous associated method"),
    }
    generic_prelude = (
        "trait Marker\nend trait\ntrait Reading<T>\nend trait\n"
        "struct Box<T>\nvalue: T\nend struct\n"
        "fn require<X: Reading<i64>>(value: X) -> i32\nreturn 1\nend fn\n"
    )
    cases.update({
        "generic_trait_arguments_must_agree": (
            generic_prelude + "impl<T> Reading<T> for Box<T>\nend impl\n"
            "fn probe() -> i32\nreturn require<Box<i32>>(Box<i32> { value: 1 })\nend fn\n",
            "does not implement trait"),
        "concrete_impl_is_not_blanket": (
            generic_prelude + "impl Reading<i64> for Box<i32>\nend impl\n"
            "fn probe() -> i32\nreturn require<Box<i64>>(Box<i64> { value: 1 })\nend fn\n",
            "does not implement trait"),
        "blanket_impl_bounds_apply": (
            generic_prelude + "impl<T: Marker> Reading<T> for Box<T>\nend impl\n"
            "fn probe() -> i32\nreturn require<Box<i64>>(Box<i64> { value: 1 })\nend fn\n",
            "does not implement trait"),
        "repeated_impl_parameter_must_agree": (
            "trait Marker\nend trait\nstruct Pair<L, R>\nleft: L\nright: R\nend struct\n"
            "impl<T> Marker for Pair<T, T>\nend impl\n"
            "fn require<X: Marker>(value: X) -> i32\nreturn 1\nend fn\n"
            "fn probe() -> i32\nreturn require<Pair<i32, i64>>(Pair<i32, i64> { left: 1, right: 2 })\nend fn\n",
            "does not implement trait"),
    })
    # No main suffix here: EOF itself is the missing-terminator boundary.
    unterminated = {
        "unterminated_trait": ("trait Contract\nfn read(self) -> i64\n", "Unterminated trait declaration"),
        "unterminated_method": ("trait Contract\nfn read(self) -> i64\nreturn 1\n", "Unterminated trait method"),
        "unterminated_impl": (record + "impl Record\n", "Unterminated impl declaration"),
    }
    cases = {name: (source + main, diagnostic) for name, (source, diagnostic) in cases.items()} | unterminated
    controls = {
        "method_generic_rename": "trait Contract\nfn echo<T>(self, value: T) -> T\nend trait\n" +
                                 implementation("echo<U>(self, value: U) -> U", "return value") + main,
        "explicit_empty_signature": "trait Contract\nfn read(self) -> i64\nend fn\nend trait\n" +
                                    implementation("read(self) -> i64") + main,
        "bounded_blanket_impl": generic_prelude +
                                "impl<T: Marker> Reading<T> for Box<T>\nend impl\n"
                                "fn probe() -> i32\nreturn require<Box<i64>>(Box<i64> { value: 1 })\nend fn\n"
                                "impl Marker for i64\nend impl\n" + main,
        "signature_alias": "type Word = i64\ntrait Contract\nfn read(self, value: Word) -> Word\nend trait\n" +
                           implementation("read(self, value: i64) -> i64", "return value") + main,
        "trait_argument_bound_proven": "trait Marker\nend trait\ntrait Contract<T: Marker>\nend trait\n" +
                                      record + "impl Contract<i32> for Record\nend impl\nimpl Marker for i32\nend impl\n" + main,
        "associated_duplicate_bound": "trait Factory<A>\nfn create(value: A) -> Self\nend trait\n"
                                      "fn make<T: Factory<i32> + Factory<i32>>() -> T\nreturn T.create(1)\nend fn\n" + main,
    }
    environment = os.environ.copy()
    for variable in ("NY_LIBRARY_PATH", "NIL_LIBRARY_PATH", "NY_LEX_DUMP", "NY_PARSE_TRACE", "NY_STDLIB_PATH"):
        environment.pop(variable, None)
    sentinel = b"previous artifact must survive rejected source\n"
    with tempfile.TemporaryDirectory(prefix="ny-trait-contract-") as temporary:
        root = Path(temporary)
        for name, (source, diagnostic) in cases.items():
            path = root / (name + ".ny")
            path.write_text(source, encoding="utf-8")
            for mode, extra in (("text", []), ("binary", ["--emit-binary-nil"])):
                output = root / (name + "-" + mode + ".nil")
                output.write_bytes(sentinel)
                result = subprocess.run(
                    [str(compiler), "--no-stdlib", "--no-bundle", "--quiet", *extra,
                     "--output", str(output), str(path)], cwd=root, env=environment,
                    stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=30, check=False)
                if result.returncode <= 0 or diagnostic not in result.stderr:
                    raise AssertionError(f"{name}/{mode}: expected {diagnostic!r}; exit={result.returncode}\n{result.stdout}\n{result.stderr}")
                if output.read_bytes() != sentinel:
                    raise AssertionError(f"{name}/{mode}: rejected source replaced prior artifact")
        for name, source in controls.items():
            path = root / (name + ".ny")
            path.write_text(source, encoding="utf-8")
            for mode, extra in (("text", []), ("binary", ["--emit-binary-nil"])):
                result = subprocess.run(
                    [str(compiler), "--no-stdlib", "--no-bundle", "--quiet", *extra,
                     "--output", str(root / (name + "-" + mode + ".nil")), str(path)], cwd=root, env=environment,
                    stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=30, check=False)
                if result.returncode != 0:
                    raise AssertionError(f"{name}/{mode}: valid contract rejected\n{result.stdout}\n{result.stderr}")
    print(f"trait contracts: {len(cases) * 2} rejected compilations; {len(controls) * 2} accepted controls")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
