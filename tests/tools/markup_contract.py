#!/usr/bin/env python3
"""A markup block rejects malformed documents and escapes what it interpolates.

The rejections are the point of the construct: a document whose elements do not
balance, a hole that would name an element or an attribute, and a conditional
arm that opens an element it does not close are all errors here rather than
broken HTML at run time. The positive checks prove the escaping that makes the
rejections worth having.
"""

import argparse
import os
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
            path.write_text(source, encoding='utf-8', newline='\n')
            for flags in routes:
                output = root / 'protected-output'
                output.write_bytes(b'previous artifact')
                result = subprocess.run([compiler, '--quiet', '-I', str(stdlib),
                                         *flags, '--output', output, path],
                                        capture_output=True, text=True, encoding='utf-8', timeout=60)
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
            path.write_text(source, encoding='utf-8', newline='\n')
            for flags in routes:
                output = root / 'protected-output'
                output.write_bytes(b'previous artifact')
                result = subprocess.run([compiler, '--quiet', '-I', str(stdlib),
                                         *flags, '--output', output, path],
                                        capture_output=True, text=True, encoding='utf-8', timeout=60)
                assert result.returncode != 0, (name, flags, result.stdout)
                assert output.read_bytes() == b'previous artifact', (name, flags)
                assert result.stderr.strip(), (name, flags)
                checks += 1

        # HTML escaping is not JavaScript/CSS/nested-document escaping. These
        # contexts and URL lists require an explicit trusted raw value. Reject
        # at compile time on every route, preserving the previous artifact.
        shared_rejected = [
            ('event-handler', '<button onclick="{label}">x</button>'),
            ('mixed-event-handler', '<button oNcLiCk="prefix {label}">x</button>'),
            ('style-attribute', '<p STYLE="{label}">x</p>'),
            ('nested-document', '<iframe srcdoc="{label}"></iframe>'),
            ('srcset', '<img srcset="{label}">'),
            ('imagesrcset', '<link imagesrcset="{label}">'),
            ('ping', '<a ping="{label}">x</a>'),
            ('dynamic-scheme', '<a href="{label}:marker">x</a>'),
            ('dynamic-scheme-fragment', '<a href="java{label}script:marker">x</a>'),
            ('two-url-holes', '<a href="{label}{label}">x</a>'),
            ('executable-prefix', '<a href="javascript:{label}">x</a>'),
            ('entity-scheme-prefix', '<a href="javascript&#58;{label}">x</a>'),
            ('raw-attribute-name', '<div {raw label}="x">y</div>'),
            ('raw-comment', '<!-- {raw label} -->'),
            ('unfinished-comment', '<!-- unfinished'),
            ('unfinished-void-tag', '<img'),
            ('unfinished-void-attribute', '<img src="unfinished'),
            ('unfinished-close', '<p>x</p'),
            ('unfinished-declaration', '<!DOCTYPE html'),
            ('closing-attributes', '<p>x</p class="x">'),
            ('self-closing-script', '<script/>{label}'),
            ('self-closing-style', '<STYLE />{label}'),
            ('self-closing-div', '<div/>'),
            ('self-closing-svg', '<svg><circle/></svg>'),
            ('multiline-raw-hole', '<script>{raw\n label}</script>'),
        ]
        for language in ('ny', 'matrix'):
            opening, alternative, end = (('if', 'else', 'end if') if language == 'ny'
                                          else ('when', 'otherwise', 'end when'))
            contextual_rejected = [
                ('second-else', f'{{{opening} flag}}a{{{alternative}}}b{{{alternative}}}c{{{end}}}'),
                ('condition-after-else', f'{{{opening} flag}}a{{{alternative}}}b{{{alternative} {opening} flag}}c{{{end}}}'),
                ('split-attribute-quote', f'<div {{{opening} flag}}title="{{{alternative}}}{{{end}}}{{label}}">x</div>'),
                ('split-attribute-name', f'<div {{{opening} flag}}onclick{{{alternative}}}title{{{end}}}="{{label}}">x</div>'),
                ('directive-in-url', f'<a href="{{{opening} flag}}{{label}}{{{alternative}}}x{{{end}}}">x</a>'),
                ('directive-in-comment', f'<!-- {{{opening} flag}}x{{{end}}} -->'),
                ('split-boolean-attribute', f'<input {{{opening} flag}}disabled{{{end}}}>'),
                ('self-closing-arm-parent', f'<div {{{opening} flag}}/><script{{{alternative}}}/><div{{{end}}}>{{label}}</div><!-- </script> -->'),
            ]
            for name, body in shared_rejected + contextual_rejected:
                if language == 'ny':
                    source = ('import nil.std.markup.escaping\nfn main() -> i32\n'
                              'let flag = true\nlet label = "marker"\n'
                              f'let page = $markup\n{body}\nend markup\nreturn 0\nend fn\n')
                else:
                    source = (HEAD + 'main returns override integer begin paragraph\n'
                              'value flag is true\nvalue label as text is "marker"\n'
                              f'value page is begin markup\n{body}\nend markup\ngive 0\nend paragraph\n')
                path = root / f'context-{name}.{language}'
                path.write_text(source, encoding='utf-8', newline='\n')
                for flags in routes:
                    output = root / 'protected-context-output'
                    output.write_bytes(b'previous artifact')
                    result = subprocess.run([compiler, '--quiet', '-I', str(stdlib),
                                             *flags, '--output', output, path],
                                            capture_output=True, text=True, encoding='utf-8', timeout=60)
                    assert result.returncode != 0, (name, language, flags, result.stdout)
                    assert output.read_bytes() == b'previous artifact', (name, language, flags)
                    assert result.stderr.strip(), (name, language, flags)
                    checks += 1

        def render_routes(name, language, source, expected):
            nonlocal checks
            path = root / f'{name}.{language}'
            path.write_text(source, encoding='utf-8', newline='\n')
            for route, flags in enumerate(routes):
                artifact = root / f'{name}-{language}-{route}'
                result = subprocess.run([compiler, '--quiet', '-I', str(stdlib),
                                         *flags, '--output', artifact, path],
                                        capture_output=True, text=True, encoding='utf-8', timeout=60)
                assert result.returncode == 0, (name, flags, result.stderr)
                if '--jit' in flags:
                    actual = result.stdout
                else:
                    executable = artifact.with_suffix('.exe') if os.name == 'nt' else artifact
                    command = ([executable] if '--native' in flags else
                               [args.interpreter.resolve(), artifact])
                    run = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', timeout=60)
                    assert run.returncode == 0, (name, flags, run.stderr)
                    actual = run.stdout
                assert actual == expected, (name, language, flags, actual, expected)
                checks += 1

        # Both surfaces use the same context decisions, and literal script
        # lookalikes inside comments/quoted attributes never hide later holes.
        for language in ('ny', 'matrix'):
            opening, alternative, end = (('if', 'else', 'end if') if language == 'ny'
                                          else ('when', 'otherwise', 'end when'))
            body = ('<a href="/q?v={label}">path</a>\n'
                    '<a href="https://example.invalid/{label}">absolute</a>\n'
                    '<a href="?q={label}">query</a>\n'
                    '<a href="#part-{label}">fragment</a>\n'
                    '<a href="{target}">whole</a>\n'
                    '<svg><a xlink:href="{target}">svg</a></svg>\n'
                    '<p data-example="<script>x</script>">{label}</p>\n'
                    '<!-- <script> -->\n<p>{label}</p>\n'
                    '<script>const x = "</scripted>"; {{keep}}</script>\n'
                    '<script>{raw label} + {raw label}</script>\n'
                    '<script data-label=">">var obj = { key: 1 };</script>\n'
                    f'<div {{{opening} flag}}title="{{label}}"{{{alternative}}}title="other"{{{end}}}>x</div>\n'
                    f'<p title="before {{{opening} flag}}{{label}}{{{alternative}}}other{{{end}}} after">text</p>\n'
                    '<button onclick="{raw trusted}">trusted</button>')
            expected = ('<a href="/q?v=%3cmarker%3e%26">path</a>\n'
                        '<a href="https://example.invalid/%3cmarker%3e%26">absolute</a>\n'
                        '<a href="?q=%3cmarker%3e%26">query</a>\n'
                        '<a href="#part-%3cmarker%3e%26">fragment</a>\n'
                        '<a href="JaVaScRiPt%3amarker">whole</a>\n'
                        '<svg><a xlink:href="JaVaScRiPt%3amarker">svg</a></svg>\n'
                        '<p data-example="<script>x</script>">&lt;marker&gt;&amp;</p>\n'
                        '<!-- <script> -->\n<p>&lt;marker&gt;&amp;</p>\n'
                        '<script>const x = "</scripted>"; {{keep}}</script>\n'
                        '<script>{raw label} + {raw label}</script>\n'
                        '<script data-label=">">var obj = { key: 1 };</script>\n'
                        '<div title="&lt;marker&gt;&amp;">x</div>\n'
                        '<p title="before &lt;marker&gt;&amp; after">text</p>\n'
                        '<button onclick="marker()">trusted</button>\n')
            if language == 'ny':
                source = ('import nil.std.markup.escaping\nfn main() -> i32\n'
                          'let flag = true\nlet label = "<marker>&"\n'
                          'let target = "JaVaScRiPt:marker"\nlet trusted = "marker()"\n'
                          f'let page = $markup\n{body}\nend markup\n'
                          'println(nil.std.markup::render(page))\nreturn 0\nend fn\n')
            else:
                source = (HEAD + 'main returns override integer begin paragraph\n'
                          'value flag is true\nvalue label as text is "<marker>&"\n'
                          'value target as text is "JaVaScRiPt:marker"\nvalue trusted as text is "marker()"\n'
                          f'value page is begin markup\n{body}\nend markup\n'
                          'say nil.std.markup::render with doc being page\ngive 0\nend paragraph\n')
            render_routes('context-positive', language, source, expected)

        # The whole URL decision follows browser control normalization. These
        # strings remain data; no browser or JavaScript execution is needed.
        controls = [('java\\tscript:marker', 'java\tscript%3amarker'),
                    ('java\\nscript:marker', 'java\nscript%3amarker'),
                    ('java\\rscript:marker', 'java\nscript%3amarker'),
                    ('\\v\\f JaVaScRiPt:marker', '\v\f JaVaScRiPt%3amarker'),
                    ('\x01javascript:marker', '\x01javascript%3amarker')]
        source = 'import nil.std.markup.escaping\nfn main() -> i32\n'
        expected = ''
        for index, (value, escaped) in enumerate(controls):
            source += f'let value{index} = "{value}"\nlet page{index} = $markup\n<a href="{{value{index}}}">x</a>\nend markup\n'
            source += f'println(nil.std.markup::render(page{index}))\n'
            expected += f'<a href="{escaped}">x</a>\n'
        source += ('let page = $markup\n<p>{"a\\\"}b"}</p>\n<p>{"x" /* } */}</p>\nend markup\n'
                   'println(nil.std.markup::render(page))\nreturn 0\nend fn\n')
        expected += '<p>a"}b</p>\n<p>x</p>\n'
        render_routes('url-controls-and-ny-holes', 'ny', source, expected)

        # The line-anchored terminator is a keyword, not the prefix of a
        # longer body word, and Ny still permits expression continuation.
        source = ('import nil.std.markup.escaping\nfn main() -> i32\n'
                  'println(nil.std.markup::render($markup\n<p>\n'
                  'end markupExtra\nend markup_extra\nend markupé\n</p>\n'
                  'end markup))\nreturn 0\nend fn\n')
        render_routes('ny-terminator-boundary', 'ny', source,
                      '<p>\nend markupExtra\nend markup_extra\nend markupé\n</p>\n')

        # An unterminated Ny block is a lexer error too.
        path = root / 'ny-unterminated.ny'
        path.write_text('import nil.std.markup.escaping\n'
                        'fn main() -> i32\n    let page = $markup\n        <p>x</p>\n'
                        '    return 0\nend fn\n', encoding='utf-8', newline='\n')
        result = subprocess.run([compiler, '--quiet', '-I', str(stdlib), '--no-bundle',
                                 '--output', root / 'ny-unterminated.nil', path],
                                capture_output=True, text=True, encoding='utf-8', timeout=60)
        assert result.returncode != 0 and 'markup block' in result.stderr, result.stderr
        checks += 1

        # An unterminated block is a lexer error, before any markup parsing.
        path = root / 'unterminated.matrix'
        path.write_text(HEAD + 'main returns override integer begin paragraph\n'
                        '    value page is begin markup\n        <p>x</p>\n'
                        '    give 0\nend paragraph\n', encoding='utf-8', newline='\n')
        result = subprocess.run([compiler, '--quiet', '-I', str(stdlib), '--no-bundle',
                                 '--output', root / 'unterminated.nil', path],
                                capture_output=True, text=True, encoding='utf-8', timeout=60)
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
        path.write_text(hostile, encoding='utf-8', newline='\n')
        artifact = root / 'escaping.nil'
        result = subprocess.run([compiler, '--quiet', '-I', str(stdlib), '--no-bundle',
                                 '--output', artifact, path],
                                capture_output=True, text=True, encoding='utf-8', timeout=60)
        assert result.returncode == 0, result.stderr
        run = subprocess.run([args.interpreter.resolve(), artifact],
                             capture_output=True, text=True, encoding='utf-8', timeout=60)
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
