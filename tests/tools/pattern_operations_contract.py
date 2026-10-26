#!/usr/bin/env python3
"""Pattern collections and literal replacement preserve bounds across execution routes."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


SOURCE = r'''import nil.std.text

fn check(passed: bool, label: text)
    if not passed then panic(label) end if
end fn

fn doubled(seed: text, rounds: i32) -> text
    let mut result = seed
    let mut index = 0
    while index < rounds do
        result = result + result
        index = index + 1
    end while
    return result
end fn

fn same_parts(actual: array<text>, expected: array<text>) -> bool
    if actual.len() != expected.len() then return false end if
    let mut index = 0
    while index < actual.len() do
        if actual[index] != expected[index] then return false end if
        index = index + 1
    end while
    return true
end fn

fn all_parts(actual: array<text>, expected: text) -> bool
    let mut index = 0
    while index < actual.len() do
        if actual[index] != expected then return false end if
        index = index + 1
    end while
    return true
end fn

fn main() -> i32
    let empty = compile_pattern("").unwrap()
    let dot = compile_pattern(".").unwrap()
    let comma = compile_pattern("(,)").unwrap()
    let absent = compile_pattern("z").unwrap()
    check(same_parts(find_all_matches(empty, "ab").unwrap(), ["", "", ""]), "empty whole matches")
    check(same_parts(split_matches(empty, "ab").unwrap(), ["", "a", "b", ""]), "empty delimiters preserve bytes")
    check(replace_matches(empty, "ab", "-").unwrap() == "-a-b-", "empty replacement insertion")
    check(same_parts(find_all_matches(empty, "").unwrap(), [""]), "empty input empty match")
    check(same_parts(split_matches(empty, "").unwrap(), ["", ""]), "empty input empty split")
    check(replace_matches(empty, "", "-").unwrap() == "-", "empty input replacement")
    check(same_parts(split_matches(comma, ",a,,").unwrap(), ["", "a", "", ""]), "split keeps boundary fields without captures")
    check(same_parts(find_all_matches(comma, ",a,,").unwrap(), [",", ",", ","]), "whole matches exclude capture duplication")
    check(replace_matches(comma, ",a,,", "-").unwrap() == "-a--", "adjacent delimiters")
    check(find_all_matches(absent, "ab").unwrap().len() == 0, "no matches")
    check(same_parts(split_matches(absent, "ab").unwrap(), ["ab"]), "no split matches")
    check(replace_matches(absent, "ab", "-").unwrap() == "ab", "no replacement matches")
    let greedy = compile_pattern("a*").unwrap()
    check(same_parts(find_all_matches(greedy, "a").unwrap(), ["a", ""]), "terminal empty match after consuming match")
    check(same_parts(split_matches(greedy, "a").unwrap(), ["", "", ""]), "terminal empty delimiter")
    check(replace_matches(greedy, "a", "-").unwrap() == "--", "terminal empty replacement")
    let ahead = compile_pattern("(?=b)").unwrap()
    check(same_parts(split_matches(ahead, "ab").unwrap(), ["a", "b"]), "lookahead does not consume split byte")
    check(replace_matches(ahead, "ab", "-").unwrap() == "a-b", "lookahead does not consume replacement byte")
    let literal = $"$1\\{{name}}"
    check(replace_matches(dot, "ab", literal).unwrap() == literal + literal, "replacement is literal")
    let utf8 = "é"
    check(find_all_matches(empty, utf8).unwrap().len() == 3, "empty matches advance one byte")
    check(replace_matches(empty, utf8, "").unwrap() == utf8, "zero-width empty replacement preserves UTF-8 bytes")

    # These returned arrays, nested Result payloads and text values remain live
    # while later operations allocate enough objects/bytes to trigger GC.
    let held_matches = find_all_matches(compile_pattern("[a-z]+").unwrap(), "alpha1beta2")
    let held_parts = split_matches(comma, "left,middle,right")
    let held_replacement = replace_matches(comma, "left,middle,right", "/")

    let input4096 = doubled("a", 12)
    let at_limit_matches = find_all_matches(dot, input4096).unwrap()
    check(at_limit_matches.len() == 4096, "exact match limit succeeds")
    check(all_parts(at_limit_matches, "a"), "every exact-limit whole match")
    let at_limit_parts = split_matches(dot, input4096).unwrap()
    check(at_limit_parts.len() == 4097, "exact match limit permits final split field")
    check(all_parts(at_limit_parts, ""), "every exact-limit split field")
    let replacement256 = doubled("zy", 7)
    let expected_mebibyte = doubled(replacement256, 12)
    let at_limit_output = replace_matches(dot, input4096, replacement256)
    check(at_limit_output.unwrap().len() == 1048576, "exact output byte limit succeeds")
    check(at_limit_output.unwrap() == expected_mebibyte, "balanced join preserves every output byte")
    check(replace_matches(dot, input4096, replacement256 + "x").unwrap_error() == "pattern replacement output exceeds 1048576 bytes", "output size above limit errors")

    let input4097 = input4096 + "a"
    check(find_all_matches(dot, input4097).unwrap_error() == "pattern match count exceeds 4096", "find-all cannot return truncated match prefix")
    check(split_matches(dot, input4097).unwrap_error() == "pattern match count exceeds 4096", "split cannot return truncated fields")
    check(replace_matches(dot, input4097, "").unwrap_error() == "pattern match count exceeds 4096", "replace cannot return partially changed input")
    let input4095 = input4096.substr(0, 4095)
    let empty_limit_matches = find_all_matches(empty, input4095).unwrap()
    check(empty_limit_matches.len() == 4096, "empty matches include EOF at exact limit")
    check(all_parts(empty_limit_matches, ""), "every exact-limit empty match")
    let empty_limit_parts = split_matches(empty, input4095).unwrap()
    check(empty_limit_parts.len() == 4097, "empty delimiter split exact limit")
    check(empty_limit_parts[0] == "" and empty_limit_parts[4096] == "", "empty delimiter split boundary fields")
    let mut index = 1
    while index < 4096 do
        if empty_limit_parts[index] != "a" then panic("empty delimiter split lost an interior byte") end if
        index = index + 1
    end while
    check(replace_matches(empty, input4095, "").unwrap() == input4095, "empty replacement at exact match limit")
    check(find_all_matches(empty, input4096).unwrap_error() == "pattern match count exceeds 4096", "EOF empty match exceeds find-all limit")
    check(split_matches(empty, input4096).unwrap_error() == "pattern match count exceeds 4096", "EOF empty match exceeds split limit")
    check(replace_matches(empty, input4096, "").unwrap_error() == "pattern match count exceeds 4096", "EOF empty match exceeds replacement limit")

    let expensive = compile_pattern("x|(a+)+$").unwrap()
    let late_input = "xaaaaaaaaaaaaaaaaaaaaaaaaaaaa!"
    check(capture_index(first_match(expensive, late_input).unwrap().unwrap(), 0).unwrap() == "x", "late budget fixture first finds a match")
    check(find_all_matches(expensive, late_input).unwrap_error() == "pattern matching budget exceeded", "find-all propagates late search failure")
    check(split_matches(expensive, late_input).unwrap_error() == "pattern matching budget exceeded", "split propagates late search failure")
    check(replace_matches(expensive, late_input, "-").unwrap_error() == "pattern matching budget exceeded", "replace propagates late search failure")

    let oversized = expected_mebibyte + "x"
    check(find_all_matches(dot, oversized).is_error(), "oversized find-all input")
    check(split_matches(dot, oversized).is_error(), "oversized split input")
    check(replace_matches(dot, oversized, "").is_error(), "oversized replace input")
    check(replace_matches(absent, "ab", oversized).unwrap_error() == "pattern replacement must be NUL-free and at most 1048576 bytes", "unused oversized replacement still errors")
    check(replace_matches(dot, "a", oversized).is_error(), "oversized used replacement errors")
    check(replace_matches(dot, "a", expected_mebibyte).unwrap() == expected_mebibyte, "maximum replacement is accepted")
    let invalid = Pattern { source: "[" }
    check(find_all_matches(invalid, "").is_error(), "invalid pattern on empty find-all input")
    check(split_matches(invalid, "").is_error(), "invalid pattern on empty split input")
    check(replace_matches(invalid, "", "").is_error(), "invalid pattern on empty replacement input")

    check(same_parts(held_matches.unwrap(), ["alpha", "beta"]), "retained find-all result survives allocation")
    check(same_parts(held_parts.unwrap(), ["left", "middle", "right"]), "retained split result survives allocation")
    check(held_replacement.unwrap() == "left/middle/right", "retained replacement result survives allocation")
    check(all_parts(at_limit_matches, "a") and at_limit_matches.len() == 4096, "retained large match array survives allocation")
    check(all_parts(at_limit_parts, "") and at_limit_parts.len() == 4097, "retained large split array survives allocation")
    check(at_limit_output.unwrap() == expected_mebibyte, "retained large replacement survives allocation")
    println("pattern operation bounds and lifetime checks passed")
    return 0
end fn
'''


NUL_SOURCE = '''import nil.std.text
import nil.std.io
fn main() -> i32
    let input = read_text("nul-input.txt").unwrap()
    let dot = compile_pattern(".").unwrap()
    let absent = compile_pattern("z").unwrap()
    println(input.len() == 3)
    println(find_all_matches(dot, input).is_error())
    println(split_matches(dot, input).is_error())
    println(replace_matches(dot, input, "").is_error())
    println(replace_matches(absent, "ab", input).is_error())
    println(replace_matches(dot, "a", input).is_error())
    return 0
end fn
'''


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for tool in ('compiler', 'interpreter', 'stdlib'):
        parser.add_argument('--' + tool, type=Path, required=True)
    for tool in ('assembler', 'linker', 'runtime'):
        parser.add_argument('--' + tool, type=Path)
    parser.add_argument('--llvm', choices=('ON', 'OFF'), default='OFF')
    args = parser.parse_args()
    if args.llvm == 'ON' and any(getattr(args, tool) is None
                                 for tool in ('assembler', 'linker', 'runtime')):
        parser.error('--llvm ON requires --assembler, --linker and --runtime')
    tools = {key: str(value.resolve(strict=True)) for key, value in vars(args).items()
             if isinstance(value, Path)}
    env = {key: value for key, value in os.environ.items()
           if not key.startswith(('NIL_', 'NY_'))}
    checks = 0
    routes = 0
    with tempfile.TemporaryDirectory(prefix='nil pattern operations ') as directory:
        root = Path(directory)

        def run(command, *, expected=None, profile=None, safe=False):
            nonlocal checks, routes
            result = subprocess.run(list(map(str, command)), cwd=root,
                env=env if profile is None else {**env, 'NIL_MEMORY_PROFILE': str(profile)},
                stdin=subprocess.DEVNULL, capture_output=True, timeout=120)
            assert result.returncode == 0, (command, result)
            if expected is not None:
                assert result.stdout == expected and not result.stderr, (command, result)
                routes += 1
            if profile is not None:
                evidence = json.loads(profile.read_text())
                assert evidence['completed'] and not evidence['errors'], evidence
                assert evidence['totals']['allocated_objects'] >= 4096, evidence
                if safe:
                    assert evidence['collections']['count'] > 0, evidence
                    assert evidence['totals']['freed_objects'] > 0, evidence
            checks += 1
            return result

        def native(artifact, name, mode):
            obj, executable = root / (name + '.o'), root / name
            run([tools['assembler'], *(['--unmanaged'] if mode == 'unsafe' else []),
                 '-o', obj, artifact])
            run([tools['linker'], '--runtime', tools['runtime'], '-o', executable, obj])
            return [executable]

        base = [tools['compiler'], '--quiet', '--no-bundle', '--no-stdlib',
                '--sdk-path', tools['stdlib']]
        source = root / 'operations.ny'
        source.write_text(SOURCE)
        expected = b'pattern operation bounds and lifetime checks passed\n'
        for encoding, flags in (('text', []), ('binary', ['--emit-binary-nil'])):
            artifact = root / (encoding + '.nil')
            run([*base, '--debug-info', *flags, '-o', artifact, source])
            for mode in ('safe', 'unsafe'):
                commands = [('vm', [tools['interpreter'], '--no-ai',
                                    '--memory-mode=' + mode, artifact])]
                if args.llvm == 'ON':
                    commands.append(('native', native(artifact, encoding + '-' + mode, mode)))
                for route, command in commands:
                    profile = root / (encoding + '-' + mode + '-' + route + '.json')
                    run(command, expected=expected, profile=profile, safe=mode == 'safe')

        if args.llvm == 'ON':
            for mode in ('safe', 'unsafe'):
                run([*base, '--native', '--jit',
                     *(['--unmanaged'] if mode == 'unsafe' else []),
                     '-o', root / (mode + '.jit'), source], expected=expected)

        # VM file ingress preserves embedded NUL, unlike native text ingress.
        # Exercise real runtime text, not an already rejected source literal.
        (root / 'nul-input.txt').write_bytes(b'a\0b')
        nul_source = root / 'nul-operations.ny'
        nul_source.write_text(NUL_SOURCE)
        for encoding, flags in (('text', []), ('binary', ['--emit-binary-nil'])):
            artifact = root / ('nul-' + encoding + '.nil')
            run([*base, *flags, '-o', artifact, nul_source])
            for mode in ('safe', 'unsafe'):
                run([tools['interpreter'], '--no-ai', '--memory-mode=' + mode, artifact],
                    expected=b'1\n' * 6)
    assertions = SOURCE.count('    check(')
    print(f'pattern operations contract: {checks} executions/checks across {routes} routes passed '
          f'({assertions} bounds/semantics/lifetime assertions per main route)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
