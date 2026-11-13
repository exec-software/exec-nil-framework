#!/usr/bin/env python3
"""A markup block rejects malformed documents and escapes what it interpolates.

The rejections are the point of the construct: a document whose elements do not
balance, a hole that would name an element or an attribute, and a conditional
arm that opens an element it does not close are all errors here rather than
broken HTML at run time. The positive checks prove the escaping that makes the
rejections worth having.
"""

import argparse
from pathlib import Path
import subprocess
import tempfile


HEAD = 'book markup_rejection\nread everything from nil.std.markup\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--interpreter', type=Path, required=True)
    parser.add_argument('--stdlib', type=Path, required=True)
    parser.add_argument('--llvm', choices=('ON', 'OFF'), required=True)
    args = parser.parse_args()
    compiler = args.compiler.resolve()
    stdlib = args.stdlib.resolve()
    routes = [('--no-bundle',), ('--no-bundle', '--emit-binary-nil')]
    if args.llvm == 'ON':
        routes += [('--native',), ('--native', '--jit')]
    checks = 0

    rejected = [
        # (name, body of main, whether nil.std.markup is read)
        ('mismatched-close', '<div><p>x</div>', True),
        ('unclosed-element', '<div><p>x</p>', True),
        ('stray-close', '</p>', True),
        ('arm-opens-only', '<div>{when flag}<p>a{end when}</div>', True),
        ('arm-closes-foreign', '{repeat for each n in rows}</div>{end repeat}', True),
        ('hole-names-attribute', '<div {label}="x">y</div>', True),
        ('hole-names-element', '<{label}>y</{label}>', True),
        ('unquoted-attribute', '<div class=x>y</div>', True),
        ('document-in-attribute', '<div class="{inner}">y</div>', True),
        ('script-hole-without-raw', '<script>{label}</script>', True),
        ('comment-hole', '<!-- {label} -->', True),
        ('end-without-open', '<p>x</p>{end when}', True),
        ('repeat-over-scalar', '{repeat for each n in flag}<p>x</p>{end repeat}', True),
        ('condition-not-boolean', '{when label}<p>x</p>{end when}', True),
        ('unsupported-hole-type', '<p>{rows}</p>', True),
        ('missing-read', '<p>x</p>', False),
    ]
    with tempfile.TemporaryDirectory(prefix='nil-markup-contract-') as directory:
        root = Path(directory)
        for name, body, reads in rejected:
            head = HEAD if reads else 'book markup_rejection\n'
            source = (head + 'main returns override integer begin paragraph\n'
                      '    value flag is true\n'
                      '    value label as text is "cls"\n'
                      '    value rows as array of number is [1, 2]\n'
                      + ('    value inner is begin markup\n        <em>x</em>\n    end markup\n'
                         if reads else '') +
                      '    value page is begin markup\n'
                      f'        {body}\n'
                      '    end markup\n'
                      '    give 0\n'
                      'end paragraph\n')
            path = root / f'{name}.matrix'
            path.write_text(source, newline='\n')
            for flags in routes:
                output = root / 'protected-output'
                output.write_bytes(b'previous artifact')
                result = subprocess.run([compiler, '--quiet', '-I', str(stdlib),
                                         *flags, '--output', output, path],
                                        capture_output=True, text=True, timeout=60)
                assert result.returncode != 0, (name, flags, result.stdout)
                assert output.read_bytes() == b'previous artifact', (name, flags)
                assert result.stderr.strip(), (name, flags)
                checks += 1

        # Ny rejects the same documents through the same shared structure,
        # with its own control words.
        ny_rejected = [
            ('ny-mismatched-close', '<div><p>x</div>'),
            ('ny-unclosed-element', '<div><p>x</p>'),
            ('ny-arm-opens-only', '<div>{if flag}<p>a{end if}</div>'),
            ('ny-hole-names-attribute', '<div {label}="x">y</div>'),
            ('ny-unquoted-attribute', '<div class=x>y</div>'),
            ('ny-document-in-attribute', '<div class="{inner}">y</div>'),
            ('ny-script-hole-without-raw', '<script>{label}</script>'),
            ('ny-end-without-open', '<p>x</p>{end if}'),
            ('ny-condition-not-boolean', '{if label}<p>x</p>{end if}'),
            ('ny-unsupported-hole-type', '<p>{rows}</p>'),
        ]
        for name, body in ny_rejected:
            source = ('import nil.std.markup.escaping\n'
                      'fn main() -> i32\n'
                      '    let flag = true\n'
                      '    let label = "cls"\n'
                      '    let rows = [1, 2]\n'
                      '    let inner = $markup\n        <em>x</em>\n    end markup\n'
                      '    let page = $markup\n'
                      f'        {body}\n'
                      '    end markup\n'
                      '    return 0\n'
                      'end fn\n')
            path = root / f'{name}.ny'
            path.write_text(source, newline='\n')
            for flags in routes:
                output = root / 'protected-output'
                output.write_bytes(b'previous artifact')
                result = subprocess.run([compiler, '--quiet', '-I', str(stdlib),
                                         *flags, '--output', output, path],
                                        capture_output=True, text=True, timeout=60)
                assert result.returncode != 0, (name, flags, result.stdout)
                assert output.read_bytes() == b'previous artifact', (name, flags)
                assert result.stderr.strip(), (name, flags)
                checks += 1

        # An unterminated Ny block is a lexer error too.
        path = root / 'ny-unterminated.ny'
        path.write_text('import nil.std.markup.escaping\n'
                        'fn main() -> i32\n    let page = $markup\n        <p>x</p>\n'
                        '    return 0\nend fn\n', newline='\n')
        result = subprocess.run([compiler, '--quiet', '-I', str(stdlib), '--no-bundle',
                                 '--output', root / 'ny-unterminated.nil', path],
                                capture_output=True, text=True, timeout=60)
        assert result.returncode != 0 and 'markup block' in result.stderr, result.stderr
        checks += 1

        # An unterminated block is a lexer error, before any markup parsing.
        path = root / 'unterminated.matrix'
        path.write_text(HEAD + 'main returns override integer begin paragraph\n'
                        '    value page is begin markup\n        <p>x</p>\n'
                        '    give 0\nend paragraph\n', newline='\n')
        result = subprocess.run([compiler, '--quiet', '-I', str(stdlib), '--no-bundle',
                                 '--output', root / 'unterminated.nil', path],
                                capture_output=True, text=True, timeout=60)
        assert result.returncode != 0 and 'begin markup' in result.stderr, result.stderr
        checks += 1

        # Hostile values reach the page escaped, in every context, and a
        # document composed into another is not escaped a second time.
        hostile = (HEAD +
                   'inner receives name as text returns nil.std.markup.document begin paragraph\n'
                   '    give begin markup\n        <em>{name}</em>\n    end markup\n'
                   'end paragraph\n'
                   'main returns override integer begin paragraph\n'
                   '    value attack as text is "<script>alert(1)</script>"\n'
                   '    value page is begin markup\n'
                   '        <div class="{attack}" href="{attack}">{attack}</div>\n'
                   '        <a href="/q?v={attack}">q</a>\n'
                   '        {inner with name being attack}\n'
                   '    end markup\n'
                   '    say nil.std.markup::render with doc being page\n'
                   '    give 0\n'
                   'end paragraph\n')
        path = root / 'escaping.matrix'
        path.write_text(hostile, newline='\n')
        artifact = root / 'escaping.nil'
        result = subprocess.run([compiler, '--quiet', '-I', str(stdlib), '--no-bundle',
                                 '--output', artifact, path],
                                capture_output=True, text=True, timeout=60)
        assert result.returncode == 0, result.stderr
        run = subprocess.run([args.interpreter.resolve(), artifact],
                             capture_output=True, text=True, timeout=60)
        assert run.returncode == 0, run.stderr
        page = run.stdout
        # The only '<' that survive are the ones the document itself wrote.
        assert '<script>alert(1)</script>' not in page, page
        assert '&lt;script&gt;alert(1)&lt;/script&gt;' in page, page
        assert 'class="&lt;script&gt;alert(1)&lt;/script&gt;"' in page, page
        assert '/q?v=%3cscript%3ealert(1)%3c%2fscript%3e' in page, page
        assert '<em>&lt;script&gt;alert(1)&lt;/script&gt;</em>' in page, page
        assert '&amp;lt;' not in page, page   # composed documents escape once
        checks += 6
    print(f'{checks} markup contract checks passed')


if __name__ == '__main__':
    main()
