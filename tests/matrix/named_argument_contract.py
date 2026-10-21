#!/usr/bin/env python3
"""Named Matrix calls bind labels to checked declarations and immutable values."""

import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--interpreter', type=Path, required=True)
    parser.add_argument('--stdlib', type=Path)
    parser.add_argument('--llvm', default='OFF')
    args = parser.parse_args()
    compiler = str(args.compiler.resolve(strict=True))
    interpreter = str(args.interpreter.resolve(strict=True))
    checks = 0
    with tempfile.TemporaryDirectory(prefix='matrix-named-arguments-') as directory:
        root = Path(directory)
        common = [compiler, '--no-stdlib', '-I', str(root)]
        if args.stdlib:
            common += ['--sdk-path', str(args.stdlib.resolve(strict=True))]

        def run(command, status=0):
            result = subprocess.run(command, cwd=root, stdin=subprocess.DEVNULL,
                capture_output=True, text=True, timeout=45)
            assert result.returncode == status, (command, result.returncode, result.stdout, result.stderr)
            return result

        def check(name, source, expected, native=False):
            nonlocal checks
            path = root / (name + '.matrix')
            path.write_text(source, encoding='utf-8')
            analysis = json.loads(run([*common, '--analyze', '--semantic-index', str(path)]).stdout)
            assert analysis['success'] and analysis['semantic'], analysis
            checks += 1
            for encoding, flags in [('text', []), ('binary', ['--emit-binary-nil'])]:
                artifact = root / (name + '-' + encoding + '.nil')
                run([*common, '--no-bundle', *flags, '--output', str(artifact), str(path)])
                result = run([interpreter, str(artifact)])
                assert result.stdout == expected and not result.stderr, (name, encoding, result)
                checks += 1
            if native and args.llvm == 'ON':
                executable = root / (name + '-native')
                run([*common, '--native', '--output', str(executable), str(path)])
                result = run([str(executable)])
                assert result.stdout == expected and not result.stderr, (name, 'native', result)
                checks += 1
            return analysis

        def rejected(name, source, message):
            nonlocal checks
            path = root / (name + '.matrix')
            path.write_text(source, encoding='utf-8')
            result = json.loads(run([*common, '--analyze', '--semantic-index', str(path)], 1).stdout)
            assert not result['success'] and result['semantic'] is None, result
            assert any(message in item['message'] for item in result['diagnostics']), result
            checks += 1

        declaration = '''operation receives first-part as number, second-part as number returns number begin paragraph
 give first-part times 100 plus second-part
end paragraph
'''
        alternate = '''alternate receives second-part as number, first-part as number returns number begin paragraph
 give first-part times 100 plus second-part
end paragraph
'''
        # The actual shadowed callback has deliberately reversed formals, so
        # retaining the old page-wide signature would still fail this case.
        shadow = 'book shadow\n' + declaration + alternate + '''main returns number begin paragraph
 value operation is the paragraph alternate
 say operation with second-part being 2, first-part being 1
 value copied is the paragraph operation
 say copied with FIRST_PART being 3, secondpart being 4
 when true then
  value operation is anonymous receives first-part as number, second-part as number returns number begin paragraph
   give first-part times 100 plus second-part
  end paragraph
  say operation with second-part being 6, first-part being 5
 end when
 say operation with second-part being 8, first-part being 7
 give 0
end paragraph
'''
        check('shadow', shadow, '102\n304\n506\n708\n')
        renamed = shadow.replace('value operation is the paragraph alternate',
            'value updated-operation is the paragraph alternate').replace(
            'say operation with second-part being 2', 'say updated-operation with second-part being 2').replace(
            'copied is the paragraph operation', 'copied is the paragraph updated-operation').replace(
            'say operation with second-part being 8', 'say updated-operation with second-part being 8')
        check('renamed', renamed, '102\n304\n506\n708\n')

        # Later declarations, escaping, variadic fixed parameters and positional
        # calls remain independent of the order in which pages are parsed.
        direct = '''book direct
main returns number begin paragraph
 say operation with `SECOND_PART` being 2, firstpart being 1
 say operation with 3, 4
 say collect with right being 2, left being 1, more 3, 4
 give 0
end paragraph
''' + declaration + '''collect receives left as number, right as number, more rest as number returns number begin paragraph
 give left times 1000 plus right times 100 plus (rest at 0) times 10 plus (rest at 1)
end paragraph
'''
        check('direct', direct, '102\n304\n1234\n', native=True)

        (root / 'ny_provider.ny').write_text('module ny_provider\n'
            'pub fn ordered(first_part: i64, second_part: i64) -> i64\n'
            ' return first_part * 100 + second_part\nend fn\n'
            'pub fn select<T>(chosen: T, ignored: i64) -> T\n return chosen\nend fn\n'
            'pub type Callback = fn(i64, i64) -> i64\n', encoding='utf-8')
        (root / 'matrix_provider.matrix').write_text('book matrix-provider\npublic ' + alternate,
            encoding='utf-8')
        imports = '''book imports
read ordered, select from ny-provider
read everything from matrix-provider
main returns number begin paragraph
 say ordered with second-part being 2, FIRST_PART being 1
 say matrix-provider::alternate with first-part being 3, second-part being 4
 say select with ignored being 9, chosen being "inferred"
 say select of text with ignored being 9, chosen being "explicit"
 give 0
end paragraph
'''
        check('imports', imports, '102\n304\ninferred\nexplicit\n', native=True)
        callback_import = imports.replace(' say ordered with',
            ' value ny-copy is the paragraph ordered\n say ny-copy with').replace(
            ' say matrix-provider::alternate with',
            ' value matrix-copy is the paragraph alternate\n say matrix-copy with')
        check('callback-imports', callback_import, '102\n304\ninferred\nexplicit\n')

        (root / 'generic_provider.ny').write_text('module generic_provider\n'
            'pub struct Container<T>\n item: T\nend struct\n'
            'impl<T> Container<T>\n'
            ' pub fn make(chosen: T, ignored: i64) -> Container<T>\n'
            '  return Container<T> { item: chosen }\n end fn\n'
            ' pub fn choose(self, chosen: T, ignored: i64) -> T\n'
            '  return chosen\n end fn\nend impl\n', encoding='utf-8')
        generic_methods = '''book generic-methods
read Container from generic-provider
main returns number begin paragraph
 value instance is Container::make of text with ignored being 9, chosen being "static"
 say instance.item
 say instance.choose with ignored being 7, chosen being "instance"
 give 0
end paragraph
'''
        check('generic-methods', generic_methods, 'static\ninstance\n', native=True)

        builtin = '''book builtin
main returns number begin paragraph
 value source as text is "abcdef"
 say source.substr with length being 3, start being 1
 give 0
end paragraph
'''
        check('builtin', builtin, 'bcd\n', native=True)
        rejected('builtin-unknown', builtin.replace('length being', 'wrong being'),
            "No parameter named 'wrong'")

        methods = '''book methods
calculator begin record
 field unused as number
end record
calculator begin implementation
 calculate receives left as number, right as number returns number begin paragraph
  give left times 100 plus right
 end paragraph
 select receives chosen as text, ignored as number without receiver returns text begin paragraph
  give chosen
 end paragraph
end implementation
reader begin class
 field unused as number
 public calculate receives left as number, right as number returns number begin paragraph
  give left times 100 plus right
 end paragraph
end class
main returns number begin paragraph
 value record-value is build calculator with unused being 0
 value class-value is build reader with unused being 0
 say record-value.calculate with right being 2, left being 1
 say class-value.calculate with right being 4, left being 3
 say calculator.select with ignored being 9, chosen being "associated"
 give 0
end paragraph
'''
        check('methods', methods, '102\n304\nassociated\n', native=True)

        base = 'book invalid\n' + declaration + 'main returns number begin paragraph\n%s\n give 0\nend paragraph\n'
        for name, call, message in [
            ('unknown', 'say operation with wrong being 1, second-part being 2', "No parameter named 'wrong'"),
            ('duplicate', 'say operation with first-part being 1, FIRST_PART being 2, second-part being 3', 'given more than once'),
            ('missing', 'say operation with first-part being 1', "Missing argument 'second_part'"),
            ('tail', 'say operation with first-part being 1, second-part being 2, more 3', 'requires a variadic callable'),
            ('mutable', 'variable callback is the paragraph operation\nsay callback with second-part being 2, first-part being 1', 'use positional arguments'),
        ]:
            rejected(name, base % call, message)
        parameter = '''book parameter
read Callback from ny-provider
invoke receives worker as Callback returns number begin paragraph
 give worker with second-part being 2, first-part being 1
end paragraph
main returns number begin paragraph
 give 0
end paragraph
'''
        rejected('parameter', parameter, 'use positional arguments')
        variadic_callback = '''book variadic-callback
collect receives fixed-part as number, more rest as number returns number begin paragraph
 give fixed-part
end paragraph
main returns number begin paragraph
 value callback is the paragraph collect
 say callback with fixed-part being 1, more 2, 3
 give 0
end paragraph
'''
        rejected('variadic-callback', variadic_callback, 'Named variadic callback calls are not supported')
        # Unknown labels through an immutable callback still diagnose against
        # its selected declaration, never a same-spelled paragraph elsewhere.
        rejected('callback-unknown', base % ('value callback is the paragraph operation\n'
            'say callback with missing being 1, second-part being 2'), "No parameter named 'missing'")
    print(f'PASS: {checks} Matrix named-argument checks')


if __name__ == '__main__':
    main()
