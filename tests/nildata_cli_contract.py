#!/usr/bin/env python3
"""Independent CLI contract for lossless NILData 1 and explicit legacy migration."""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile


@dataclass(frozen=True)
class Number:
    """Keep the original JSON number lexeme; never compare through a float."""
    text: str


def parse_json(source: str):
    def pairs(entries):
        result = {}
        for key, value in entries:
            assert key not in result, f"duplicate output member {key!r}"
            result[key] = value
        return result

    def invalid_constant(value):
        raise AssertionError(f"non-JSON output constant {value}")

    return json.loads(source, parse_int=Number, parse_float=Number,
                      parse_constant=invalid_constant, object_pairs_hook=pairs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    args = parser.parse_args()
    compiler = args.compiler.resolve()
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(('NY_', 'NIL_'))}
    checks = 0
    with tempfile.TemporaryDirectory(prefix='NILData CLI contract ') as directory:
        root = Path(directory)

        def convert(option, source, suffix, *, success=True, line_error=False, extra=()):
            nonlocal checks
            path = root / f'input {checks}{suffix}'
            path.write_bytes(source if isinstance(source, bytes) else source.encode('utf-8'))
            result = subprocess.run([str(compiler), option, str(path), *extra],
                                    cwd=root, env=environment, stdin=subprocess.DEVNULL,
                                    capture_output=True, timeout=30)
            checks += 1
            context = (option, source, result.returncode, result.stdout, result.stderr)
            if success:
                assert result.returncode == 0, context
                assert not result.stderr, context
                assert result.stdout, context
                return result.stdout.decode('utf-8')
            assert result.returncode != 0, context
            assert not result.stdout, context
            assert result.stderr.strip(), context
            if line_error:
                assert re.search(rb'line\s+\d+', result.stderr, re.IGNORECASE), context
            return ''

        def from_json(source):
            data = convert('--to-nildata', source, '.json')
            assert data.startswith('nildata 1\n'), data
            assert '\0' not in data, repr(data)
            restored = convert('--to-json', data, '.nildata')
            assert '\0' not in restored, repr(restored)
            assert parse_json(restored) == parse_json(source), (source, data, restored)
            assert convert('--to-nildata', restored, '.json') == data, (source, data, restored)
            return data

        # Scalars remain typed, and decimal spellings survive without binary floating point.
        for source in ['null', 'true', 'false', '"null"', '"true"', '"false"', '""',
                       '0', '-0', '-1.6', '1.23000', '9007199254740993',
                       '1234567890123456789012345678901234567890', '1e9999',
                       '1E+9999', '-4.2000e-9999', '[]', '{}']:
            from_json(source)
        assert from_json('[]') == 'nildata 1\narray\nend array\n'
        assert from_json('{}') == 'nildata 1\nobject\nend object\n'
        from_json('''{"array":[[],{},[1,"2",false,null]],"numbered":{"0":"a","1":"b"},
                     "empty":{},"0":[-0,1e9999],"item":"value"}''')
        escaped = {
            '': '', 'plain_name': 'is an object', 'two words': '# not a comment',
            'hash#key': 'end array', 'quote"key': '"quoted"', 'line\nkey': '\0',
            'café😀': 'café😀', 'escapes': '\"/\b\f\n\r\t\\',
            'null': 'null', 'true': 'true', 'false': 'false', 'number': '1e9999'
        }
        from_json(json.dumps(escaped, ensure_ascii=False))
        from_json(r'{"unicode":"\uD83D\uDE00","controls":"\u0000\u001f","slash":"\/"}')

        def data_value(source, expected):
            actual = convert('--to-json', source, '.nildata')
            assert parse_json(actual) == parse_json(expected), (source, actual, expected)

        data_value('''# preamble
nildata 1 # version

object
  enabled is true # actual boolean
  text is "true"
  absent is null
  "hash#key" is "# quoted comment {literal}"
  "0" is an object
    item is "member"
  end object
  items is an array
    item is -0
    item is an array
      item is 1e9999
    end array
    item is an object
    end object
  end array # container
end object
# trailing comment
''', '{"enabled":true,"text":"true","absent":null,"hash#key":"# quoted comment {literal}","0":{"item":"member"},"items":[-0,[1e9999],{}]}')
        for opening, expected in [('object', '{}'), ('an object', '{}'), ('array', '[]'), ('an array', '[]')]:
            data_value('nildata 1\n' + opening + '\nend ' + opening.removeprefix('an ') + '\n', expected)
        data_value('nildata 1\r\narray\r\n  item is "null"\r\n  item is null\r\nend array\r\n', '["null",null]')
        data_value('nildata 1\n"# {x} end object" # literal\n', '"# {x} end object"')

        invalid_data = [
            '', '# comments only\n', 'object\nend object\n', 'nildata 2\nnull\n',
            'nildata 01\nnull\n', 'NILDATA 1\nnull\n', 'nildata 1\n',
            'nildata 1\nnildata 1\nnull\n', 'nildata 1\ntrue\nfalse\n',
            'nildata 1\nobject\n', 'nildata 1\nobject\nend array\n',
            'nildata 1\narray\nend object\n', 'nildata 1\nobject\nend object\nend object\n',
            'nildata 1\nobject\nx is 1\nx is 2\nend object\n',
            'nildata 1\nobject\nx is 1\n"x" is 2\nend object\n',
            'nildata 1\nobject\n"x" is 1\n"\\u0078" is 2\nend object\n',
            'nildata 1\nobject\n0 is 1\nend object\n',
            'nildata 1\nobject\nnot-valid is 1\nend object\n',
            'nildata 1\nobject\nkey = 1\nend object\n',
            'nildata 1\nobject\nkey is\nend object\n',
            'nildata 1\nobject\nkey is array\nend array\nend object\n',
            'nildata 1\narray\nother is 1\nend array\n',
            'nildata 1\narray\nitem 1\nend array\n',
            'nildata 1\nlist\nend list\n',
            'nildata 1\n"unterminated\n', 'nildata 1\n"raw\nnewline"\n',
            'nildata 1\n"raw\x01control"\n', 'nildata 1\n"bad\\q"\n',
            'nildata 1\n"bad\\u123"\n', 'nildata 1\n"\\uD800"\n',
            'nildata 1\n"\\uDC00"\n',
        ]
        for scalar in ['01', '-01', '+1', '.5', '1.', '1e', '1e+', 'NaN', 'Infinity',
                       '-Infinity', 'TRUE', 'Null', '0x10', '1 + 2', 'call()',
                       '$"{value}"', 'formatted "value"', '[]', '{}']:
            invalid_data.append('nildata 1\n' + scalar + '\n')
        for source in invalid_data:
            convert('--to-json', source, '.nildata', success=False, line_error=True)
        convert('--to-json', b'nildata 1\n"\xff"\n', '.nildata', success=False)
        for source in ['', '{', '[1,]', '{"x":1,}', 'true false', 'NaN', '+1', '01',
                       '{"x":1,"x":2}', '{"x":1,"\\u0078":2}', '"\\q"',
                       '"\\uD800"', '"raw\0control"']:
            convert('--to-nildata', source, '.json', success=False)
        convert('--to-nildata', b'"\xc0\xaf"', '.json', success=False)

        def legacy(option, source, expected):
            output = convert(option, source, '.nydata')
            assert output.startswith('nildata 1\n'), output
            data_value(output, expected)

        # Old fields are text, including quoted bodies and comment-looking text.
        old_object = '''object settings
  field enabled
    true
  end enabled
  field absent
    null
  end absent
  field number
    9007199254740993
  end number
  field quoted
    "true"
  end quoted
  field comments
    # preserve this
  end comments
  object numbered
    field 0
      first
    end 0
    field 1
      second
    end 1
  end numbered
end settings
'''
        old_expected = '{"enabled":"true","absent":"null","number":"9007199254740993","quoted":"\\"true\\"","comments":"# preserve this","numbered":{"0":"first","1":"second"}}'
        legacy('--import-nydata', old_object, old_expected)
        legacy('--import-nydata-compiler', old_object, '{"settings":' + old_expected + '}')
        old_list = '''list values
  item
    raw text
  end item
  item
    object nested
      field value
        7
      end value
    end nested
  end item
  item
    list empty
    end empty
  end item
end values
'''
        legacy('--import-nydata', old_list, '["raw text",{"value":"7"},[]]')
        legacy('--import-nydata', 'list empty\nend empty\n', '[]')
        legacy('--import-nydata', 'object empty\nend empty\n', '{}')
        legacy('--import-nydata', 'field root\ntrue\nend root\n', '"true"')
        legacy('--import-nydata-compiler', 'field root\ntrue\nend root\n', '{"root":"true"}')
        multi = 'field first\n1\nend first\nfield second\nfalse\nend second\n'
        legacy('--import-nydata-compiler', multi, '{"first":"1","second":"false"}')
        convert('--import-nydata', multi, '.nydata', success=False)
        convert('--import-nydata-compiler', old_list, '.nydata', success=False)
        convert('--to-json', old_object, '.nydata', success=False)
        for source in ['object root\n', 'field root\nbody\n', 'object root\nend other\n',
                       'object root\nunknown value\nend root\n',
                       'object root\nfield x\n1\nend x\nfield x\n2\nend x\nend root\n']:
            for option in ['--import-nydata', '--import-nydata-compiler']:
                convert(option, source, '.nydata', success=False)
        for source in ['list values\nitem\nraw\nend values\n',
                       'list values\nfield x\nbody\nend x\nend values\n']:
            convert('--import-nydata', source, '.nydata', success=False)
        convert('--to-nydata', '{}', '.json', success=False)
        convert('--to-nildata', '{}', '.json', success=False, extra=('--unknown',))
        convert('--to-json', 'nildata 1\nnull\n', '.nildata', success=False, extra=('extra.nildata',))

        # Conversion accepts bounded regular files. In particular a FIFO must
        # fail promptly without needing a writer, rather than blocking on open.
        paths = [root, root / 'missing input.json']
        regular = root / 'regular input.json'
        regular.write_text('{}', encoding='utf-8')
        oversized = root / 'oversized input.json'
        oversized.write_bytes(b' ' * (1024 * 1024 + 1))
        paths.append(oversized)
        if os.name != 'nt':
            fifo = root / 'input fifo'
            os.mkfifo(fifo)
            paths.append(fifo)
            link = root / 'input symlink.json'
            link.symlink_to(regular)
            paths.append(link)
        for option in ['--to-nildata', '--to-json', '--import-nydata', '--import-nydata-compiler']:
            for path in paths:
                result = subprocess.run([str(compiler), option, str(path)], cwd=root,
                                        env=environment, stdin=subprocess.DEVNULL,
                                        capture_output=True, timeout=5)
                checks += 1
                assert result.returncode != 0 and not result.stdout and result.stderr.strip(), (option, path, result)
    print(f'NILData CLI contract: {checks} checks passed')


if __name__ == '__main__':
    main()
