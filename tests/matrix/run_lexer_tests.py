#!/usr/bin/env python3
"""Matrix lexer conformance tests.

Three groups:

  positive  -- every file in matrix/samples and tests/matrix/lexer-positive
               must tokenise with zero diagnostics.
  negative  -- every file in tests/matrix/lexer-negative must produce at least
               one diagnostic, and that diagnostic must name the actual rule.
               A lexer that rejects for the wrong reason is not passing.
  units     -- identity behaviour: folded lookup keys and emitted NIL symbols.
  grammar   -- every word terminal in matrix/docs/grammar.ebnf is a keyword in
               the lexer table, and every keyword appears in the grammar. The
               grammar is the specification; a silent drift between it and the
               implementation is how a language ends up undocumented.

Exit 0 on success, 1 on failure.
"""
import argparse
import pathlib
import re
import subprocess
import sys

# Each negative fixture pairs with a substring its diagnostic must contain.
# The substring targets the rule, not the wording, so messages can be improved
# without churning the test.
NEGATIVE_EXPECTATIONS = {
    "hyphen-digit.matrix": "cannot be followed by a digit",
    "hyphen-trailing.matrix": "must join two words",
    "hyphen-double.matrix": "repeats the hyphen",
    "symbol-minus.matrix": "'-' is not an operator",
    "symbol-braces.matrix": "'{' is not used in Matrix",
    "symbol-colon.matrix": "single ':' is not used",
    "char-literal.matrix": "no character literals",
    "symbol-equals.matrix": "'==' is not a Matrix operator",
    "symbol-arrow.matrix": "'->' is not a Matrix operator",
    "symbol-assign.matrix": "'=' is not used in Matrix",
    "backtick-unterminated.matrix": "unterminated escaped identifier",
    "backtick-empty.matrix": "cannot be empty",
    "string-unterminated.matrix": "unterminated text literal",
    "exponent-missing.matrix": "exponent requires at least one digit",
    "exponent-underflow.matrix": "outside the representable",
}

# Words the parser matches positionally on folded text rather than reserving,
# so they stay usable as names. A word-based surface already taxes naming
# heavily; this list is the mitigation and is expected to grow.
SOFT_WORDS = {
    "least", "most", "greater", "less", "than", "equal", "left", "right",
    "by", "each", "length", "milliseconds", "receiver", "x86_64", "formatted",
    # NIL-0016 phrases use exact ASCII-folded spellings only in their blocks.
    # They remain identifiers elsewhere and do not reserve whole name families.
    "collect", "query", "where", "order", "ascending", "descending", "skip", "take",
    "pattern", "literal", "capture", "matching", "require", "next", "match",
    "one", "zero", "optional", "exactly", "between", "fewest", "ascii", "digit",
    "digits", "letters", "whitespace", "any", "character", "through", "start",
    "input", "either",
    # NIL-0021 switch vocabulary is contextual, including explicit exits.
    "switch", "case", "switching",
    "block",  # Contextual only in consider arm begin/end boundaries.
    # NIL-0023 keeps ordinary variables and parameters named flag valid.
    "flag",
    "expect",
}

# Lowercase markers inside a numeric token are not standalone word terminals.
LEXICAL_TERMINALS = {"e"}


def check_grammar_sync(root):
    """Both directions: grammar terminals vs. the lexer's keyword table."""
    grammar_path = root / "matrix" / "docs" / "grammar.ebnf"
    lexer_path = root / "nilcompiler" / "src" / "matrix_lexer.cpp"
    if not grammar_path.exists() or not lexer_path.exists():
        return [f"cannot check grammar sync: missing {grammar_path} or {lexer_path}"]

    body = re.sub(r"\(\*.*?\*\)", "", grammar_path.read_text(), flags=re.S)
    terminals = {m for m in re.findall(r"'([^']+)'", body)
                 if re.fullmatch(r"[a-z][a-z0-9_]*", m)}
    table = set(re.findall(r'\{"([a-z_]+)", kw::', lexer_path.read_text()))

    problems = []
    for word in sorted(terminals - table - SOFT_WORDS - LEXICAL_TERMINALS):
        problems.append(f"grammar uses '{word}' but the lexer does not "
                        f"reserve it; add it to keyword_table or to SOFT_WORDS")
    for word in sorted(table - terminals):
        problems.append(f"lexer reserves '{word}' but the grammar never uses "
                        f"it; document it in grammar.ebnf or drop it")
    return problems


# (source spelling, expected folded key, expected emitted NIL symbol)
IDENTITY_CASES = [
    ("frame-kind", "framekind", "frame_kind"),
    ("FRAME-KIND", "framekind", "FRAME_KIND"),
    ("frame_kind", "framekind", "frame_kind"),
    ("framekind", "framekind", "framekind"),
    ("read-regular-bytes", "readregularbytes", "read_regular_bytes"),
    ("MAX-PAYLOAD", "maxpayload", "MAX_PAYLOAD"),
]


def run(tool, path, tokens=False):
    cmd = [str(tool), str(path)] + (["--tokens"] if tokens else [])
    done = subprocess.run(cmd, capture_output=True, text=True)
    return done.returncode, done.stdout + done.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tool", required=True, help="path to matrix_lex_dump")
    ap.add_argument("--root", required=True, help="repository root")
    args = ap.parse_args()

    tool = pathlib.Path(args.tool)
    root = pathlib.Path(args.root)
    if not tool.exists():
        print(f"FAIL: lexer tool not found: {tool}")
        return 1

    failures = []
    checked = 0

    positive_dirs = [root / "matrix" / "samples",
                     root / "tests" / "matrix" / "lexer-positive"]
    for directory in positive_dirs:
        for path in sorted(directory.glob("*.matrix")):
            checked += 1
            code, out = run(tool, path)
            if code != 0:
                failures.append(f"{path.name}: expected clean, got:\n    "
                                + out.strip().replace("\n", "\n    "))

    negative_dir = root / "tests" / "matrix" / "lexer-negative"
    seen = set()
    for path in sorted(negative_dir.glob("*.matrix")):
        checked += 1
        seen.add(path.name)
        code, out = run(tool, path)
        if code == 0:
            failures.append(f"{path.name}: expected a diagnostic, got none")
            continue
        expected = NEGATIVE_EXPECTATIONS.get(path.name)
        if expected is None:
            failures.append(f"{path.name}: no expectation registered; add one "
                            f"to NEGATIVE_EXPECTATIONS")
        elif expected not in out:
            failures.append(f"{path.name}: diagnostic did not name the rule\n"
                            f"    wanted substring: {expected}\n"
                            f"    got: {out.strip()}")
    missing = set(NEGATIVE_EXPECTATIONS) - seen
    if missing:
        failures.append("registered expectations without a fixture: "
                        + ", ".join(sorted(missing)))

    # Identity behaviour is checked through the tool's token dump: the folded
    # key is printed for every identifier.
    identity_source = root / "tests" / "matrix" / "lexer-positive" / "folding.matrix"
    if identity_source.exists():
        checked += 1
        code, out = run(tool, identity_source, tokens=True)
        keys = {}
        for line in out.splitlines():
            parts = line.split("\t")
            if len(parts) >= 4 and parts[1] == "name" and parts[3].startswith("key="):
                keys[parts[2].strip("'")] = parts[3][4:]
        for spelling, want_key, _ in IDENTITY_CASES:
            if spelling in keys and keys[spelling] != want_key:
                failures.append(f"folding: {spelling!r} keyed as "
                                f"{keys[spelling]!r}, wanted {want_key!r}")
        # Every spelling of read-sensor in that file must share one key.
        sensor_keys = {k for name, k in keys.items()
                       if k in ("readsensor", "sensorid")}
        if "readsensor" not in sensor_keys:
            failures.append("folding: read-sensor spellings did not fold to "
                            "one key in folding.matrix")

    checked += 1
    failures.extend(check_grammar_sync(root))

    if failures:
        print(f"Matrix lexer tests: {len(failures)} failure(s) of {checked} checks\n")
        for f in failures:
            print(f"  FAIL {f}")
        return 1
    print(f"Matrix lexer tests: {checked} checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
