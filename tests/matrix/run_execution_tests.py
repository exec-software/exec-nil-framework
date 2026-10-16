#!/usr/bin/env python3
"""Matrix execution conformance.

Each tests/matrix/execution/*.matrix page is compiled to NIL and run on the
interpreter; its stdout and exit status must match the matching *.expected
file byte for byte.

The point is not that the parser accepts the page -- the lexer tests already
cover acceptance. The point is that the lowering produces a program whose
observable behaviour is exactly what the surface promises. A page that parses
and then prints the wrong number is the failure mode this guards.

Where a `<name>-twin.ny` file exists beside `<name>.matrix`, it is the same
program written by hand in Ny. Two things are then checked:

  1. the twin compiles and runs against its own `.expected`, so both front ends
     drive the shared type checker and code generator to a working program; and
  2. the NIL **type sections** of the two artifacts are byte-identical.

(2) is the cross-language proof and it is the reason this file exists rather
than relying on matching stdout. Matching output only shows the two programs
behave alike; identical schemas show an Ny module can construct, pass, store
and raise a Matrix-declared record or choice, because in NIL there is nothing
left to distinguish them. It caught a real break: Ny normalises `module a.b` to
`a::b`, and Matrix was emitting `a.b`, so every schema name differed while
every printed value matched.
"""
import argparse
import pathlib
import re
import subprocess
import sys
import tempfile


def compile_and_run(compiler, interpreter, source, workdir, stdlib_root=None):
    # A fixture may span several pages. A sibling '.sources' manifest lists
    # them in compile order; cross-page 'read' is a core feature and deserves
    # execution coverage rather than a single-file approximation.
    manifest = source.with_suffix(".sources")
    if manifest.exists():
        pages = [source.parent / line.strip()
                 for line in manifest.read_text().splitlines() if line.strip()]
    else:
        pages = [source]

    # Most fixtures are compiled with --no-stdlib so they test the front end
    # rather than the evolving library. A '.needs-stdlib' marker opts one in,
    # for pages that read a real ny.lang book.
    # Library discovery is relative to the compiler and the working directory,
    # so an explicit -I keeps a fixture working under CTest, which runs from
    # the build tree rather than the repository root.
    stdlib = source.with_suffix(".needs-stdlib").exists()
    if stdlib:
        flags = ["--no-bundle"]
        if stdlib_root is not None:
            flags += ["-I", str(stdlib_root)]
    else:
        flags = ["--no-stdlib", "--no-bundle"]
    nil = pathlib.Path(workdir) / (source.stem + ".nil")
    built = subprocess.run(
        [str(compiler)] + flags + ["--output", str(nil)]
        + [str(page) for page in pages],
        capture_output=True, text=True)
    if built.returncode != 0:
        return None, 0, f"compile failed:\n{built.stdout}{built.stderr}"
    ran = subprocess.run([str(interpreter), str(nil)],
                         capture_output=True, text=True)
    return ran.stdout, ran.returncode, None


def nil_type_section(path):
    """The struct, enum and class records from a textual NIL module, in order.

    Classes carry ancestry and method declarations (NIL section 7): the
    abstract flag, each base, and per method its implementation symbol,
    receiver type, return type and flag bits for virtual and abstract. That is
    what an Ny program needs in order to derive from a Matrix class, take an
    ancestor view of it and dispatch virtually through it, so it belongs in the
    comparison alongside the struct and enum schemas.
    """
    section, inside = [], False
    for line in pathlib.Path(path).read_text().splitlines():
        if re.match(r"^(struct|enum|class) ", line):
            inside = True
            section.append(line)
            continue
        if inside:
            section.append(line)
            if re.match(r"^end (struct|enum|class)", line):
                inside = False
    return section


def compile_to_nil(compiler, source, workdir, tag):
    nil = pathlib.Path(workdir) / (source.stem + tag + ".nil")
    done = subprocess.run(
        [str(compiler), "--no-stdlib", "--no-bundle", "--output", str(nil),
         str(source)],
        capture_output=True, text=True)
    if done.returncode != 0:
        return None, f"{done.stdout}{done.stderr}"
    return nil, None


def check_schema_equality(compiler, directory, workdir):
    """Each <name>.matrix with a <name>-twin.ny must agree on NIL schemas."""
    problems = []
    for page in sorted(list(directory.glob("*.matrix"))
                       + list(directory.glob("*.mat"))):
        twin = page.with_name(page.stem + "-twin.ny")
        if not twin.exists():
            continue
        matrix_nil, failed = compile_to_nil(compiler, page, workdir, "-mx")
        if failed:
            problems.append(f"{page.name}: compile failed:\n{failed}")
            continue
        twin_nil, failed = compile_to_nil(compiler, twin, workdir, "-ny")
        if failed:
            problems.append(f"{twin.name}: compile failed:\n{failed}")
            continue
        mx, ny = nil_type_section(matrix_nil), nil_type_section(twin_nil)
        if mx != ny:
            import difflib
            diff = "\n".join(difflib.unified_diff(
                ny, mx, twin.name, page.name, lineterm=""))
            problems.append(f"{page.name}: NIL type/class sections differ from "
                            f"its Ny twin\n{diff}")
        elif not mx:
            problems.append(f"{page.name}: has an Ny twin but neither artifact "
                            f"declares a struct, enum or class -- the schema "
                            f"comparison would pass vacuously")
    return problems


# Diagnostics that must fire, paired with a substring naming the rule. A
# compiler that accepts these is as broken as one that rejects a valid page,
# and the message matters as much as the rejection: these are the
# mistakes the type, naming and ownership rules are designed to catch early.
DIAGNOSTIC_EXPECTATIONS = {
    "type-name-collision.matrix": "is already the name of a type",
    "bare-machine-type.matrix": "is a machine type and needs",
    "reserved-machine-type.matrix": "is reserved but not available",
    "extension-collision.matrix": "by two implementations",
    "ownership-as-expression.matrix": "is a statement, not a value",
    "unknown-type.matrix": "Nothing in scope declares it",
    "context-on-a-contract.matrix": "'context' cannot attach to a contract",
    "context-on-a-statement.matrix": "'context' annotates a declaration, not a statement",
}


# Keywords an agent brings from a language it already knows. None of these is
# a Matrix keyword, so each arrives as an ordinary name; without a targeted
# message the only report is "expected a statement, found 'fn'", which teaches
# nothing. An agent has no training on Matrix, so every one of these is a
# first-contact mistake and deserves the equivalent spelled out.
FOREIGN_KEYWORDS = [
    ("fn greet()", "begin paragraph"),
    ("return 0", "give <value>"),
    ("print \"hi\"", "say <value>"),
    ("if x then", "when <condition> then"),
    ("switch x", "consider <value>"),
    ("foreach item in xs", "repeat for each"),
    ("break", "stop repeating"),
    ("import io", "read <book>"),
    ("module thing", "book <name>"),
    ("struct point", "begin record"),
    ("enum colour", "begin choice"),
    ("interface shape", "begin contract"),
    ("typedef alias", "means <type>"),
    ("self.value", "bare 'my'"),
    ("try", "attempt"),
    ("catch problem", "caught <FaultRecord>"),
    ("finally", "always"),
    ("throw bad", "raise <record>"),
    ("null", "no null"),
    ("void", "nothing"),
    ("int count", "override integer"),
    ("string name", "text"),
    ("bool flag", "boolean"),
    ("i64 total", "Ny spelling"),
    ("new thing", "build <record>"),
    ("static total", "forever"),
]


def check_foreign_keywords(compiler, workdir):
    """Each foreign keyword must be met with the Matrix spelling."""
    problems = []
    for index, (snippet, wanted) in enumerate(FOREIGN_KEYWORDS):
        page = pathlib.Path(workdir) / f"foreign-{index}.matrix"
        page.write_text(
            "book foreign.check\n\nmain returns number begin paragraph\n"
            f"    {snippet}\n    give 0\nend paragraph\n")
        nil = pathlib.Path(workdir) / f"foreign-{index}.nil"
        done = subprocess.run(
            [str(compiler), "--no-stdlib", "--no-bundle", "--output", str(nil),
             str(page)],
            capture_output=True, text=True)
        output = done.stdout + done.stderr
        if done.returncode == 0:
            problems.append(f"{snippet!r}: compiled cleanly, expected a "
                            f"diagnostic")
        elif wanted not in output:
            problems.append(f"{snippet!r}: diagnostic did not name the Matrix "
                            f"spelling\n    wanted: {wanted}\n"
                            f"    got: {output.strip()}")
    return problems


def check_diagnostics(compiler, root, workdir):
    directory = root / "tests" / "matrix" / "diagnostics"
    if not directory.exists():
        return []
    problems, seen = [], set()
    secondary = set()
    for manifest in directory.glob("*.sources"):
        own = manifest.with_suffix(".matrix").name
        for line in manifest.read_text().splitlines():
            if line.strip() and line.strip() != own:
                secondary.add(line.strip())
    for page in sorted(directory.glob("*.matrix")):
        if page.name in secondary:
            continue
        seen.add(page.name)
        manifest = page.with_suffix(".sources")
        pages = ([page.parent / line.strip()
                  for line in manifest.read_text().splitlines() if line.strip()]
                 if manifest.exists() else [page])
        nil = pathlib.Path(workdir) / (page.stem + "-diag.nil")
        done = subprocess.run(
            [str(compiler), "--no-stdlib", "--no-bundle", "--output", str(nil)]
            + [str(one) for one in pages],
            capture_output=True, text=True)
        output = done.stdout + done.stderr
        if done.returncode == 0:
            problems.append(f"{page.name}: compiled cleanly, expected a "
                            f"diagnostic")
            continue
        wanted = DIAGNOSTIC_EXPECTATIONS.get(page.name)
        if wanted is None:
            problems.append(f"{page.name}: no expectation registered; add one "
                            f"to DIAGNOSTIC_EXPECTATIONS")
        elif wanted not in output:
            problems.append(f"{page.name}: diagnostic did not name the rule\n"
                            f"    wanted: {wanted}\n    got: {output.strip()}")
    for missing in sorted(set(DIAGNOSTIC_EXPECTATIONS) - seen):
        problems.append(f"registered diagnostic expectation without a "
                        f"fixture: {missing}")
    return problems



def check_include_path_resolution(compiler, interpreter, workdir):
    """A book must resolve from an include path, as an Ny module does.

    Before this worked, an Ny `import geo.point` resolved from -I while the
    identical Matrix `read everything from geo.point` did not, so every page
    of a program had to be named on the command line. A project keeping one
    type to a file hit that immediately.

    The layout mirrors the book path, which is the convention the standard
    library already uses. The hyphen matters: Matrix joins words in a name
    with '-' and NIL identifiers admit none, so a book spelled
    'geo.rectangle-ext' is the module 'geo::rectangle_ext' while the file
    keeps the author's spelling.
    """
    root = pathlib.Path(workdir) / "include-path"
    (root / "lib" / "geo").mkdir(parents=True, exist_ok=True)
    (root / "lib" / "geo" / "point.mat").write_text(
        "book geo.point\n\n"
        "public point begin record\n"
        "    field x as number\n"
        "    field y as number\n"
        "end record\n")
    (root / "lib" / "geo" / "point-ext.mat").write_text(
        "book geo.point-ext\n\n"
        "read everything from geo.point\n\n"
        "point begin extension\n"
        "    sum returns number begin paragraph\n"
        "        give my x plus my y\n"
        "    end paragraph\n"
        "end extension\n")
    page = root / "app.matrix"
    page.write_text(
        "book app\n\n"
        "read everything from geo.point\n"
        "read everything from geo.point-ext\n\n"
        "main returns number begin paragraph\n"
        "    value p is build point with x being 3, y being 4\n"
        "    say p.sum with nothing\n"
        "    give 0\n"
        "end paragraph\n")

    nil_path = root / "app.nil"
    compiled = subprocess.run(
        [str(compiler), "--no-bundle", "--no-stdlib",
         "-I", str(root / "lib"), "--output", str(nil_path), str(page)],
        capture_output=True, text=True)
    if compiled.returncode != 0:
        return [f"include path: compile failed\n{compiled.stdout}{compiled.stderr}"]
    ran = subprocess.run([str(interpreter), str(nil_path)],
                         capture_output=True, text=True)
    if ran.stdout.strip() != "7":
        return [f"include path: expected 7, got {ran.stdout.strip()!r}"]
    return []


def check_import_diagnostic_surface(compiler, workdir):
    """A dependency's source language must not select the caller's renderer.

    The first Matrix standard-library page exposed this: an ordinary Ny
    program importing collections suddenly received Matrix diagnostics.
    Check both language directions through source and binary-only libraries.
    """
    failures = []
    for language in ("ny", "matrix"):
        root = pathlib.Path(workdir) / ("diagnostic-import-" + language)
        sources = root / "sources" / "bridge"
        binaries = root / "binaries" / "bridge"
        sources.mkdir(parents=True)
        binaries.mkdir(parents=True)
        if language == "ny":
            dependency = sources / "helper.matrix"
            dependency.write_text(
                "book bridge.helper\n"
                "public answer returns override integer begin paragraph\n"
                "    give 42\nend paragraph\n")
            application = (
                "import bridge.helper\n"
                "fn main() -> i32\n"
                "    let result = answer()\n"
                "    return missing\nend fn\n")
            expected = ("Type checking failed:\n"
                        "  line 4: Undefined identifier 'missing'\n")
        else:
            dependency = sources / "helper.ny"
            dependency.write_text(
                "module bridge.helper\n"
                "pub fn answer() -> i32\n"
                "    return 42\nend fn\n")
            application = (
                "book consumer\n"
                "read everything from bridge.helper\n"
                "main returns override integer begin paragraph\n"
                "    value result is answer with nothing\n"
                "    give missing\nend paragraph\n")
            expected = ("error: Undefined identifier 'missing'\n"
                        "  --> <input>:5\n")
        page = root / ("consumer." + language)
        page.write_text(application)
        base = [str(compiler.resolve()), "--no-stdlib", "--no-bundle", "--quiet"]

        for mode in ("source", "binary"):
            if mode == "binary":
                built = subprocess.run(
                    [*base, "--emit-binary-nil", "--emit-library", "--output",
                     str(binaries / "helper.nil"), str(dependency)],
                    cwd=root, stdin=subprocess.DEVNULL, capture_output=True,
                    text=True, timeout=30)
                if (built.returncode, built.stdout, built.stderr) != (0, "", ""):
                    failures.append(f"{language} diagnostic dependency: binary build failed\n"
                                    f"{built.stdout}{built.stderr}")
                    continue
                # The binary case must resolve its embedded source without
                # retaining a readable source dependency as a fallback.
                dependency.unlink()
            include = sources.parent if mode == "source" else binaries.parent
            output = root / (mode + "-diagnostic.nil")
            previous = b"previous artifact survives a type-check failure\n"
            output.write_bytes(previous)
            rejected = subprocess.run(
                [*base, "-I", str(include), "--output", str(output), str(page)],
                cwd=root, stdin=subprocess.DEVNULL, capture_output=True,
                text=True, timeout=30)
            actual = (rejected.returncode, rejected.stdout, rejected.stderr)
            if actual != (1, "", expected):
                failures.append(
                    f"{language}/{mode} diagnostic renderer: expected {(1, '', expected)!r}, "
                    f"got {actual!r}")
            if output.read_bytes() != previous:
                failures.append(f"{language}/{mode} diagnostic replaced the previous artifact")
    return failures



def check_declaration_context(compiler, workdir):
    """Every declaration that reaches the artifact must carry its stated purpose.

    NIL-0093 makes '@ai_context' part of what an agent image carries: an
    agent reads a program's intent from the artifact rather than from the
    source it was built from. An annotation the compiler accepts and then drops
    would satisfy every output test here while leaving the artifact silent,
    which is the one failure this check exists for.

    A paragraph, record, class and choice each have a declaration in the
    emitted NIL. A contract, a binding and a read do not, and are refused by
    the parser instead -- covered by the diagnostic fixtures.
    """
    root = pathlib.Path(workdir) / "declaration-context"
    root.mkdir(parents=True, exist_ok=True)
    page = root / "intent.matrix"
    page.write_text(
        "book intent.probe\n\n"
        'context "The shape of a thing on screen."\n'
        "public rectangle begin record\n"
        "    field width as number\n"
        "end record\n\n"
        'context "A surface the compositor owns."\n'
        "public surface begin class\n"
        "    field depth as number\n"
        "end class\n\n"
        'context "What a window is doing right now."\n'
        "public window-state begin choice\n"
        "    when Hidden\n"
        "    when Visible\n"
        "end choice\n\n"
        'context "Answers how wide a rectangle is."\n'
        "widen receives box as rectangle returns number begin paragraph\n"
        "    give box.width times 2\n"
        "end paragraph\n\n"
        "main returns number begin paragraph\n"
        "    value box is build rectangle with width being 8\n"
        "    say widen with box being box\n"
        "    give 0\n"
        "end paragraph\n")
    nil_path = root / "intent.nil"
    compiled = subprocess.run(
        [str(compiler), "--no-bundle", "--no-stdlib", "--output", str(nil_path), str(page)],
        capture_output=True, text=True)
    if compiled.returncode != 0:
        return [f"declaration context: compile failed\n{compiled.stdout}{compiled.stderr}"]

    lines = nil_path.read_text().splitlines()
    # Each intent must sit immediately above the declaration it describes, not
    # merely somewhere in the file.
    expected = [
        ("The shape of a thing on screen.", "struct ", "rectangle"),
        ("A surface the compositor owns.", "class ", "surface"),
        ("What a window is doing right now.", "enum ", "window_state"),
        ("Answers how wide a rectangle is.", "function ", "widen"),
    ]
    def carries(prefix, name, marker):
        """Is the marker in this declaration's leading block?

        A function header is preceded by a '# declaration_line' comment, so the
        annotation is not literally the line above. Comments are skipped and
        nothing else is: reaching a blank line or another declaration means the
        marker belongs to something else, or to nothing.
        """
        for index, line in enumerate(lines):
            if not (line.startswith(prefix) and name in line):
                continue
            cursor = index - 1
            while cursor >= 0 and lines[cursor].lstrip().startswith("#"):
                cursor -= 1
            if cursor >= 0 and lines[cursor].strip() == marker:
                return True
        return False

    failures = []
    for text, prefix, name in expected:
        marker = f"@ai_context({text})"
        if not carries(prefix, name, marker):
            kind = prefix.strip()
            failures.append(
                f"declaration context: {kind} '{name}' does not carry its "
                f"'@ai_context' immediately above it")
    return failures


def check_debug_metadata(compiler, interpreter, workdir):
    """Matrix paragraphs and methods need source maps for editor breakpoints."""
    page = pathlib.Path(workdir) / "source-debug.matrix"
    page.write_text(
        "book debug-check\n\n"
        "Reading begin record\n    field level as number\nend record\n"
        "Reading begin implementation\n"
        "    doubled returns number begin paragraph\n"
        "        give my level times 2\n    end paragraph\nend implementation\n"
        "identity receives input-value as number returns number begin paragraph\n"
        "    give input-value\nend paragraph\n"
        "main returns number begin paragraph\n"
        "    value sample as Reading is build Reading with level being 7\n"
        "    say identity with input-value being (sample.doubled with nothing)\n"
        "    give 0\nend paragraph\n")
    failures = []
    for debug, binary in ((False, False), (True, False), (True, True)):
        artifact = pathlib.Path(workdir) / f"source-debug-{debug}-{binary}.nil"
        flags = (["--debug-info"] if debug else []) + (["--emit-binary-nil"] if binary else [])
        built = subprocess.run(
            [str(compiler), "--no-stdlib", "--no-bundle", *flags,
             "--output", str(artifact), str(page)], capture_output=True, text=True)
        if built.returncode:
            failures.append(f"source debug: compile failed: {built.stdout}{built.stderr}")
            continue
        data = artifact.read_bytes()
        if (str(page).encode() in data) != debug:
            failures.append(f"source debug: path presence incorrect for debug={debug}, binary={binary}")
        if debug and not binary:
            functions = [part for part in data.decode().split("\nfunction ")[1:]]
            if len(functions) != 3 or any("# source_debug " not in part.split("end function")[0] for part in functions):
                failures.append("source debug: paragraph or method lost its instruction source map")
        ran = subprocess.run([str(interpreter), str(artifact)], capture_output=True, text=True)
        if ran.returncode or ran.stdout != "14\n":
            failures.append(f"source debug: execution changed: {ran.returncode}, {ran.stdout!r}, {ran.stderr}")
    return failures


def check_source_extensions(compiler, interpreter, workdir):
    """Editor routing and compiler routing must agree for mixed-case suffixes."""
    failures = []
    for suffix in (".MAT", ".MATRIX", ".MaTrIx"):
        page = pathlib.Path(workdir) / ("source-extension" + suffix)
        page.write_text(
            "book suffix-check\n\n"
            "main returns number begin paragraph\n"
            '    say "Matrix extension selected"\n'
            "    give 0\nend paragraph\n")
        output, code, problem = compile_and_run(
            compiler, interpreter, page, workdir)
        if problem or code != 0 or output != "Matrix extension selected\n":
            failures.append(f"{suffix}: {problem or f'exit {code}, output {output!r}'}")
    return failures


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--compiler", required=True)
    ap.add_argument("--interpreter", required=True)
    ap.add_argument("--root", required=True)
    args = ap.parse_args()

    compiler = pathlib.Path(args.compiler)
    interpreter = pathlib.Path(args.interpreter)
    directory = pathlib.Path(args.root) / "tests" / "matrix" / "execution"
    for tool in (compiler, interpreter):
        if not tool.exists():
            print(f"FAIL: missing tool {tool}")
            return 1

    # A page named in another fixture's '.sources' manifest is compiled as
    # part of that fixture, not on its own.
    secondary = set()
    for manifest in directory.glob("*.sources"):
        for line in manifest.read_text().splitlines():
            if line.strip() and line.strip() != manifest.with_suffix(".matrix").name:
                secondary.add(line.strip())
    # '.mat' is the short form of '.matrix'; both select the Matrix front end.
    sources = sorted(path for path in
                     list(directory.glob("*.matrix"))
                     + list(directory.glob("*.mat"))
                     + list(directory.glob("*.ny"))
                     if path.name not in secondary)
    # A page that only appears inside a manifest has no .expected of its own.
    if not sources:
        print(f"FAIL: no fixtures in {directory}")
        return 1

    failures = []
    with tempfile.TemporaryDirectory() as workdir:
        failures.extend(check_schema_equality(compiler, directory, workdir))
        failures.extend(check_diagnostics(compiler, pathlib.Path(args.root),
                                          workdir))
        failures.extend(check_foreign_keywords(compiler, workdir))
        failures.extend(check_source_extensions(compiler, interpreter, workdir))
        failures.extend(check_debug_metadata(compiler, interpreter, workdir))
        failures.extend(check_declaration_context(compiler, workdir))
        failures.extend(check_include_path_resolution(compiler, interpreter, workdir))
        failures.extend(check_import_diagnostic_surface(compiler, workdir))
        for source in sources:
            # A fixture whose output depends on the clock cannot be compared
            # byte for byte. An '.exit-only' marker beside it means: must
            # compile and exit 0, output not asserted. Used sparingly -- a
            # fixture that opts out of output comparison is checking much less.
            exit_only = source.with_suffix(".exit-only").exists()
            expected_path = source.with_suffix(".expected")
            if not exit_only and not expected_path.exists():
                failures.append(f"{source.name}: no .expected file beside it, "
                                f"and no .exit-only marker")
                continue
            expected = None if exit_only else expected_path.read_text()
            actual, code, problem = compile_and_run(
                compiler, interpreter, source, workdir,
                pathlib.Path(args.root) / "nilstd" / "src")
            if problem:
                failures.append(f"{source.name}: {problem}")
                continue
            if code != 0:
                failures.append(f"{source.name}: exited {code}, expected 0")
            if expected is None:
                continue
            if actual != expected:
                exp = expected.splitlines()
                got = actual.splitlines()
                detail = []
                for index in range(max(len(exp), len(got))):
                    e = exp[index] if index < len(exp) else "<missing>"
                    g = got[index] if index < len(got) else "<missing>"
                    if e != g:
                        detail.append(f"      line {index + 1}: "
                                      f"expected {e!r}, got {g!r}")
                failures.append(f"{source.name}: output differs\n"
                                + "\n".join(detail[:12]))

    if failures:
        print(f"Matrix execution tests: {len(failures)} failure(s) of "
              f"{len(sources)} fixtures\n")
        for f in failures:
            print(f"  FAIL {f}")
        return 1
    twins = len(list(directory.glob("*-twin.ny")))
    exit_only = len(list(directory.glob("*.exit-only")))
    stdlib_fixtures = len(list(directory.glob("*.needs-stdlib")))
    diags = len(DIAGNOSTIC_EXPECTATIONS)
    print(f"Matrix execution tests: {len(sources)} fixtures passed, "
          f"{twins} schema comparison(s) identical, "
          f"{diags} diagnostic(s) fired, "
          f"{exit_only} clock-dependent fixture(s) checked for exit only, "
          f"{stdlib_fixtures} against the standard library, "
          f"{len(FOREIGN_KEYWORDS)} foreign keyword(s) redirected, "
          f"mixed-case source suffixes selected Matrix, "
          f"books resolved from an include path, "
          f"cross-language source/binary imports preserve the diagnostic renderer, "
          f"declaration intent reaches the artifact")
    return 0


if __name__ == "__main__":
    sys.exit(main())
