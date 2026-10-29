# Getting started with NIL Framework

This guide builds the shared Ny/Matrix SDK and exercises its compiler and
interpreter. The [release profile](../release/v1-profile.md) and
[standard-library reference](../standard-library/index.md) describe the supported
native routes and API boundaries as the framework approaches v1.

## Prerequisites

- CMake 3.21 or newer (SDK test reporting uses CTest's `--output-junit`)
- a C++17 compiler for the SDK itself
- Make, Ninja, or another CMake-supported build tool
- Python 3.12 or newer for the documented Ubuntu 24.04 environment
- Pandoc when packaging the SDK's HTML documentation
- optional LLVM development packages for the LLVM backend
- X11 development headers/libraries on Linux only to enable the experimental
  `nilagent` window-control features

Linux is the supported native release target; other hosts remain experimental.
On Ubuntu, install the development dependencies with:

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build python3 pandoc llvm-dev \
  zlib1g-dev libzstd-dev libx11-dev libsqlite3-dev libpq-dev
```

`libsqlite3-dev` and `libpq-dev` enable the optional SQLite and PostgreSQL
providers; they are not required for the core language. The October 2026
development workstation uses Ubuntu 26.04,
GCC 15.2 and LLVM 21.1.8. This development setup is separate from the published
[release profile](../release/v1-profile.md), which targets Ubuntu 24.04 and LLVM 18.

## Build the SDK

From the repository root, run:

```bash
./scripts/build-sdk.sh
```

By default the helper configures in `build/cmake`, runs CTest, and publishes a
validated SDK at `sdk`. The command executables are directly at that SDK
root. The same directory contains `nillib` with strictly compiled binary NIL
modules, `nilstd` with the matching Ny and Matrix sources, compiled HTML in `docs`, and a
hash-bearing `manifest.json`.

The complete command set is `nilcompiler`, `nilinterpreter`, `nilassembler`,
`nillinker`, `nillsp`, `nildebugger`, `nilschema`, `nilagent`, `nilbuild`, and `aperture` (with the host
platform's executable suffix where needed). The helper validates this layout
and only replaces the active SDK after the entire standard library and smoke
path pass.

The framework rebrand changes the checkout folder to `exec-nil-framework`,
with components such as `nilcompiler`, `nilstd`, `ny`, `matrix`, and `vscode`
directly beneath it; the former `code/` and `ext/` wrapper directories are removed.
The generated SDK is at `<checkout>/sdk`, while build files stay in `build`.
The rebrand also changes
commands from `nycompiler`/`nyinterpreter`/etc. to `nilcompiler`/`nilinterpreter`/etc.,
and SDK directories from `nystd`/`nylib` to `nilstd`/`nillib`. Linux native
libraries are `nillib/native/libnil.so` and `nillib/native/libnil.a`; AOT linking
uses the static companion. Rebuild an SDK as a whole instead of mixing old
and new tools. Existing `.ny`, `.matrix`, `.mat`, and `ny.lang.*` imports retain
their meanings. The VS Code extension keeps its installed identity and
`ny.*` configuration keys; update an explicit `ny.sdkPath` if it names the
old checkout folder. Documented build settings now use `NIL_*`; supported
legacy `NY_*` aliases are accepted when their new equivalents are absent.

A developer build and test run, without packaging the complete SDK, is below.
After moving machines or checkout paths, start with an empty `build/cmake`:
move any copied build directory aside first, because CMake caches absolute
source, compiler and dependency paths.

```bash
cmake -S . -B build/cmake -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DNIL_ENABLE_LLVM=ON \
  -DLLVM_DIR="$(llvm-config --cmakedir)"
cmake --build build/cmake --parallel 6
ctest --test-dir build/cmake --parallel 4 --output-on-failure
```

If selecting a versioned LLVM installation, use its matching `llvm-config-21`
(or other version) in that command. Check the configure output for `Found LLVM`:
`NIL_ENABLE_LLVM=ON` alone does not guarantee that LLVM was discovered.

To package the configured build while working on uncommitted changes, run:

```bash
NIL_ALLOW_DIRTY_SOURCE=ON CMAKE_BUILD_PARALLEL_LEVEL=6 CTEST_PARALLEL_LEVEL=4 \
  ./scripts/build-sdk.sh
```

The helper reuses `build/cmake`, including its selected LLVM installation,
reruns the tests and validates the SDK. This produces a development SDK marked
with its dirty-source status; it does not qualify or publish a release.
Omit `NIL_ALLOW_DIRTY_SOURCE` for a clean checkout. Adjust the parallel job counts
to suit available memory.

LLVM is optional. If CMake cannot find it, configure with
`-DNIL_ENABLE_LLVM=OFF`; with the helper, set `NIL_ENABLE_LLVM=OFF`. The core
compiler, NIL interpreter, debugger, language server, assembler, and linker are
always built. `NIL_BUILD_EXPERIMENTAL_TOOLS=OFF` omits only `nilschema` and
`nilagent` in a direct developer build. The SDK helper keeps the option enabled
because the complete SDK includes all ten commands. Aperture is a separately versioned preview.

When tests are enabled, CMake requires Python rather than silently omitting the
route matrix and application contracts. Set `NIL_RUN_TESTS=OFF` with the helper,
or `-DBUILD_TESTING=OFF` with CMake, only for an intentional test-free build.

## Compile and run a program

Create `hello.ny`:

```ny
fn main() -> i32
    println("Hello from Ny")
    return 0
end fn
```

Compile to a separate NIL file, then run it in the interpreter:

```bash
sdk/nilcompiler --no-stdlib --no-bundle \
  --output hello.nil hello.ny
sdk/nilinterpreter hello.nil
```

This uses `--no-stdlib` because `println` is a compiler/runtime intrinsic and
the broader library is still being repaired. It uses `--no-bundle` so the
compiler always leaves an inspectable `.nil` file instead of attempting to
combine it with an adjacent `nilinterpreter` executable.

## Work with a project

For an application with imports, use a `nil.project.nildata` manifest and
`nilbuild check`, `nilbuild build` or `nilbuild run`. The
[project guide](../tools/projects.md) includes the format and both execution
targets. The same project context is used by the VS Code extension.

## Optional LLVM path

When the SDK was configured with `NIL_ENABLE_LLVM=ON` and LLVM was found,
`nilcompiler --native` selects the LLVM backend. `--jit` requests its JIT mode.
This backend is experimental and more platform-sensitive than the NIL smoke
path. Native AOT linking uses the C++ compiler driver selected by CMake; set
`-DNIL_NATIVE_LINKER_DRIVER=/path/to/driver` at configure time to override it.
See the [tool reference](../tools/index.md) before using it.

## Run tests

```bash
ctest --test-dir build/cmake --output-on-failure
```

The registered tests cover loader validation, argument and exit-code behavior,
textual and binary NIL, bundles, LLVM JIT/AOT when available, focused language
regressions, negative inputs, and every configured production-application
route: interpreter and bundle routes in all builds, plus native routes when
LLVM is available. This is a growing executable baseline, not yet a complete
language conformance suite. See [tests/README.md](../../tests/README.md).

## Editor integration

The first-party Visual Studio Code extension is maintained under
[`vscode`](../../vscode). It provides syntax highlighting, snippets,
SDK discovery, a stdio client for `nillsp`, and commands and tasks for compiling,
running, native builds, SDK builds, and CTest. Build and development directions
are in the extension's [README](../../vscode/README.md).

For other editors, `nillsp` is a stdio LSP server. Configure a generic LSP client
to start the absolute path to `sdk/nillsp`. Its semantic features are
still limited and experimental.

## Next steps

- [Tool reference](../tools/index.md)
- [Examples](../../examples/README.md)
- [Standard-library status](../standard-library/index.md)
- [Language reference](../language-reference/lexical-structure.md)
