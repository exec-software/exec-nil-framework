# Ny examples

Ny is early-stage, and examples can become stale as the parser and backends
converge. This page therefore starts with the same minimal program used by the
CTest smoke path instead of presenting unverified standard-library APIs as
working examples.

## Hello from Ny

```ny
fn main() -> i32
    println("Hello from Ny")
    return 0
end fn
```

From the repository root, after running `./scripts/build-sdk.sh`:

```bash
sdk/nilcompiler --no-stdlib --no-bundle \
  --output hello.nil tests/smoke/hello.ny
sdk/nilinterpreter hello.nil
```

Expected program output:

```text
Hello from Ny
```

The checked-in source is [`tests/smoke/hello.ny`](../tests/smoke/hello.ny).

## Function keyword fixture

The parser accepts both short and long function keywords in the development
fixture:

```ny
fn short_form()
    println("short form")
end fn

function long_form()
    println("long form")
end function
```

See [`tests/language/function-keywords.ny`](../tests/language/function-keywords.ny).
Files in `tests/language` are regression inputs, not a language-conformance
promise; some exercise parser/compiler behavior without being registered as
runtime tests.

## Application conformance suite

[`applications/`](applications/README.md) contains eight deterministic,
production-shaped programs covering control flow, recursion, arrays, structs,
traits, enums, generics, modules, manual memory, and `@ai_context` metadata.
Each application has exact output and exit contracts and runs through the
textual and binary interpreter paths, both bundle formats, LLVM JIT, and LLVM
AOT. Known native-backend gaps are explicit XFAILs rather than silent success.

## Larger experimental programs

`nilaix/examples` and the first-party trees under `packages` contain
larger UI, IDE, engine, and integration experiments. They are useful design and
compiler-development material, but they are not curated, supported examples and
may require native dependencies or unfinished language features.

For syntax beyond the smoke fixtures, consult the
[language reference](../docs/language-reference/lexical-structure.md) together
with current tests and compiler diagnostics. The reference includes intended
syntax that may not be implemented uniformly across all backends.
