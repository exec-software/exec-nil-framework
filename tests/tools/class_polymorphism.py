#!/usr/bin/env python3
"""Check directional class conversions, dispatch proofs, and reference invariance."""

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
    declarations = """class Base
value: i32
pub virtual fn read(self) -> i32 { return self.value }
end class
class Derived inherits Base
extra: i32
pub override fn read(self) -> i32 { return self.value + self.extra }
end class
class Other
value: i32
end class
"""
    base = "Base { value: 1 }"
    derived = "Derived { value: 2, extra: 3 }"

    def program(body: str, extra: str = "") -> str:
        return declarations + extra + "\nfn main() -> i32\n" + body + "\nreturn 0\nend fn\n"

    negatives = {
        "downcast_variable": program(f"let value: Derived = {base}"),
        "downcast_assignment": program(f"let mut value = {derived}\nvalue = {base}"),
        "downcast_parameter": program(f"accept({base})", "fn accept(value: Derived) -> i32 { return 1 }"),
        "downcast_return": program("", f"fn make() -> Derived {{ return {base} }}"),
        "downcast_field": program(f"let value = Holder {{ item: {base} }}", "struct Holder { item: Derived }"),
        "unrelated": program("let value: Base = Other { value: 1 }"),
        "reference_covariance": program(f"let value = {derived}\nlet reference: ref<Base> = ref value"),
        "pointer_covariance": program("let pointer = allocate<Derived>(1)\nlet base: ptr<Base> = pointer"),
        "array_covariance": program(f"let values = [{derived}]\nlet bases: array<Base> = values"),
        "inferred_if_base_first": program(f"let value = if true then {base} else {derived} end if"),
        "inferred_if_derived_first": program(f"let value = if true then {derived} else {base} end if"),
        "inferred_array_base_first": program(f"let values = [{base}, {derived}]"),
        "inferred_array_derived_first": program(f"let values = [{derived}, {base}]"),
        "unknown_to_class": program("let value: Base = unknown()", "fn unknown() -> any { return 1 }"),
        "class_to_unknown": program(f"accept({derived})", "fn accept(value: any) -> i32 { return 1 }"),
        "integer_cast": program("let value = 7 as Base"),
        "explicit_downcast": program(f"let value = {base} as Derived"),
        "unrelated_parameter": program("accept(Other { value: 1 })", "fn accept(value: Base) -> i32 { return 1 }"),
        "abstract_construction": program("let value = Abstract {}", "abstract class Abstract\nend class"),
        "reserved_bare_i64": "class i64\nend class\nfn main() -> i32 { return 0 }\n",
        "reserved_bare_text": "class Text\nend class\nfn main() -> i32 { return 0 }\n",
    }
    positives = {
        "qualified_scalar_like_class": ("""module names
pub class Text
value: i32
pub fn read(self) -> i32 { return self.value }
end class
module client
fn main() -> i32
let value = names::Text { value: 7 }
println(value.read())
return 0
end fn
""", "7\n"),
        "explicit_boundaries": (program(f"""let child = {derived}
let mut parent: Base = child
let holder = Holder {{ item: child }}
println(parent.read())
println(accept(child))
println(make().read())
holder.item = child
holder.item.value = 9
println(child.read())
parent = child
println(parent.read())""", f"""struct Holder {{ item: Base }}
fn accept(value: Base) -> i32 {{ return value.read() }}
fn make() -> Base {{ return {derived} }}"""), "5\n5\n5\n12\n12\n"),
        "inherited_self_escape": ("""class Parent
value: i32
pub virtual fn read(self) -> i32 { return self.value }
pub fn identity(self) -> Parent { return self }
pub fn alias(self) -> i32 { let other = self return other.read() }
pub fn argument(self) -> i32 { return accept(self) }
end class
class Child inherits Parent
pub override fn read(self) -> i32 { return self.value + 1 }
end class
fn accept(value: Parent) -> i32 { return value.read() }
fn main() -> i32
let child = Child { value: 7 }
println(child.identity().read())
println(child.alias())
println(child.argument())
return 0
end fn
""", "8\n8\n8\n"),
        "abstract_receivers": ("""abstract class Abstract
pub abstract fn read(self) -> i32
end fn
pub fn relay(self) -> i32 { return self.read() }
pub fn other(self, value: Abstract) -> i32 { return value.read() }
end class
class Concrete inherits Abstract
pub override fn read(self) -> i32 { return 7 }
end class
fn accept(value: Abstract) -> i32 { return value.relay() }
fn main() -> i32
let value: Abstract = Concrete {}
println(value.read())
println(accept(value))
println(value.other(value))
return 0
end fn
""", "7\n7\n7\n"),
        "unmarked_virtual_chain": ("""class Parent
pub virtual fn read(self) -> i32 { return 1 }
end class
class Middle inherits Parent
pub fn read(self) -> i32 { return 2 }
end class
class Child inherits Middle
pub override fn read(self) -> i32 { return 3 }
end class
fn main() -> i32
let parent: Parent = Child {}
let middle: Middle = Child {}
println(parent.read())
println(middle.read())
return 0
end fn
""", "3\n3\n"),
        "exact_reference_receiver": ("""class Parent
value: i32
pub virtual fn read(self: ref<Self>) -> i32 { return self.value }
fn secret(self: ref<Self>) -> i32 { return self.value + 10 }
pub fn inherited(self: ref<Self>) -> i32 { return self.read() + self.secret() }
end class
class Child inherits Parent
pub override fn read(self: ref<Self>) -> i32 { return self.value + 1 }
end class
fn main() -> i32
let child = Child { value: 7 }
let reference = ref child
println(reference.read())
println(reference.inherited())
let parent: Parent = child
let parent_reference = ref parent
println(parent_reference.read())
println(parent_reference.inherited())
return 0
end fn
""", "8\n25\n8\n25\n"),
    }
    environment = {key: value for key, value in os.environ.items() if not key.startswith("NY_")}
    sentinel = b"preserve previous artifact\n"
    with tempfile.TemporaryDirectory(prefix="ny-class-polymorphism-") as temporary:
        root = Path(temporary)
        for name, source in negatives.items():
            path = root / f"{name}.ny"
            path.write_text(source, encoding="utf-8")
            for mode, flags in (("text", []), ("binary", ["--emit-binary-nil"]),
                                ("unchecked-text", ["--no-typecheck"]),
                                ("unchecked-binary", ["--no-typecheck", "--emit-binary-nil"])):
                output = root / f"{name}-{mode}.nil"
                output.write_bytes(sentinel)
                result = subprocess.run([str(compiler), "--no-stdlib", "--no-bundle", "--quiet",
                                         *flags, "--output", str(output), str(path)],
                                        cwd=root, env=environment, stdin=subprocess.DEVNULL,
                                        capture_output=True, text=True, timeout=30, check=False)
                if result.returncode <= 0 or not any(message in result.stderr for message in (
                    "Type checking failed:", "Code generation failed", "codegen", "error:", "Error:"
                )):
                    raise AssertionError(f"{name}/{mode}: expected semantic rejection; {result.returncode}\n{result.stdout}\n{result.stderr}")
                if "Parse error" in result.stderr or "Syntax error" in result.stderr:
                    raise AssertionError(f"{name}/{mode}: test did not reach semantic checking\n{result.stderr}")
                if output.read_bytes() != sentinel:
                    raise AssertionError(f"{name}/{mode}: rejection replaced prior artifact")
        for name, (source, expected) in positives.items():
            path = root / f"{name}.ny"
            path.write_text(source, encoding="utf-8")
            for mode, flags in (("text", []), ("binary", ["--emit-binary-nil"])):
                output = root / f"{name}-{mode}.nil"
                compiled = subprocess.run([str(compiler), "--no-stdlib", "--no-bundle", "--quiet",
                                           *flags, "--output", str(output), str(path)],
                                          cwd=root, env=environment, stdin=subprocess.DEVNULL,
                                          capture_output=True, text=True, timeout=30, check=False)
                if compiled.returncode != 0:
                    raise AssertionError(f"{name}/{mode}: compile failed\n{compiled.stdout}\n{compiled.stderr}")
                executed = subprocess.run([str(interpreter), str(output)], cwd=root, env=environment,
                                          stdin=subprocess.DEVNULL, capture_output=True, text=True,
                                          timeout=30, check=False)
                if (executed.returncode, executed.stdout, executed.stderr) != (0, expected, ""):
                    raise AssertionError(f"{name}/{mode}: execution mismatch\n{executed.stdout}\n{executed.stderr}")

        # Exercise the actual binary-library discovery/import path, not merely
        # round-tripping an executable container. The original provider source
        # is removed before either consumer compilation.
        provider_root = root / "provider"
        provider_root.mkdir()
        provider = provider_root / "polymorphism.ny"
        provider.write_text("""module ny.test.polymorphism
pub abstract class Base
value: i32
pub abstract fn read(self) -> i32
end fn
pub fn relay(self) -> i32 { return self.read() * 2 }
pub fn identity(self) -> Base { return self }
end class
pub class Derived inherits Base
extra: i32
pub override fn read(self) -> i32 { return self.value + self.extra }
end class
pub fn make() -> Base { return Derived { value: 30, extra: 1 } }
""", encoding="utf-8")
        library_root = root / "binary-sdk"
        library = library_root / "ny" / "test" / "polymorphism.nil"
        library.parent.mkdir(parents=True)
        compiled_library = subprocess.run(
            [str(compiler), "--no-stdlib", "--no-bundle", "--quiet",
             "--emit-library", "--emit-binary-nil", "--output", str(library), str(provider)],
            cwd=provider_root, env=environment, stdin=subprocess.DEVNULL,
            capture_output=True, text=True, timeout=30, check=False)
        if compiled_library.returncode != 0:
            raise AssertionError(f"class library compilation failed\n{compiled_library.stdout}\n{compiled_library.stderr}")
        header = library.read_bytes()[:16]
        if (header[:4], int.from_bytes(header[4:8], "little"),
                int.from_bytes(header[8:12], "little")) != (b"NIL\0", 4, 1):
            raise AssertionError("class library did not use the v4 library container")
        provider.unlink()
        provider_root.rmdir()
        if any(library_root.rglob("*.ny")) or provider.exists():
            raise AssertionError("binary library proof retained source files")
        consumer_root = root / "consumer"
        consumer_root.mkdir()
        consumer = consumer_root / "main.ny"
        consumer.write_text("""module client
import ny.test.polymorphism.{Base, Derived, make}
fn main() -> i32
let derived = Derived { value: 20, extra: 1 }
let base: Base = derived
println(base.relay())
println(base.identity().read())
println(make().read())
return 0
end fn
""", encoding="utf-8")
        for mode, flags in (("text", []), ("binary", ["--emit-binary-nil"])):
            output = consumer_root / f"consumer-{mode}.nil"
            compiled = subprocess.run(
                [str(compiler), "--no-stdlib", "--no-bundle", "--quiet",
                 "--sdk-path", str(library_root), *flags, "--output", str(output), str(consumer)],
                cwd=consumer_root, env=environment, stdin=subprocess.DEVNULL,
                capture_output=True, text=True, timeout=30, check=False)
            if compiled.returncode != 0:
                raise AssertionError(f"binary-only class library consumer/{mode} failed\n{compiled.stdout}\n{compiled.stderr}")
            executed = subprocess.run([str(interpreter), str(output)], cwd=consumer_root,
                                      env=environment, stdin=subprocess.DEVNULL,
                                      capture_output=True, text=True, timeout=30, check=False)
            if (executed.returncode, executed.stdout, executed.stderr) != (0, "42\n21\n31\n", ""):
                raise AssertionError(f"binary-only class library consumer/{mode} execution mismatch\n{executed.stdout}\n{executed.stderr}")
    print(f"class polymorphism: {len(negatives) * 4} rejected compilations; "
          f"{len(positives) * 2} executed positive controls; 2 binary-only library consumer executions")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
