#!/usr/bin/env python3
"""Explicit consider arms preserve nested control flow and lexical scope."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

SOURCE = '''book nested.arms
Response begin choice
 when Good holding number
 when Bad holding text
end choice
score receives item as Response returns number begin paragraph
 consider item
  when Response.Good holding amount provided amount is at least 0 then begin block
   variable total as number is 0
   when amount is greater than 3 then
    repeat for index from 0 to 3
     set total to total plus amount
    end repeat
   otherwise
    set total to amount plus 1
   end when
   consider amount
    when 4 then begin block
     when total equals 12 then set total to 20 end when
    end block
    otherwise begin block
     when total equals 1 then set total to 2 end when
    end block
   end consider
   give total
  end block
  when Response.Good holding amount then give 0
  when Response.Bad holding reason then begin block
   when reason equals "retry" then give -1 end when
   give -2
  end block
 end consider
end paragraph
note suite "matrix.consider.blocks"
nested_branches begin test
 expect (score with (Response.Good holding 4)) equals 20
 expect (score with (Response.Good holding 0)) equals 2
 expect (score with (Response.Good holding -1)) equals 0
 expect (score with (Response.Bad holding "retry")) equals -1
 expect (score with (Response.Bad holding "stop")) equals -2
end test
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('compiler', 'interpreter', 'tester'):
        parser.add_argument('--' + option, type=Path, required=True)
    parser.add_argument('--llvm', choices=('ON', 'OFF'), required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='nil-consider-block-') as directory:
        work = Path(directory)
        source = work / 'nested.matrix'
        source.write_text(SOURCE)
        command = [args.tester.resolve(), 'run', '--compiler', args.compiler.resolve(),
                   '--interpreter', args.interpreter.resolve(), '--target',
                   'all' if args.llvm == 'ON' else 'vm', '--format', 'json', source]
        result = subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=90)
        assert result.returncode == 0, (result.stdout, result.stderr)
        report = json.loads(result.stdout)
        assert report['summary']['passed'] == (2 if args.llvm == 'ON' else 1), report
        invalid = {
            'missing-close': ('main begin paragraph\n consider 1\n when 1 then begin block\n say 2\n end consider\nend paragraph\n', "expected 'end block'"),
            'scope': ('main begin paragraph\n consider 1\n when 1 then begin block\n value inside is 2\n end block\n when 2 then say inside\n end consider\nend paragraph\n', 'inside'),
            'outside-arm': ('main begin paragraph\n begin block\n say 2\n end block\nend paragraph\n', 'expected a statement'),
        }
        for name, (text, diagnostic) in invalid.items():
            path = work / (name + '.matrix')
            path.write_text('book invalid.arm\n' + text)
            artifact = work / (name + '.nil')
            artifact.write_text('retained output')
            result = subprocess.run([str(args.compiler.resolve()), '--no-bundle', '-o', str(artifact), str(path)],
                                    capture_output=True, text=True, timeout=30)
            assert result.returncode != 0 and diagnostic in result.stderr, (name, result.stdout, result.stderr)
            assert artifact.read_text() == 'retained output', name
    print('Nested consider control flow, guards, scope and missing boundaries passed')


if __name__ == '__main__':
    main()
